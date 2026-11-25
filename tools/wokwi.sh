#!/usr/bin/env bash
# Wokwi でターゲット版を動かす（P7。docs/quality.md「エミュレーター」）。wokwi-cli（v0.29.1）と WOKWI_CLI_TOKEN が要る。
#
#   tools/wokwi.sh            チップとファームウェアをビルドし、wokwi/scenarios のシナリオを全部流す
#   tools/wokwi.sh <名前>...   指定したシナリオだけを流す（boot_readings・buttons_mode・wifi_heap・watchdog）
#
# WOKWI_CLI_TOKEN がなければ「省略」と出して成功で終わる（CI でシークレットがないとき。P7 の決定）。
# watchdog は構成 wokwi-hang（ui が10秒で止まる）、ほかは構成 wokwi で動かす。ファームウェアのビルドは tools/qemu.sh build。
# シリアルのログは build/wokwi/<名前>.log、OLED の画面は build/wokwi/*.png、ヒープの標本は build/wokwi/<名前>-heap.csv。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build/wokwi"
HEAP_MIN_BYTES=30720  # #114

# トークンは環境変数（CI のシークレット）か ~/.config/wokwi/token（Mac。1行、chmod 600）から読む。
# 値は出力に出さず、export もしない（wokwi-cli を起動するときに、そのプロセスにだけ渡す）
token="${WOKWI_CLI_TOKEN:-}"
unset WOKWI_CLI_TOKEN
TOKEN_FILE="${WOKWI_TOKEN_FILE:-$HOME/.config/wokwi/token}"
if [[ -z "$token" && -r "$TOKEN_FILE" ]]; then
  IFS= read -r token < "$TOKEN_FILE" || true
fi
if [[ -z "$token" ]]; then
  echo "WOKWI_CLI_TOKEN がないので Wokwi の試験を省略する（シークレットか $TOKEN_FILE を置くと流れる）"
  exit 0
fi
command -v wokwi-cli > /dev/null || { echo "wokwi-cli がない（https://github.com/wokwi/wokwi-cli/releases の v0.29.1）" >&2; exit 1; }

names=("$@")
[[ ${#names[@]} -gt 0 ]] || names=(boot_readings buttons_mode wifi_heap watchdog)
mkdir -p "$OUT"

# チップ（C → WASM。wokwi-cli が WASI-SDK を取ってくる）
for chip in bme280 bh1750; do
  (cd "$ROOT/wokwi/chips" && wokwi-cli chip compile -q "$chip.chip.c" -o "$chip.chip.wasm")
done

need_hang=0
need_main=0
for n in "${names[@]}"; do
  if [[ "$n" == watchdog ]]; then need_hang=1; else need_main=1; fi
done
[[ $need_main -eq 0 ]] || "$ROOT/tools/qemu.sh" build wokwi
if [[ $need_hang -eq 1 ]]; then
  "$ROOT/tools/qemu.sh" build wokwi-hang
  # 構成 wokwi-hang の wokwi.toml（図とチップは wokwi/ のものを使う）
  mkdir -p "$OUT/hang"
  sed -e "s#'../build/firmware-wokwi/#'../../firmware-wokwi-hang/#" -e "s#'chips/#'../../../wokwi/chips/#" \
    "$ROOT/wokwi/wokwi.toml" > "$OUT/hang/wokwi.toml"
fi

# シナリオごとのタイムアウト（シミュレーションの時間、ms）。目安の倍ほどにする（docs/quality.md「Wokwi の時間」）
timeout_of() {
  case "$1" in
    boot_readings) echo 40000 ;;
    buttons_mode) echo 60000 ;;
    wifi_heap) echo 90000 ;;
    watchdog) echo 40000 ;;
    *) echo "シナリオ ${1} がない" >&2; exit 2 ;;
  esac
}

# 使ったシミュレーションの時間（秒）。ヒープの標本（--heap-csv）の最後の time_s。標本は 1 秒ごとだが抜けることがあるので、
# 行の数ではなく時刻で数える
sim_seconds() {
  local csv="$1"
  [[ -f "$csv" ]] || { echo 0; return; }
  awk -F, 'NR > 1 { t = $2 } END { printf "%d", (t == "" ? 0 : t + 0.999) }' "$csv"
}

failed=0
total=0
for n in "${names[@]}"; do
  project="$ROOT/wokwi"
  [[ "$n" == watchdog ]] && project="$OUT/hang"
  args=(--diagram-file "$ROOT/wokwi/diagram.json" --scenario "$ROOT/wokwi/scenarios/$n.yaml" --timeout "$(timeout_of "$n")"
        --serial-log-file "$OUT/$n.log" --heap-csv "$OUT/$n-heap.csv")
  [[ "$n" == wifi_heap ]] && args+=(--heap-min-free "$HEAP_MIN_BYTES")
  echo "== $n"
  # Wokwi のサーバーとの接続が、シミュレーションが始まる前に切れることがある（API Error、code 1006）。
  # 時間を使わずに失敗したとき（標本がない）だけ、15 秒待って 2 回まで流し直す。シナリオの失敗は流し直さない
  for attempt in 1 2 3; do
    rm -f "$OUT/$n-heap.csv"
    rc=0
    WOKWI_CLI_TOKEN="$token" wokwi-cli "$project" "${args[@]}" || rc=$?
    secs="$(sim_seconds "$OUT/$n-heap.csv")"
    [[ $rc -ne 0 && $secs -eq 0 && $attempt -lt 3 ]] || break
    echo "Wokwi との接続が始まる前に切れた。15 秒後に流し直す（${attempt} 回目）"
    sleep 15
  done
  total=$(( total + secs ))
  echo "シミュレーションの時間：${n} ${secs} 秒"
  if [[ $rc -ne 0 ]]; then
    echo "[不合格] シナリオ ${n}（終了コード ${rc}）"; failed=1; continue
  fi
  if [[ "$n" == wifi_heap ]]; then
    python3 "$ROOT/tools/emu_check.py" heap "$OUT/$n.log" || failed=1
  fi
done
echo "シミュレーションの時間の合計：${total} 秒（Wokwi の CI の無料枠は 30 日で 50 分）"
exit $failed

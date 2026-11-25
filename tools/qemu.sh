#!/usr/bin/env bash
# QEMU でターゲット版を動かす（P7。docs/quality.md「エミュレーター」）。ESP-IDF の Docker イメージ（espressif/idf:v5.5.6）の中で動かす。
#
#   tools/qemu.sh build <構成>          構成を重ねてビルドし、フラッシュの全体のイメージ（flash.bin）を作る（build/firmware-<構成>）
#   tools/qemu.sh run <構成> <秒>        QEMU を動かし、シリアルのログを build/qemu/<構成>.log に保存する
#   tools/qemu.sh test                  構成 qemu で、起動・4つのタスク・BOOT（power_on）・stats を確かめる（tools/emu_check.py）
#   tools/qemu.sh contract              構成 qemu-fakehw に、ホストの Mosquitto で契約テストを流す（#132）
#   tools/qemu.sh soak <分>             構成 qemu-fakehw を Mosquitto につないで動かし、NFR-03 を確かめる（5分後と終わりのヒープの最低残量）
#
# 構成：qemu（sdkconfig.qemu。機器はつながっていない）、qemu-fakehw（それに sdkconfig.qemu-fakehw。hal をフェイクに替える）。
# wokwi・wokwi-hang は tools/wokwi.sh が使う。
# QEMU は ESP32-S3 の Wi-Fi・I2C・LEDC・ADC を再現しない（esp-toolchain-docs の qemu/esp32s3/README.md）ので、ネットワークは
# OpenCores Ethernet（ユーザーモードの NAT。ゲートウェイ 10.0.2.2 は QEMU を動かすコンテナの localhost）を使う（#120）。
# idf.py qemu はタイマーグループのウォッチドッグを止めるので、qemu-system-xtensa を直接起動する（それでも QEMU では
# タスクウォッチドッグが働かない。#131。ウォッチドッグは Wokwi で確かめる）。
# 契約テストと soak は、QEMU を動かすコンテナを Mosquitto のコンテナとネットワークを分け合うように起動する。
# MQTT のパスワードは IOTSPEC_MQTT_PASSWORD（既定は iot-common-spec の開発用の値）をビルドのディレクトリにだけ書く。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="espressif/idf:v5.5.6"
COMPOSE=(docker compose -f "$ROOT/external/iot-common-spec/docker-compose.yml")
PASSWORD="${IOTSPEC_MQTT_PASSWORD:-iotspec-dev}"

usage() {
  sed -n '2,10p' "$0" >&2
  exit 2
}

extra_of() {
  case "$1" in
    qemu) echo "sdkconfig.qemu" ;;
    qemu-fakehw) echo "sdkconfig.qemu;sdkconfig.qemu-fakehw" ;;
    wokwi) echo "sdkconfig.wokwi" ;;                     # tools/wokwi.sh から使う
    wokwi-hang) echo "sdkconfig.wokwi;sdkconfig.test-hang" ;;
    *) usage ;;
  esac
}

# コンテナの中で動かす（"--" までは docker run の追加の引数、その後はシェルのコマンド）
in_idf() {
  local args=()
  while [[ $# -gt 0 && "$1" != "--" ]]; do args+=("$1"); shift; done
  shift
  docker run --rm -v "$ROOT":/project -e HOME=/tmp ${args[@]+"${args[@]}"} "$IMAGE" \
    bash -c ". /opt/esp/idf/export.sh >/dev/null 2>&1; $*"
}

build() {
  local v="$1"
  local b="build/firmware-$v"
  mkdir -p "$ROOT/$b"
  # パスワードは秘密なので、ビルドのディレクトリ（.gitignore の build/）にだけ書く（#102）
  printf 'CONFIG_NODE_MQTT_PASSWORD="%s"\n' "$PASSWORD" > "$ROOT/$b/sdkconfig.secrets"
  rm -f "$ROOT/$b/sdkconfig"  # 構成の defaults を毎回重ね直す
  in_idf -w /project/firmware -e "NODE_SDKCONFIG_EXTRA=$(extra_of "$v");/project/$b/sdkconfig.secrets" -- \
    "idf.py -B /project/$b -DSDKCONFIG=/project/$b/sdkconfig build > /project/$b/build.log 2>&1 || { tail -30 /project/$b/build.log; exit 1; }
     cd /project/$b && esptool.py --chip esp32s3 merge_bin --fill-flash-size 2MB -o flash.bin @flash_args > /dev/null"
  echo "built $b"
}

# QEMU の起動のコマンド（eFuse は既定のすべて 0）
qemu_cmd() {
  local v="$1" log="$2"
  echo "qemu-system-xtensa -M esp32s3 -m 32M -drive file=/project/build/firmware-$v/flash.bin,if=mtd,format=raw" \
    "-nic user,model=open_eth -nographic -monitor none -serial file:$log"
}

mosquitto_net() {
  local id
  id="$("${COMPOSE[@]}" ps -q mosquitto)"
  [[ -n "$id" ]] || { echo "Mosquitto が動いていない（docker compose -f external/iot-common-spec/docker-compose.yml up -d mosquitto）" >&2; exit 1; }
  echo "container:$id"
}

# run <構成> <秒> [ネットワーク]
run() {
  local v="$1" secs="$2" net=()
  [[ $# -ge 3 ]] && net=(--network "$3")
  mkdir -p "$ROOT/build/qemu"
  in_idf ${net[@]+"${net[@]}"} -- "timeout $secs $(qemu_cmd "$v" "/project/build/qemu/$v.log") || true"
  echo "log: build/qemu/$v.log"
}

case "${1:-}" in
  build) [[ $# -eq 2 ]] || usage; build "$2" ;;
  run) [[ $# -eq 3 ]] || usage; run "$2" "$3" ;;
  test)
    build qemu
    run qemu 75
    python3 "$ROOT/tools/emu_check.py" boot "$ROOT/build/qemu/qemu.log"
    ;;
  contract)
    net="$(mosquitto_net)"
    build qemu-fakehw
    # 契約テストキットは IDF の Python に入れる（コンテナを使い捨てにするので毎回）
    in_idf --network "$net" -e "IOTSPEC_MQTT_PASSWORD=$PASSWORD" -e "IOTSPEC_MQTT_URL=mqtt://localhost:1883" -- \
      "python -m pip install -q 'iotspec @ git+https://github.com/io0323/iot-common-spec@v0.2.1' >/dev/null 2>&1 &&
       cd /project/build/qemu && iotspec-contract --role mcu-cpp-01 --junit contract-qemu.xml \
         --target-cmd '$(qemu_cmd qemu-fakehw /project/build/qemu/contract-serial.log)'"
    ;;
  soak)
    [[ $# -eq 2 ]] || usage
    net="$(mosquitto_net)"
    build qemu-fakehw
    run qemu-fakehw "$(( $2 * 60 + 30 ))" "$net"
    cp "$ROOT/build/qemu/qemu-fakehw.log" "$ROOT/build/qemu/soak.log"
    python3 "$ROOT/tools/emu_check.py" soak "$ROOT/build/qemu/soak.log"
    ;;
  *) usage ;;
esac

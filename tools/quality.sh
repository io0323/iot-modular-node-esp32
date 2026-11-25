#!/usr/bin/env bash
# 品質の検査（docs/quality.md）。使い方：tools/quality.sh <coverage|tidy|cppcheck|lizard|doxygen|all>
# 検査は品質のイメージ（tools/docker/quality.Dockerfile）の中で動かす。Mac も CI も同じ（open-questions #78）。
# 出力は build/quality/<検査名>/ に置く。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="iot-node-quality:p5"

usage() {
  echo "usage: tools/quality.sh <coverage|tidy|cppcheck|lizard|doxygen|all>" >&2
  exit 2
}
[[ $# -eq 1 ]] || usage
case "$1" in coverage|tidy|cppcheck|lizard|doxygen|all) ;; *) usage ;; esac

# ---- コンテナの外：イメージを作って、同じスクリプトを中で動かす
if [[ "${NODE_QUALITY_IN_CONTAINER:-0}" != 1 ]]; then
  docker build -q -t "$IMAGE" -f "$ROOT/tools/docker/quality.Dockerfile" "$ROOT/tools/docker" >/dev/null
  exec docker run --rm -v "$ROOT":/src -w /src --user "$(id -u):$(id -g)" \
    -e NODE_QUALITY_IN_CONTAINER=1 "$IMAGE" tools/quality.sh "$1"
fi

# ---- コンテナの中
cd /src
OUT=build/quality
BUILD=build/host-coverage
# 検査の対象から外すもの（docs/quality.md「対象」）：外部のコード（external/・FetchContent の依存は src/ の外）と、フォントの表
EXCLUDE_FONT=src/drivers/font5x7.hpp
mkdir -p "$OUT"

configure() {
  # compile_commands.json もここで作る（tidy・cppcheck が使う）
  [[ -f "$BUILD/build.ninja" ]] || cmake --preset host-coverage >"$OUT/configure.log" 2>&1 || { cat "$OUT/configure.log"; return 1; }
}

versions() {
  {
    g++ --version | head -1
    gcov --version | head -1
    clang-tidy --version | grep -i version | head -1
    cppcheck --version
    gcovr --version | head -1
    echo "lizard $(lizard --version)"
    echo "doxygen $(doxygen --version)"
  } | tee "$OUT/versions.txt"
}

run_coverage() {
  local d="$OUT/coverage"
  rm -rf "$d" && mkdir -p "$d"
  configure || return 1
  cmake --build --preset host-coverage >"$d/build.log" 2>&1 || { tail -30 "$d/build.log"; return 1; }
  find "$BUILD" -name '*.gcda' -delete
  ctest --preset host-coverage >"$d/ctest.log" 2>&1 || { tail -30 "$d/ctest.log"; return 1; }
  tail -3 "$d/ctest.log"
  # 一度だけ集計して、層ごとの値はその結果から出す
  gcovr -r /src --object-directory "$BUILD" --filter 'src/' --exclude "$EXCLUDE_FONT" \
    --json "$d/coverage.json" --html-details "$d/index.html" --txt "$d/all.txt" --print-summary | tail -3
  local ok=0
  for layer in domain app; do
    echo "== src/$layer（合格条件：行 80% 以上）"
    gcovr --add-tracefile "$d/coverage.json" --filter "src/$layer/" --txt "$d/$layer.txt" --print-summary \
      --fail-under-line 80 | tail -3 || ok=1
  done
  echo "== src/infra（報告だけ）"
  gcovr --add-tracefile "$d/coverage.json" --filter 'src/infra/' --txt "$d/infra.txt" --print-summary | tail -3
  echo "== src/drivers（fake を除く。報告だけ）"
  gcovr --add-tracefile "$d/coverage.json" --filter 'src/drivers/' --exclude 'src/drivers/fake/' \
    --txt "$d/drivers.txt" --print-summary | tail -3
  return $ok
}

run_tidy() {
  local d="$OUT/tidy"
  rm -rf "$d" && mkdir -p "$d"
  configure || return 1
  # 対象は src/ の .cpp（ヘッダは .clang-tidy の HeaderFilterRegex で src/ の下だけを見る）
  local rc=0
  run-clang-tidy -p "$BUILD" -quiet -j "$(nproc)" '^/src/src/' >"$d/tidy.txt" 2>&1 || rc=$?
  grep -E 'warning:|error:' "$d/tidy.txt" | sort -u >"$d/findings.txt" || true
  echo "clang-tidy の指摘：$(wc -l <"$d/findings.txt") 件"
  [[ $rc -eq 0 && ! -s "$d/findings.txt" ]] || { head -50 "$d/findings.txt"; return 1; }
}

run_cppcheck() {
  local d="$OUT/cppcheck"
  rm -rf "$d" && mkdir -p "$d"
  configure || return 1
  local rc=0
  # --file-filter は /src からの相対パスで合わせる。フォントの表はヘッダなので --suppress で外す（cppcheck の求め）
  cppcheck --project="$BUILD/compile_commands.json" --file-filter='src/*' --suppress="*:/src/$EXCLUDE_FONT" \
    --enable=warning,style,performance,portability --std=c++20 --inline-suppr --error-exitcode=1 \
    --suppressions-list=tools/cppcheck.supp -j "$(nproc)" --quiet \
    --template='{file}:{line}: {severity}: {message} [{id}]' 2>"$d/cppcheck.txt" || rc=$?
  echo "cppcheck の指摘：$(grep -c ': ' "$d/cppcheck.txt" || true) 件"
  [[ $rc -eq 0 ]] || { head -50 "$d/cppcheck.txt"; return 1; }
}

run_lizard() {
  local d="$OUT/lizard"
  rm -rf "$d" && mkdir -p "$d"
  # 報告：関数ごとの複雑度・長さ・引数の数
  lizard src -x "./$EXCLUDE_FONT" -x "$EXCLUDE_FONT" --sort cyclomatic_complexity >"$d/report.txt" || true
  # 合格条件：循環的複雑度 10 以下（-C 10 -w。超えた関数だけを出し、1件でもあれば失敗）
  if lizard src -x "./$EXCLUDE_FONT" -x "$EXCLUDE_FONT" -C 10 -L 100000 -a 100 -w >"$d/over.txt"; then
    echo "複雑度 10 を超える関数：0 件（最大 $(awk 'NR>3 && $2 ~ /^[0-9]+$/ {print $2}' "$d/report.txt" | sort -n | tail -1)）"
  else
    echo "複雑度 10 を超える関数：$(wc -l <"$d/over.txt") 件"; cat "$d/over.txt"; return 1
  fi
}

run_doxygen() {
  local d="$OUT/doxygen"
  rm -rf "$d" && mkdir -p "$d"
  if doxygen Doxyfile >"$d/doxygen.log" 2>&1; then
    echo "Doxygen の警告：0 件"
  else
    echo "Doxygen の警告：$(wc -l <"$d/warnings.txt" 2>/dev/null || echo '?') 件"
    head -50 "$d/warnings.txt" 2>/dev/null || tail -20 "$d/doxygen.log"
    return 1
  fi
}

versions >/dev/null
if [[ "$1" == all ]]; then
  failed=()
  for c in coverage tidy cppcheck lizard doxygen; do
    echo "########## $c"
    "run_$c" || failed+=("$c")
  done
  echo "##########"
  cat "$OUT/versions.txt"
  if [[ ${#failed[@]} -gt 0 ]]; then echo "不合格：${failed[*]}"; exit 1; fi
  echo "すべて合格"
else
  "run_$1"
fi

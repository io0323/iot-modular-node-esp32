# 作品02 フェーズ P5：品質ゲート

## はじめに読むもの

1. `CLAUDE.md`（必ず従う）
2. `docs/design/01-requirements-basic-design.md`：第3章（NFR-03・NFR-07・NFR-09）
3. `docs/open-questions.md`（#73〜#77）
4. P4 の PR の「P5 への申し送り」（PahoMqttLink を ThreadSanitizer の主な対象に、broker_restart.py の取りこぼしの弱点など）
5. いまの `CMakePresets.json`・`cmake/`・`.github/workflows/ci.yml`・`tests/system/broker_restart.py`

読み終えたら、作業に入る前に「P5 で作るもの」「作らないもの」「不明点」を短くまとめて報告する。

## 準備

- main（P4 のマージ後）から `phase/p5-quality` を切る
- iot-common-spec にタグ v0.2.2 ができていれば submodule と CI の契約テストキットを上げる（#12）。なければ v0.2.1 のまま
- open-questions の #74〜#77 を「決定（2026-11-25、io 確認）：推奨案のとおり」にする。#73 は「決定（2026-11-25）：送り直しは seq も変えないので、共通仕様の (device_id, boot_id, kind, seq) による重複排除で正しく扱われる。P4 プロンプトの『msg_id で重複を除ける』は誤り」にする
- このプロンプトの「P5 で決めたこと」を、open-questions に「決定（2026-11-25、P5 プロンプト）」として1行ずつ足す

## P5 の原則

- **振る舞いは変えない。** 道具の指摘を直すための書き換えだけをする。直すと振る舞いが変わる指摘（例：境界の比較が仕様と違っていた）は、直さずに open-questions に書いて止まる
- 抑止（NOLINT・cppcheck の suppress・lizard の除外）は、理由を1行添えたものだけ認める。すべてを `docs/quality.md` の表に載せる
- 外部のコード（`external/`・FetchContent の依存）と、生成したもの・フォントの表は検査の対象から外す

## P5 で決めたこと（open-questions に写す）

| 論点 | 決定 |
|---|---|
| 道具の動かし方 | 検査の道具はすべて Docker のイメージ（`tools/docker/quality.Dockerfile`、ubuntu:24.04 に apt で入れる GCC 13・clang-tidy・cppcheck・gcovr・lizard・Doxygen）の中で動かす。Mac も CI も同じイメージを使う（この Mac は Homebrew が使えないため）。入口は `tools/quality.sh <検査名｜all>` |
| カバレッジ（NFR-07） | GCC の gcov と gcovr で、`src/domain`・`src/app` の**行カバレッジ 80% 以上**を合格条件にする。分岐カバレッジと、`src/infra`・`src/drivers`（fake を除く）の値は報告だけする |
| clang-tidy | 有効：`bugprone-*`・`cert-*`・`cppcoreguidelines-*`・`misc-*`・`modernize-*`・`performance-*`・`portability-*`・`readability-*`。外す：`modernize-use-trailing-return-type`・`readability-identifier-length`・`readability-magic-numbers`・`cppcoreguidelines-avoid-magic-numbers`・`misc-include-cleaner`・`cppcoreguidelines-pro-bounds-pointer-arithmetic`（hal・drivers の固定長バッファで多発するため。理由を `.clang-tidy` に書く）。警告はすべてエラー。対象は `src/` 全体（`src/platform/host` も含む） |
| cppcheck | `--enable=warning,style,performance,portability --std=c++20 --inline-suppr --error-exitcode=1`、compile_commands.json を使う。対象は `src/` |
| lizard（NFR-09） | 関数ごとの循環的複雑度 10 以下を合格条件にする（`-C 10 -w`）。対象は `src/`。関数の長さと引数の数は報告だけする |
| Doxygen（NFR-09） | `src/**/*.hpp` の公開 API。`WARN_IF_UNDOCUMENTED`・`WARN_NO_PARAMDOC`・`WARN_AS_ERROR=FAIL_ON_WARNINGS`。HTML は CI の成果物にし、コミットしない |
| ThreadSanitizer | プリセット `host-tsan`（出力先 `build/host-tsan`）。Paho も FetchContent でビルドするので同じ設定でかける。CTest・契約テスト（既定の構成）・broker_restart.py をこのビルドで流し、指摘ゼロを合格条件にする。ASan・UBSan の host-debug はそのまま |
| 最適化ビルド | プリセット `host-release`（`-O2`、サニタイザーなし、出力先 `build/host-release`）。警告ゼロと CTest の全件合格を合格条件にする（最適化で出る警告を拾うため） |
| broker_restart.py の取りこぼし | 再接続の間隔はジッターなしで決まっている（#69：切れてから 1・3・7・15・31・61 秒後に試す）。スクリプトはブローカーを止めてから戻すまでの時間をこの予定から選び、**ブローカーを戻してから本体の次の試行まで5秒以上空く**ようにする（例：止めて20秒で戻すと、次の試行は31秒）。observer はブローカーを戻したら 100ms ごとに接続を試し、本体の次の試行より前につながったことを確かめてから判定に進む。つながらなければ「環境の要因」と分かる失敗の文で終える |

## P5 で作るもの

1. `tools/docker/quality.Dockerfile`、`tools/quality.sh`（`coverage`・`tidy`・`cppcheck`・`lizard`・`doxygen`・`all`）
2. `CMakePresets.json` に `host-coverage`（GCC、`--coverage`、サニタイザーなし）・`host-tsan`・`host-release` を足す。`CMAKE_EXPORT_COMPILE_COMMANDS` を ON にする
3. `.clang-tidy`、`Doxyfile`、cppcheck の抑止のファイル（必要なら）
4. 指摘の修正（上の原則に従う）。足りない Doxygen コメントを足す。カバレッジが 80% に届かなければ、テストを足して届かせる（テストのための製品コードの変更はしない）
5. `tests/system/broker_restart.py` の取りこぼし対策（上の決定）
6. CI：品質のジョブを足す（上のイメージを使う。coverage・tidy・cppcheck・lizard・doxygen）、TSan のジョブ（CTest・契約テスト・broker_restart）、host-release のジョブ（ビルドと CTest）。カバレッジの HTML・Doxygen の HTML・各道具の出力を成果物にする
7. `docs/quality.md`：検査ごとに「何を確かめるか・合格条件・ローカルでの動かし方」、抑止の一覧（場所・理由）、いまの数値（カバレッジ・最大の複雑度）
8. README：品質ゲートの節（`docs/quality.md` へのリンクと、いまの数値）

## P5 で作らないもの

機能の追加・変更、ESP-IDF（P6）、エミュレーター（P7）。

## 完了条件（すべて満たしてから PR）

1. host-debug：Mac と CI の両方で警告ゼロ・CTest 全件合格。契約テスト9件（既定・fakehw）と broker_restart.py が合格、ASan・UBSan の指摘ゼロ（P4 からの後戻りがない）
2. host-release：Mac と CI の両方で警告ゼロ・CTest 全件合格
3. host-tsan：CI で CTest・契約テスト（既定）・broker_restart.py が合格し、TSan の指摘ゼロ（Mac でも流し、結果を PR に載せる）
4. `tools/quality.sh all` が Mac（Docker Desktop）と CI の両方で合格：domain・app の行カバレッジ 80% 以上、clang-tidy の警告ゼロ、cppcheck の指摘ゼロ、複雑度 10 を超える関数ゼロ、Doxygen の警告ゼロ
5. 抑止がすべて `docs/quality.md` の表にあり、理由が書いてある（件数を PR に載せる）
6. P1〜P4 の grep の検査が0件のまま
7. broker_restart.py を Mac で5回続けて流し、5回とも合格（毎回の再接続までの秒数を PR に載せる）
8. open-questions に #73〜#77 の決定と「P5 で決めたこと」の各行が入っている

PR は `gh pr create` で作る。説明には、完了条件ごとの結果、道具のバージョン、カバレッジ（層ごとの行・分岐）、直した指摘の件数（道具ごと）、抑止の一覧、P6 への申し送りを書く。マージはしない。

## 止まって相談すること

- 指摘を直すと振る舞いが変わるとき
- 抑止しないと通らない指摘が、1つの検査で10件を超えるとき（設定の見直しが要るかもしれない）
- カバレッジが、製品コードを変えないと 80% に届かないとき
- TSan が、Paho の中だけで指摘を出し、自分のコードでは直せないとき

いずれも `docs/open-questions.md` に記録し、その件に関わる作業を止めて報告する。iot-common-spec 側は変更しない。

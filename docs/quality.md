# 品質ゲート

作品02（mcu-cpp-01）の品質の検査と、その合格条件・動かし方・抑止の一覧・いまの数値です（P5。基本設計書 NFR-07・NFR-09、open-questions #78〜#92）。

## 検査の一覧

| 検査 | 何を確かめるか | 合格条件 | ローカルでの動かし方 | CI のジョブ |
|---|---|---|---|---|
| ビルド（host-debug） | ASan・UBSan 付きのビルドと全テスト、契約テスト、ブローカーの停止・再開 | 警告ゼロ、CTest 全件合格、契約テスト 9 件（既定・fakehw）、broker_restart.py 合格、ASan・UBSan の指摘ゼロ | `cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug` | host |
| 最適化ビルド（host-release） | `-O2` でだけ出る警告と、最適化で変わる振る舞い | 警告ゼロ、CTest 全件合格 | `cmake --preset host-release && cmake --build --preset host-release && ctest --preset host-release` | release |
| ThreadSanitizer（host-tsan） | Paho のスレッドとメインのスレッドの間のデータ競合（PahoMqttLink のトークン表など）。Paho の中だけの 2 件は抑止する（#89・#93） | CTest・契約テスト（既定）・broker_restart.py が合格し、TSan の指摘ゼロ | `cmake --preset host-tsan && cmake --build --preset host-tsan` の後、`TSAN_OPTIONS="suppressions=$PWD/tools/tsan.supp" ctest --preset host-tsan`。契約テストと broker_restart.py は `./build/host-tsan/mcu_cpp_01` を渡す | tsan |
| カバレッジ（NFR-07） | GCC の gcov と gcovr で、行・分岐のカバレッジ | `src/domain`・`src/app` の行カバレッジが層ごとに 80% 以上（#79・#88）。分岐と `src/infra`・`src/drivers`（fake を除く）は報告だけ | `tools/quality.sh coverage`（HTML は `build/quality/coverage/index.html`） | quality |
| clang-tidy | 下の「clang-tidy の設定」のチェック。`src/` 全体（`src/platform/host` も含む） | 警告ゼロ（警告はすべてエラー） | `tools/quality.sh tidy` | quality |
| cppcheck | `--enable=warning,style,performance,portability --std=c++20 --inline-suppr`、compile_commands.json を使う | 指摘ゼロ | `tools/quality.sh cppcheck` | quality |
| lizard（NFR-09） | 関数ごとの循環的複雑度（関数の長さと引数の数は報告だけ） | 複雑度 10 を超える関数ゼロ（`-C 10 -w`） | `tools/quality.sh lizard`（全関数の表は `build/quality/lizard/report.txt`） | quality |
| Doxygen（NFR-09） | `src/**/*.hpp` の公開 API のコメント（`@brief`・`@param`・`@return`） | 警告ゼロ（`WARN_IF_UNDOCUMENTED`・`WARN_NO_PARAMDOC`・`WARN_AS_ERROR=FAIL_ON_WARNINGS`） | `tools/quality.sh doxygen`（HTML は `build/quality/doxygen/html/index.html`。コミットしない） | quality |

`tools/quality.sh all` は coverage・tidy・cppcheck・lizard・doxygen を順に流し、1つでも不合格なら失敗で終わります。

### 道具の動かし方

検査の道具は、すべて Docker のイメージ `tools/docker/quality.Dockerfile`（ubuntu:24.04）の中で動かします。Mac（Docker Desktop）と CI で同じイメージを使います（この Mac は Homebrew が使えないため。#78）。
`tools/quality.sh` はイメージを作り、リポジトリを `/src` に置いたコンテナの中で自分を呼び直します。出力は `build/quality/<検査名>/` に置きます。

| 道具 | 版 | 入れ方 |
|---|---|---|
| GCC・gcov | 13.3.0 | apt（ubuntu:24.04） |
| clang-tidy | 18.1.3 | apt |
| cppcheck | 2.13.0 | apt |
| gcovr | 7.0 | apt |
| Doxygen | 1.9.8 | apt |
| lizard | 1.17.31 | pip（apt にないので版を固定して入れる） |

カバレッジ・clang-tidy・cppcheck は、イメージの中で構成した `build/host-coverage`（GCC、`--coverage`、サニタイザーなし）の compile_commands.json を使います。

### 対象から外すもの

- 外部のコード：`external/`（iot-common-spec）と FetchContent の依存（Paho・jsmn・GoogleTest）。どれも `src/` の外にあり、検査は `src/` だけを見ます
- フォントの表：`src/drivers/font5x7.hpp`（5×7 のグリフのデータ。cppcheck・lizard・Doxygen・カバレッジの対象から外す）
- 生成したもの：`build/` の下（コミットしない）

## clang-tidy の設定

`.clang-tidy` で有効にするのは `bugprone-*`・`cert-*`・`cppcoreguidelines-*`・`misc-*`・`modernize-*`・`performance-*`・`portability-*`・`readability-*` です（#80）。外したチェックと理由は次のとおりです。

| 外したチェック | 理由 | 決めたところ |
|---|---|---|
| `modernize-use-trailing-return-type` | 戻り値を後ろに書く形にそろえない | P5 プロンプト（#80） |
| `readability-identifier-length` | 狭い範囲のループや書き手の変数に `i`・`p`・`w` などの短い名前を使う | P5 プロンプト（#80） |
| `readability-magic-numbers`・`cppcoreguidelines-avoid-magic-numbers` | データシートのレジスタ値・ビット位置・仕様の上限を、出典のコメントつきでそのまま書く | P5 プロンプト（#80） |
| `misc-include-cleaner` | 標準ヘッダの細かい出どころまで求め、層の公開ヘッダの使い方と合わない | P5 プロンプト（#80） |
| `cppcoreguidelines-pro-bounds-pointer-arithmetic` | hal・drivers の固定長バッファで多発する | P5 プロンプト（#80） |
| `cppcoreguidelines-avoid-const-or-ref-data-members` | 依存は参照のメンバーで受け取る設計（必ずあり、差し替えない）。38 件 | io の判断（#90） |
| `cppcoreguidelines-pro-bounds-constant-array-index` | 固定容量のコンテナを、範囲を確かめた後の変数の添字で引く。88 件 | io の判断（#91） |
| `bugprone-easily-swappable-parameters` | 直すには強い型で公開 API を変える必要がある（P5 は API を変えない。P8 で検討）。25 件 | io の判断（#92） |

チェックの設定：`cppcoreguidelines-special-member-functions.AllowSoleDefaultDtor: true`。インターフェース（`virtual ~X() = default` だけを持つ抽象クラス）にコピー・ムーブの宣言を求めません（20 件）。

## 抑止の一覧

コードの中の抑止は 15 件です（clang-tidy 7 件・cppcheck 6 件・TSan 2 件）。lizard と Doxygen の抑止はありません。

| # | 道具 | 場所 | 抑止 | 理由 |
|---|---|---|---|---|
| 1 | clang-tidy | `src/domain/specs.hpp`（`max_on_s`） | `readability-redundant-member-init` | 構成表の集成体初期化で後ろの項目を省くので、`{}` がないと `-Wmissing-field-initializers` が出る |
| 2 | clang-tidy | `src/domain/specs.hpp`（`min_on_ms`） | `readability-redundant-member-init` | 同上 |
| 3 | clang-tidy | `src/domain/specs.hpp`（`min_off_ms`） | `readability-redundant-member-init` | 同上 |
| 4 | clang-tidy | `src/platform/host/main.cpp`（`g_stop`） | `cppcoreguidelines-avoid-non-const-global-variables` | シグナルハンドラーから書くので、名前空間の `volatile std::sig_atomic_t` にする必要がある |
| 5 | clang-tidy | `src/platform/host/jsonl_log_sink.cpp`（コンストラクタの `fopen`） | `cppcoreguidelines-owning-memory` | `FILE*` はこのクラスが持ち、デストラクタで閉じる（gsl::owner は使わない） |
| 6 | clang-tidy | `src/platform/host/jsonl_log_sink.cpp`（デストラクタの `fclose`） | `cppcoreguidelines-owning-memory` | 同上 |
| 7 | clang-tidy | `src/platform/host/paho_mqtt_link.cpp`（`msg.payload`） | `cppcoreguidelines-pro-type-const-cast` | Paho の API は const を受けないが、送る前に写しを取るので書き換えない |
| 8 | cppcheck | `src/support/result.hpp`（`Result(T)`） | `noExplicitConstructor` | 値・エラーをそのまま `return` できるように暗黙の変換にする（std::expected と同じ考え方） |
| 9 | cppcheck | `src/support/result.hpp`（`Result(const Error&)`） | `noExplicitConstructor` | 同上 |
| 10 | cppcheck | `src/support/result.hpp`（`Result<void>(const Error&)`） | `noExplicitConstructor` | 同上 |
| 11 | cppcheck | `src/platform/host/paho_mqtt_link.cpp`（onSuccess のラムダ） | `constParameterPointer` | 引数の型は Paho のコールバックの型どおりで、const にできない |
| 12 | cppcheck | `src/platform/host/paho_mqtt_link.cpp`（onFailure のラムダ） | `constParameterPointer` | 同上 |
| 13 | cppcheck | `src/app/node.cpp`（`build_offline_state` の `buf`） | `constParameterReference` | `buf` は JsonWriter が書き込むので const にできない（cppcheck の誤検出） |
| 14 | TSan | `tools/tsan.supp` | `race:MQTTAsync_checkTimeouts` | Paho v1.3.16 の中だけの race。`MQTTAsync_connect` がミューテックスを外した後にビットフィールドを書き、送信スレッドが同じワードを読む。自分のコードは公開 API を呼ぶだけで避けられない（#89） |
| 15 | TSan | `tools/tsan.supp` | `race:MQTTAsync_tostop` | Paho v1.3.16 の中だけの race（CI の GCC の TSan で出る）。再接続のとき `MQTTAsync_connect` がミューテックスの中でグローバル変数 `MQTTAsync_tostop` を書き、送信・受信スレッドがミューテックスなしで読む。変数名で絞り、この変数の race だけを外す（#93） |

ほかに、P1 からあるコンパイラの警告の抑止が 1 つあります：`src/platform/host/paho_mqtt_link.cpp` の `NODE_PAHO_INIT_BEGIN`／`END`（GCC の `-Wmissing-field-initializers` を、Paho の初期化マクロ `MQTTAsync_*_initializer` の間だけ外す。Paho のマクロが後ろの項目を省くため）。

## いまの数値（P5 の完了時）

### カバレッジ（GCC 13.3、gcovr 7.0、CTest 206 件）

| 層 | 行 | 分岐 | 合格条件 |
|---|---|---|---|
| `src/domain` | 92.4%（220／238） | 86.5%（135／156） | 行 80% 以上 |
| `src/app` | 95.9%（1137／1186） | 86.0%（614／714） | 行 80% 以上 |
| `src/infra` | 96.1%（520／541） | 81.2%（328／404） | 報告だけ |
| `src/drivers`（fake を除く） | 88.4%（335／379） | 70.6%（120／170） | 報告だけ |
| `src/` 全体（参考） | 78.3%（2657／3393） | 67.5%（1329／1969） | — |

`src/` 全体の値が低いのは、`src/platform/host`（Paho・main。CTest では動かさず、契約テストと broker_restart.py で動かす）を含むためです。

### 複雑度（lizard 1.17.31）

- 関数 625 個、平均の循環的複雑度 2.4、最大 10（6 関数：`Bme280::init`・`PahoMqttLink::publish`・`CommandService::set_actuator`・`CommandService::precheck`・`JsonWriter::put_escaped`・`support::to_string`）
- 最も長い関数：`main`（55 行）。引数の最大：`Node::Node`（8 個）

### その他

- clang-tidy の指摘：0 件
- cppcheck の指摘：0 件
- Doxygen の警告：0 件

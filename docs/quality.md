# 品質ゲート

作品02（mcu-cpp-01）の品質の検査と、その合格条件・動かし方・抑止の一覧・いまの数値です（P5。基本設計書 NFR-07・NFR-09、open-questions #78〜#92）。

## 検査の一覧

| 検査 | 何を確かめるか | 合格条件 | ローカルでの動かし方 | CI のジョブ |
|---|---|---|---|---|
| ビルド（host-debug） | ASan・UBSan 付きのビルドと全テスト、契約テスト、ブローカーの停止・再開 | 警告ゼロ、CTest 全件合格、契約テスト 9 件（既定・fakehw）、broker_restart.py 合格、ASan・UBSan の指摘ゼロ | `cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug` | host |
| 最適化ビルド（host-release） | `-O2` でだけ出る警告と、最適化で変わる振る舞い | 警告ゼロ、CTest 全件合格 | `cmake --preset host-release && cmake --build --preset host-release && ctest --preset host-release` | release |
| ThreadSanitizer（host-tsan） | Paho のスレッドとメインのスレッドの間のデータ競合（PahoMqttLink のトークン表など）。Paho の中だけの 2 件は抑止する（#89・#93） | CTest・契約テスト（既定）・broker_restart.py が合格し、TSan の指摘ゼロ | `cmake --preset host-tsan && cmake --build --preset host-tsan` の後、`TSAN_OPTIONS="suppressions=$PWD/tools/tsan.supp" ctest --preset host-tsan`。契約テストと broker_restart.py は `./build/host-tsan/mcu_cpp_01` を渡す | tsan |
| カバレッジ（NFR-07） | GCC の gcov と gcovr で、行・分岐のカバレッジ | `src/domain`・`src/app` の行カバレッジが層ごとに 80% 以上（#79・#88）。分岐と `src/infra`・`src/drivers`（fake を除く）は報告だけ | `tools/quality.sh coverage`（HTML は `build/quality/coverage/index.html`） | quality |
| ターゲット版のビルド（P6） | ESP-IDF v5.5.6 で ESP32-S3 向けにビルドできること、自分のコンポーネント（node_core・node_platform_esp32）の警告 | 警告ゼロ（`-Werror` と CLAUDE.md の警告。ESP-IDF 本体には当てない）。`idf.py size` を成果物にする | 下の「ターゲット版のビルド」 | firmware |
| clang-tidy | 下の「clang-tidy の設定」のチェック。`src/` 全体（`src/platform/host` も含む。`src/platform/esp32` は対象外：下記） | 警告ゼロ（警告はすべてエラー） | `tools/quality.sh tidy` | quality |
| cppcheck | `--enable=warning,style,performance,portability --std=c++20 --inline-suppr`。ホスト版は compile_commands.json を使う。`src/platform/esp32` はファイルを渡して読む（下記） | 指摘ゼロ | `tools/quality.sh cppcheck` | quality |
| lizard（NFR-09） | 関数ごとの循環的複雑度（関数の長さと引数の数は報告だけ）。`src/platform/esp32` を含む | 複雑度 10 を超える関数ゼロ（`-C 10 -w`） | `tools/quality.sh lizard`（全関数の表は `build/quality/lizard/report.txt`） | quality |
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

### ターゲット版のビルド（P6）

ESP-IDF は v5.5.6（v5 系の最新の安定版。2026-10-16 公開）に固定し、Espressif の公式 Docker イメージ `espressif/idf:v5.5.6`（xtensa-esp-elf-g++ 14.2.0）でビルドします。Mac と CI で同じイメージです。

```sh
docker run --rm -v "$PWD":/project -w /project/firmware espressif/idf:v5.5.6 \
  bash -c '. $IDF_PATH/export.sh >/dev/null && idf.py -B /project/build/firmware build && idf.py -B /project/build/firmware size'
```

- 自分のコンポーネントは `-std=gnu++20`・例外と RTTI なし・CLAUDE.md の警告（`-Werror`）。設定は `firmware/components/node_flags.cmake`
- FreeRTOS の API のマクロ（`xQueueSend` など）は展開すると古い形のキャストになり `-Wold-style-cast` に当たるので、マクロが呼ぶ公開の関数を `src/platform/esp32/rtos.hpp` から直接呼ぶ（抑止はしない）
- jsmn はホスト版と同じく SYSTEM のインクルード（外部のコード）
- **clang-tidy は `src/platform/esp32` を対象にしない**：xtensa の GCC のフラグ（`-mlongcalls` など）と ESP-IDF のヘッダを clang が読めないため（#109）。`src/platform/esp32` は cppcheck と lizard で見る
- **cppcheck の `src/platform/esp32`**：品質のイメージに ESP-IDF のヘッダがないので、ファイルを渡して `-I src` で読み、見つからない include の知らせ（`missingInclude`・`missingIncludeSystem`）だけを外す

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

コードの中の抑止は 16 件です（clang-tidy 7 件・cppcheck 7 件・TSan 2 件）。lizard と Doxygen の抑止はありません。

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
| 16 | cppcheck | `src/platform/esp32/esp32_mqtt.cpp`（`EspMqttComm::on_event`） | `constParameterCallback` | 引数の型は esp-mqtt のイベントハンドラー（`esp_event_handler_t`）の型どおりで、const にできない（P6） |
| 15 | TSan | `tools/tsan.supp` | `race:MQTTAsync_tostop` | Paho v1.3.16 の中だけの race（CI の GCC の TSan で出る）。再接続のとき `MQTTAsync_connect` がミューテックスの中でグローバル変数 `MQTTAsync_tostop` を書き、送信・受信スレッドがミューテックスなしで読む。変数名で絞り、この変数の race だけを外す（#93） |

ほかに、P1 からあるコンパイラの警告の抑止が 1 つあります：`src/platform/host/paho_mqtt_link.cpp` の `NODE_PAHO_INIT_BEGIN`／`END`（GCC の `-Wmissing-field-initializers` を、Paho の初期化マクロ `MQTTAsync_*_initializer` の間だけ外す。Paho のマクロが後ろの項目を省くため）。

## エミュレーター（P7）

ターゲット版のファームウェアを、QEMU と Wokwi で動かして確かめます。どちらも実機の代わりにはならないので、確かめられないことを下の表にまとめます。

### 動かし方

```sh
# QEMU（Docker の espressif/idf:v5.5.6 の中で動かす。Mac と CI で同じ）
tools/qemu.sh test                 # 構成 qemu：起動・4つのタスク・BOOT（power_on）・stats・スタックの残り
docker compose -f external/iot-common-spec/docker-compose.yml up -d mosquitto
tools/qemu.sh contract             # 構成 qemu-fakehw：ファームウェアに契約テスト（--role mcu-cpp-01、9件）
tools/qemu.sh soak 30              # 構成 qemu-fakehw：Mosquitto につないで30分。NFR-03（5分後と終わりのヒープの最低残量）

# Wokwi（wokwi-cli v0.29.1。トークンは環境変数 WOKWI_CLI_TOKEN か ~/.config/wokwi/token）
tools/wokwi.sh                     # 4本のシナリオ（boot_readings・buttons_mode・wifi_heap・watchdog）
```

構成は `firmware/sdkconfig.defaults` に、構成ごとのファイルを重ねて作ります（`NODE_SDKCONFIG_EXTRA`）。試験用の Kconfig の項目は、どれも既定で無効です。製品の構成（`firmware/sdkconfig.defaults` だけ）には入りません。

| 構成 | 重ねるファイル | 試験用の項目 | 使うところ |
|---|---|---|---|
| qemu | `sdkconfig.qemu` | `NODE_NET_QEMU_ETH`（OpenCores Ethernet。#120）・`NODE_TEST_NO_ADC`（#129） | `tools/qemu.sh test` |
| qemu-fakehw | ＋`sdkconfig.qemu-fakehw` | ＋`NODE_TEST_FAKE_HW`（hal をフェイクに替える。#132） | `tools/qemu.sh contract`・`soak` |
| wokwi | `sdkconfig.wokwi` | `NODE_WIFI_ALLOW_OPEN`（Wokwi-GUEST。#128） | `tools/wokwi.sh` |
| wokwi-hang | ＋`sdkconfig.test-hang` | ＋`NODE_TEST_HANG_TASK`（ui が10秒で止まる。#119・#131） | `tools/wokwi.sh watchdog` |

MQTT のパスワードは、ビルドのディレクトリ（`build/`。コミットしない）にだけ書きます。ログの判定は `tools/emu_check.py` です。

### 確かめられること・確かめられないこと

| 項目 | QEMU | Wokwi | 出典・理由 |
|---|---|---|---|
| 起動・4つのタスク・BOOT（power_on） | ○ | ○ | 両方の試験で確かめた |
| タスクウォッチドッグからの再起動（BOOT が watchdog） | ×（働かない。#131） | ○ | QEMU：資料は「Timer Group watchdog timers are emulated」とするが、ESP-IDF v5.5.6 同梱の QEMU（esp_develop_9.2.2_20260417）では段階の割り込みもリセットも起きなかった（#131） |
| Wi-Fi | × | ○（Wokwi-GUEST） | QEMU：esp-toolchain-docs の `qemu/esp32s3/README.md` の対応する周辺の一覧に Wi-Fi がない。Wokwi：docs.wokwi.com/guides/esp32-wifi「virtual WiFi access point called Wokwi-GUEST. It is an open access point」（チャネル 6） |
| MQTT（ACL・パスワード付きの Mosquitto）と契約テスト | ○（OpenCores Ethernet。9件合格） | × | QEMU：同 README の一覧に「OpenCores Ethernet MAC」。`idf.py qemu` も `-nic user,model=open_eth` を付ける。Wokwi：ローカルにつなぐ Private Gateway（`host.wokwi.internal`）は「only available for paying users」（同 esp32-wifi）で、wokwi-cli・CI で使えるかは資料に書かれていない。公開のブローカーは使わない（#122） |
| I2C の機器（BME280・BH1750・SSD1306） | ×（フェイクに替えて確かめる。#132） | ○（BME280・BH1750 はカスタムチップ、SSD1306 は部品） | QEMU：同 README の一覧に I2C がない。Wokwi：部品の一覧に BME280・BH1750 がない。チップは Custom Chips API（`wokwi-api.h`）でデータシートのレジスタだけを再現し、値は P3 のフェイクと同じ（#127） |
| ADC（土壌水分） | ×（起動時に止まるので使わない。#129） | ○（ポテンショメーター） | QEMU：同 README の一覧に ADC がない。esp_adc の起動時の校正が終わらない（#129） |
| PWM（LEDC）・GPIO の出力 | × | △（LED・リレーをつなぐが、シナリオでは確かめない） | QEMU：同 README の一覧に LEDC・GPIO がない |
| ボタンとモード（FR-08） | × | ○ | Wokwi：wokwi-pushbutton の制御 `pressed` |
| OLED の表示 | × | ○（画面の画像を比べる）。消灯（Display OFF）は写らない | Wokwi の画面の取り込みは表示のメモリの内容（P7 で確かめた）。表示の向きは180度回る（#134。実機フェーズで確かめる） |
| ヒープの最低残量（Wi-Fi が動いた後） | 参考（Wi-Fi なし） | ○ | 判定は Wokwi（#115） |
| NFR-03（ヒープが減らない） | ○（30分） | × | 24時間は実機フェーズ（#125） |
| I2C のバスの回復・センサーの故障と復帰 | × | × | どちらも機器の故障を入れられない。ホスト版（fakehw と単体テスト）で確かめる |
| ライトスリープ・消費電流・電源の瞬断 | × | × | P6 で入れていない（#107）。実機フェーズ |

### 測った値（P7）

スタックの残りの最小（high water mark。単位はバイト）。各タスクで最も少なかった値を太字にしています。

| タスク | 大きさ（#97） | QEMU・機器なし（75秒） | QEMU・fakehw・MQTT 接続（30分） | Wokwi・Wi-Fi（62秒） | 最小（割合） |
|---|---|---|---|---|---|
| control | 8,192 | 4,768 | **3,768** | 4,856 | 3,768（46%） |
| sense | 4,096 | **1,920** | 2,392 | 1,960 | 1,920（47%） |
| comm | 6,144 | 4,560 | **4,424** | 4,568 | 4,424（72%） |
| ui | 4,096 | **1,696** | 2,120 | 1,736 | 1,696（41%） |

どのタスクも 25% 以上残るので、#97 の値は変えません（#124）。ただし、どの環境でも通らない経路があります。I2C のバスの回復（SCL の15回のクロック）、Wi-Fi の切断と再接続、MQTT の再接続の直後に一度に送る処理です。実機フェーズで、これらを通した後の値を見直します。

ヒープ（`stats` の heap_min と、wokwi-cli の `--heap-csv` の標本）：

| 環境 | heap_min | 判定 |
|---|---|---|
| Wokwi：Wi-Fi に接続した後（MQTT は接続を試みるだけ） | 144,432 バイト | 30KB 以上（#115）：合格 |
| QEMU：Ethernet と MQTT で30分 | 5分後 210,592 → 30分後 210,592 バイト | 下がっていない（NFR-03。#125）：合格 |

Wi-Fi と MQTT の接続を同時に測れる環境はありません。QEMU では、Ethernet・lwIP・esp-mqtt が動いた状態での heap_free が約 217KB でした。Wokwi では、Wi-Fi が動いた状態で約 147KB でした。両方が動いても 30KB を大きく上回る見込みですが、確かめるのは実機フェーズです。

### Wokwi の時間と無料枠

- 無料の利用者の CI は、シミュレーションの時間で30日あたり50分まで、1回の実行は実際の経過時間で5分まで（docs.wokwi.com/wokwi-ci/getting-started「Free users: 50 minutes」「The simulation time is calculated as the sum of the simulation time of all the tests」）
- `tools/wokwi.sh` は、シナリオごとのシミュレーションの時間と合計をログに出す。P7 の目安：boot_readings 13秒・buttons_mode 41秒・wifi_heap 62秒・watchdog 17秒、合計で約133秒。無料枠では30日におよそ22回流せる
- タイムアウト（シミュレーションの時間）は、目安の約2倍に設定している。boot_readings 40秒・buttons_mode 60秒・wifi_heap 90秒・watchdog 40秒
- CI の Wokwi は別のワークフロー（`.github/workflows/wokwi.yml`）。動くのは次のときだけ：main への push で `firmware/`・`src/`・`wokwi/` が変わったとき、PR にラベル `wokwi` を付けたとき、手動の実行のとき（#133・#138。手動の実行は、ワークフローのファイルが main にあるときだけ使える）
- Wokwi のサーバーとの接続が、シミュレーションが始まる前に切れることがある（API Error、code 1006。P7 で2回あった）。時間を使わずに失敗したときだけ、15秒待って2回まで流し直す

## RAM の見直し（P7）

振る舞いは変えずに、持ち方だけを変えました（#116）。

- **イベントの保持と確認待ち**：JSON の写しではなく、電文を作り直すのに要る値を持つ。送った後は msg_id・seq・ts も持つ（`infra::EnvelopeFields`）
- **重複排除の64件**：ack を作り直すのに要る値（`domain::AckRecord`）を持つ
- **送り直し待ち**：8件の1件あたりを 2KB から 1KB にした。cmd_ack と state の最大の電文が収まることを、単体テストで確かめている

作り直した電文が元とバイト単位で同じになることは、単体テストで確かめています（イベント、確認待ち、9種類の cmd への重複した ack）。

| 区分（`idf.py size`、製品の構成） | 見直す前（P6） | 見直した後（P7） |
|---|---|---|
| DIRAM | 303,535／341,760 バイト（88.8%） | 231,439／341,760 バイト（67.7%。残り 110,321） |
| Node（静的に確保） | 124.5KB | 52.4KB |
| フラッシュ（コード） | 744,666 バイト | 745,294 バイト |
| フラッシュ（データ） | 146,044 バイト | 146,060 バイト |
| イメージ全体 | 1,010,381 バイト | 1,011,025 バイト |

- **IRAM の 100%**：ESP32-S3 では、命令の専用の 16KB を使い切った分は、データと共有の DIRAM に置かれます。そのため見る値は DIRAM です。出典は ESP-IDF v5.5.6 のリンカースクリプト（`memory.ld`・`sections.ld`「iram0_0_seg and dram0_0_seg reflect the same address space」）と、memory-types.rst です（#126）
- **comm のリングバッファ**（静的な領域で、DIRAM に含まれる）は変えていません。理由は次のとおりです
  - 送信の依頼（16KB）：再接続の直後に一度にたまる量で考えた。保持しているイベント32件（QoS 1 の確認待ちの上限も32件）に state と telemetry を足すと、約13KB になる。電文の大きさは実測で、state 742・event 約300・telemetry 最大731 バイトに、トピックと見出しを足した。あふれても publish が Busy を返し、Publisher が種類ごとの決まりどおり後回しにするか捨てる（#15）
  - 受信（8KB）：最大の cmd（電文 2,048 バイト＋トピックと見出し）を3件以上持てる。control は 10ms ごとに取り出す

## いまの数値（P7 の完了時）

### カバレッジ（GCC 13.3、gcovr 7.0、CTest 223 件）

| 層 | 行 | 分岐 | 合格条件 |
|---|---|---|---|
| `src/domain` | 92.5%（222／240） | 86.2%（131／152） | 行 80% 以上 |
| `src/app` | 96.1%（1163／1210） | 85.8%（602／702） | 行 80% 以上 |
| `src/infra` | 96.2%（530／551） | 82.0%（333／406） | 報告だけ |
| `src/drivers`（fake を除く） | 89.4%（415／464） | 73.9%（176／238） | 報告だけ |
| `src/` 全体（参考） | 79.1%（2782／3519） | 67.7%（1376／2031） | — |

`src/` 全体の値が低いのは、`src/platform/host`（Paho・main）を含むためです。これは CTest では動かさず、契約テストと broker_restart.py で動かしています。`src/platform/esp32` はホスト版でビルドしないので、数に入りません（QEMU と Wokwi で動かす）。

### ターゲット版のイメージの大きさ（`idf.py size`、ESP-IDF v5.5.6、製品の構成）

| 区分 | 使用 | 割合 |
|---|---|---|
| フラッシュ（コード `.text`） | 745,294 バイト | — |
| フラッシュ（データ `.rodata` など） | 146,060 バイト | — |
| DIRAM（`.bss`・`.data`・`.text`） | 231,439／341,760 バイト | 67.7%（残り 110,321） |
| IRAM | 16,384／16,384 バイト | 100%（あふれた分は DIRAM に置かれる。#126） |
| イメージ全体 | 1,011,025 バイト | — |

見直す前（P6）との比べは「RAM の見直し（P7）」にあります。Wi-Fi が動いた後のヒープの最低残量（Wokwi）は 144,432 バイトです。

### 複雑度（lizard 1.17.31）

- 関数 745 個（`src/platform/esp32` を含む）、平均の循環的複雑度 2.5、最大 10（6 関数：`Bme280::init`・`PahoMqttLink::publish`・`CommandService::set_actuator`・`CommandService::precheck`・`JsonWriter::put_escaped`・`support::to_string`）
- 最も長い関数：`main`（ホスト版。57 行）。引数の最大：`Node::Node`（10 個）

### その他

- clang-tidy の指摘：0 件
- cppcheck の指摘：0 件
- Doxygen の警告：0 件

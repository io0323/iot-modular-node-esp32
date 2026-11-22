# 作品02 フェーズ P1：ホスト版の骨組み・MQTT 接続・契約テスト（CI）

## フェーズ全体（参考）

| フェーズ | 内容 |
|---|---|
| **P1** | **ホスト版の骨組み、MQTT 接続、cmd の受け付け、CI での契約テスト（このプロンプト）** |
| P2 | 制御の中核：ControlArbiter の5段の優先度、ModeManager、RuleEngine、インターロックの残り（ファン・センサー故障）、遷移ごとのイベント |
| P3 | 機器ドライバ：II2cBus・IAdc・IPwm・IGpio、BME280・BH1750・土壌水分・PWM・リレー、データシートに沿った I2C フェイク、DeviceRegistry と構成表 |
| P4 | 通信の作り込み：SNTP 同期後の接続と uptime からの ts の逆算、イベント保持32件、指数バックオフ、送信量の制限、ブローカー停止・復旧の結合テスト |
| P5 | 品質ゲート：カバレッジ80%、clang-tidy、cppcheck、lizard、Doxygen、ThreadSanitizer |
| P6 | ESP-IDF ターゲット版：platform/esp32、FreeRTOS のタスク、TWDT とリセット要因、NVS、esp-mqtt、CI でのビルド |
| P7 | エミュレーター：QEMU（起動・WDT・リセット要因）、Wokwi（センサー・PWM・Wi-Fi 経由の MQTT） |
| P8 | 拡張性の実証（SHT31 を追加する PR）と書類の仕上げ |

P2 以降のプロンプトは、P1 の結果を見てから作る。

## はじめに読むもの

1. `CLAUDE.md`（このリポジトリの決まり。必ず従う）
2. `docs/design/01-requirements-basic-design.md`（とくに第5・6・7章）
3. iot-common-spec の `docs/spec.md` 第2〜6章・第8章、`docs/contract-tests.md`、`profiles/mcu-cpp-01.yaml`、`schemas/`（common・telemetry・event・state・cmd・cmd_ack）
4. `src/iotspec/contract/roles/mcu_cpp_01.py` と `roles/common.py`（契約テストが何を期待しているか）
5. `src/iotspec/sim/devices/mcu_cpp_01.py`（作品02の代役。参考。仕様ではない）

読み終えたら、作業に入る前に「P1 で作るもの」「作らないもの」「不明点」を短くまとめて報告する。不明点があれば open-questions に記録してから進めること。

## 準備

- main から `phase/p1-host-skeleton` を切る。`CLAUDE.md`・`docs/design/`・`docs/prompts/` が未コミットなら、このブランチの最初のコミットに入れる
- `docs/open-questions.md` を作る。表の列は「# | 区分（作品02／共通仕様）| 論点 | 選択肢 | 推奨 | 状態（未決／仮実装／決定）」。基本設計書 第7章「未決事項」のうち、まだチェックの付いていないものを最初の行として写す
- submodule `external/iot-common-spec` を追加し、タグ **v0.2.2** に固定する。v0.2.2 のタグがまだなければ v0.2.1 に固定し、その旨を open-questions と README に書いて進める（P1 の範囲では v0.2.1 と v0.2.2 で作品02への要求は変わらない）
- FetchContent で取り込む（バージョンはすべてタグかコミットに固定し、README に書く）
  - GoogleTest（gMock を含む）：最新の安定タグ
  - Eclipse Paho MQTT C（`paho.mqtt.c`）：最新の安定タグ。TLS なし、静的ライブラリ、MQTTAsync を使う。ホスト版の MQTT クライアントとしてだけ使う
  - jsmn（zserge/jsmn）：固定したコミット。cmd の JSON の解析に使う（ヒープを使わない）

## ディレクトリ

```
CMakeLists.txt  CMakePresets.json  cmake/
src/
  support/          Result・Error・固定容量コンテナ（Error Framework の仮の代わり）
  domain/           値・機器の契約・リース・重複排除・ポンプのインターロック
  app/              CommandService・ControlArbiter（仮版）・TelemetryService・StateService・EventService
  infra/            IMqttLink・IClock・IRandom（インターフェース）、JsonWriter・CmdParser・Envelope・Topic・RateLimiter
  drivers/sim/      ホスト用の模擬センサー・模擬アクチュエーター
  config/           devices.cpp（構成表）
  platform/host/    PahoMqttLink・HostClock・HostRandom・main
tests/unit/  tests/integration/
docs/design/  docs/prompts/  docs/open-questions.md
external/iot-common-spec/
```

## P1 で作るもの

### 1. ビルド

- `CMakeLists.txt`、`CMakePresets.json`（P1 は `host-debug` だけ。Ninja、出力先 `build/host`、ASan・UBSan 付き）、`cmake/` の警告設定（CLAUDE.md のとおり）、層ごとのライブラリ（support・domain・infra・app・drivers_sim・platform_host）と実行ファイル `build/host/mcu_cpp_01`。`.gitignore`
- 製品コードは `-fno-exceptions -fno-rtti`。テストの実行ファイルは GoogleTest の都合で既定のままでよい

### 2. support

- `Result<T>`・`Result<void>`・`Error`（`ErrorCode` の列挙と短い文字列）。`[[nodiscard]]`
- `StaticVector<T, N>`、`FixedString<N>`
- ファイルの先頭に「Error Framework の仮の代わり。取り込み元が決まったら置き換える」と書く

### 3. domain

- `Level`（0–1000 の整数）、`ChannelSpec`（channel・unit・period_ms）、`ActuatorSpec`（id・min・max・max_on_s。上限なしは「なし」）
- `ISensor`・`IActuator`（基本設計書 第5章のインターフェース。P1 では模擬の機器だけが実装する）
- `Lease`：起点は受信時刻（単調時計の ms）。`lease_s` は省略時60・上限600。「起点＋lease_s×1000」ちょうどで期限切れ
- `CmdDedupCache`：直近64件・10分。cmd_id → 前回の ack の payload。64件を超えたら古いものから消す。rejected の結果も覚える
- `PumpInterlock`：連続 max_on_s 秒で出力を0にする。再開できるのは「要求がいったん0に戻り（リース切れ・release・level 0）、かつ停止から60秒経った」後だけ。効いた・解除したの遷移を返す
- `Owner`：interlock・safety・manual・auto・schedule・local・none

### 4. app

- **ControlArbiter（仮版。P2 で5段にする）**：アクチュエーターごとに「インターロック ＞ 上位指令（リース中）＞ 既定値（0）」で出力を決め、owner を返す（インターロック → `interlock`、リース中 → cmd の priority、既定値 → `none`）。同じ対象への新しい set_actuator は、priority によらず古い指令を置き換える。リースが切れたら下位に戻す。P1 では本体ボタンとローカルルールがないので `local` は出ない
- **CommandService**：基本設計書 第6章「指令の受け付け」の表どおり
  - set_actuator：level を min〜max に収めて出力し、ack は done＋`applied: {"level": <出力した値>}`。丸めたら `limited: true`。max_on_s のある対象に lease_s が max_on_s を超える指令が来たら、受けた時点で `limited: true`（applied はその時点の出力）。ポンプの再開待ち中なら出力は0のままで `limited: true`。土壌水分（soil）の quality が fault の間のポンプは rejected（unsafe）
  - release：done。その対象のリースを解除する
  - set_mode：auto・manual・power_save なら done（P1 は現在のモードを記録し、MODE_CHANGED を1件送り、state を出し直すだけ。モードによる動きの違いは P2）。ほかの値は rejected（invalid_arg）
  - get_status：done。state を出し直す
  - set_param・未知の op：rejected（unknown_op）
  - 共通：`expires_at` を過ぎていたら rejected（expired）。未知の機器 ID は unknown_target。範囲外の値・必須の欠け・型違い（level が整数でない、lease_s が 1〜600 の外など）は invalid_arg。`lease_s` は set_actuator 以外では値によらず無視する
  - JSON として読めない、または cmd_id が読めない電文は ack を返さずに破棄し、理由ごとに数える
  - 重複した cmd_id（CmdDedupCache にある）は実行せず、payload は前回と同じ、エンベロープ（msg_id・seq・ts）は新しい ack を返す
  - 即時に終わるので accepted は送らず done を返す
- **TelemetryService**：チャネルごとの周期（profile の period_ms：temp_air・humidity・pressure 2000ms、lux 1000ms、soil 10000ms）が来たチャネルの readings と、**全アクチュエーターの level と owner** を入れて送る（QoS 0）
- **StateService**：state（QoS 1、retain）。status・mode・channels[]（period_ms）・actuators[]（min・max・max_on_s。ポンプは30、ほかは null）・fw_version（`"0.1"`）。接続したとき・モードを変えたとき・get_status のときに送る。offline の state は LWT と正常終了のときに使い、seq を進めない（その時点で次に使う seq を入れる）
- **EventService**：BOOT（info。`detail.reset_reason` は環境変数 `MCU_CPP_01_RESET_REASON`、なければ `power_on`。`detail.platform_reason` は `"HOST"`）、MODE_CHANGED（info）、INTERLOCK_ACTIVE（warning）／INTERLOCK_RELEASED（info）（ポンプのインターロックが実際に効いた・解除したとき。丸めた指令を受けただけでは出さない）。遷移1回につき1件。接続前のイベントは固定容量（32件）にためて、接続後に送る

### 5. infra

- `IMqttLink`：接続（URL・client id・ユーザー名・パスワード・LWT）、publish（topic・payload・QoS・retain）、subscribe、受信の取り出し、接続状態の通知。QoS 1 の確認待ちの件数を自分で数え、32件に達したら publish は `busy` を返す
- `IClock`：単調時計（ms）、UTC の現在時刻、`synced()`（P1 のホスト版は常に true。SNTP の扱いは P4）
- `IRandom`：乱数（boot_id と ULID に使う）
- `JsonWriter`：固定バッファに書く。文字列のエスケープ、整数、小数（readings の value は最大3桁の小数）。バッファが足りなければエラー
- `CmdParser`：jsmn で cmd の電文をトークンに分け、固定長の `CmdRequest` に詰める。エンベロープ（schema・msg_id・device_id・seq・boot_id・ts・ts_source）の形も確かめる
- `Envelope`：schema（`iot.v1.{kind}`。cmd/ack は `iot.v1.cmd_ack`）、msg_id（ULID。48ビットの ms 時刻＋80ビットの乱数、Crockford Base32 の26文字）、device_id、seq（kind ごと、0 から、uint32 で一周）、boot_id（16進小文字8桁、起動時に IRandom から）、ts（UTC、ミリ秒3桁、末尾 Z）、ts_source（`device`）、uptime_ms
- `Topic`：`iot/v1/{site}/mcu-cpp-01/{kind}`。site の既定は `home`。QoS と retain は spec.md 第2章の表どおり
- `RateLimiter`：kind ごとに平均毎秒10件・瞬間50件（トークンバケツ）。超えた telemetry は捨てて数える

### 6. drivers/sim と構成表

- `SimSensor`：決まった値を返す（temp_air 25.0 degC、humidity 50.0 pct_rh、pressure 1013.25 hPa、lux 300 lux、soil 40.0 pct、quality good）。BME280 に当たる模擬機器は1台で3チャネルを返す
- `SimActuator`：受けた level を覚えて返すだけ
- `src/config/devices.cpp`：profile と同じ5チャネル・4アクチュエーター（fan_main・led_grow・pump（max_on_s 30）・buzzer）。インスタンスは静的に確保し、レジストリは参照だけを持つ

### 7. platform/host

- `PahoMqttLink`（MQTTAsync）：client id・ユーザー名とも `mcu-cpp-01`、パスワードは `IOTSPEC_MQTT_PASSWORD`（既定 `iotspec-dev`）。clean session、keepalive 10秒。**LWT は接続のたびに作り直す**（offline の state、retain、QoS 1、seq はその時点で次に使う値、ts は接続時刻、boot_id は online の state と同じ）。`maxInflight` も32に設定する。P1 の再接続は1秒ごとの単純な再試行でよい（指数バックオフは P4）
- Paho のコールバックは別スレッドで来るので、受信は固定容量のキュー（mutex と condvar）に入れ、アプリのロジックは main のスレッドだけで動かす
- `HostClock`・`HostRandom`（`getentropy()`。macOS は `<sys/random.h>`）
- `main`：`mcu_cpp_01 [--broker host:port] [--site <site>] [--log <file.jsonl>]`。`--broker` がなければ `IOTSPEC_MQTT_URL`（既定 `mqtt://localhost:1883`）。起動したら BOOT をためて接続し、接続できたら state（online）→ BOOT の順に送る。10ms ごとに tick する。SIGTERM・SIGINT では offline の state を送ってから切断し、終了コード 0。ログは JSON Lines（boot・connect・disconnect・tx・rx・rx_discard・ack・diag）

### 8. 単体テスト（GoogleTest＋gMock）

- support：Result、StaticVector、FixedString
- domain：Lease（省略時60・上限600・期限ちょうど）、CmdDedupCache（64件の押し出し、10分、rejected も覚える）、PumpInterlock（30秒で停止、要求が0に戻らないと再開しない、60秒経たないと再開しない、両方そろうと再開する、遷移が1回ずつ）
- app：CommandService（表の全行、reason ごと、expired、lease_s の無視、重複 cmd_id で payload が同じでエンベロープが新しい、soil fault 中のポンプの unsafe、lease_s が max_on_s を超えるときの limited）、ControlArbiter（優先度と owner、置き換え、リース切れで none）、TelemetryService（周期ごとのチャネル、全アクチュエーターを含む）、StateService（offline の seq を進めない）、EventService（遷移1回につき1件、接続前にためる）
- infra：JsonWriter（エスケープ、バッファ不足）、CmdParser、ULID（26文字・Crockford Base32・時刻部）、boot_id（`^[0-9a-f]{8}$`）、seq（kind ごと、一周）、ts の形、RateLimiter
- **共通仕様のサンプル**：`external/iot-common-spec/samples/valid/cmd__*.json` を CmdParser と CommandService が受け付け、`samples/invalid/cmd__*.json` を必ず拒否または破棄することを、ファイルを読む形のテストで確かめる（件数を数え、0件ならテストを失敗にする）

### 9. ホスト結合テスト

- `FakeMqttLink`（メモリ上）と仮想時計でアプリ全体を動かす：set_actuator → ack（done）→ telemetry の owner が priority → リース切れで none に戻る、重複 cmd_id、ポンプの limited と30秒での停止、接続時の state と LWT の中身

### 10. CI（`.github/workflows/ci.yml`、ubuntu-24.04）

1. submodule ごと取り出す
2. host-debug でビルド → CTest
3. `astral-sh/setup-uv`（Python 3.12）
4. Mosquitto：`external/iot-common-spec/deploy/mosquitto/gen_passwd.sh` → `docker compose -f external/iot-common-spec/docker-compose.yml up -d mosquitto` → ポート 1883 が開くまで待つ。`services:` は使わない（設定ファイルを渡せないため。contract-tests.md 第4章）
5. `uv tool install "iotspec @ git+https://github.com/io0323/iot-common-spec@<固定したタグ>"`
6. `iotspec-contract --role mcu-cpp-01 --target-cmd "./build/host/mcu_cpp_01 --log contract-log.jsonl" --junit contract.xml`
7. `contract.xml`・`contract-log.jsonl`・Mosquitto のログを成果物としてアップロード（失敗時も）

環境変数は `IOTSPEC_MQTT_URL: mqtt://localhost:1883`、`IOTSPEC_MQTT_PASSWORD: ci-${{ github.run_id }}`。

### 11. README.md（最小）

何をするものか、対応する共通仕様の版（固定したタグ）、依存のバージョン、ビルド・テスト・契約テストの手順、フェーズの進み具合。

## P1 で作らないもの

ESP-IDF のコード・ターゲットのプリセット、FreeRTOS のタスク、実機のドライバと I2C フェイク、ModeManager・RuleEngine・本体ボタン・OLED、ファンのインターロックと気温センサー故障時の扱い、SNTP と uptime からの ts の逆算、指数バックオフ、NVS、品質ゲート（カバレッジ・clang-tidy・cppcheck・lizard・Doxygen）、自作ライブラリの取り込み。手を付けたくなったら、仮版で済ませるか open-questions に書く。

## 完了条件（すべて満たしてから PR）

1. `cmake --preset host-debug && cmake --build --preset host-debug` が警告ゼロで通り、`ctest --preset host-debug --output-on-failure` が全件合格
2. 契約テスト `iotspec-contract --role mcu-cpp-01 --target-cmd "./build/host/mcu_cpp_01"` の全チェック（ack・lease_expiry・duplicate_cmd_id・limited_applied・set_param_unknown_op・lwt・schema・seq_and_boot_id・publish_rate）が、この Mac（Docker Desktop の Mosquitto）と CI の両方で合格。省略（skip）が1件もないこと
3. ASan・UBSan の指摘ゼロ（CTest と契約テストの両方の実行で）
4. 上記8・9のテストがそろっていて、共通仕様のサンプルのテストが valid・invalid とも1件以上を読んでいる
5. 次の grep の結果を PR に載せ、どれも0件であること
   - `src/` のうち `src/platform/` 以外での、Paho・POSIX・`<thread>`・`<mutex>`・`<condition_variable>` の include
   - `src/app`・`src/domain`・`src/drivers`・`src/infra` での `new `・`malloc`・`std::string`（`std::string_view` は除く）・`std::vector`・`std::map`・`std::function`・`std::shared_ptr`
6. `docs/open-questions.md` があり、基本設計書の未決事項が写してある

PR は `gh pr create` で作る。説明には、完了条件ごとの結果（実行したコマンドと出力の要点、契約テストの結果表）、固定したタグとバージョンの一覧、open-questions に足した項目、P2 への申し送り（仮版にしたものの一覧）を書く。マージはしない。

## 止まって相談すること

- 契約テストの期待と基本設計書・このプロンプトが食い違うとき（例：このとおりに作ると不合格になる）
- 共通仕様の Schema とこのプロンプトが食い違うとき
- 値や順序が書かれていないことを決める必要が出たとき
- FetchContent の依存がこの Mac でビルドできないとき

いずれも `docs/open-questions.md` に記録し、その件に関わる作業を止めて報告する。iot-common-spec 側は変更しない。

# iot-modular-node-esp32

[![ci](https://github.com/io0323/iot-modular-node-esp32/actions/workflows/ci.yml/badge.svg)](https://github.com/io0323/iot-modular-node-esp32/actions/workflows/ci.yml)

種類の違う複数のセンサーとアクチュエーターを共通のインターフェースで扱う、ESP32-S3 向けのスマート制御ユニットのファームウェアです（C++20、作品02「モジュール型スマートデバイス制御システム」、device_id `mcu-cpp-01`）。
機器の追加・交換は「新しいクラス＋構成表1行」で済み、既存のクラスは変更しない設計を主題にしています。上位（作品03）とは MQTT でやり取りします。

**いまの段階（P7）**：PC で動くホスト版と、ESP32-S3 向けのターゲット版（ESP-IDF v5.5.6）です。MQTT に接続して telemetry・event・state を送り、cmd を受けて cmd/ack を返します。
制御の中核（5段の調停・安全インターロック・自動／手動／省電力のモード・ローカルルール・センサー故障の扱い）と、
機器のドライバ（BME280・BH1750・土壌水分・PWM・リレー・SSD1306・ボタン）まであります。ドライバはデータシートのレジスタ応答を再現するフェイクで確かめています。
通信は「ネットワーク → 時刻の同期 → MQTT」の順に接続し、切れたら 1・2・4…60 秒の間隔でつなぎ直します。送れない間のイベントは32件まで保持し、
確認の取れなかった event は同じ電文のまま送り直し、つなぎ直したら UPSTREAM_LOST・UPSTREAM_RESTORED で切れていた間を知らせます。
ターゲット版は FreeRTOS の4つのタスク（control・sense・comm・ui）に分け、アプリのロジックは control のタスクだけで動かします。
ターゲット版は、エミュレーター（QEMU・Wokwi）で動かして確かめています（QEMU ではファームウェアに契約テストも流す）。**実機では確かめていません。**

**基本設計書との差（P6）**：省電力モードのライトスリープは入れていません（消費電流を測れず確かめられないため。計測周期の5倍と表示の消灯だけ。実機フェーズで入れる。open-questions #107）。
ルールのしきい値と周期の NVS への保存（FR-12）も入れていません（#108）。

## 対応する共通仕様

[iot-common-spec](https://github.com/io0323/iot-common-spec) **v0.2.1**（`external/iot-common-spec` に submodule で固定）。
P1 の時点で v0.2.2 のタグはまだないため v0.2.1 に固定しています（[docs/open-questions.md](docs/open-questions.md) #12）。

## 依存（すべて版を固定）

| 依存 | 版 | 取り込み方 | 用途 |
|---|---|---|---|
| iot-common-spec | v0.2.1（e6280d0） | git submodule | Schema・サンプル・プロファイル・Mosquitto の設定・契約テストキット |
| Eclipse Paho MQTT C | v1.3.16 | FetchContent | ホスト版の MQTT クライアント（MQTTAsync、TLS なし、静的ライブラリ） |
| ESP-IDF | v5.5.6（esp-mqtt 0e9ec171） | Docker イメージ `espressif/idf:v5.5.6` | ターゲット版（ESP32-S3）のビルド |
| jsmn | 25647e692c7906b96ffd2b05ca54c097948e879c | FetchContent | cmd の JSON の字句解析（ヒープを使わない） |
| GoogleTest（gMock を含む） | v1.18.0 | FetchContent | 単体テスト・ホスト結合テスト |

## ビルドとテスト

必要なもの：CMake 3.25 以上、Ninja、C++20 のコンパイラ（Apple clang 21・GCC 13 で確認）、Docker（契約テストの Mosquitto）、uv（契約テストキット）。

```sh
git submodule update --init --recursive
cmake --preset host-debug && cmake --build --preset host-debug   # ASan・UBSan 付き、出力は build/host
ctest --preset host-debug --output-on-failure
```

## 契約テスト

```sh
# Mosquitto（ACL・パスワード付き。passwd は submodule の中にできるがコミットしない）
external/iot-common-spec/deploy/mosquitto/gen_passwd.sh
docker compose -f external/iot-common-spec/docker-compose.yml up -d mosquitto

uv tool install "iotspec @ git+https://github.com/io0323/iot-common-spec@v0.2.1"
IOTSPEC_MQTT_PASSWORD=iotspec-dev \
  iotspec-contract --role mcu-cpp-01 --target-cmd "./build/host/mcu_cpp_01 --log contract-log.jsonl" --junit contract.xml
```

### ブローカーの停止・再開の試験

Mosquitto を `docker compose stop`／`start` で20秒止めて戻し、再接続（60秒以内）・UPSTREAM_LOST → 停止中のイベント → UPSTREAM_RESTORED の順・
最後に強制終了したときの LWT を observer で確かめます（CI でも流しています）。

```sh
IOTSPEC_MQTT_PASSWORD=iotspec-dev uv run tests/system/broker_restart.py
```

## 品質ゲート

clang-tidy・cppcheck・lizard・Doxygen・カバレッジは、Docker のイメージ（`tools/docker/quality.Dockerfile`）の中で `tools/quality.sh` が動かします（Mac も CI も同じ）。
ほかに ThreadSanitizer（`host-tsan`）と最適化ビルド（`host-release`）のプリセットがあります。検査ごとの合格条件・抑止の一覧は [docs/quality.md](docs/quality.md) にあります。

```sh
tools/quality.sh all   # coverage・tidy・cppcheck・lizard・doxygen（出力は build/quality/）
```

| 検査 | いまの数値（P5） | 合格条件 |
|---|---|---|
| 行カバレッジ | domain 92.4%・app 95.9%（分岐 86.5%・86.0%） | 層ごとに 80% 以上 |
| clang-tidy・cppcheck | 指摘 0 件 | 0 件 |
| 循環的複雑度（lizard） | 最大 10（平均 2.4） | 10 以下 |
| Doxygen（公開 API） | 警告 0 件 | 0 件 |
| ThreadSanitizer | CTest・契約テスト・broker_restart.py で指摘 0 件（Paho の中だけの 2 件を抑止。open-questions #89・#93） | 0 件 |

## ターゲット版（ESP32-S3、ESP-IDF v5.5.6）

ESP-IDF は **v5.5.6** に固定し、Espressif の公式 Docker イメージ `espressif/idf:v5.5.6` でビルドします（Mac も CI も同じ。open-questions #94）。

```sh
cp firmware/sdkconfig.defaults.local.example firmware/sdkconfig.defaults.local   # Wi-Fi・MQTT の値を入れる（コミットしない）
docker run --rm -v "$PWD":/project -w /project/firmware espressif/idf:v5.5.6 \
  bash -c '. $IDF_PATH/export.sh >/dev/null && idf.py -B /project/build/firmware build size'
```

- 接続の情報は Kconfig の `NODE_WIFI_SSID`・`NODE_WIFI_PASSWORD`・`NODE_MQTT_URL`・`NODE_MQTT_PASSWORD`・`NODE_SITE`（既定は空）。`firmware/sdkconfig.defaults.local` があれば `sdkconfig.defaults` に重ねる（#102）
- SNTP の1つ目は MQTT のブローカーと同じホスト、2つ目は `pool.ntp.org`（`NODE_SNTP_SERVER1`・`2` で変えられる。#103）
- ピンの割り当て（仮、ESP32-S3-DevKitC-1）：I2C0 SDA GPIO8・SCL GPIO9、土壌水分 GPIO1（ADC1）、fan_main GPIO4・led_grow GPIO5・buzzer GPIO6（LEDC）、ポンプのリレー GPIO7、MODE ボタン GPIO10・ACTION ボタン GPIO11（`src/config/esp32_pins.hpp`。#105）
- イメージの大きさ（P7）：全体 1,011,025 バイト、DIRAM 67.7%（P6 は 88.8%。持ち方だけを変えて減らした。[docs/quality.md](docs/quality.md)）

| タスク | 優先度 | コア | 周期 | 呼ぶもの |
|---|---|---|---|---|
| control | 6 | 1 | 10ms | Node（app のクラスはここからだけ） |
| sense | 5 | 1 | 依頼を待つ | drivers::SensorIo（センサーの read・init・回復） |
| comm | 4 | 0 | 依頼を待つ（20ms） | esp-mqtt の API |
| ui | 3 | 1 | 20ms | drivers::UiIo（ボタン・OLED） |

## エミュレーター（QEMU・Wokwi）

ターゲット版のファームウェアを、実機の代わりにエミュレーターで動かします。手順・構成・確かめられないことの表・測った値は [docs/quality.md の「エミュレーター」](docs/quality.md) にあります。

```sh
tools/qemu.sh test        # QEMU：起動・4つのタスク・BOOT・stats（Docker の espressif/idf:v5.5.6）
tools/qemu.sh contract    # QEMU：ファームウェアに契約テスト（Mosquitto を先に起動しておく）
tools/qemu.sh soak 30     # QEMU：30分動かして、ヒープの最低残量が下がらないこと（NFR-03）
tools/wokwi.sh            # Wokwi：起動と計測値・ボタンとモード・Wi-Fi とヒープ・タスクウォッチドッグ
```

- **QEMU**（ESP-IDF v5.5.6 同梱の qemu-xtensa）：Wi-Fi・I2C・ADC・LEDC は再現されない。ネットワークは仮想の Ethernet（OpenCores Ethernet）で、ホストの Mosquitto につないで契約テストを流す。I2C と ADC の機器は、試験用の構成でフェイクに替える。タスクウォッチドッグは働かないので、Wokwi で確かめる
- **Wokwi**（wokwi-cli v0.29.1）：図は `wokwi/diagram.json`。BME280 と BH1750 は、データシートのレジスタだけを再現したカスタムチップ（`wokwi/chips/`）。Wi-Fi は Wokwi-GUEST。MQTT はつなげない（ローカルのブローカーにつなぐ Private Gateway は有料の機能。公開のブローカーは使わない）
- Wokwi のトークンは、環境変数 `WOKWI_CLI_TOKEN` か `~/.config/wokwi/token`（1行、`chmod 600`）。CI ではシークレット `WOKWI_CLI_TOKEN` を使い、なければジョブを「省略」として通す
- **Wokwi の無料枠**：CI はシミュレーションの時間で30日あたり50分、1回の実行は実際の経過時間で5分まで（docs.wokwi.com/wokwi-ci/getting-started）。4本で約133秒なので、30日におよそ22回流せる。そのため CI の Wokwi は、次のときだけ動かす：main への push で `firmware/`・`src/`・`wokwi/` が変わったとき、PR にラベル `wokwi` を付けたとき（マージの前に1回流す）、手動の実行（`gh workflow run wokwi.yml --ref <ブランチ>`）のとき

## 動かし方（ホスト版）

```sh
./build/host/mcu_cpp_01 [--broker host:port] [--site <site>] [--log <file.jsonl>] [--devices sim|fakehw]
```

`--devices` は機器の構成です。`sim`（既定）は P2 の模擬機器、`fakehw` は実機用のドライバをフェイクの I2C バス・ADC・PWM・GPIO・機器（BME280・BH1750・SSD1306）につないだ構成です。契約テストは両方の構成で流しています。

| 設定 | 既定 |
|---|---|
| ブローカー | `--broker`、なければ `IOTSPEC_MQTT_URL`（既定 `mqtt://localhost:1883`） |
| MQTT のユーザー名・client id | `mcu-cpp-01`（ACL が `%u` を使うため device_id と同じ） |
| パスワード | `IOTSPEC_MQTT_PASSWORD`（既定 `iotspec-dev`） |
| site | `--site`（既定 `home`） |
| BOOT の reset_reason | `MCU_CPP_01_RESET_REASON`（既定 `power_on`） |

SIGTERM・SIGINT では offline の state を送ってから切断します。強制終了ではブローカーが LWT（offline の state）を発行します。

### デバッグ入力（ホスト版だけ。省略すれば使わない）

| 引数 | 動き |
|---|---|
| `--sim <channel>=<value>[@<秒>]` | 起動から指定秒後に模擬センサーの値を変える（複数指定可） |
| `--sim-fault <センサーID>@<開始秒>[-<終了秒>]` | 模擬センサーを読めなくする（センサー ID は `bme280`・`bh1750`・`soil_moisture`。fakehw では I2C のタイムアウトか ADC の故障） |
| `--press-at <秒>` | ボタンの短押しを1回入れる（自動 → 手動 → 省電力 → 自動） |
| `--manual <id>=<level>@<秒>` | 手動の出力を入れる（手動モードでなければ無視してログに残す） |
| `--sntp-delay <秒>` | 起動から指定秒は時刻が同期していないことにする（その間は MQTT に接続しない。イベントは発生時刻で保持する） |
| `--net-down <開始秒>-<終了秒>` | その間ネットワークが使えないことにする（複数指定可。MQTT を自分で切るので LWT は出ない。open-questions #76） |

```sh
# 気温を5秒後に46℃、20秒後に42℃にする（ファンの高温インターロックが効いて、10秒後に解除される）
./build/host/mcu_cpp_01 --log run.jsonl --sim temp_air=46@5 --sim temp_air=42@20
# 5〜12秒はネットワークが使えない。その間の8秒に土壌水分センサーを故障させる（戻った後に LOST → SENSOR_FAULT → RESTORED）
./build/host/mcu_cpp_01 --log run.jsonl --net-down 5-12 --sim-fault soil_moisture@8
```

## 制御の調停

アクチュエーターごとに「安全インターロック ＞ 上位指令（リース中）＞ 本体の手動操作（手動モード）＞ ローカルルール（自動・省電力モード）＞ 既定値（0）」で出力を決めます（`src/app/control_arbiter.*`）。
インターロックとルールは構成表（`src/config/devices.*`）の1行ずつで、ControlArbiter は具体的なインターロックのクラスを知りません。

| インターロック（rule） | 対象 | 動き |
|---|---|---|
| `max_on_s` | pump | 連続30秒で停止。要求が0に戻り、かつ停止から60秒経つまで再開しない |
| `temp_high` | fan_main | temp_air が 45.0℃以上で1000に固定。43.0℃未満が10秒続いたら解除 |
| `sensor_fault` | fan_main | temp_air が fault・stale の間は1000に固定 |
| `sensor_fault` | pump | soil が fault・stale の間は上限0。その間の set_actuator は rejected（unsafe） |

| ルール | 入力 | 入る／出る | アクション |
|---|---|---|---|
| `light-on-dark`（照明系） | lux | 200 未満／250 以上 | led_grow 1000 |
| `cool-on-high-temp` | temp_air | 30.0℃以上／28.0℃未満 | fan_main 600 |
| `water-on-dry` | soil | 30.0% 未満 | pump 1000 を5秒（クールダウン10分） |

## 機器のドライバとデータシート

| ドライバ | 機器 | 出典（コメントに節・表・ページを書いている） |
|---|---|---|
| `Bme280` | Bosch BME280（温度・湿度・気圧、I2C） | BME280 Data sheet BST-BME280-DS001-24 rev 1.24（Feb 2024）。補正式の照合は BMP280 Data sheet BST-BMP280-DS001-26 rev 1.26（Oct 2021）3.12 節 |
| `Bh1750` | ROHM BH1750FVI（照度、I2C） | BH1750FVI Technical Note 2011.11 Rev.D |
| `Ssd1306` | Solomon Systech SSD1306（128×64 OLED、I2C） | SSD1306 Rev 1.1（Apr 2008）、SSD1306 Application Note Rev 0.4（Jan 2009） |
| `SoilMoisture` | 静電容量式の土壌水分センサー（ADC） | データシートなし。乾 3000＝0%・湿 1300＝100% の直線（**仮の校正値**。実機フェーズで校正） |
| `PwmActuator`・`RelayActuator`・`Button` | ファン・LED・ブザー（PWM）、ポンプ（リレー）、タクトスイッチ×2 | データシートなし。値の決まりは open-questions #41〜#47・#58 |

ボタンは2つです。MODE の短押しでモードを切り替えます（自動 → 手動 → 省電力 → 自動）。ACTION は手動モードでだけ働き、短押しで操作する機器を選び（fan_main → led_grow → pump → buzzer）、長押しでその機器を 0 と 1000 で切り替えます。

## 構成

```
src/
  support/        Result・Error・固定容量コンテナ（自作 Error Framework の仮の代わり）
  hal/            ハードのインターフェースだけ（I2C・ADC・PWM・GPIO・ミューテックス・タイマー。自作 iot-hal に置き換える前提）
  domain/         値・機器の契約（ISensor・IActuator・IInterlock・IButton・IDisplay）・リース・重複排除・インターロック3種・ルール・計測値と品質
  app/            ControlArbiter・ModeManager・ManualControl・RuleEngine・SensorPoller・UiService・CommandService・Telemetry・State・Event・
                  ConnectionManager・Publisher・Node
  infra/          IMqttLink・INetwork・ITimeSync・IClock・IRandom・JsonWriter・CmdParser・エンベロープ・トピック・送信量の制限
  drivers/        機器のドライバ（データシートどおり）
  drivers/fake/   hal と機器のフェイク（テストと --devices fakehw）
  drivers/sim/    ホスト用の模擬センサー・模擬アクチュエーター
  config/         構成表（devices.cpp：模擬機器、hw_table.hpp：実機用の1行が1台の表（HwRig が展開）、fakehw_table.hpp：フェイクの機器の表）
  platform/host/  Paho・時計・時刻の同期・ネットワーク（模擬）・乱数・JSON Lines のログ・デバッグ入力・main（POSIX・Paho を使えるのはここだけ）
  platform/esp32/ ESP-IDF の hal・infra の実装、タスク、app_main（ESP-IDF・FreeRTOS を使えるのはここだけ）
firmware/         ESP-IDF のプロジェクト（src/ をコンポーネント node_core・node_platform_esp32 として読む）
tests/unit/  tests/integration/  tests/system/（broker_restart.py）
tools/          品質のイメージ（docker/quality.Dockerfile）・quality.sh・TSan の抑止（tsan.supp）
```

## フェーズ

| フェーズ | 内容 | 状態 |
|---|---|---|
| P1 | ホスト版の骨組み、MQTT 接続、cmd の受け付け、CI での契約テスト | 完了（PR #1） |
| P2 | ControlArbiter の5段の優先度、ModeManager、RuleEngine、インターロックの残り | 完了（PR #2） |
| P3 | 機器ドライバと I2C フェイク、ボタンと表示 | 完了（PR #3） |
| P4 | SNTP 同期後の接続、イベント保持、指数バックオフ、送信量の制限、ブローカー停止・復旧 | 完了（PR #4） |
| P5 | 品質ゲート（カバレッジ・clang-tidy・cppcheck・lizard・Doxygen・ThreadSanitizer） | 完了（PR #5） |
| P6 | ESP-IDF ターゲット版 | 完了（PR #6） |
| P7 | RAM の見直しとエミュレーター（QEMU・Wokwi） | 進行中 |
| P8 | 拡張性の実証（SHT31 を追加する PR）と書類の仕上げ | — |

書類：[要件定義・基本設計書（写し）](docs/design/01-requirements-basic-design.md)、プロンプト（[P1](docs/prompts/P1-prompt.md)・[P2](docs/prompts/P2-prompt.md)・[P3](docs/prompts/P3-prompt.md)・[P4](docs/prompts/P4-prompt.md)・[P5](docs/prompts/P5-prompt.md)・[P6](docs/prompts/P6-prompt.md)・[P7](docs/prompts/P7-prompt.md)）、[品質ゲート](docs/quality.md)、[未決の論点](docs/open-questions.md)。

# iot-modular-node-esp32

[![ci](https://github.com/io0323/iot-modular-node-esp32/actions/workflows/ci.yml/badge.svg)](https://github.com/io0323/iot-modular-node-esp32/actions/workflows/ci.yml)

種類の違う複数のセンサーとアクチュエーターを共通のインターフェースで扱う、ESP32-S3 向けのスマート制御ユニットのファームウェアです（C++20、作品02「モジュール型スマートデバイス制御システム」、device_id `mcu-cpp-01`）。
機器の追加・交換は「新しいクラス＋構成表1行」で済み、既存のクラスは変更しない設計を主題にしています。上位（作品03）とは MQTT でやり取りします。

**いまの段階（P1）**：PC で動くホスト版の骨組みです。模擬のセンサー・アクチュエーターで動き、MQTT に接続して telemetry・event・state を送り、cmd を受けて cmd/ack を返します。
ESP-IDF のターゲット版・実機のドライバ・エミュレーターはまだありません（下の「フェーズ」）。**実機では確かめていません。**

## 対応する共通仕様

[iot-common-spec](https://github.com/io0323/iot-common-spec) **v0.2.1**（`external/iot-common-spec` に submodule で固定）。
P1 の時点で v0.2.2 のタグはまだないため v0.2.1 に固定しています（[docs/open-questions.md](docs/open-questions.md) #12）。

## 依存（すべて版を固定）

| 依存 | 版 | 取り込み方 | 用途 |
|---|---|---|---|
| iot-common-spec | v0.2.1（e6280d0） | git submodule | Schema・サンプル・プロファイル・Mosquitto の設定・契約テストキット |
| Eclipse Paho MQTT C | v1.3.16 | FetchContent | ホスト版の MQTT クライアント（MQTTAsync、TLS なし、静的ライブラリ） |
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

## 動かし方（ホスト版）

```sh
./build/host/mcu_cpp_01 [--broker host:port] [--site <site>] [--log <file.jsonl>]
```

| 設定 | 既定 |
|---|---|
| ブローカー | `--broker`、なければ `IOTSPEC_MQTT_URL`（既定 `mqtt://localhost:1883`） |
| MQTT のユーザー名・client id | `mcu-cpp-01`（ACL が `%u` を使うため device_id と同じ） |
| パスワード | `IOTSPEC_MQTT_PASSWORD`（既定 `iotspec-dev`） |
| site | `--site`（既定 `home`） |
| BOOT の reset_reason | `MCU_CPP_01_RESET_REASON`（既定 `power_on`） |

SIGTERM・SIGINT では offline の state を送ってから切断します。強制終了ではブローカーが LWT（offline の state）を発行します。

## 構成

```
src/
  support/        Result・Error・固定容量コンテナ（自作 Error Framework の仮の代わり）
  domain/         値・機器の契約（ISensor・IActuator）・リース・重複排除・ポンプのインターロック
  app/            ControlArbiter（仮版）・CommandService・TelemetryService・StateService・EventService・Node
  infra/          IMqttLink・IClock・IRandom・JsonWriter・CmdParser・エンベロープ・トピック・送信量の制限
  drivers/sim/    ホスト用の模擬センサー・模擬アクチュエーター
  config/         構成表（devices.cpp）
  platform/host/  Paho・時計・乱数・JSON Lines のログ・main（POSIX・Paho を使えるのはここだけ）
tests/unit/  tests/integration/
```

## フェーズ

| フェーズ | 内容 | 状態 |
|---|---|---|
| P1 | ホスト版の骨組み、MQTT 接続、cmd の受け付け、CI での契約テスト | 進行中 |
| P2 | ControlArbiter の5段の優先度、ModeManager、RuleEngine、インターロックの残り | — |
| P3 | 機器ドライバと I2C フェイク、DeviceRegistry と構成表 | — |
| P4 | SNTP 同期後の接続、イベント保持、指数バックオフ、送信量の制限、ブローカー停止・復旧 | — |
| P5 | 品質ゲート（カバレッジ・clang-tidy・cppcheck・lizard・Doxygen・ThreadSanitizer） | — |
| P6 | ESP-IDF ターゲット版 | — |
| P7 | エミュレーター（QEMU・Wokwi） | — |
| P8 | 拡張性の実証（SHT31 を追加する PR）と書類の仕上げ | — |

書類：[要件定義・基本設計書（写し）](docs/design/01-requirements-basic-design.md)、[P1 のプロンプト](docs/prompts/P1-prompt.md)、[未決の論点](docs/open-questions.md)。

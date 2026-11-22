# 作品02 フェーズ P3：機器ドライバ・I2C フェイク・ボタンと表示

## はじめに読むもの

1. `CLAUDE.md`（必ず従う。このフェーズで「層と依存の向き」に `hal` を足す。下の「準備」）
2. `docs/open-questions.md`（とくに #2・#5・#34・#39・#40）
3. `docs/design/01-requirements-basic-design.md`：第1章（接続する機器）、第2章（FR-01・FR-02・FR-03・FR-13）、第3章（NFR-01・制約）、第5章（I2cTransaction・機器を追加する手順）、第7章（I2C センサー無応答・I2C バスのハング）
4. P2 のコード：`src/domain/device.hpp`、`src/app/sensor_poller.*`、`src/app/telemetry_service.*`、`src/app/manual_control.*`、`src/app/mode_manager.*`、`src/config/devices.cpp`、`src/drivers/sim/`
5. 各機器のデータシート（Bosch BME280・BMP280、ROHM BH1750FVI、Solomon Systech SSD1306）。数値・レジスタ・手順はデータシートから取り、出典（文書名・版・節）をコードのコメントに書く。データシートを読めない場合は推測で埋めず、open-questions に書いて止まる

読み終えたら、作業に入る前に「P3 で作るもの」「作らないもの」「不明点」を短くまとめて報告する。

## 準備

- main（P2 のマージ後）から `phase/p3-drivers` を切る
- iot-common-spec にタグ v0.2.2 ができていれば submodule と CI の契約テストキットを上げる（#12）。なければ v0.2.1 のまま
- `CLAUDE.md` の「層と依存の向き」を次のように直す（このフェーズの最初のコミット）
  - `src/hal/`：ハードのインターフェースだけ（II2cBus・IAdc・IPwm・IGpio・IMutex）。依存先は support だけ
  - `drivers → domain・hal`、`platform → hal・infra・drivers`（実装を提供する）
  - 自作 iot-hal の取り込み（#5）が決まったら `src/hal/` を置き換える前提であることを、`src/hal/` の先頭に書く
- open-questions の #39・#40 を「決定（2026-11-22、io 確認）：推奨案（A）のとおり」にする
- このプロンプトの「P3 で決めたこと」を、open-questions に「決定（2026-11-22、P3 プロンプト）」として1行ずつ足す。#34 もここで決定にする

## 最初に直すこと：故障中のチャネルの telemetry（P2 の申し送り）

共通仕様 第5章の表は、センサー故障を「発信元が `quality: fault` で送る」と定めている。P2 の「読めたチャネルだけを送る」は誤り（P2 プロンプトの書き方の誤り）なので、次のとおり直す。

- 周期が来たチャネルは、読めても読めなくても telemetry に入れる。読めなかったチャネルは `quality: "fault"`、`value` は最後に good で読めた値（一度も読めていなければ 0）
- これで全センサーが故障しても、周期ごとに telemetry（とアクチュエーターの状態）が出る
- 本体側の stale（#25）は、ルールとインターロックの入力としてだけ使い、telemetry の quality には出さない（P2 のまま）
- 単体テストとホスト結合テストのシナリオ3に、「故障中も telemetry が出て、そのチャネルが fault」を足す

## P3 で決めたこと（open-questions に写す）

| 論点 | 決定 |
|---|---|
| #34 ボタンの割り当て | タクトスイッチを2つにする。**MODE**：短押しでモードを切り替える（P2 の順）。**ACTION**：手動モードでだけ働く。短押しで操作する機器を選び直し（fan_main → led_grow → pump → buzzer → fan_main）、長押しで選んだ機器を 0 と 1000 で切り替える（ManualControl に渡す。ポンプはインターロックに従う）。手動モード以外の ACTION は無視する |
| ボタンの判定 | 20ms ごとに読み、同じ値が3回続いたら（60ms）押した・離したと判定する。押してから1,000ms 未満で離したら短押し、1,000ms 押し続けた時点で長押し（離すのを待たない。長押しの後に離しても短押しにしない） |
| ボタンの論理 | 押すと Low（内部プルアップ）。構成で反転できるようにする |
| 土壌水分の換算 | ADC の生値を「乾いた値＝0%、湿った値＝100%」で直線に換算し、0〜100 に収める。既定の校正値は乾 3000・湿 1300（12ビット）。**仮の値**で、実機フェーズで校正する（コメントと open-questions に書く） |
| PWM | 周波数と分解能は構成で持つ。既定：fan_main 25kHz、led_grow 1kHz、buzzer 2.7kHz、分解能はどれも10ビット。level 0 は必ず出力0、level 1000 は最大デューティ |
| ブザーの level | level を PWM のデューティとして扱う（500 で 50%。音量の調整ではなくオン・オフの用途。0 以外を同じ扱いにするかは実機フェーズで決める） |
| ポンプのリレー | level が 0 より大きければオン、0 ならオフ。オンの論理（High か Low か）は構成で持つ。既定は High でオン |
| 最小オン・オフ時間 | ActuatorSpec に min_on_ms・min_off_ms を持てるようにする（既定はどの機器もなし）。最小時間の間は出力の変更を待たせる。インターロックの強制値・上限は最小時間より優先する |
| I2C の無応答 | 1回の通信のタイムアウトは50ms。3回続けて失敗したら、そのバスの回復（バスリカバリ）を1回だけ要求し、その後の再初期化は P2 の SensorPoller の30秒ごとの再試行に任せる |
| I2C バスの排他 | `I2cTransaction`（基本設計書 第5章）。ミューテックスの取得のタイムアウトは100ms。取れなければその計測は失敗として扱う |
| OLED の表示 | 128×64、5×7 ドットの ASCII フォントで8行。1行目にモード・接続状態、2〜6行目に temp_air・humidity・lux・soil・pressure（fault は `--` と表示）、7行目にアクチュエーター4台の level、8行目は手動モードのときだけ選んでいる機器。1秒ごとに書き換える。省電力モードでは表示を消す（ディスプレイオフのコマンド） |
| ホスト版の機器 | ホスト版の既定は P2 の模擬機器のまま（デバッグ入力をそのまま使う）。ドライバと I2C フェイクの組み合わせは `--devices fakehw` で選べるようにし、ホスト結合テストでも使う |

## P3 で作るもの

1. **hal**（インターフェースだけ）：`II2cBus`（`write_read(addr, tx, rx, timeout_ms)`・`write(addr, tx, timeout_ms)`・`recover()`）、`IAdc`（`read_raw(channel)`）、`IPwm`（`configure(channel, freq_hz, resolution_bits)`・`set_duty(channel, duty)`）、`IGpio`（`configure_input_pullup`・`configure_output`・`read`・`write`）、`IMutex`（`try_lock_for(ms)`・`unlock`）、`I2cTransaction`（RAII）。P6 で ESP-IDF v5 の i2c_master・adc_oneshot・LEDC・gpio に素直に対応できる形にする
2. **drivers**（データシートどおり。初期化の手順、レジスタ、換算式、待ち時間の出典をコメントに書く）
   - `Bme280`：チップ ID（0x60）の確認、ソフトリセット、補正係数の読み出し、オーバーサンプリングの設定、強制モードでの計測、データシートの整数の補正式（温度・気圧・湿度）。1台で temp_air・humidity・pressure の3チャネル
   - `Bh1750`：電源オン、連続高分解能モード、生値から lux への換算（データシートの式。測定時間の待ちを守る）
   - `SoilMoisture`：IAdc の生値から % への換算（上の決定）
   - `PwmActuator`（LEDC）：level から デューティへの換算（上の決定）
   - `RelayActuator`：GPIO（上の決定）
   - `Ssd1306`：初期化コマンド列、ページ単位の書き込み、表示のオン・オフ。フレームバッファ（1,024バイト）は静的に確保
   - `Button`：上の判定。MODE と ACTION の2つ
3. **app**
   - `UiService`：ボタンの判定結果を ModeManager と ManualControl に渡す。OLED の表示を作る（上の決定）。省電力モードでは消す
   - ManualControl に「選んでいる機器」の状態を足す（ACTION の短押しで選び直す）
   - SensorPoller：I2C の3回連続失敗でバスの回復を要求する（上の決定）
   - ControlArbiter：最小オン・オフ時間（上の決定）
4. **drivers/fake**（テストとホスト版の `--devices fakehw` 用。データシートのレジスタ応答を再現する）
   - `FakeI2cBus`：アドレスごとに機器を振り分ける。NACK・タイムアウト・バスのハングを注入できる。`recover()` の呼び出しを数える
   - `FakeBme280Device`：チップ ID、補正係数のレジスタ、制御レジスタ、計測中のビット、データレジスタ。補正係数と生値はテストから与える
   - `FakeBh1750Device`、`FakeSsd1306Device`（受けたコマンドとデータを記録し、フレームバッファを再現する）
   - `FakeAdc`、`FakePwm`、`FakeGpio`
5. **config**：構成表に実機用の行（Bme280・Bh1750・SoilMoisture・PwmActuator×3・RelayActuator・Ssd1306・Button×2）を足す。ホスト版は P2 の模擬機器の行と `--devices fakehw` の行を選べるようにする。ピン番号は P6 で決めるので、P3 では論理名（`I2C0`・`ADC_SOIL` など）にする

## テスト

- Bme280：初期化のレジスタの書き込み順、チップ ID の不一致でエラー、計測中のビットが下りるまで待つ、補正式（温度・気圧は BMP280 データシートの計算例の補正係数と生値で照合、湿度は BME280 データシートの式を倍精度で計算した値と照合し、許容差をテストに書く）、通信のタイムアウトでエラー
- Bh1750：生値から lux（0・1・65535 を含む）、測定時間を待つこと
- SoilMoisture：乾・湿の値と、その外側が 0・100 に収まること、真ん中
- PwmActuator：level 0・1・500・999・1000 のデューティ、周波数と分解能の設定
- RelayActuator：オンの論理の反転
- 最小オン・オフ時間：変更が待たされること、ちょうどの時刻、インターロックが優先すること
- Ssd1306：初期化コマンド列、ページの書き込み、表示のオン・オフ
- Button：チャタリング（60ms 未満の変化を無視）、短押し（999ms）・長押し（1,000ms ちょうど）、長押しの後に離しても短押しにならない
- UiService：MODE と ACTION の割り当て、手動モード以外の ACTION を無視、選び直しの順番、長押しで 0 と 1000 の切り替え、表示の内容（行ごとの文字列で確かめる。fault は `--`）、省電力モードで表示を消す
- SensorPoller：3回連続の失敗でバスの回復を1回だけ要求する
- telemetry：故障中のチャネルが fault で入る、全センサー故障でも telemetry が出る
- ホスト結合テスト（`--devices fakehw` の構成、FakeMqttLink＋仮想時計）：FakeBme280Device の生値を変えて気温を46℃相当にする → ファンのインターロック（P2 のシナリオ1をドライバ経由で）、BME280 を無応答にする → SENSOR_FAULT 3件・telemetry の3チャネルが fault・バスの回復1回・30秒後に応答を戻すと SENSOR_RECOVERED 3件、ACTION の長押しでポンプを動かす → 30秒で止まる

## P3 で作らないもの

ESP-IDF の実装（platform/esp32）とピン番号（P6）、SNTP・イベント保持の本版・指数バックオフ・送信量の制限の本版（P4）、品質ゲート（P5）、SHT31 の追加（P8）、NVS への校正値の保存。

## 完了条件（すべて満たしてから PR）

1. `cmake --preset host-debug && cmake --build --preset host-debug` が Mac と CI の両方で警告ゼロ、`ctest --preset host-debug --output-on-failure` が全件合格
2. 契約テストの9チェックが Mac と CI の両方で合格し、省略0件。既定の構成と `--devices fakehw` の構成の両方で流す（CI にジョブか手順を足す）
3. ASan・UBSan の指摘ゼロ（CTest と契約テストの両方）
4. 上の「テスト」がすべてそろっている
5. P1・P2 の grep の検査が0件のまま。加えて
   - `src/drivers/`（`src/drivers/fake/` を除く）が `src/platform/` のヘッダを include していない
   - `src/hal/` が support 以外を include していない
6. 各ドライバのコメントに、データシートの出典（文書名・版・節）がある（PR に一覧を載せる）
7. open-questions に #39・#40・#34 の決定と「P3 で決めたこと」の各行が入っている

PR は `gh pr create` で作る。説明には、完了条件ごとの結果、データシートの出典の一覧、open-questions に足した項目、P4 への申し送りを書く。マージはしない。

## 止まって相談すること

- データシートの値・式・手順が読めない、または資料どうしで食い違うとき
- このプロンプトの決定と、基本設計書・共通仕様・データシートが食い違うとき
- 値や順序が書かれていないことを決める必要が出たとき

いずれも `docs/open-questions.md` に記録し、その件に関わる作業を止めて報告する。iot-common-spec 側は変更しない。

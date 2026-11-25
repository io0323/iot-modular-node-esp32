# 作品02 フェーズ P6：ESP-IDF ターゲット版

## はじめに読むもの

1. `CLAUDE.md`（必ず従う）
2. `docs/design/01-requirements-basic-design.md`：第1章（前提・対象外）、第3章（NFR-03・NFR-04・NFR-05）、第4章、第6章（FreeRTOS タスク・接続と時刻・リセット要因の対応）、第7章（タスク停止）
3. `docs/open-questions.md`（とくに #4・#11・#58・#64〜#72）
4. P5 の PR の「P6 への申し送り」
5. いまのコード：`src/hal/hal.hpp`、`src/infra/`（mqtt_link・network・time_sync・clock・random）、`src/app/node.*`・`connection_manager.*`・`sensor_poller.*`・`ui_service.*`、`src/platform/host/`、`src/config/`
6. ESP-IDF の、固定する版のドキュメント：i2c_master（新しい I2C ドライバ）、adc_oneshot、LEDC、GPIO、esp_wifi（STA）、esp_netif_sntp、esp-mqtt、Task Watchdog、esp_reset_reason、esp_random。API・設定の名前と動きはドキュメントとその版のソースから取り、推測で書かない

読み終えたら、作業に入る前に「P6 で作るもの」「作らないもの」「不明点」を短くまとめて報告する。

## 準備

- main（P5 のマージ後）から `phase/p6-esp-idf` を切る
- iot-common-spec にタグ v0.2.2 ができていれば submodule と CI の契約テストキットを上げる（#12）。なければ v0.2.1 のまま（BOOT の `detail` は任意の object なので、`reset_reason`・`platform_reason` を入れても v0.2.1 の Schema に合う）
- ESP-IDF は **v5 系の最新の安定版のタグ**に固定する。ビルドは Espressif の公式 Docker イメージ（`espressif/idf:<その版>`）で行い、Mac も CI も同じイメージを使う（この Mac は Homebrew が使えないため）。版を README と open-questions に書く
- このプロンプトの「P6 で決めたこと」を、open-questions に「決定（2026-11-25、P6 プロンプト）」として1行ずつ足す。#4 は、最小構成でのビルドの結果を書いて決定にする

## P6 の原則

- **domain・app・infra・drivers・config は、ホスト版とターゲット版で同じソースを使う。** ターゲット版のためにこれらへ `#ifdef ESP_PLATFORM` を書かない。違いは `src/platform/esp32/` と構成表（ピン・周期などの値）にだけ置く
- **アプリのロジックは1つのタスクだけで動かす。** app の Node・ControlArbiter・CommandService などは control タスクからだけ呼ぶ。ほかのタスクとは FreeRTOS のキューで受け渡す（共有変数を作らない。基本設計書 第6章）
- ホスト版の検査（CTest・契約テスト・broker_restart・TSan・`tools/quality.sh all`）は P5 のまま通り続けること

## P6 で決めたこと（open-questions に写す）

| 論点 | 決定 |
|---|---|
| ESP-IDF のプロジェクトの形 | `firmware/` に ESP-IDF のプロジェクト（`firmware/CMakeLists.txt`・`firmware/main/`・`sdkconfig.defaults`）を置く。`src/` の層は ESP-IDF のコンポーネントとして読み込む（`firmware/components/node_core`：support・hal・domain・infra・app・drivers（fake と sim を除く）・config の実機用の表、`firmware/components/node_platform_esp32`：`src/platform/esp32/`）。ソースを写さない |
| 言語の設定 | 自分のコンポーネントは `-std=gnu++20`、例外と RTTI は無効（ESP-IDF の既定のまま）、`-Werror` を含む CLAUDE.md の警告。ESP-IDF 本体のコードには当てない |
| タスクの分け方 | **control**（優先度6・コア1・10ms ごとの tick と受信キュー）：Node の全処理。**sense**（5・コア1）：周期の来たセンサーを読み、結果をまとめて control へ送る（I2C の待ちを control に持ち込まない）。**comm**（4・コア0）：esp-mqtt のイベントを受けて control へ送り、control からの送信の依頼を esp-mqtt に渡す。**ui**（3・コア1・20ms）：ボタンを読んで判定結果を control へ送り、control が作った表示の内容を OLED に書く。この分け方に合わせて、SensorPoller と UiService を「読む・書く部分（ハードに触る）」と「判断する部分（Node の中）」に分ける。ホスト版は分けた後も1つのスレッドで順に呼ぶ |
| スタックの大きさ | 仮の値：control 8KB、sense 4KB、comm 6KB、ui 4KB。起動から1分ごとに各タスクの残り（high water mark）をログに出し、P7 のエミュレーターでの値を見て見直す |
| タスクウォッチドッグ（FR-10） | 4つのタスクをすべて登録し、各ループで知らせる。タイムアウトは5秒、切れたらパニックで再起動する。割り込みのウォッチドッグは ESP-IDF の既定のまま |
| 起動時の出力 | 各アクチュエーターを初期化した直後に 0 を出してから、タスクを起こす（基本設計書 第7章） |
| リセット要因の対応 | 基本設計書 第6章の表。ESP-IDF の値を、hal に置く機器に依らない列挙（`PlatformResetCause`）に変える部分だけを platform/esp32 に置き、列挙から共通仕様の7値への対応は infra か app に置いてホスト版で単体テストする。`detail.platform_reason` には ESP-IDF の列挙の名前（`ESP_RST_PANIC` など）を入れる |
| boot_id | Wi-Fi を起動した後に esp_random から作る（RF が動いてから乱数が真の乱数になるため。ESP-IDF のドキュメントで確かめ、引用を残す）。boot_id は送るときにエンベロープへ入れるので、それより前に起きた BOOT にも同じ値が付く |
| Wi-Fi・MQTT の接続情報 | Kconfig の項目（`NODE_WIFI_SSID`・`NODE_WIFI_PASSWORD`・`NODE_MQTT_URL`・`NODE_MQTT_PASSWORD`・`NODE_SITE`）にし、既定値は空。値は `firmware/sdkconfig.defaults.local`（`.gitignore` に入れる）に書き、ビルド時に `SDKCONFIG_DEFAULTS` で重ねる。見本として `sdkconfig.defaults.local.example` をコミットする |
| #11・#68 SNTP サーバー | 1つ目は MQTT のブローカーと同じホスト（作品03の Raspberry Pi がブローカーと NTP を兼ねる前提）、2つ目は `pool.ntp.org`。Kconfig で変えられるようにする。作品03の設計にこの前提を伝えることは、open-questions に「作品03へ申し送り」として残す |
| esp-mqtt の設定 | clean session、keepalive 10秒、自動の再接続は切る（ConnectionManager が再接続する）。LWT は接続のたびに作り直す（P1 の決定）。確認待ちの件数は MQTT_EVENT_PUBLISHED で数える。切断のとき、確認待ちだった電文を esp-mqtt が自分で送り直すかどうかを、固定した版のソースで確かめてから #71 の送り直しを組む（二重に送り直さない） |
| ピンの割り当て（仮） | ESP32-S3-DevKitC-1 を想定し、`src/config/` の実機用の表に置く：I2C0 は SDA GPIO8・SCL GPIO9、土壌水分は GPIO1（ADC1）、fan_main GPIO4・led_grow GPIO5・buzzer GPIO6（LEDC）、pump のリレー GPIO7、MODE ボタン GPIO10・ACTION ボタン GPIO11。ストラッピングピン（0・3・45・46）と USB（19・20）を避ける。実機フェーズで見直す |
| I2C のバスの回復 | i2c_master のドライバを外し、SCL を GPIO として15回クロックを出してから、ドライバを作り直す（基本設計書 第7章。回数はその表のとおり） |
| 省電力モードのライトスリープ | **P6 では入れない**（消費電流を測れず、確かめられないため）。計測周期の5倍と表示の消灯だけを動かし、ライトスリープは実機フェーズに回す（open-questions に記録し、基本設計書との差として README にも書く） |
| FR-12 の NVS への保存 | P6 では入れない。`nvs_flash_init` は Wi-Fi のために呼ぶ。ルールのしきい値と周期は構成表のまま（FR-12 は Should。P8 で扱いを決める） |
| ターゲットでの品質の検査 | ESP-IDF のビルドを CI のジョブにし、警告ゼロ（自分のコンポーネント）とイメージの大きさ（`idf.py size` の出力）を成果物にする。`src/platform/esp32/` は cppcheck と lizard の対象に足す。clang-tidy は xtensa の GCC のフラグを読めないので対象外とし、理由を `docs/quality.md` に書く |

## P6 で作るもの

1. `firmware/`（上の形）。`idf.py set-target esp32s3` で、`sdkconfig.defaults` に必要な設定（FreeRTOS の tick 1000Hz、タスクウォッチドッグとパニック、esp-mqtt、ログの段階など）を入れる
2. `src/platform/esp32/`：
   - hal の実装：I2C（i2c_master。タイムアウトは #48 の50ms、バスの回復は上の決定）、ADC（adc_oneshot）、PWM（LEDC。#44・#58）、GPIO、ミューテックス（FreeRTOS の mutex）、タイマー（esp_timer）
   - infra の実装：単調時計（esp_timer_get_time）、乱数（esp_random）、INetwork（esp_wifi の STA）、ITimeSync（esp_netif_sntp）、IMqttLink（esp-mqtt）、ログ（ESP_LOG。JSON Lines と同じ項目を出す）
   - リセット要因の変換、タスク（上の決定）、タスクウォッチドッグ、`app_main`
3. SensorPoller・UiService の分割（上の決定）。ホスト版の振る舞いは変えない
4. リセット要因の対応表の単体テスト（ホスト版）。表の全行と「そのほか」
5. CI：ESP-IDF のビルドのジョブ（Docker イメージ、警告ゼロ、`idf.py size` を成果物に）
6. `docs/quality.md`・README に、ターゲット版のビルドの手順・版・イメージの大きさ・ライトスリープを入れていないことを書く

## P6 で作らないもの

エミュレーターでの実行（P7）、実機での確認、ライトスリープ、NVS の設定の保存、OTA・プロビジョニング・TLS（基本設計書の対象外）。

## 完了条件（すべて満たしてから PR）

1. `firmware/` が、固定した ESP-IDF の Docker イメージで Mac と CI の両方からビルドでき、自分のコンポーネントの警告ゼロ。`idf.py size` の結果（フラッシュと RAM）を PR に載せる
2. ホスト版の検査がすべて P5 のまま合格：host-debug・host-release・host-tsan の CTest、契約テスト（既定・fakehw）、broker_restart.py、`tools/quality.sh all`（`src/platform/esp32/` を cppcheck と lizard に足した後も）
3. リセット要因の対応表の単体テストがある
4. `src/domain`・`src/app`・`src/infra`・`src/drivers`・`src/config` に `ESP_PLATFORM`・ESP-IDF のヘッダ・FreeRTOS のヘッダが出てこない（grep の結果を PR に載せる）。P1〜P5 の grep の検査も0件のまま
5. Node と app のクラスを呼ぶのが control タスクだけであることを、PR にタスクごとの「呼ぶもの」の表で示す
6. `firmware/sdkconfig.defaults.local` がコミットされていない。パスワードなどの秘密がリポジトリにない
7. open-questions に「P6 で決めたこと」の各行と #4 の結果が入っている

PR は `gh pr create` で作る。説明には、完了条件ごとの結果、ESP-IDF の版とイメージ、タスクの表、確かめた ESP-IDF の挙動（esp-mqtt の送り直し、esp_random と RF など）とその出典、P7 への申し送りを書く。マージはしない。

## 止まって相談すること

- esp-mqtt の送り直しの挙動が #71 の決定と両立しないとき
- ESP-IDF の API が、hal・infra のインターフェースと合わず、インターフェースを変える必要があるとき（ホスト版にも影響するため）
- C++20 の機能が ESP-IDF の GCC で使えず、domain などの書き換えが要るとき
- タスクの分け方を守ると、app の中に排他（ミューテックス）が要りそうなとき
- 値や順序が書かれていないことを決める必要が出たとき

いずれも `docs/open-questions.md` に記録し、その件に関わる作業を止めて報告する。iot-common-spec 側は変更しない。

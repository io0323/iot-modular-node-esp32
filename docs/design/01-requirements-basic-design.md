# 作品02 要件定義・基本設計書：モジュール型スマートデバイス制御システム（C++ × ESP32-S3）

Nov 20, 2026 · @io

2026-11-22：共通仕様 v0.2.1・v0.2.2 の決定に合わせて改訂した。本書と共通仕様が食い違う場合は共通仕様（iot-common-spec の docs/spec.md と schemas/）を優先する。

## 1. 概要とスコープ

種類の違う複数のセンサーとアクチュエーターを共通インターフェースで扱い、機器の追加や交換に既存コードを変更せず対応できるスマート制御ユニットをC++で開発する。対象はESP32-S3（デュアルコア240MHz、Wi-Fi）、ESP-IDF v5系とFreeRTOSを使う。

ポートフォリオとして示すことは3つ。資源の限られた環境でも拡張しやすいオブジェクト指向設計ができること。RTOSのタスク分割と排他制御を設計できること。モックを使ってロジックをPC上でテストできること。

### 前提

- プログラム先行で開発する。実機は使わず、GoogleTestによるホストテスト、ESP-IDFのLinuxターゲット（ホスト上での実行）、QEMUまたはWokwiで検証する
- 接続する機器：BME280（温度・湿度・気圧、I2C）、BH1750（照度、I2C）、静電容量式土壌水分センサー（ADC）、LED・ファン・ブザー（LEDCのPWM）、給水ポンプ（GPIO＋リレー）、SSD1306 OLED（I2C）、タクトスイッチ
- 上位とは共通仕様 v0.2.2（タグ v0.2.2）のMQTTトピックとJSONエンベロープで通信する。MQTTはユーザー名とパスワードで認証し、ユーザー名はデバイスIDと同じにする（ACLが %u を使うため）。デバイスIDは `mcu-cpp-01`
- 自作ライブラリ（EventBus、Error Framework、MQTTアダプタ）を取り込んで使う。各ライブラリはタグを付けて取り込み、作品02の中では直さない。合わない箇所があればライブラリ側を改善してタグを上げ、その経緯もADRに残す（取り込み元のリポジトリは未確認。第7章の未決事項）

### 対象外（本版）

- OTA更新、Wi-Fiの初期設定画面（プロビジョニング）、TLSによるMQTTの暗号化（拡張で対応）
- システム横断の判定と履歴の長期保存（作品03・04の責任）
- ディープスリープ。省電力モードはライトスリープと計測間隔の延長までとする。MQTT指令による設定変更（set\_param。v0.2 では unknown\_op で拒否する）

## 2. 機能要件

| ID | 機能 | 要件 | 優先度 |
| --- | --- | --- | --- |
| FR-01 | 機器の登録 | 起動時に静的な構成テーブルからセンサーとアクチュエーターを生成し、レジストリに登録する。各機器は文字列ID（例：`temp_air`、`fan_main`）で指定できる | Must |
| FR-02 | 周期計測 | 機器ごとの周期で計測する（初期値：BME280 2秒、BH1750 1秒、土壌水分 10秒）。計測値には単位と品質（good／stale／fault）を付ける。周期が変わったら（省電力モードなど）stateを出し直す | Must |
| FR-03 | アクチュエーター制御 | 出力を0〜1000‰の整数で指定する。機器ごとに上限・下限、最小オン／オフ時間、連続稼働の上限を持てる（ポンプは最大30秒） | Must |
| FR-04 | モード管理 | 自動／手動／省電力の3モード。ボタン短押しまたはMQTT指令で切り替える。手動は30分操作がなければ自動に戻る。切り替えたらMODE\_CHANGEDを1件送り、stateを出し直す | Must |
| FR-05 | ローカル制御ルール | 自動モードで「条件・ヒステリシス・アクション・クールダウン」からなるルールを評価する。例：照度<200lxでLED点灯、気温30℃以上でファン60%、土壌水分<30%でポンプ5秒（クールダウン10分） | Must |
| FR-06 | 上位指令と調停 | 作品03からの指令は有効期限（リース）付きで受け、期限中はローカルルールより優先する。安全インターロックは常に最優先（第6章） | Must |
| FR-07 | MQTT送受信 | telemetry（計測周期ごと。毎回、全アクチュエーターのlevelとownerを含める）、event（第6章のコード）、state（retain、LWTでoffline）を送り、cmdを受けてcmd/ackを返す。送信はkindごとに平均毎秒10件・瞬間50件まで、QoS 1の確認待ちは32件まで | Must |
| FR-08 | 接続管理 | Wi-FiとMQTTの切断を検知し、指数バックオフ（1秒〜60秒）で再接続する。MQTTへの接続はSNTPで時刻を合わせた後に行う。切断中もローカル制御は継続する | Must |
| FR-09 | 時刻 | Wi-Fi接続後にSNTP（第一候補は作品03のRaspberry Pi）で時刻を合わせ、同期してからMQTTに接続する。電文はすべて `ts_source: device` で送る。同期前に起きたイベントはuptime\_msで記録しておき、同期後にtsを逆算して送る。telemetryは同期前は送らない | Must |
| FR-10 | タスク監視 | 全タスクをタスクウォッチドッグに登録し、停止したら再起動する。再起動要因をBOOTイベントのdetail.reset\_reason（7値）で報告し、ESP-IDFの元の値をdetail.platform\_reasonに残す（対応は第6章） | Must |
| FR-11 | 切断中のイベント保持 | MQTT切断中とSNTP同期前のeventを最大32件保持し、接続後に送る。telemetryは保持しない | Should |
| FR-12 | 設定の永続化 | ルールのしきい値と計測周期をNVSに保存する。MQTT指令での変更はv0.2では扱わない（set\_paramはunknown\_opで拒否） | Should |
| FR-13 | 表示 | OLEDに主要な計測値、モード、接続状態を表示する。省電力モードでは消灯 | Should |

## 3. 非機能要件と制約

| ID | 分類 | 要件 | 確認方法 |
| --- | --- | --- | --- |
| NFR-01 | 拡張性 | 新しいセンサー・アクチュエーターの追加で変更するのは「新しいクラスのファイル」と「構成テーブル1行」だけ。既存クラスは変更しない | 実際に1種類追加するPRを作り、差分をREADMEに載せる |
| NFR-02 | 応答性 | cmd受信からアクチュエーター出力まで100ms以内、ack送信まで200ms以内（省電力モードは対象外） | ホスト結合テストで仮想時間計測、実機フェーズで実測 |
| NFR-03 | メモリ | 自分のコード（app・domain・drivers・infra）は初期化完了後にヒープを確保しない（esp-mqtt・lwIP・Wi-Fiの内部の確保は対象外）。JSONは固定バッファで組み立て・解析する。コンテナは `std::array` と固定容量の自作コンテナを使う。稼働24時間でヒープの最低残量が下がり続けない | ヒープの空きと最低残量をログで監視（telemetryには含めない） |
| NFR-04 | 言語機能 | C++20（std::spanなどC++20の機能を使うため）、例外とRTTIは無効。エラーは自作Error FrameworkのResult型で返す | ビルド設定、コードレビュー |
| NFR-05 | 移植性 | ESP-IDFに依存するのは `platform/` だけ。それ以外はLinux上でビルド・テストできる | CIでホスト版をビルド |
| NFR-06 | 安全 | ポンプの連続稼働上限などのインターロックは、どの経路の指令でも破られない | 全経路からの違反指令を単体テスト |
| NFR-07 | 品質 | ドメイン層とアプリ層の行カバレッジ80%以上。clang-tidyとcppcheckで警告ゼロ | CI（gcovr） |
| NFR-08 | 可用性 | AP復旧後60秒以内にMQTTに再接続する。切断中も計測とローカル制御は止まらない | ブローカー停止・再開の結合テスト |
| NFR-09 | 保守性 | 公開APIにはDoxygenコメント。各関数の循環的複雑度10以下 | lizard等で計測 |

制約：開発段階では実機を使わない。センサードライバはデータシートに基づきレジスタ応答を再現するI2Cフェイクでテストし、実機フェーズで実デバイスとの差異を確認する。

## 4. アーキテクチャ

「計測（sense）」「制御（control）」「通信（comm）」を別タスクに分け、アクチュエーターへの出力を `ControlArbiter` 一箇所に集める。通信が止まっても計測と制御は止まらない。

&#91;embedded content: タスク構成と制御の流れ\]

各クラスの層は上から app（制御・モード・指令）、domain（機器の契約・ルール）、drivers（各機器）、infra（MQTT・設定・時刻）、platform（ESP-IDF／ホスト）。`ModeManager` はモードに応じてRuleEngineと手動操作の有効／無効を切り替え、画面とボタンは別の `ui` タスクが担う。

## 5. クラス設計とインターフェース

機器は `ISensor` と `IActuator` の2つのインターフェースだけで扱い、アプリ層は具体的な機器クラスを知らない。バスやGPIOもインターフェース経由で渡し（コンストラクタ注入）、テストではモックに差し替える。

### 主要クラス

| 層 | クラス | 責務 |
| --- | --- | --- |
| app | `ModeManager` | 自動／手動／省電力の状態（Stateパターン）と手動モードのタイムアウト |
| app | `RuleEngine` | ローカルルールの評価。ヒステリシスとクールダウンを管理し、「提案」を出す |
| app | `ControlArbiter` | 安全インターロック・上位指令・手動操作・ローカルルールの提案を優先度で合成し、最終出力を決める |
| app | `CommandService` | cmdの検証、リースの登録、ackの生成 |
| app | `TelemetryService` | 計測値と状態を共通エンベロープに変換して送信キューへ |
| domain | `ISensor`, `IActuator`, `DeviceRegistry` | 機器の共通契約とIDによる検索 |
| domain | `Reading`, `Rule`, `Interlock`, `Lease` | 値オブジェクトと制御データ |
| drivers | `Bme280`, `Bh1750`, `SoilMoisture`, `PwmActuator`, `RelayActuator`, `Ssd1306` | 各機器の実装。データシートに従う初期化・補正計算 |
| infra | `MqttLink`, `NvsConfig`, `WifiManager`, `Clock` | 通信・設定保存・時刻。自作MQTTアダプタの上に作る |
| platform | `II2cBus`, `IAdc`, `IPwm`, `IGpio` とそのESP-IDF実装・ホスト実装 | ハードの抽象化 |

### インターフェース定義（抜粋）

```cpp
enum class Quality : uint8_t { Good, Stale, Fault };

struct Reading {
    std::string_view channel;   // 例: "temp_air", "humidity"
    int32_t value_milli;        // 固定小数（1/1000単位）
    Unit unit;                  // degC, pct_rh, hPa, lux, pct
    Quality quality;
};

class ISensor {
public:
    virtual ~ISensor() = default;
    virtual std::string_view id() const = 0;
    virtual Result<void> init() = 0;
    // 1台で複数チャネルを持てる（BME280は温度・湿度・気圧）
    virtual Result<std::size_t> read(std::span<Reading> out) = 0;
    virtual std::chrono::milliseconds period() const = 0;
};

class IActuator {
public:
    virtual ~IActuator() = default;
    virtual std::string_view id() const = 0;
    virtual Result<void> init() = 0;
    virtual Result<void> apply(Level level) = 0;   // Level = 0–1000（‰）
    virtual Level current() const = 0;
    virtual const ActuatorLimits& limits() const = 0;  // 上下限、最小オン／オフ、連続上限
};

// I2Cバスの排他はRAIIで保証する
class I2cTransaction {
public:
    explicit I2cTransaction(II2cBus& bus);   // ミューテックス取得（タイムアウト付き）
    ~I2cTransaction();                      // 解放
    I2cTransaction(const I2cTransaction&) = delete;
    I2cTransaction& operator=(const I2cTransaction&) = delete;
    Result<void> write_read(uint8_t addr, std::span<const uint8_t> tx, std::span<uint8_t> rx);
};
```

### 機器を追加する手順

1. `drivers/` に `ISensor`（または `IActuator`）を実装したクラスを追加する
2. 同じクラスの単体テストを、I2CやADCのモックを使って追加する
3. `config/devices.cpp` の構成テーブルに1行追加する（ID、クラス、接続先、周期）
4. 必要ならルールを設定で追加する（コード変更なし）

インスタンスは静的に確保し、レジストリは参照だけを持つ。動的なプラグイン読み込みは行わない。

## 6. 制御の調停・モード・タスクと通信

### 制御の調停

アクチュエーターへの出力は `ControlArbiter` だけが決める。各経路は「提案」を出すだけで、機器を直接動かさない。優先度は上が強い。

1. 安全インターロック：ポンプは連続30秒で強制停止し、要求がいったん0に戻り、かつ停止から60秒経つまで再開しない。ファンは気温45℃以上で100%にし、43℃未満が10秒続いたら解除する。気温センサーの故障中もファンは100%。土壌水分センサーの故障中はポンプ禁止（その間のポンプへのset\_actuatorはrejected（unsafe））。どの経路も上書きできない
2. 上位指令（作品03）：set\_actuatorのリース期限中だけ有効。期限はlease\_s（省略時60秒、上限600秒）で、デバイスの受信時刻から数え、ちょうどで切れる。切れたら自動で下位に戻る。同じ対象への新しい指令はpriorityによらず古い指令を置き換える（優先度の調停は作品03が行う）
3. 本体ボタンによる手動操作：手動モード中のみ
4. ローカルルール：自動モード中のみ
5. 既定値：すべて停止（ポンプを動かしたままにしない）

リース方式により、作品03が停止しても指令が残り続けることはない。作品03が継続して制御したい場合は期限前に同じ指令を再送する。全体計画の「指令を出すのは03だけ」という原則はシステム内の他の作品に対するものであり、本体の自律制御はこの優先度の下で許される。

### 出力の持ち主（telemetry の owner）

調停で勝った経路を、telemetry の `actuators[].owner` で示す。`manual` は作品04の手動操作（作品03経由）を指し、本体ボタンではない。

| 勝った経路 | owner |
| --- | --- |
| 安全インターロック | `interlock` |
| 上位指令（リース中） | cmd の priority（`safety`・`manual`・`auto`・`schedule`） |
| 本体ボタン（手動モード）、ローカルルール | `local` |
| 既定値 | `none` |

### 指令の受け付け

| op | args | 結果 |
| --- | --- | --- |
| `set_actuator` | `id`、`level`（0–1000）、`lease_s` | done＋`applied`。安全制限で丸めたら `limited: true`。lease\_s が max\_on\_s を超える指令は、受けた時点で limited、applied はその時点の出力。土壌水分センサーの故障中のポンプは rejected（unsafe） |
| `release` | `id` | done。その対象のリースを解除する |
| `set_mode` | `mode`（auto・manual・power\_save） | done。値が違えば rejected（invalid\_arg） |
| `get_status` | なし | done。state を出し直す |
| `set_param`、そのほか | — | rejected（unknown\_op） |

共通の検査：`expires_at` を過ぎて届いた cmd は rejected（expired）。未知の機器IDは unknown\_target、範囲外の値は invalid\_arg。`lease_s` は set\_actuator 以外では無視する。MQTT への接続は SNTP 同期の後なので、expires\_at はいつも判断できる。

### モード

| モード | ローカルルール | 手動操作 | 計測周期 | 表示 | 入る条件 | 出る条件 |
| --- | --- | --- | --- | --- | --- | --- |
| 自動 | 有効 | 無効 | 既定 | 点灯 | 起動時、手動のタイムアウト | ボタン、cmd |
| 手動 | 無効 | 有効 | 既定 | 点灯 | ボタン、cmd | 30分無操作、ボタン、cmd |
| 省電力 | 有効（照明系は停止） | 無効 | 5倍に延長（stateを出し直す） | 消灯 | ボタン、cmd | ボタン、cmd |

どのモードでも安全インターロックと上位指令のリースは有効。モードを切り替えたら MODE\_CHANGED を1件送り、state を出し直す（計測周期が変わるため。受信側は period\_ms の3倍で stale と判定する）。

### FreeRTOSタスク

| タスク | 優先度 | コア | 周期・起動条件 | 処理 |
| --- | --- | --- | --- | --- |
| `control` | 6 | 1 | 100ms＋イベント | ルール評価、調停、アクチュエーター出力、インターロック監視 |
| `sense` | 5 | 1 | 機器ごとの周期 | センサー読み取り。結果をEventBusで配信 |
| `comm` | 4 | 0 | キュー受信＋1s | MQTT送受信、再接続、切断中イベントの保持 |
| `ui` | 3 | 1 | 20ms | ボタン判定、OLED更新 |

タスク間は自作EventBus（内部はFreeRTOSキュー）でつなぎ、共有変数を作らない。Wi-Fiスタックと同じコア0には通信だけを置き、制御の周期が通信負荷で乱れないようにする。

### MQTTの使い方

| トピック | 向き | 内容 |
| --- | --- | --- |
| `iot/v1/{site}/mcu-cpp-01/telemetry` | 送信 | 計測値（readings\[\]）と、全アクチュエーターのlevel・owner |
| `.../event` | 送信 | 下の「イベント」の表のコード |
| `.../state` | 送信（retain、LWT） | status（online／offline）、mode、channels\[\]（period\_ms）、actuators\[\]（min・max・max\_on\_s。ポンプは30、ほかはnull）、fw\_version。offlineのstateはseqを進めない |
| `.../cmd` | 受信 | `set_actuator`、`release`、`set_mode`、`get_status`（set\_param は拒否。上の「指令の受け付け」） |
| `.../cmd/ack` | 送信 | cmd\_id、status（accepted／done／rejected／failed）、limited・applied、reason |

登録機器の一覧をstateに含めることで、作品03は機器を追加しても設定変更なしに新しい機器を認識できる。

### 接続と時刻

- SNTP で時刻を合わせた後に MQTT に接続する。同期前は何も送らない。SNTP サーバーの第一候補は作品03の Raspberry Pi（LAN の中で閉じる）
- 同期前に起きたイベント（BOOT、SENSOR\_FAULT など）は `uptime_ms` で記録し、同期後に「同期した時刻 −（いまの uptime − 発生時の uptime）」で ts を求めて送る。telemetry は同期前は送らない
- 同期できない間、作品02は作品03から offline に見える。ローカル制御は続ける
- MQTT のユーザー名は `mcu-cpp-01`。パスワードは設定で渡し、リポジトリに入れない
- LWT（offline の state）は接続のたびに登録し直す。seq はその時点で次に使う値、ts は接続時刻、boot\_id は online の state と同じ。seq は進めない
- QoS 1 の確認待ちの上限32件は MqttLink が数えて守る（esp-mqtt の outbox の上限はバイト数で、件数では制限できない）
- boot\_id は RNG（esp\_random）から作る16進小文字8桁。無線が動く前に作る場合は、乱数源が有効になっていることを詳細設計で確かめる
- 重複した cmd\_id（直近64件・10分）には、payload は前回と同じで、エンベロープ（msg\_id・seq・ts）は新しい ack を返す

### イベント

1回の遷移につき1件を送る（v0.2.2 #41）。ローカルルールの発火ではイベントを出さず、telemetry の owner で分かるようにする。

| コード | 送るとき |
| --- | --- |
| `BOOT` | 起動。`detail.reset_reason`（7値）と `detail.platform_reason`（ESP-IDF の元の値） |
| `MODE_CHANGED` | モードの切替 |
| `INTERLOCK_ACTIVE`／`INTERLOCK_RELEASED` | 安全制限が実際に効いたとき／解除したとき。丸めた指令を受けただけでは出さない |
| `SENSOR_FAULT`／`SENSOR_RECOVERED` | センサーの故障／復旧（BME280 の故障で何件出すかは未決） |
| `UPSTREAM_LOST`／`UPSTREAM_RESTORED` | MQTT の切断／復帰。LOST は復帰後に発生時刻で送る |

### リセット要因の対応

| esp\_reset\_reason() | reset\_reason |
| --- | --- |
| `ESP_RST_POWERON` | power\_on |
| `ESP_RST_EXT` | pin |
| `ESP_RST_SW` | software |
| `ESP_RST_PANIC` | software（作品01と揃えるため共通仕様の変更案で確認する） |
| `ESP_RST_INT_WDT`・`ESP_RST_TASK_WDT`・`ESP_RST_WDT` | watchdog |
| `ESP_RST_BROWNOUT`・`ESP_RST_PWR_GLITCH` | brownout |
| `ESP_RST_DEEPSLEEP` | low\_power |
| そのほか（`ESP_RST_UNKNOWN`・USB・JTAG など） | unknown |

## 7. 異常系設計・テスト方針・未決事項

### 異常系の扱い

| 異常 | 検知方法 | 対応 |
| --- | --- | --- |
| I2Cセンサー無応答 | トランザクションのタイムアウト、IDレジスタ不一致 | qualityをfaultにしてSENSOR\_FAULTを送り（復旧でSENSOR\_RECOVERED）、30秒ごとに再初期化を試みる。その機器を参照するルールは停止 |
| I2Cバスのハング | 連続タイムアウト | バスリカバリ（SCLを15クロック送出）後にドライバを再初期化 |
| 計測値の古さ | 最終更新から周期の3倍経過 | qualityをstaleにし、ルールの判定に使わない |
| Wi-Fi／MQTT切断 | イベントハンドラ、PINGRESP途絶 | 指数バックオフで再接続。上位のリースは期限で自然に失効し、ローカル制御で継続 |
| SNTP未同期 | 起動後、または同期の失敗 | MQTTに接続せず同期を再試行。イベントはuptime\_msで保持し、ローカル制御は続ける |
| 不正なcmd | JSON Schema相当の検証、未知の機器ID、範囲外の値 | rejectedとreason（invalid\_arg・unknown\_op・unknown\_target・expired）をackで返す。cmd\_idが読めない電文は破棄して数える |
| 重複cmd | 直近64件・10分のcmd\_idと結果を保持 | 再実行せず、payloadは前回と同じでエンベロープは新しいackを返す（冪等性） |
| インターロック違反の指令 | ControlArbiterでの照合 | 出力は制限値に丸め、ackに `limited: true` と `applied` を付ける。禁止中（土壌水分センサー故障中のポンプ）は rejected（unsafe）。INTERLOCK\_ACTIVE は制限が実際に効いたときに1件 |
| タスク停止 | タスクウォッチドッグ | 再起動し、BOOTイベントのreset\_reason（watchdog）で報告。起動直後は全アクチュエーター停止から始める |

### テスト方針

| レベル | 対象 | 手段 | 主な観点 |
| --- | --- | --- | --- |
| 単体 | domain・app・drivers | GoogleTest＋gMock（Linux） | 調停の全優先度組み合わせ、リース期限切れ、ヒステリシスとクールダウン、各ドライバの補正計算（データシートの計算例と照合） |
| 結合（ホスト） | アプリ全体＋フェイク機器 | ESP-IDFのLinuxターゲットまたはホスト用main、PC上のMosquitto | MQTT指令→出力→ack、ブローカー停止と復旧、モード遷移 |
| 結合（エミュレータ） | ESP32向けバイナリ | QEMUまたはWokwi | QEMU：起動、タスク構成、タスクウォッチドッグ、リセット要因。Wokwi：センサー（I2C・ADC）、PWM（LEDC）、Wi-Fi経由のMQTT。CIに載せるかは確認後に決める |
| 契約 | MQTTペイロード | `iot-common-spec` の契約テスト（iotspec-contract --role mcu-cpp-01。Mosquittoはdocker composeで起動）と、samples/ の受け付け・破棄の単体テスト | 作品03・04との解釈一致 |
| 拡張性の実証 | 機器追加 | 例：SHT31（温湿度）を追加するPR | NFR-01の差分条件を満たすこと |
| 実機（後日） | 実機一式 | 実センサー、電流計 | ドライバの実機差異、応答時間、省電力モードの消費電流 |

### 受け入れ基準（MVP）

- [ ] FR-01〜10がテスト仕様書に紐付き、ホスト結合テストで合格している
- [ ] 機器追加PRでNFR-01を実証している
- [ ] 契約テストとNFR-03・05・07をCIで確認できる

### 未決事項

- [ ] QEMUとWokwiの使い分け（テスト方針の表）の確認。WokwiはESP32-S3のI2C（マスター）・LEDC・ADC・Wi-Fiに対応するが、BME280・BH1750の部品の有無、CIからMosquittoへの接続、APIトークンの扱いを確かめる。QEMUは周辺の一覧をREADMEで確かめる
- [ ] 安全インターロックの値（ポンプ30秒、気温45℃）を設定で変えられるようにするか。安全側の値なのでビルド時固定を推奨
- [ ] 自作EventBusのcoreプロファイルで十分か、fullが必要か

* [ ] ESP-IDFのツールチェーンでC++20（std::span、concepts）を問題なく使えるか。着手時に最小構成でビルドして確認する
* [ ] 自作ライブラリ（EventBus・Error Framework・MQTTアダプタ）の取り込み元。GitHub にあるのは iot-hal（GPIO まで）だけ。ホスト版の MQTT 実装の有無、iot-hal と Error Framework の Result をどちらに揃えるかもあわせて決める
* [ ] BME280 の故障で SENSOR\_FAULT をチャネルごとに3件出すか、1件にして detail にチャネルを並べるか
* [ ] ESP\_RST\_PANIC を software にする対応を、共通仕様の変更案として作品01と揃える
* [ ] FR-11 のイベント32件があふれたときの扱い（古いものから捨てるか、捨てた件数をどこで示すか）
* [ ] JSON の組み立てと解析に使う方法（ヒープを使わないもの）
* [ ] ポンプの連続上限を、control タスクとは別に esp\_timer でも止めるか
* [ ] SNTP サーバーを作品03に置くことを、作品03の設計に反映する
* [ ] iot-common-spec v0.2.2 の PR のマージとタグ付け（作品02の P1 の前）

# 作品02 フェーズ P2：制御の中核（5段の調停・インターロック・モード・ローカルルール）

## はじめに読むもの

1. `CLAUDE.md`（必ず従う）
2. `docs/open-questions.md`（とくに #2・#6・#8、#14〜#18）
3. `docs/design/01-requirements-basic-design.md`：第2章（FR-02〜FR-06）、第5章、第6章（制御の調停・出力の持ち主・指令の受け付け・モード・イベント）、第7章（異常系の扱い）
4. P1 のコード：`src/app/control_arbiter.*`、`src/app/command_service.*`、`src/app/node.*`、`src/domain/pump_interlock.*`、`src/domain/reading_store.hpp`、`src/config/devices.cpp`
5. iot-common-spec の `docs/spec.md` 第5章（データ品質）・第6章（イベント）

読み終えたら、作業に入る前に「P2 で作るもの」「作らないもの」「不明点」を短くまとめて報告する。不明点は open-questions に記録してから進めること。

## 準備

- main（P1 のマージ後）から `phase/p2-control-core` を切る
- iot-common-spec にタグ v0.2.2 ができていれば、submodule と CI の契約テストキットを v0.2.2 に上げ、#12 を「決定」にして README の版も直す。なければ v0.2.1 のまま進める
- `docs/open-questions.md` の #14〜#18 を「決定（2026-11-22、io 確認）：推奨案（A）のとおり」に変える。#16 の `detail` は P2 で足すインターロックにも同じ形（`{"id": …, "rule": …}`）を使う
- このプロンプトの「P2 で決めたこと」の各項目を、open-questions に「決定（2026-11-22、P2 プロンプト）」として1行ずつ足す（#19 から）。#6 もここで決定にする

## P2 で決めたこと（設計の決定。open-questions に写す）

| 論点 | 決定 |
|---|---|
| #6 BME280 の故障で出す SENSOR_FAULT | **チャネルごとに1件**（temp_air・humidity・pressure の3件。event の `channel` に入れる）。復旧も同じ |
| インターロックの持ち方 | `domain::IInterlock` を実装したクラスを、構成表で対象のアクチュエーターに結び付ける。ControlArbiter は具体的なインターロックのクラスを知らない（NFR-01：新しいインターロックも「新しいクラス＋構成表1行」） |
| インターロックの効き方 | 各インターロックは「強制値（あれば、その値に固定）」と「上限（その値を超えさせない）」を返す。強制値は上限より優先する。同じアクチュエーターに複数あれば、強制値はいちばん大きいもの、上限はいちばん小さいものを使う |
| ファンのインターロック（`rule: temp_high`） | temp_air が 45.0℃以上で fan_main を1000に固定。43.0℃未満が10秒続いたら解除（43.0℃以上に戻ったら10秒を数え直す） |
| 気温センサー故障時（`rule: sensor_fault`） | temp_air の quality が fault または stale の間は fan_main を1000に固定（安全側）。good に戻った時点で解除し、そこからは temp_high の判定に従う |
| 土壌水分センサー故障時（`rule: sensor_fault`） | soil の quality が fault または stale の間、pump の上限を0にする。その間のポンプへの set_actuator は rejected（unsafe）（P1 のまま）。**動いていたポンプを止めたときだけ** INTERLOCK_ACTIVE を出す（センサーの故障そのものは SENSOR_FAULT で伝わる） |
| INTERLOCK_ACTIVE／RELEASED を出す時点 | インターロックの条件が成り立って出力を実際に変えたとき／その条件が解けたとき。遷移1回につき1件。`detail: {"id": "<アクチュエーター>", "rule": "max_on_s"｜"temp_high"｜"sensor_fault"}` |
| 本体側の stale の判定 | 最後に good で読めた時刻から、そのチャネルの周期の3倍を超えたら stale（ルールとインターロックの入力としてだけ使う。telemetry は読めたチャネルだけを送るので変わらない） |
| ボタンの短押し | 自動 → 手動 → 省電力 → 自動 の順に切り替える。手動操作に使うボタンの割り当て（どのボタンでどの機器をどう動かすか）は P3 で決める（open-questions に「未決」で足す） |
| 手動モードのタイムアウト | 手動モードに入った時刻か、最後の手動操作の時刻から30分（1,800,000ms）ちょうどで自動に戻る。手動モードを出たら手動の設定は消す |
| 省電力モード | 計測周期を5倍にし（state を出し直す）、ルールのうち `lighting` の印のもの（led_grow のルール）を止める。インターロックと上位指令は有効のまま |
| ローカルルールの既定値 | 下の表。値は構成表（`src/config/`）に置き、ビルド時に固定する（NVS での変更は扱わない） |
| ルールの入力が good でないとき | そのルールは「不明」として提案を出さない（実行中のパルスも止める）。共通仕様 第5章「疑わしい値で制御しない」 |
| 同じアクチュエーターに複数のルールが提案したとき | 大きい方の level を使う |
| パルスの途中で上位の段（上位指令・手動）が勝ったとき | パルスは時刻で数え続ける。上位の段が外れたときに5秒が残っていれば残りだけ動かし、過ぎていれば動かさない。クールダウンはパルスの開始から数える |
| MODE_CHANGED の detail | `{"from": "<前のモード>", "to": "<後のモード>", "cause": "button"｜"cmd"｜"timeout"}` |

ローカルルールの既定値（基本設計書 FR-05 の例に、ヒステリシスとアクションの値を足したもの）：

| ルール ID | 入力 | 入る条件 | 出る条件 | アクション | クールダウン | 印 |
|---|---|---|---|---|---|---|
| `light-on-dark` | lux | 200 未満 | 250 以上 | led_grow を1000 | なし | lighting |
| `cool-on-high-temp` | temp_air | 30.0℃以上 | 28.0℃未満 | fan_main を600 | なし | — |
| `water-on-dry` | soil | 30.0% 未満 | —（パルス） | pump を1000で5秒 | 10分（パルスの開始から） | — |

## P2 で作るもの

1. **domain**
   - `IInterlock`：入力（ReadingStore の最新値と品質、そのアクチュエーターへの要求、時刻）から「強制値・上限・遷移」を返す。ID（`rule`）を持つ
   - `PumpInterlock` を IInterlock に合わせる（中身の決まりは P1 のまま）。`FanTempInterlock`（temp_high）、`SensorFaultInterlock`（sensor_fault。対象チャネルと「強制値か上限か」を構成で渡す）を足す
   - `Rule`（ルール ID・入力チャネル・入る条件と出る条件・アクション（固定の level、または「level と秒数」のパルス）・クールダウン・印）
   - ReadingStore に「最後に good で読めた時刻」と stale の判定を足す
2. **app**
   - **ControlArbiter（本版）**：アクチュエーターごとに「インターロック ＞ 上位指令（リース中）＞ 本体の手動操作（手動モードのときだけ）＞ ローカルルール（自動・省電力モードのときだけ）＞ 既定値（0）」で出力を決め、owner を返す（interlock／cmd の priority／local／local／none）。インターロックの強制値・上限は、どの段の結果にもかける。上限で値を下げた、または強制値で値を変えたら、そのアクチュエーターの owner は interlock
   - set_actuator の ack：インターロックで値が変わったら `limited: true`、`applied` は出力した値（ファンのインターロック中に fan_main を300で指令 → applied 1000・limited true）。P1 の「lease_s が max_on_s を超える指令」の扱いはそのまま
   - **ModeManager**（State パターン）：3モード、短押しでの切り替え、cmd（set_mode）での切り替え、手動モードのタイムアウト。切り替えたら MODE_CHANGED を1件と state の出し直し。いまと同じモードへの set_mode は done だけ（#18）
   - **ManualControl**：手動モードでの手動の出力（`set_manual(id, level)`）。手動モードのときだけ受け付け、最後の操作の時刻を ModeManager に知らせる。P2 ではボタンの割り当てがないので、テストとホスト版のデバッグ入力（下の7）から呼ぶ
   - **RuleEngine**：表のルールを評価し、アクチュエーターごとの「提案」を出す。ヒステリシス、パルス、クールダウン、「不明」の扱い、省電力モードで lighting を止めること
   - **SensorPoller**：周期ごとに ISensor::read を呼び、失敗したらそのセンサーのチャネルを fault にして SENSOR_FAULT（critical）をチャネルごとに1件、読めたら SENSOR_RECOVERED（info）を1件出す。fault の間は30秒ごとに ISensor::init を呼んで再初期化を試みる。省電力モードでは周期を5倍にする
   - TelemetryService・StateService：省電力モードの周期を反映する（state の `channels[].period_ms` も5倍の値にする）
3. **config**：構成表に、インターロック（fan_main ← FanTempInterlock と SensorFaultInterlock(temp_air, 強制1000)、pump ← PumpInterlock と SensorFaultInterlock(soil, 上限0)）とルールの表を足す。どちらも「1行が1つ」
4. **drivers/sim**：SimSensor に、値の変更と故障の注入を足す（テストとデバッグ入力から使う）
5. **イベント**：MODE_CHANGED（info）、INTERLOCK_ACTIVE（warning）／RELEASED（info）、SENSOR_FAULT（critical）／RECOVERED（info）。遷移1回につき1件。ローカルルールの発火ではイベントを出さない
6. **CommandService**：set_mode を ModeManager に渡す。ほかの op は P1 のまま
7. **ホスト版のデバッグ入力**（`platform/host` だけ）：`mcu_cpp_01` に次の引数を足す。どれも省略でき、省略時の動きは P1 と同じ（契約テストはこれを使わない）
   - `--sim <channel>=<value>[@<秒>]`（起動から指定秒後に模擬センサーの値を変える。複数指定可）
   - `--sim-fault <センサーID>@<開始秒>[-<終了秒>]`（模擬センサーを読めなくする）
   - `--press-at <秒>`（その時刻にボタンの短押しを1回入れる。複数指定可）
   - `--manual <id>=<level>@<秒>`（その時刻に手動の出力を入れる。手動モードでなければ無視してログに残す）

## テスト

GoogleTest。境界の値と「ちょうど」の時刻は必ず入れる。

- ControlArbiter：5段の優先度の組み合わせを表で網羅（各段の有無 × モード）、owner、インターロックの強制値と上限が各段にかかること、複数インターロックの合成、limited と applied
- FanTempInterlock：44.9℃では効かない、45.0℃で効く、42.9℃が9,999ms では解除しない、10,000ms ちょうどで解除、途中で43.0℃に戻ると数え直す
- SensorFaultInterlock：fault・stale で効く、good で解除、ポンプが止まっているときは INTERLOCK_ACTIVE を出さない
- RuleEngine：各ルールの入る・出る境界（199/200、249/250、29.9/30.0、27.9/28.0、29.9%/30.0%）、パルス5秒ちょうど、クールダウン10分ちょうど、入力が fault・stale のとき提案を出さずパルスを止める、省電力モードで light-on-dark が止まる、複数ルールの大きい方
- ModeManager：短押しの順番、cmd での切り替え、同じモードへの set_mode、タイムアウト30分ちょうど（手前の1ms では戻らない）、手動操作でタイムアウトを数え直す、手動モードを出ると手動の設定が消える、切り替えごとに MODE_CHANGED が1件と state の出し直し
- SensorPoller：失敗で fault とチャネルごとの SENSOR_FAULT、30秒ごとの再初期化、復旧で SENSOR_RECOVERED、省電力モードで周期が5倍
- ReadingStore：周期の3倍ちょうどの前後で stale
- ホスト結合テスト（FakeMqttLink＋仮想時計）：
  1. 気温を46℃にする → fan_main が1000・owner interlock・INTERLOCK_ACTIVE 1件 → 42℃にして10秒 → INTERLOCK_RELEASED 1件、ルールの判定に戻る
  2. 照度を150 lux にする（自動モード）→ led_grow が1000・owner local → 省電力モードにする → led_grow が0・state の period_ms が5倍
  3. 土壌水分を25% にする → ポンプが5秒動いて止まり、10分は動かない → センサーを故障させる → SENSOR_FAULT 1件、ポンプへの set_actuator が rejected（unsafe）
  4. 手動モードで手動の出力 → owner local → 上位指令が来ると上位が勝つ → リースが切れると手動の値に戻る → 30分操作がないと自動に戻り MODE_CHANGED（cause: timeout）

## P2 で作らないもの

実機のドライバ・I2C フェイク・II2cBus などの platform のインターフェース（P3）、ボタンの GPIO とチャタリング除去・OLED（P3）、SNTP と uptime からの ts の逆算・イベント保持の本版・指数バックオフ（P4）、品質ゲート（P5）、ESP-IDF・FreeRTOS のタスク・ライトスリープ（P6）、NVS での設定の保存。

## 完了条件（すべて満たしてから PR）

1. `cmake --preset host-debug && cmake --build --preset host-debug` が Mac と CI の両方で警告ゼロ、`ctest --preset host-debug --output-on-failure` が全件合格
2. 契約テストの9チェックが Mac と CI の両方で合格し、省略0件（P1 からの後戻りがない）
3. ASan・UBSan の指摘ゼロ（CTest と契約テストの両方）
4. 上の「テスト」がすべてそろっている
5. P1 の grep の2つの検査が0件のまま。加えて、`src/app/control_arbiter.*` に具体的なインターロックのクラス名（PumpInterlock・FanTempInterlock・SensorFaultInterlock）と機器 ID の文字列（`"fan_main"` など）が出てこないこと（grep の結果を PR に載せる）
6. open-questions に #14〜#18 の決定と、「P2 で決めたこと」の各行が入っている
7. ホスト版をデバッグ入力付きで動かした例（`--sim temp_air=46@5 --sim temp_air=42@20` など）と、そのとき observer で受けた event・telemetry の要点を PR に載せる

PR は `gh pr create` で作る。説明には、完了条件ごとの結果、open-questions に足した項目、P3 への申し送り（仮版にしたもの、ボタンの割り当てなど）を書く。マージはしない。

## 止まって相談すること

- 契約テストが、このプロンプトどおりに作ると不合格になるとき
- このプロンプトの決定と、基本設計書・共通仕様が食い違うとき
- 値や順序が書かれていないことを決める必要が出たとき（例：手動モードの間にルールの入る条件が成り立ったとき、自動に戻った瞬間にパルスを始めるか）

いずれも `docs/open-questions.md` に記録し、その件に関わる作業を止めて報告する。iot-common-spec 側は変更しない。

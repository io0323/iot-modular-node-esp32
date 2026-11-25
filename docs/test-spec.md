# テスト仕様書

作品02（mcu-cpp-01）の要件（基本設計書 第2章 FR-01〜FR-13・第3章 NFR-01〜NFR-09）ごとに、確かめるテストとその結果をまとめます。実機を使わない開発段階の結果（P8 の完了時、main の c60702b 以降）で、実機でしか確かめられないものは「実機フェーズ」と書いています。

## 確かめる手段

| 手段 | 中身 | どこで動かすか |
|---|---|---|
| 単体テスト | GoogleTest。`tests/unit/*_test.cpp`。ドライバは、データシートのレジスタ応答を再現する I2C のフェイクで確かめる | CTest（host-debug・host-release・host-tsan）、Mac と CI |
| ホスト結合テスト | アプリ全体（Node）を、フェイクの MQTT・仮想の時計で動かす。`tests/integration/node_test.cpp`（模擬機器）、`node_fakehw_test.cpp`・`node_sht31_test.cpp`（ドライバとフェイクの機器） | CTest、Mac と CI |
| 契約テスト | iot-common-spec v0.2.1 の `iotspec-contract --role mcu-cpp-01`。ACL・パスワード付きの Mosquitto に対して9件 | ホスト版（既定・`--devices fakehw`・TSan）、QEMU のファームウェア（qemu-fakehw）。Mac と CI |
| ブローカーの停止・再開 | `tests/system/broker_restart.py`。Mosquitto を止めて戻し、LWT・再接続・LOST／RESTORED を確かめる | ホスト版（ASan・TSan）、Mac と CI |
| QEMU | `tools/qemu.sh test`（起動・4タスク・BOOT・stats）、`contract`、`soak 30`（NFR-03） | Mac と CI（soak は Mac で1回） |
| Wokwi | `tools/wokwi.sh`：`wokwi/scenarios/` の4本（boot_readings・buttons_mode・wifi_heap・watchdog） | Mac と CI（main への push と PR のラベル `wokwi`。#133・#138） |
| 品質の検査 | `tools/quality.sh all`：カバレッジ・clang-tidy・cppcheck・lizard・Doxygen | Mac と CI |
| grep の検査 | 層の依存とヒープの型（PR の説明に載せている。下の NFR-03・NFR-05） | Mac（PR ごと） |

契約テストの9件：`ack`・`lease_expiry`・`duplicate_cmd_id`・`limited_applied`・`set_param_unknown_op`・`schema`・`seq_and_boot_id`・`publish_rate`・`lwt`。

テスト名は「ファイル：テストの組.テスト名」で書きます。どれも CTest の一覧（`ctest --preset host-debug -N`。236件）にあり、P8 の完了時にすべて合格しています。

## 機能要件

| ID | 要件（要点） | 確かめるテスト | 結果 |
|---|---|---|---|
| FR-01 | 構成表から機器を作って登録し、ID で指定できる | `services_test.cpp`：ServicesTest.StatePayloadHasChannelsActuatorsAndFw、`node_fakehw_test.cpp`：NodeFakeHwTest.ReadsSensorsThroughDriversAndShowsOled、`node_sht31_test.cpp`：NodeSht31Test.StateListsTheTwoNewChannels、`command_service_test.cpp`：CommandServiceTest.UnknownTarget、契約テスト `schema` | 合格 |
| FR-02 | 機器ごとの周期で計測し、単位と品質（good・stale・fault）を付ける。周期が変わったら state を出し直す | `sensor_poller_test.cpp`：SensorPollerTest.ReadsEachSensorAtItsPeriod・PowerSaveMultipliesPeriodByFive・FailureMarksFaultAndEmitsOnePerChannel、`domain_test.cpp`：ReadingStore.StaleAfterThreeTimesThePeriod・NeverReadIsStale、`node_test.cpp`：NodeTest.TelemetryCadenceFollowsChannelPeriods・ButtonCyclesModesAndRepublishesStateEachTime、`node_sht31_test.cpp`：NodeSht31Test.TelemetryCarriesTheChannelsEveryPeriod、Wokwi `boot_readings` | 合格 |
| FR-03 | 出力は 0〜1000‰。上下限・最小オン／オフ・連続の上限（ポンプ30秒） | `drivers_test.cpp`：PwmActuator.LevelToDutyAt10Bits・ConfiguresFrequencyAndResolutionAndStartsAtZero、RelayActuator.ActiveHighAndInverted、`control_arbiter_test.cpp`：ControlArbiterTest.ClampsEveryStageToLimits、`ui_service_test.cpp`：MinOnOffTime.HoldsSwitchingUntilExactTimeAndInterlockWins、`domain_test.cpp`：PumpInterlock.PassesRequestUntil30sThenStops、`node_test.cpp`：NodeTest.PumpLimitedThenStoppedAt30sWithInterlockEvent | 合格。PWM の実際の波形・リレーの動きは実機フェーズ |
| FR-04 | 自動・手動・省電力の3モード。ボタンの短押しか cmd で切り替え、手動は30分で自動に戻る。MODE_CHANGED と state | `mode_manager_test.cpp`：ModeManager.ButtonCyclesAutoManualPowerSave・StateBehaviours・CmdSwitchesAndSameModeDoesNothing・ManualTimesOutAtExactly30Minutes・ManualOperationRestartsTimeout・OtherModesDoNotTimeOut、ManualControl.AcceptedOnlyInManualModeAndTouchesTimeout、`node_test.cpp`：NodeTest.SetModeSendsModeChangedAndState・ButtonCyclesModesAndRepublishesStateEachTime・Scenario4_ManualThenUpperCommandThenLeaseExpiryThenTimeout、Wokwi `buttons_mode` | 合格 |
| FR-05 | 自動モードのローカルルール（条件・ヒステリシス・アクション・クールダウン） | `rule_engine_test.cpp`：RuleEngineTest の8件（LightOnDarkHysteresis199_200_249_250・CoolOnHighTemp29_9_30_27_9_28・WaterOnDryPulseExactly5sAndCooldownExactly10min など）、`node_test.cpp`：NodeTest.Scenario1_HighTempForcesFanThenReleasesBackToRule・Scenario2_DarkTurnsLedOnThenPowerSaveTurnsItOff・Scenario3_DrySoilPulsesPumpThenSensorFaultMakesPumpUnsafe | 合格 |
| FR-06 | 上位指令はリース付きで、期限中はルールより優先。インターロックが最優先 | `control_arbiter_test.cpp`：ControlArbiterTest.FiveStagePriorityTable・LeaseReplacesAndExpiresBackToLowerStage、`domain_test.cpp`：Lease.OmittedIs60AndMaxIs600・ExpiresExactlyAtStartPlusLease、`command_service_test.cpp`：CommandServiceTest の18件、`node_test.cpp`：NodeTest.SetActuatorAckTelemetryOwnerAndLeaseExpiry・DuplicateCmdIdGetsSamePayloadWithNewEnvelope、契約テスト `ack`・`lease_expiry`・`duplicate_cmd_id`・`limited_applied`・`set_param_unknown_op` | 合格 |
| FR-07 | telemetry・event・state（retain・LWT）を送り、cmd に cmd/ack を返す。kind ごとに毎秒10件・瞬間50件、QoS 1 の確認待ち32件 | `services_test.cpp`：ServicesTest.TelemetryIncludesGivenReadingsAndAllActuators・PublisherQueuesWhenInflightIsFull ほか、`infra_test.cpp`：Envelope の5件・RateLimiter.Burst50ThenTenPerSecond・Topics.BuildsTopicsAndQos、`node_test.cpp`：NodeTest.ConnectsWithLwtThenSendsOnlineStateThenBoot・ShutdownSendsOfflineStateWithoutAdvancingSeq、`spec_samples_test.cpp`：SpecSamples の3件、契約テスト `schema`・`seq_and_boot_id`・`publish_rate`・`lwt`（QEMU のファームウェアでも9件） | 合格 |
| FR-08 | Wi-Fi と MQTT の切断を検知し、1〜60秒の指数バックオフで再接続。接続は SNTP の後。切断中もローカル制御を続ける | `connection_manager_test.cpp`：Backoff.DoublesFromOneSecondUpToSixty、ConnectionManagerTest の8件、`node_test.cpp`：NodeTest.NetworkDownDisconnectsMqttAndReconnectsAfterNetworkReturns・LwtIsRebuiltOnReconnectWithNextSeq、broker_restart.py | 合格。Wi-Fi の AP の停止と復旧は実機フェーズ（Wokwi では Wi-Fi に接続するところまで） |
| FR-09 | SNTP で同期してから MQTT に接続。同期前のイベントは uptime で記録し、送るときに ts を逆算。telemetry は同期前は送らない | `connection_manager_test.cpp`：ConnectionManagerTest.ConnectsNetworkThenWaitsForSyncThenMqtt、`infra_test.cpp`：Envelope.TsIsMonotonicPlusOffsetAndUptimeIsAtStamp、`event_service_test.cpp`：EventServiceTest.QueuesWhileDisconnectedAndStampsAtOccurrence、`node_test.cpp`：NodeTest.P4Scenario1_SntpDelayHoldsEverythingThenSendsWithOccurrenceTs、QEMU・Wokwi（SNTP の同期の後に BOOT の ts が UTC になる） | 合格 |
| FR-10 | 全タスクをタスクウォッチドッグに登録し、止まったら再起動。BOOT の reset_reason（7値）と platform_reason | `reset_cause_test.cpp`：ResetReasonTable.MapsToSevenValues（11件）・ResetReason.OutOfRangeValueIsUnknown・IsConstexpr、`node_test.cpp`：NodeTest.ConnectsWithLwtThenSendsOnlineStateThenBoot、QEMU `test`（power_on・ESP_RST_POWERON）、Wokwi `watchdog`（ui を止めると5秒後に再起動し、次の BOOT が watchdog・ESP_RST_TASK_WDT） | 合格。QEMU ではタスクウォッチドッグが働かない（#131）ので Wokwi で確かめた。power_on と watchdog 以外のリセット要因の実際の発生は実機フェーズ |
| FR-11 | 切断中と同期前の event を最大32件保持し、接続後に送る。telemetry は保持しない | `event_service_test.cpp`：EventServiceTest の10件（KeepsThirtyTwoDroppingOldestButNeverBoot・UnackedEventsAreResentWithSameMsgIdSeqAndTs など）、`node_test.cpp`：NodeTest.EventsWaitUntilConnected・NoLostOrRestoredBeforeFirstConnect・P4Scenario2_FortyEventsDuringOutageKeepThirtyTwoAndRestoredCountsEight・P4Scenario3_UnackedEventIsResentWithSameMsgIdAfterReconnect・CmdAckIsNotResentAfterReconnect、broker_restart.py | 合格 |
| FR-12 | ルールのしきい値と計測周期を NVS に保存 | なし | **本版では扱わない**（Should。#108・#144）。値は構成表に固定 |
| FR-13 | OLED に計測値・モード・接続状態。省電力モードでは消灯 | `drivers_test.cpp`：Ssd1306Test の3件、`ui_service_test.cpp`：UiServiceTest.ComposesEightLines・FaultShowsDashes・RefreshesEverySecondAndTurnsOffInPowerSave、`node_fakehw_test.cpp`：NodeFakeHwTest.ReadsSensorsThroughDriversAndShowsOled・PowerSaveTurnsOledOff、Wokwi `boot_readings`・`buttons_mode`（画面を基準の画像と比べる） | 合格。表示の向き（#56・#134）と実際の見え方は実機フェーズ。Wokwi の画面の取り込みは消灯を写さない |

## 非機能要件

| ID | 要件（要点） | 確かめ方 | 結果 |
|---|---|---|---|
| NFR-01 | 機器の追加で変えるのは、新しいクラスのファイルと構成表1行だけ。既存クラスは変えない | SHT31 を足す PR（#9）の差分。`git diff --numstat main...feature/add-sht31 -- src firmware tests` と、`sht31_test.cpp` の9件・`node_sht31_test.cpp` の4件（下の「NFR-01 の経緯」） | 合格（PR #8 の後）。既存のファイルの変更は「追加1行×7」 |
| NFR-02 | cmd を受けてから出力まで 100ms 以内、ack まで 200ms 以内 | `node_test.cpp`：NodeTest.Nfr02_OutputAndAckFinishWithinTheTickThatReceivedCmd（仮想の時計で、cmd を受けた tick の中で出力と ack の送信が終わる。#72） | ホスト結合テストで合格。実機での実測は実機フェーズ |
| NFR-03 | 初期化の後に自分のコードがヒープを確保しない。24時間でヒープの最低残量が下がり続けない | grep（app・domain・drivers・infra に `new`・`malloc`・`std::string`・`std::vector`・`std::map`・`std::function`・`std::shared_ptr` がない）。QEMU の30分（`tools/qemu.sh soak 30`）。Wokwi `wifi_heap`（`--heap-min-free 30720`） | grep は0件。QEMU：最低残量 210,592 バイトが5分後から30分後まで変わらない。Wokwi：Wi-Fi に接続した後の最低残量 144,432 バイト（30KB 以上）。24時間は実機フェーズ（#125） |
| NFR-04 | C++20、例外と RTTI は無効、エラーは Result 型 | ビルドの設定（ホスト版 `-std=c++20 -fno-exceptions -fno-rtti`、ターゲット版 `-std=gnu++20`。#95）。`support_test.cpp`：Result.HoldsValueOrError・VoidSpecialization | 合格。Error Framework ではなく自前の `support::Result`（ADR 0001） |
| NFR-05 | ESP-IDF に依存するのは platform だけ。ほかは Linux でビルド・テストできる | ホスト版のビルドとテスト（CI の host・host-release・host-tsan）。grep（domain・app・infra・drivers・config に `ESP_PLATFORM`・ESP-IDF・FreeRTOS のヘッダがない） | 合格 |
| NFR-06 | インターロックは、どの経路の指令でも破られない | 上位指令：`control_arbiter_test.cpp`：ControlArbiterTest.ForceAppliesOverEveryStage・UpperAppliesOverEveryStageAndOwnerOnlyWhenChanged・RejectsCommandsWhenAnInterlockSaysSo、`command_service_test.cpp`：CommandServiceTest.LeaseOverMaxOnIsLimitedAtReceipt・PumpRejectedUnsafeWhileSoilFault・PumpRejectedUnsafeWhileSoilStale、`node_test.cpp`：NodeTest.PumpLimitedThenStoppedAt30sWithInterlockEvent。本体の手動操作：`node_fakehw_test.cpp`：NodeFakeHwTest.ActionLongPressRunsPumpThenInterlockStopsAt30s。ローカルルール：`node_test.cpp`：NodeTest.Scenario3_DrySoilPulsesPumpThenSensorFaultMakesPumpUnsafe・Scenario1_HighTempForcesFanThenReleasesBackToRule。最小オン／オフとの関係：MinOnOffTime.HoldsSwitchingUntilExactTimeAndInterlockWins | 合格 |
| NFR-07 | domain と app の行カバレッジ 80% 以上。clang-tidy と cppcheck の警告0 | `tools/quality.sh all`（CI の品質のジョブ） | 合格。domain 92.5%・app 96.1%、clang-tidy 0・cppcheck 0 |
| NFR-08 | AP が戻ってから60秒以内に MQTT に再接続。切断中も計測とローカル制御を続ける | broker_restart.py（ブローカーが戻ってから再接続まで）。`connection_manager_test.cpp`：ConnectionManagerTest.NetworkDownTakesMqttDownAndReconnectsInOrder、`node_test.cpp`：NodeTest.NetworkDownDisconnectsMqttAndReconnectsAfterNetworkReturns・P4Scenario2_FortyEventsDuringOutageKeepThirtyTwoAndRestoredCountsEight | 合格。ブローカーが戻ってから再接続まで 10.2〜10.6 秒（Mac）。Wi-Fi の AP の停止と復旧は実機フェーズ |
| NFR-09 | 公開 API に Doxygen のコメント。関数の循環的複雑度 10 以下 | `tools/quality.sh all` の Doxygen（`WARN_AS_ERROR`）と lizard（`-C 10`） | 合格。Doxygen の警告0、複雑度の最大10 |

### NFR-01 の経緯

1. **P3〜P7 の構成表では、NFR-01 を満たしていなかった。** 実機用の構成表 `config::HwRig` と fakehw 用の `config::FakeHwRig` は表ではなくクラスで、機器ごとにメンバー・初期化子・一覧の配列を書いていた。リファクタリングの前（a09ad65）の構成表に SHT31 を実際に足して数えると、次の変更が要った（#145）
   - 変える既存のファイル：4つ
   - 差分：追加16行・変更3行（`sensors_` の大きさ・並び、FakeHwRig の初期化子）
   - 変わる既存のクラス：HwRig と FakeHwRig の2つ
2. **P8 で SHT31 を足そうとして見つかった。**
3. **PR #8 で、構成表を「1行が1台」の表（X マクロ）に直した。** 振る舞いは変えていない。表は `src/config/hw_table.hpp`・`fakehw_table.hpp`（ADR 0006）
4. **その後の P8a（PR #9）では、既存のファイルの変更が「追加1行×7」になった。** 内訳は次のとおり
   - 構成表：表の行2つ、そのクラスの include 2つ
   - ビルド定義：ホスト版の CMake、ESP-IDF のコンポーネント、テストの登録
   - 削除と変更は0行

「変えてよいもの」は、新しいファイル、構成表の1行とそのクラスの include の1行、ビルド定義の1行です（#139）。README・open-questions の変更は数えていません（#146）。模擬機器の構成（`config/devices.cpp` の `SimRig`。ホスト版の既定）は試験用で、表にしていません（#147）。

## 受け入れ基準（MVP）に対する結論

基本設計書 第7章「受け入れ基準（MVP）」の3項目です。

| 項目 | 結論 | 根拠 |
|---|---|---|
| FR-01〜10 がテスト仕様書に紐付き、ホスト結合テストで合格している | **満たす** | 上の表で、FR-01〜10 のどれにも `tests/integration/` のテストがあり、合格している。FR-10 のタスクウォッチドッグからの再起動はホスト版では起こせないので、ホスト結合テストでは BOOT と reset_reason の対応を確かめ、再起動そのものは Wokwi で確かめた |
| 機器追加 PR で NFR-01 を実証している | **満たす** | PR #9（SHT31）。既存のファイルの変更は、構成表とビルド定義への追加1行×7。ただし、そうなったのは PR #8 で構成表を表に直した後で、P3〜P7 の構成表では満たしていなかった（上の経緯） |
| 契約テストと NFR-03・05・07 を CI で確認できる | **一部を満たす** | 契約テスト（ホスト版の既定・fakehw・TSan、QEMU のファームウェア）・NFR-05（ホスト版のビルドとテスト）・NFR-07（品質のジョブ）は、push のたびに CI で確かめている。NFR-03 は2つが CI の外にある<br>・「自分のコードがヒープを確保しない」の grep は CI のジョブになく、PR ごとに手で流して説明に載せている<br>・ヒープの最低残量は、Wokwi のジョブ（main への push と PR のラベル `wokwi` だけで動く）で 30KB 以上を確かめている。24時間の監視は実機フェーズ<br>grep を CI の手順に足すことは、作品02の次の作業への申し送りにする（README「この後の作業」） |

## 実機フェーズで確かめること

開発段階（実機を使わない）では確かめられず、実機フェーズに残したものです。

- ドライバと実際の機器の差：BME280・BH1750・SHT31・SSD1306・土壌水分の校正値（#43）、I2C のプルアップ（#113）
- PWM（LEDC）の波形・リレー・ブザーの実際の動き
- OLED の表示の向き（#56・#134）
- NFR-02 の実測（cmd を受けてから出力・ack まで）
- NFR-03 の24時間（ヒープの最低残量）と、Wi-Fi と MQTT が同時に動いた状態のヒープ
- NFR-08 の Wi-Fi の AP の停止と復旧
- スタックの残りの見直し。I2C のバスの回復、Wi-Fi・MQTT の再接続の直後の一括の送信を通した後の値（#137）
- power_on と watchdog 以外のリセット要因（brownout など）の実際の発生
- ポンプが動き続ける最長（30秒＋タスクウォッチドッグの5秒。ADR 0005）
- 省電力モードのライトスリープと消費電流（#107）

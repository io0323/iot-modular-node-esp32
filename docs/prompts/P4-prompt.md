# 作品02 フェーズ P4：通信の作り込み（時刻・接続管理・イベント保持・送信量）

## はじめに読むもの

1. `CLAUDE.md`（必ず従う）
2. `docs/open-questions.md`（とくに #8・#11・#15）
3. `docs/design/01-requirements-basic-design.md`：第2章（FR-07〜FR-11）、第3章（NFR-02・NFR-08）、第6章（接続と時刻・イベント）、第7章（Wi-Fi／MQTT 切断・SNTP 未同期）
4. iot-common-spec の `docs/spec.md` 第2章（エンベロープの ts・ts_source・uptime_ms、QoS・送信量）、第5章（時刻ずれ）、第6章（UPSTREAM_LOST／RESTORED、DATA_DROPPED は 03・04 のもの）
5. P3 までのコード：`src/app/node.*`・`publisher.*`・`event_service.*`、`src/infra/clock.hpp`・`mqtt_link.hpp`・`rate_limiter.hpp`、`src/platform/host/paho_mqtt_link.*`・`main.cpp`・`debug_inputs.*`

読み終えたら、作業に入る前に「P4 で作るもの」「作らないもの」「不明点」を短くまとめて報告する。

## 準備

- main（P3 のマージ後）から `phase/p4-comm` を切る
- iot-common-spec にタグ v0.2.2 ができていれば submodule と CI の契約テストキットを上げる（#12）。なければ v0.2.1 のまま
- open-questions の #57・#59・#60・#61・#63 を「決定（2026-11-22、io 確認）：推奨案のとおり」にする
- このプロンプトの「P4 で決めたこと」を、open-questions に「決定（2026-11-22、P4 プロンプト）」として1行ずつ足す。#8・#15 もここで決定にする

## P4 で決めたこと（open-questions に写す）

| 論点 | 決定 |
|---|---|
| 接続の順序 | 「ネットワーク（Wi-Fi）が使える → SNTP で同期済み → MQTT に接続」の順に進める状態機械（ConnectionManager）を app に置く。Wi-Fi と SNTP は `INetwork`・`ITimeSync`（infra のインターフェース）で抽象化し、ESP-IDF の実装は P6。ホスト版は「いつも使える・すぐ同期済み」の実装と、デバッグ入力で遅らせる・落とす実装を持つ |
| 同期前に起きたイベントの ts | イベントには発生時の単調時計（ms）を持たせる。送るときに「同期で得た UTC − 単調時計」の差（最新の値）を足して ts を求める。エンベロープの `uptime_ms` も発生時の値にする（共通仕様 第2章「元の時間情報を残す」） |
| seq を振る時点 | 送るときに振る（seq の順と送った順を一致させる）。ts は発生時刻なので、seq の順と ts の順は一致しないことがある |
| 同期が外れたとき | 一度同期したら、以後は同期済みとして扱う（再同期で時刻が直っても、送った電文は直さない）。SNTP の再同期の間隔は ESP-IDF の既定のまま（P6） |
| SNTP サーバー | 構成で持つ（最大3つ）。既定値は P6 で決める（#11 の作品03の設計とあわせる）。P4 ではホスト版に関係しない |
| 再接続の間隔（FR-08） | 1秒から始めて失敗のたびに2倍、上限60秒。ゆらぎ（ジッター）は入れない。接続できたら1秒に戻す。Wi-Fi の再接続と MQTT の再接続で別々に数える |
| UPSTREAM_LOST／RESTORED | 一度 MQTT に接続した後に切れたら UPSTREAM_LOST（warning）を発生時刻で記録し、再接続して state（online）を送った後に、LOST → RESTORED（info）の順で送る。起動後の最初の接続の前は出さない。RESTORED の `detail` は `{"down_ms": <切れていた時間>, "dropped": <保持できずに捨てたイベントの件数>}` |
| #8 イベントの保持（FR-11） | 送れない間（未接続・同期前・確認待ちが上限）のイベントは最大32件保持する。あふれたら**古いものから捨てる**。ただし起動後の BOOT は捨てない（BOOT を除く中で最も古いもの）。捨てた件数は次の UPSTREAM_RESTORED の `dropped` と diag のログに出す。最初の接続の前に捨てた件数は、BOOT の後の最初の機会に diag のログにだけ出す |
| 確認待ちのまま切れた event | QoS 1 の event が PUBACK を受ける前に切断されたら、同じ電文（msg_id・seq・ts を変えない）を保持の先頭に戻し、再接続後に送り直す。受信側は msg_id で重複を除ける（共通仕様の重複排除）。cmd_ack は送り直さない（作品03が2秒で再送し、重複した cmd_id への ack が返るため）。telemetry（QoS 0）は送り直さない。state は再接続のたびに新しく送る |
| #15 送信量・確認待ちの上限 | kind ごとのトークンバケツ（平均毎秒10件・瞬間50件）と、QoS 1 の確認待ち32件の上限は P1 のまま。超えたとき：telemetry は捨てて数える。event はイベントの保持に入れる。cmd_ack と state は固定容量（8件）の送り直し待ちに入れて次の tick で送り、それもあふれたら捨てて数える（diag に出す） |
| NFR-02 の確かめ方 | ホスト結合テストで仮想時計を使い、cmd を受けた tick の中で出力と ack の publish まで終わること（受信から出力まで 0ms・ack まで 0ms）を確かめる。実時間の計測は実機フェーズ |

## P4 で作るもの

1. **infra**：`INetwork`（使えるか・使えるようになった／落ちたの通知）、`ITimeSync`（同期済みか・同期で得た UTC と単調時計の差）。IClock の `synced()` は ITimeSync に移すか、ITimeSync を見るようにする（どちらにしたかを報告する）
2. **app**
   - `ConnectionManager`：上の接続の順序と再接続の間隔。MQTT の接続・切断を Node から引き取る
   - EventService：発生時刻（単調時計）での記録、32件の保持（上の決定）、BOOT を捨てない、確認待ちの電文の戻し、UPSTREAM_LOST／RESTORED
   - Publisher：送るときに ts（同期の差から）と seq を付ける。送り直し待ち（cmd_ack・state）と捨てた件数
   - Node：ConnectionManager を使う形に直す
3. **platform/host**
   - `HostNetwork`・`HostTimeSync`（いつも使える・すぐ同期済み）
   - PahoMqttLink：PUBACK を受けた電文と、切断で確認が取れなかった電文を Node に知らせる
   - デバッグ入力を足す：`--sntp-delay <秒>`（起動から指定秒は同期前）、`--net-down <開始秒>-<終了秒>`（その間ネットワークが使えない。複数指定可）
4. **CI・試験用スクリプト**：`tests/system/broker_restart.py`（uv で動く Python。paho-mqtt で購読する）。Mosquitto を `docker compose stop`／`start` で止めて戻し、次を確かめる：停止の前後で state（offline は LWT、再接続後に online）、UPSTREAM_LOST → RESTORED の順と detail、停止中に起こしたイベント（`--sim` で故障を注入）が再接続後に届くこと、ブローカーが戻ってから60秒以内に再接続すること（NFR-08）。CI の host ジョブに、既定の構成で1回流す手順を足す

## テスト

- ConnectionManager：順序（ネットワーク → 同期 → MQTT）、同期前は MQTT に接続しない、再接続の間隔（1・2・4・8・16・32・60・60 秒）、接続できたら1秒に戻る、ネットワークが落ちたら MQTT の状態も切断にする
- ts：同期前に起きた BOOT・SENSOR_FAULT の ts が「同期の差＋発生時の単調時計」になる、uptime_ms が発生時の値、seq は送った順
- イベントの保持：32件で古いものから捨てる、BOOT は捨てない、捨てた件数が RESTORED の dropped に入る、確認待ちのまま切れた event が同じ msg_id・seq・ts で送り直される、cmd_ack は送り直さない
- 送信量：telemetry を捨てる、event は保持へ、cmd_ack・state は送り直し待ちへ、送り直し待ちがあふれたら捨てて数える
- UPSTREAM_LOST／RESTORED：最初の接続の前は出ない、再接続後に state（online）→ LOST → RESTORED の順、down_ms
- NFR-02：cmd を受けた tick の中で出力と ack が終わる
- ホスト結合テスト（FakeMqttLink＋仮想時計）：
  1. `--sntp-delay` 相当で同期を10秒遅らせ、その間に土壌水分センサーを故障させる → 同期まで何も送らない → 同期後に接続し、state（online）・BOOT・SENSOR_FAULT が発生時刻の ts で届く
  2. 接続中に MQTT を切る → 切断中にイベントを40件起こす → 再接続後に state・LOST・保持していた32件（BOOT を含まない古いものから捨てた残り）・RESTORED（dropped 8）
  3. QoS 1 の event の PUBACK が来る前に切る → 再接続後に同じ msg_id で届く

## P4 で作らないもの

ESP-IDF の Wi-Fi・SNTP・esp-mqtt の実装（P6）、品質ゲート（P5）、ライトスリープ、NVS。

## 完了条件（すべて満たしてから PR）

1. `cmake --preset host-debug && cmake --build --preset host-debug` が Mac と CI の両方で警告ゼロ、`ctest --preset host-debug --output-on-failure` が全件合格
2. 契約テストの9チェックが、既定と `--devices fakehw` の両方の構成で、Mac と CI の両方で合格し、省略0件
3. `tests/system/broker_restart.py` が Mac と CI の両方で合格（ブローカーが戻ってから再接続までの秒数を PR に載せる）
4. ASan・UBSan の指摘ゼロ（CTest・契約テスト・broker_restart の実行のすべてで）
5. 上の「テスト」がすべてそろっている
6. P1〜P3 の grep の検査が0件のまま
7. ホスト版を `--sntp-delay 10 --sim-fault soil_moisture@3` で動かしたときに observer で受けた電文（state・BOOT・SENSOR_FAULT の ts と uptime_ms）を PR に載せる
8. open-questions に #57〜#63 の決定と「P4 で決めたこと」の各行が入っている

PR は `gh pr create` で作る。説明には、完了条件ごとの結果、open-questions に足した項目、P5 への申し送りを書く。マージはしない。

## 止まって相談すること

- 契約テストが、このプロンプトどおりに作ると不合格になるとき
- このプロンプトの決定と、基本設計書・共通仕様が食い違うとき（例：共通仕様の重複排除が msg_id ではなく seq を見ていて、送り直しが捨てられる）
- Paho で「PUBACK を受けた電文」「切断で確認が取れなかった電文」を見分けられないとき
- 値や順序が書かれていないことを決める必要が出たとき

いずれも `docs/open-questions.md` に記録し、その件に関わる作業を止めて報告する。iot-common-spec 側は変更しない。

# 作品02 フェーズ P7：RAM の見直しとエミュレーターでの実行（QEMU・Wokwi）

## はじめに読むもの

1. `CLAUDE.md`（必ず従う）
2. `docs/open-questions.md`（とくに #97・#104・#110・#112〜#114）
3. P6 の PR の「P7 への申し送り」と `docs/quality.md` の「ターゲット版の大きさ」
4. `src/app/node.*`・`event_service.*`・`publisher.*`、`src/domain/cmd_dedup_cache.*`、`src/platform/esp32/`、`firmware/`
5. 固定した ESP-IDF（v5.5.6）の QEMU の案内（`idf.py qemu`、ESP32-S3 で再現される周辺の一覧）と、Wokwi のドキュメント（ESP32-S3、部品の一覧、Custom Chips API、wokwi-cli と CI、Wi-Fi とゲートウェイ）。できること・できないことは資料で確かめ、推測で書かない

読み終えたら、作業に入る前に「P7 で作るもの」「作らないもの」「不明点」と、**QEMU と Wokwi で確かめられること・確かめられないことの表**（資料の出典つき）を報告する。

## 準備

- main（P6 のマージ後）から `phase/p7-emulation` を切る
- iot-common-spec にタグ v0.2.2 ができていれば submodule と契約テストキットを上げる（#12）。なければ v0.2.1 のまま
- このプロンプトの「P7 で決めたこと」を、open-questions に「決定（2026-11-25、P7 プロンプト）」として1行ずつ足す。#114 もここで決定にする

## P7 で決めたこと（open-questions に写す）

| 論点 | 決定 |
|---|---|
| #114 RAM の目標 | ターゲット版の静的な DIRAM の使用を **70% 以下**（約 239KB 以下。ヒープに約 100KB を残す）にする。PSRAM には頼らない（実機の型番がまだ決まっていないため）。起動して Wi-Fi・MQTT が動いた後のヒープの最低残量 30KB 以上を、Wokwi で確かめる |
| 減らし方 | **振る舞いは変えずに、持ち方だけを変える。** イベントの保持と確認待ちの電文は、JSON の写しではなく「電文を作り直すのに要る値」（kind・code・severity・channel・detail の値・発生時の単調時計、送った後は msg_id・seq・ts）を固定長の記録で持ち、送るとき・送り直すときに JSON を作り直す。作り直した電文は、元の電文とバイト単位で同じになること（#71 の「同じ電文」を守る）。重複排除の64件も、ack の payload の写しではなく、ack を作り直すのに要る値で持つ（#28 の「payload は前回と同じ」を守る）。送り直し待ち（cmd_ack・state の 8×2KB）と comm のリングバッファ（24KB）も、同じ考えで要る大きさを計算して決める |
| 減らした後の確かめ | ホスト版の全検査（P6 のまま）に加え、「作り直した電文が元とバイト単位で同じ」ことの単体テスト（イベント・確認待ち・重複した cmd_id への ack）。数値は `idf.py size` で PR に載せる |
| IRAM の 100% | ESP32-S3 では、IRAM の固定の領域（16KB）を使い切った分は DIRAM に置かれるので、見る値は DIRAM。これを資料で確かめて `docs/quality.md` に書く。確かめられなければ止まって相談する |
| QEMU で確かめること | 起動、4つのタスクが動くこと、BOOT（reset_reason・platform_reason）、タスクウォッチドッグで再起動して次の BOOT が watchdog になること、ヒープとスタックのログ（`stats`）。タスクウォッチドッグの試験のために、Kconfig の試験用の項目（`NODE_TEST_HANG_TASK`。既定は無効）で ui タスクを止められるようにする。製品の構成では無効で、コードは platform/esp32 にだけ置く |
| QEMU のネットワーク | 固定した版の QEMU と ESP-IDF が ESP32-S3 で仮想の Ethernet（OpenCores Ethernet）を使えるなら、Kconfig の項目（`NODE_NET_QEMU_ETH`。既定は無効）で INetwork を Ethernet の実装に替え、ホストの Mosquitto に対して**ファームウェアで契約テスト**を流す。使えなければ、その出典を書いて QEMU ではネットワークを確かめない |
| Wokwi で確かめること | ESP32-S3-DevKitC-1 の図（`wokwi/diagram.json`）に、BME280・BH1750・SSD1306・土壌水分の代わりのポテンショメーター（ADC）・LED（PWM の出力の確認用）・リレー（または LED）・ボタン2つをつなぐ。起動、計測値、OLED の表示、ボタンでのモードの切り替え、Wi-Fi への接続とヒープの最低残量。Wokwi に部品がない機器は、Custom Chips API でデータシートのレジスタ（チップ ID・補正係数・データ）だけを再現したチップを作る（`wokwi/chips/`） |
| Wokwi と MQTT | Wokwi から ACL・パスワード付きの Mosquitto につなぐ方法（ゲートウェイ）を資料で確かめる。つなげるなら、Wokwi のファームウェアで契約テストを流す。つなげなければ、MQTT の確認は QEMU（上）とホスト版に任せ、Wokwi はシリアルのログと表示で確かめる。公開のブローカーは使わない |
| Wokwi の CI | wokwi-cli とシナリオのファイル（`wokwi/scenarios/*.yaml`）で、CI のジョブにする。トークンは GitHub のシークレット `WOKWI_CLI_TOKEN`。シークレットがないときはジョブを「省略」として通し、失敗にしない（その旨をジョブのログに出す）。無料枠の制限を資料で確かめ、README に書く |
| スタックの見直し | QEMU と Wokwi で測った各タスクのスタックの残り（high water mark）が、どれも 25% 以上残るように #97 の値を見直す。測った値と見直した値を `docs/quality.md` に表で残す |
| NFR-03（24時間でヒープが減らない） | エミュレーターでは QEMU で30分動かし、起動から5分後と30分後でヒープの最低残量が下がっていないことを確かめる。24時間は実機フェーズ |

## P7 で作るもの

1. RAM の見直し（上の決定）。`docs/quality.md` に、見直す前と後の Node と主な容量の表
2. `tools/qemu.sh`（ESP-IDF の Docker イメージの中で `idf.py qemu` を動かし、シリアルのログを保存する）と、ログを判定する試験（起動・BOOT・stats・タスクウォッチドッグからの再起動）
3. （仮想の Ethernet が使えれば）`NODE_NET_QEMU_ETH` と、QEMU のファームウェアに対する契約テストの手順
4. `wokwi/`：`diagram.json`・`wokwi.toml`・`chips/`（要るものだけ）・`scenarios/`（起動と計測値、ボタンとモード、Wi-Fi とヒープ）
5. CI：QEMU のジョブ（起動・BOOT・タスクウォッチドッグ。ネットワークが使えれば契約テストも）、Wokwi のジョブ（シークレットがあるときだけ）
6. スタックの値の見直し、`docs/quality.md`・README の更新（エミュレーターでの確かめ方、確かめられないことの表）

## P7 で作らないもの

実機での確認、ライトスリープ、NVS への設定の保存、SHT31 の追加（P8）。

## 完了条件（すべて満たしてから PR）

1. `idf.py size` で静的な DIRAM が 70% 以下。見直しの前と後の値を PR に載せる
2. 作り直した電文が元とバイト単位で同じになる単体テストがあり、合格する
3. ホスト版の検査がすべて P6 のまま合格（host-debug・host-release・host-tsan の CTest、契約テスト（既定・fakehw）、broker_restart.py、`tools/quality.sh all`）。ESP-IDF のビルドも警告ゼロのまま
4. QEMU で、起動・4つのタスク・BOOT（reset_reason: power_on と、試験用の項目での watchdog）・stats のログを確かめる試験が、Mac と CI で合格。30分の試験（NFR-03）は Mac で1回流し、結果を PR に載せる
5. Wokwi のシナリオが Mac で合格（トークンは io が用意する）。Wi-Fi に接続した後のヒープの最低残量が 30KB 以上であることを PR に載せる
6. 各タスクのスタックの残りが 25% 以上（QEMU と Wokwi の測った値を PR に載せる）
7. 「QEMU と Wokwi で確かめられること・確かめられないこと」の表が `docs/quality.md` にあり、出典が書いてある
8. P1〜P6 の grep の検査が0件のまま。試験用の Kconfig の項目が既定で無効
9. open-questions に「P7 で決めたこと」の各行が入っている

PR は `gh pr create` で作る。説明には、完了条件ごとの結果、QEMU・Wokwi・wokwi-cli の版、確かめられなかったこととその理由、P8 への申し送りを書く。マージはしない。

## 止まって相談すること

- 持ち方を変えても DIRAM が 70% に届かないとき（振る舞いを変えるか、PSRAM に頼るかの判断が要る）
- 作り直した電文が元と同じにならない箇所があるとき
- QEMU・Wokwi の資料で、確かめたいことができるかどうか分からないとき
- Wokwi の Custom Chips API でデータシートのレジスタを再現するのに、データシートにない値が要るとき
- 値や順序が書かれていないことを決める必要が出たとき

いずれも `docs/open-questions.md` に記録し、その件に関わる作業を止めて報告する。iot-common-spec 側は変更しない。

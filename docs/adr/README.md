# ADR（設計の決定の記録）

作品02の設計で、あとから理由を確かめたくなる決定を1本ずつ記録します。形は「状況・決定・理由・影響・関係する open-questions」です。細かい決定は [docs/open-questions.md](../open-questions.md) にあり、ADR はその中から設計の形を決めたものを選んでいます。

| 番号 | 決定 | 状態 |
|---|---|---|
| [0001](0001-own-support-and-hal.md) | 自作ライブラリを取り込まず、自前の `support`・`hal` にした | 採用 |
| [0002](0002-paho-and-jsmn.md) | ホスト版の MQTT に Paho、cmd の解析に jsmn を使う | 採用 |
| [0003](0003-connect-after-sntp.md) | SNTP の同期の後に MQTT に接続する | 採用 |
| [0004](0004-rebuild-messages-from-values.md) | 電文を値で持ち、送るときに作り直す | 採用（P7） |
| [0005](0005-no-second-pump-stop.md) | ポンプの二重の止めを入れない | 採用 |
| [0006](0006-config-table-x-macro.md) | 構成表を X マクロの「1行が1台」の表にした | 採用（P8） |

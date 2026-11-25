# ADR 0004：電文を値で持ち、送るときに作り直す（P7）

## 状況

P6 のターゲット版で、静的な DIRAM が 88.8%（303,535／341,760 バイト）になり、ヒープに約 38KB しか残らなかった（#114）。最大は Node（124.5KB）で、その多くは電文の JSON の写しだった。

- 保持しているイベント32件の写し
- 確認待ちの電文の写し
- 重複排除の64件分の ack の payload の写し

## 決定

振る舞いは変えずに、持ち方だけを変える（#116）。

- イベントの保持と確認待ちは、電文を作り直すのに要る値を固定長の記録で持つ。値は kind・code・severity・channel・detail・発生時の単調時計、送った後は msg_id・seq・ts（`infra::EnvelopeFields`）。送るとき・送り直すときに JSON を作り直す
- 重複排除の64件は、ack を作り直すのに要る値（`domain::AckRecord`）で持つ
- 送り直し待ち（cmd_ack・state）の1件を 2KB から 1KB にした。最大の電文が収まることを単体テストで確かめる

## 理由

- 作り直した電文が元とバイト単位で同じなら、共通仕様の「同じ電文で送り直す」（#71）と「重複した cmd_id には前回と同じ payload」（#28）を、そのまま守れる
- PSRAM には頼らない。実機の型番がまだ決まっていないため（#115）

## 影響

- 結果：DIRAM は 67.7%（231,439 バイト）になり、Node は 52.4KB になった。Wi-Fi に接続した後のヒープの最低残量は 144,432 バイト（Wokwi）
- バイト単位で同じことを、単体テストで守っている
  - `EventServiceTest.ResentEventIsByteIdenticalEvenAfterTimeOffsetChanges`
  - `CommandServiceTest.RebuiltDuplicateAckIsByteIdenticalForEveryShape`
  - `ServicesTest.RetrySlotFitsWorstCaseStateAndCmdAck`
- 電文の形を変えるときは、作り直す側の値の持ち方も一緒に直す必要がある
- 送り直し待ちの1件は 1KB。機器を足すと state が長くなるので、確かめが要る。SHT31 を足した後の state は最大 855 バイト（P8a）

## 関係する open-questions

#28・#71・#114・#115・#116・#117

# Claude Code のプロンプト

作品02は、フェーズごとのプロンプトを Claude Code に渡して作りました。プロンプトはこのディレクトリにあり、受け取ったままの文面です。

- 各フェーズは main からブランチを切り、PR を作って io がマージしました
- プロンプトに書かれていないことや、食い違いが見つかったときは、`docs/open-questions.md` に記録しました。そのうえで作業を止めて相談し、決まったことを「決定」として残しました

| フェーズ | プロンプト | 目的 | PR | 主な結果 |
|---|---|---|---|---|
| P1 | [P1-prompt.md](P1-prompt.md) | ホスト版の骨組み、MQTT への接続、cmd の受け付け、CI での契約テスト | #1 | CTest 92件。契約テスト 9/9（Mac・CI）。共通仕様は v0.2.1 に固定（#12）。JSON は JsonWriter と jsmn（#9・ADR 0002） |
| P2 | [P2-prompt.md](P2-prompt.md) | 制御の中核：5段の調停、インターロック3種、3つのモード、手動操作、ローカルルール、センサーの故障 | #2 | CTest 133件。ControlArbiter は具体的なインターロックを知らない（#19・#20） |
| P3 | [P3-prompt.md](P3-prompt.md) | 機器のドライバ（データシートどおり）と I2C のフェイク、ボタンと OLED | #3 | CTest 180件。`--devices fakehw` でも契約テスト 9/9 |
| P4 | [P4-prompt.md](P4-prompt.md) | 通信の作り込み：SNTP の後の接続、イベントの保持、指数バックオフ、送信量の制限、ブローカーの停止・再開の試験 | #4 | CTest 206件。broker_restart.py。接続の順序（#64・ADR 0003）、同じ電文での送り直し（#71） |
| P5 | [P5-prompt.md](P5-prompt.md) | 品質ゲート：カバレッジ・clang-tidy・cppcheck・lizard・Doxygen・ThreadSanitizer・最適化ビルド | #5 | 振る舞いは変えずに検査を CI に入れた。domain・app の行カバレッジ 80% 以上（docs/quality.md） |
| P6 | [P6-prompt.md](P6-prompt.md) | ESP-IDF のターゲット版（ESP32-S3、ESP-IDF v5.5.6）：hal・infra の実装、4つのタスク、タスクウォッチドッグ | #6 | CTest 219件。ビルドの警告0件。DIRAM 88.8%（#114）。esp-mqtt の送り直しの挙動を確かめた |
| P7 | [P7-prompt.md](P7-prompt.md) | RAM の見直しと、エミュレーター（QEMU・Wokwi）での実行 | #7 | DIRAM 88.8% → 67.7%（電文を値で持つ。ADR 0004）。QEMU でファームウェアの契約テスト 9/9 と30分の試験。Wokwi の4本（Wi-Fi の後のヒープ 144,432 バイト、タスクウォッチドッグ） |
| P8 | [P8-prompt.md](P8-prompt.md) | 拡張性の実証（SHT31 の追加）と書類の仕上げ | #8・#9・P8b の PR | 構成表を「1行が1台」の表にした（#8。#145・ADR 0006）。SHT31 は既存のファイルへの「追加1行×7」で足した（#9）。CTest 236件。詳細設計書・テスト仕様書・ADR・この一覧 |

P8 は、PR を3つに分けました。

1. **PR #8**：構成表のリファクタリング。P8a に入る前に、P3〜P7 の構成表では NFR-01 を満たせないことが分かったため（#145）
2. **PR #9（P8a）**：SHT31 を追加
3. **P8b**：書類の仕上げ

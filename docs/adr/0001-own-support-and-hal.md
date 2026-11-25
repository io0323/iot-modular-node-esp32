# ADR 0001：自作ライブラリを取り込まず、自前の `support`・`hal` にした

## 状況

基本設計書（第1章「前提」・第5章）は、自作ライブラリ（EventBus・Error Framework・MQTT アダプタ）と自作 iot-hal を、タグを付けて取り込むことを前提にしていた。ところが取り込み元のリポジトリが決まらなかった（#5）。GitHub にあるのは iot-hal（GPIO まで）だけで、I2C・ADC・PWM・ミューテックス・タイマーがなく、Error Framework の Result と揃えるかどうかも決まっていなかった。P1 から作り始める必要があった。

## 決定

作品02では自作ライブラリを取り込まない（#3・#5。P8 で決定）。必要な分だけを自前で作り、取り込み元が決まったら置き換える前提の形にする。

- `src/support/`：`Result<T>`・`Error`（Error Framework の代わり）、`StaticVector`・`FixedString`（固定容量のコンテナ）
- `src/hal/`：`II2cBus`・`IAdc`・`IPwm`・`IGpio`・`IMutex`・`ITimer` と `I2cTransaction`（iot-hal の代わり。ハードのインターフェースだけ。#54）
- EventBus は使わない。app の中はメソッドの呼び出し、ターゲット版のタスクの間は FreeRTOS のキューとリングバッファでつなぐ（#96）
- MQTT アダプタの代わりに `infra::IMqttLink` を置き、ホスト版は Paho（ADR 0002）、ターゲット版は esp-mqtt で実装する

## 理由

- 取り込み元が決まらないまま待つと、P1〜P8 の作業が進まない
- 依存の向きは CLAUDE.md の表のまま（`hal → support`、`drivers → hal`）にできる。中身を最小にしておけば、後から置き換えるときに変える箇所が少ない
- EventBus を使わなくても「タスクの間に共有変数を作らない」（基本設計書 第6章）は、キューで守れる。app のロジックは control タスクだけで動く（#96）

## 影響

- 置き換えるときに変える箇所
  - `support::Result<T>` を Error Framework の Result にする（全層が使うので、型の別名を1か所に置いて切り替えるのがよい）
  - `hal` のインターフェースを iot-hal の型にする（drivers と platform の hal の実装が使う）
  - `infra::IMqttLink` の実装を MQTT アダプタにする（platform だけ）
- `support` と `hal` は自前で持つので、単体テスト（`tests/unit/support_test.cpp` ほか）とカバレッジの対象になる
- 基本設計書 第5章の主要クラスの表（`MqttLink`・`NvsConfig`・`WifiManager`）とは名前と形が違う（詳細設計書「基本設計書との差」）

## 関係する open-questions

#3・#5・#9・#54・#96

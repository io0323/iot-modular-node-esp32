# ADR 0002：ホスト版の MQTT に Paho、cmd の解析に jsmn を使う

## 状況

- ホスト版（Linux・macOS）で、ACL・パスワード付きの Mosquitto に対して契約テスト（iot-common-spec の `iotspec-contract`）を流す必要があった（P1）。MQTT アダプタ（ADR 0001）は取り込めなかった
- cmd の JSON を、ヒープを使わずに解析する方法が決まっていなかった（基本設計書 第7章の未決事項。#9）

## 決定

- ホスト版の `IMqttLink` は Eclipse Paho MQTT C（非同期 API の MQTTAsync）v1.3.16 で実装する（`src/platform/host/paho_mqtt_link.*`）。CMake の FetchContent で、タグを固定して取り込む
- 送信の JSON は自前の `infra::JsonWriter`（固定バッファ）で組み立てる。受信の cmd は jsmn（コミット 25647e69 に固定）でトークンに分け、`infra::CmdParser` が値を取り出す（#9）

## 理由

- Paho は QoS 1・retain・LWT・clean session を持ち、非同期の API なので、ターゲット版の esp-mqtt（イベントで知らせる）と同じ形の `IMqttLink` にできる。Paho を使えるのは `src/platform/host` だけ（CLAUDE.md）
- jsmn は、呼び出し側が渡した配列にトークンを書くだけで、ヒープを確保しない。ヘッダ1つで、ESP-IDF でもそのままビルドできる
- 送信は形が決まっているので、汎用の JSON ライブラリを使うより、固定バッファの書き手のほうが小さく、バッファの不足を `ok()` で確かめられる

## 影響

- Paho の中の data race を、ThreadSanitizer で2件抑止している（`tools/tsan.supp`。#89・#93）。自分のコードは公開 API を呼ぶだけで、避けられない
- jsmn は外部のコードなので、警告の設定を当てない（`SYSTEM` で include する）
- 数値の形（整数だけを受ける項目に `1.0` を許すか など）は CmdParser が自分で決める（`CmdParser.IntegerAcceptsZeroFractionOnly`）

## 関係する open-questions

#9・#14・#89・#93

# CLAUDE.md

作品02「モジュール型スマートデバイス制御システム」（C++20 × ESP32-S3、ESP-IDF v5系・FreeRTOS）のファームウェア。
IoT・組込みポートフォリオ4作品のうちの1つで、上位（作品03）とは MQTT で通信する。device_id と MQTT のユーザー名は `mcu-cpp-01`。
作品の主題は「機器の追加・交換に、新しいクラス＋構成表1行で対応でき、既存クラスを変更しない」こと。

## 正とする書類（上が優先）

1. `external/iot-common-spec`（submodule。固定したタグは README に書く）の `docs/spec.md`・`schemas/`・`profiles/mcu-cpp-01.yaml`・`docs/contract-tests.md`。Schema と本文が食い違えば Schema が正
2. `docs/open-questions.md` で「決定」になった項目
3. `docs/design/02-detailed-design.md`（作るまでは、いまのフェーズのプロンプト `docs/prompts/Pn-prompt.md`）
4. `docs/design/01-requirements-basic-design.md`（要件定義・基本設計。Claude Docs からの写し）

食い違いを見つけたら上を優先して進め、`docs/open-questions.md` に記録する。`docs/design/` は Claude Docs からの写しなので、指示がない限り編集しない。
`external/iot-common-spec/src/iotspec/sim/devices/mcu_cpp_01.py`（作品02の代役）は動きの参考にしてよいが、仕様ではない。代役と spec.md が違えば spec.md に従う。

## 守ること

- **仕様や書類にないことは決めない。** `docs/open-questions.md` に1行足して（形式は同ファイルの表）、その件に関わる作業を止め、PR の説明か作業報告で知らせる。共通仕様の問題は区分を「共通仕様」にし、作品02の中で吸収しない
- **iot-common-spec は変更しない。** submodule はタグに固定する。変更が必要なら open-questions に書く
- **実機は使わない。** 検証はホスト版・単体テスト・ホスト結合テスト・契約テスト・（P7 以降）QEMU と Wokwi で行う。実機でしか確かめられないことは「実機フェーズで確認」と書いて残す
- **フェーズの範囲を越えない。** いまのフェーズのプロンプトにないモジュールは作らない。必要なら仮版にとどめ、仮版であることをコメント（`// 仮版（Pn で本版）`）に書く
- **自作ライブラリ（EventBus・Error Framework・MQTT アダプタ）は、取り込み元が決まるまで取り込まない。** 代わりに `src/support/` の最小限の自前実装を使い、置き換える前提で作る（open-questions 参照）

## コードの決まり

- 層と依存の向き：`app → domain`、`app → infra`（インターフェース）、`drivers → domain・hal`、`infra → support`、`hal → support`（`src/hal/` はハードのインターフェースだけ。II2cBus・IAdc・IPwm・IGpio・IMutex）、`platform → hal・infra・drivers`（実装を提供する）、すべて `→ support`。逆向きのリンクを作らない。CMake のターゲットを層ごとに分ける
- `src/hal/` は自作 iot-hal の取り込み（open-questions #5）が決まったら置き換える前提で作る
- ESP-IDF・Paho・POSIX・`<thread>`・`<mutex>` のヘッダを include できるのは `src/platform/` の下だけ。`src/drivers/`（`src/drivers/fake/` を除く）は `src/platform/` のヘッダを include しない
- C++20。製品コードは `-fno-exceptions -fno-rtti`。警告は `-Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference`。外部ライブラリには警告設定を当てない
- app・domain・drivers・infra は初期化完了後にヒープを確保しない。`new`・`malloc`・`std::string`・`std::vector`・`std::map`・`std::function`・`std::shared_ptr` を使わない。`std::array`、`std::span`、`std::string_view`、`src/support/` の固定容量コンテナを使う
- エラーは例外ではなく `support::Result<T>` で返す。戻り値を無視しない（`[[nodiscard]]`）
- 時刻は `IClock` から取る。経過時間は単調時計（ms）、電文の `ts` は UTC。直接 `std::chrono::system_clock` を呼ぶのは platform だけ
- JSON は固定バッファで組み立て・解析する（送信は自前の JsonWriter、受信は jsmn）
- 公開 API には Doxygen コメント（`@brief`・`@param`・`@return`）。循環的複雑度は 10 以下を目安にする

## コマンド

この Mac では Homebrew が使えない。依存は CMake の FetchContent で取り込み、Mosquitto は Docker Desktop で動かす。

```sh
git submodule update --init --recursive
cmake --preset host-debug && cmake --build --preset host-debug
ctest --preset host-debug --output-on-failure

# 契約テスト（Mosquitto は ACL・パスワード付き。passwd は submodule の中にできるがコミットしない）
external/iot-common-spec/deploy/mosquitto/gen_passwd.sh
docker compose -f external/iot-common-spec/docker-compose.yml up -d mosquitto
uv tool install "iotspec @ git+https://github.com/io0323/iot-common-spec@<固定したタグ>"
iotspec-contract --role mcu-cpp-01 --target-cmd "./build/host/mcu_cpp_01 --log contract-log.jsonl" --junit contract.xml
```

プリセット名・出力先・実行ファイル名（`build/host/mcu_cpp_01`）は契約テストが前提にしているので変えない。
ブローカーは `--broker host:port`、なければ環境変数 `IOTSPEC_MQTT_URL`（既定 `mqtt://localhost:1883`）。パスワードは `IOTSPEC_MQTT_PASSWORD`（既定 `iotspec-dev`）。

## ブランチと PR

- フェーズごとに main から `phase/pN-<短い名前>` を切る。作業は小さなコミットに分ける
- 完了条件をすべて満たしたら `gh pr create` で PR を作る。説明には、完了条件ごとの結果（実行したコマンドと出力の要点）、open-questions に足した項目、次のフェーズへの申し送りを書く
- マージはしない（io が行う）。次のフェーズを始める前に、前のフェーズのブランチがマージ済みであることを確かめ、ローカルとリモートのブランチを削除する
- 秘密情報（トークン、パスワード、`passwd` ファイル）をコミットしない

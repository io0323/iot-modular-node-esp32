# CLAUDE.md

作品02「モジュール型スマートデバイス制御システム」（C++20 × ESP32-S3、ESP-IDF v5.5.6・FreeRTOS）のファームウェアです。IoT・組込みポートフォリオ4作品のうちの1つで、上位（作品03）とは MQTT で通信します。device_id と MQTT のユーザー名は `mcu-cpp-01`。

作品の主題は「機器の追加・交換に、新しいクラス＋構成表1行で対応でき、既存クラスを変更しない」ことです（NFR-01。手順は詳細設計書 7 章）。

P1〜P8（開発段階。実機を使わない）は終わりました。この後は実機フェーズです（下の「実機フェーズで守ること」）。

## 正とする書類（上が優先）

1. `external/iot-common-spec`（submodule。v0.2.1 に固定）の `docs/spec.md`・`schemas/`・`profiles/mcu-cpp-01.yaml`・`docs/contract-tests.md`。Schema と本文が食い違えば Schema が正
2. `docs/open-questions.md` で「決定」になった項目
3. `docs/design/02-detailed-design.md`（詳細設計書。コードから書いたもの）と `docs/adr/`
4. `docs/design/01-requirements-basic-design.md`（要件定義・基本設計。Claude Docs からの写し）

食い違いを見つけたら、上を優先して進め、`docs/open-questions.md` に記録します。基本設計書との差は、詳細設計書の「基本設計書との差」にまとめています。

- `docs/design/01-requirements-basic-design.md` は写しなので、指示がない限り編集しない
- 詳細設計書・テスト仕様書（`docs/test-spec.md`）は、コードを変えたら一緒に直す

## 守ること

- **仕様や書類にないことは決めない。** `docs/open-questions.md` に1行足し（形式は同ファイルの表）、その件に関わる作業を止めて、PR の説明か作業報告で知らせる
  - 共通仕様の問題は区分を「共通仕様」にし、作品02の中で吸収しない
  - 状態は「未決」「仮実装」「決定」「申し送り」のどれか。P8 の完了時点で「未決」「仮実装」は0件
- **iot-common-spec は変更しない。** submodule はタグに固定する。上げる手順は README「対応する共通仕様」
- **自作ライブラリ（EventBus・Error Framework・MQTT アダプタ・iot-hal）は取り込まない**（#5・ADR 0001）。`src/support/` と `src/hal/` の自前の実装を使う。取り込むことになったら、ADR 0001 の「影響」の置き換え方に従う
- **機器の追加は構成表で行う**（ADR 0006）。次を足す。既存のクラスを変えない。変える必要が出たら、止めて相談する
  - `src/config/hw_table.hpp`（と `fakehw_table.hpp`）に1行と、そのクラスの include に1行
  - ビルド定義に1行ずつ

## コードの決まり

- 層と依存の向き。逆向きのリンクを作らない。CMake のターゲットは層ごとに分ける
  - `app → domain・infra`（インターフェース）
  - `drivers → domain・hal`
  - `config → drivers・domain・hal`
  - `infra → domain・support`（domain は `domain/types.hpp` の値の型だけ。`CmdParser` が cmd の priority に使う。#148）
  - `hal → support`（`src/hal/` はハードのインターフェースだけ：`II2cBus`・`IAdc`・`IPwm`・`IGpio`・`IMutex`・`ITimer`・`I2cTransaction`）
  - `domain → support`
  - `platform → app・config・drivers・infra・hal`（実装を提供する）
  - すべて `→ support`
- ESP-IDF・FreeRTOS・Paho・POSIX・`<thread>`・`<mutex>` のヘッダを include できるのは、`src/platform/` の下だけ。`src/drivers/`（`src/drivers/fake/` を除く）は `src/platform/` のヘッダを include しない。domain・app・infra・drivers・config に `#ifdef ESP_PLATFORM` を書かない
- ターゲット版の Node と app のクラスは、control タスクからだけ呼ぶ。ほかのタスクとは FreeRTOS のキューとリングバッファで受け渡し、共有変数を作らない（#96）
- C++20。製品コードは `-fno-exceptions -fno-rtti`。警告は `-Wall -Wextra -Werror -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference`。外部ライブラリと ESP-IDF 本体には、警告の設定を当てない
- app・domain・drivers・infra は、初期化の後にヒープを確保しない。`new`・`malloc`・`std::string`・`std::vector`・`std::map`・`std::function`・`std::shared_ptr` を使わない。`std::array`、`std::span`、`std::string_view`、`src/support/` の固定容量のコンテナを使う
- エラーは例外ではなく `support::Result<T>` で返す。戻り値を無視しない（`[[nodiscard]]`）
- 時刻は `IClock` から取る。経過時間は単調時計（ms）、電文の `ts` は UTC。`std::chrono::system_clock` や `esp_timer` を直接呼ぶのは platform だけ
- JSON は固定バッファで組み立て・解析する（送信は自前の JsonWriter、受信は jsmn）。電文の形を変えるときは、作り直す側の値の持ち方も直す（ADR 0004）
- 公開 API には Doxygen のコメント（`@brief`・`@param`・`@return`）。循環的複雑度は 10 以下
- ドライバは、データシートの文書名・版・節・表をコメントに書く。値は推測で埋めない
- 構成表のマクロ（`NODE_SENSOR` など）は `src/config` の中だけで使う

## コマンド

この Mac では Homebrew が使えません。依存は CMake の FetchContent で取り込み、Mosquitto・ESP-IDF・品質の道具は Docker Desktop で動かします。

```sh
# ホスト版
git submodule update --init --recursive
cmake --preset host-debug && cmake --build --preset host-debug     # host-release・host-tsan も同じ形
ctest --preset host-debug --output-on-failure

# 契約テスト（Mosquitto は ACL・パスワード付き。passwd は submodule の中にできるがコミットしない）
external/iot-common-spec/deploy/mosquitto/gen_passwd.sh
docker compose -f external/iot-common-spec/docker-compose.yml up -d mosquitto
uv tool install "iotspec @ git+https://github.com/io0323/iot-common-spec@v0.2.1"
iotspec-contract --role mcu-cpp-01 --target-cmd "./build/host/mcu_cpp_01 --log contract-log.jsonl" --junit contract.xml
iotspec-contract --role mcu-cpp-01 --target-cmd "./build/host/mcu_cpp_01 --devices fakehw --log contract-fakehw-log.jsonl" --junit contract-fakehw.xml
uv run tests/system/broker_restart.py --log broker-restart-log.jsonl

# 品質の検査（Docker のイメージの中。docs/quality.md）
tools/quality.sh all            # coverage・tidy・cppcheck・lizard・doxygen を個別にも動かせる

# ターゲット版（Docker の espressif/idf:v5.5.6。秘密は firmware/sdkconfig.defaults.local に書き、コミットしない）
docker run --rm -v "$PWD":/project -w /project/firmware espressif/idf:v5.5.6 \
  bash -c '. $IDF_PATH/export.sh >/dev/null && idf.py -B /project/build/firmware build size'

# エミュレーター（docs/quality.md「エミュレーター（P7）」）
tools/qemu.sh test              # 起動・4つのタスク・BOOT・stats
tools/qemu.sh contract          # QEMU のファームウェアに契約テスト（Mosquitto を先に起動）
tools/qemu.sh soak 30           # 30分の NFR-03
tools/wokwi.sh                  # Wokwi の4本（トークンは WOKWI_CLI_TOKEN か ~/.config/wokwi/token）
```

- プリセット名・出力先・実行ファイル名（`build/host/mcu_cpp_01`）は、契約テストが前提にしているので変えない
- ブローカーは `--broker host:port`、なければ環境変数 `IOTSPEC_MQTT_URL`（既定 `mqtt://localhost:1883`）。パスワードは `IOTSPEC_MQTT_PASSWORD`（既定 `iotspec-dev`）
- 試験用の Kconfig の項目（`NODE_TEST_*`・`NODE_NET_QEMU_ETH`・`NODE_WIFI_ALLOW_OPEN`）は既定で無効のまま。製品の構成（`firmware/sdkconfig.defaults`）に入れない

## 実機フェーズで守ること

- 実機で確かめる項目は、README「実機フェーズで確かめること」とテスト仕様書の「実機フェーズ」の行にある。確かめたら、テスト仕様書の結果を直す
- 実機で見つかったドライバの差は、まずフェイク（`src/drivers/fake/`）とその単体テストに再現してから、ドライバを直す。データシートと実機が違えば open-questions に記録する
- 仮の値を実機に合わせて決めたら、open-questions に「決定」として残す。対象は、ピンの割り当て（#105）、土壌水分の校正（#43）、OLED の向き（#56・#134）、I2C の設定（#113）、スタックの大きさ（#97・#137）
- 開発段階の検査（CTest・契約テスト・broker_restart・`quality.sh all`・ESP-IDF のビルド・QEMU・Wokwi）は、実機フェーズでも通り続けること
- 秘密（Wi-Fi・MQTT のパスワード、Wokwi のトークン、`passwd`）をコミットしない。`firmware/sdkconfig.defaults.local` は `.gitignore` に入っている

## ブランチと PR

- 作業ごとに main からブランチを切る（フェーズは `phase/pN-<短い名前>`）。作業は小さなコミットに分ける
- 完了条件をすべて満たしたら、`gh pr create` で PR を作る。説明には次を書く
  - 完了条件ごとの結果（実行したコマンドと出力の要点）
  - open-questions に足した項目
  - 次への申し送り
- マージはしない（io が行う）。次の作業を始める前に、前のブランチがマージ済みであることを確かめる
- 秘密情報をコミットしない

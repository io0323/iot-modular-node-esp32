// 構成表の fakehw 用の行（1行が1台。ホスト版の --devices fakehw と QEMU の試験用の構成。NFR-01、open-questions #145）。
//
// FakeHwRig（config/fakehw.hpp・fakehw.cpp）が NODE_FAKE_I2C・NODE_FAKE_I2C_ARGS を定義してからこのファイルを読み、
// メンバー・アクセサー・バスへの登録を展開する（X マクロ）。そのため #pragma once を付けず、行の外には何も書かない。
//
//   NODE_FAKE_I2C(フェイクの機器の型, 名前, I2C のアドレス)                      既定のコンストラクタで作る
//   NODE_FAKE_I2C_ARGS(フェイクの機器の型, 名前, (コンストラクタの引数), I2C のアドレス)
//     引数の中では、FakeHwRig のコンストラクタの引数 `timer`（hal::ITimer）が使える
//   どちらも FakeHwRig に「名前()」のアクセサーができる（テスト・デバッグ入力から値や故障を入れる）
//
// 機器を足すときは、ここに1行と、フェイクの機器のヘッダの include を config/fakehw.hpp に1行足す。
// 既定の値（25℃前後・300 lx など）は、各フェイクの機器のコンストラクタか FakeHwRig のコンストラクタで入れる。

NODE_FAKE_I2C(drivers::fake::FakeBme280Device, bme280_device, kAddrBme280)
NODE_FAKE_I2C_ARGS(drivers::fake::FakeBh1750Device, bh1750_device, (timer), kAddrBh1750)
NODE_FAKE_I2C(drivers::fake::FakeSsd1306Device, ssd1306_device, kAddrSsd1306)

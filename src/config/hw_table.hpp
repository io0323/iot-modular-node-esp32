// 構成表の実機用の行（1行が1台。NFR-01、基本設計書 第5章「機器を追加する手順」、open-questions #145）。
//
// HwRig（config/hw.hpp・hw.cpp）が NODE_SENSOR・NODE_ACTUATOR を定義してからこのファイルを読み、メンバー・初期化・
// 一覧を展開する（X マクロ）。そのため #pragma once を付けず、行の外には何も書かない。行の中では、コンストラクタの
// 引数 `hw`（HwInterfaces）と、HwRig の i2c(hw, アドレス) が使える。
//
//   NODE_SENSOR(ドライバの型, 名前, (ドライバの前半の引数), 周期[ms], (チャネル {ID, 単位, 周期[ms]}, ...))
//     ドライバは「前半の引数, チャネルの span, 周期」で作る（ISensor）
//   NODE_ACTUATOR(ドライバの型, 名前, (ドライバの前半の引数), (仕様 {ID, 下限, 上限, 連続の上限[s]}), インターロックの一覧)
//     ドライバは「前半の引数, ActuatorSpec」で作る（IActuator）。インターロックは HwRig のメンバーの一覧か {}
//
// 機器を足すときは、ここに1行と、ドライバのヘッダの include を config/hw.hpp に1行足す。

// ---- センサー（telemetry・state の channels[] はこの順）
NODE_SENSOR(drivers::Bme280, bme280, (i2c(hw, kAddrBme280), hw.timer), 2000, ({"temp_air", domain::Unit::DegC, 2000}, {"humidity", domain::Unit::PctRh, 2000}, {"pressure", domain::Unit::HPa, 2000}))
NODE_SENSOR(drivers::Bh1750, bh1750, (i2c(hw, kAddrBh1750), hw.timer), 1000, ({"lux", domain::Unit::Lux, 1000}))
NODE_SENSOR(drivers::SoilMoisture, soil, (hw.adc, kAdcSoil, kSoilCalibration), 10000, ({"soil", domain::Unit::Pct, 10000}))
NODE_SENSOR(drivers::Sht31, sht31, (i2c(hw, drivers::Sht31::kAddrA), hw.timer), 2000, ({"temp_aux", domain::Unit::DegC, 2000}, {"humidity_aux", domain::Unit::PctRh, 2000}))

// ---- アクチュエーター（telemetry の actuators[] はこの順）
NODE_ACTUATOR(drivers::PwmActuator, fan_main, (hw.pwm, kPwmFanConfig), ("fan_main", 0, 1000, {}), fan_interlocks_)
NODE_ACTUATOR(drivers::PwmActuator, led_grow, (hw.pwm, kPwmLedConfig), ("led_grow", 0, 1000, {}), {})
NODE_ACTUATOR(drivers::RelayActuator, pump, (hw.gpio, kGpioPumpRelay, kPumpRelayActiveHigh), ("pump", 0, 1000, std::uint16_t{kPumpMaxOnS}), pump_interlocks_)
NODE_ACTUATOR(drivers::PwmActuator, buzzer, (hw.pwm, kPwmBuzzerConfig), ("buzzer", 0, 1000, {}), {})

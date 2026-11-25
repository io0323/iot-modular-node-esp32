// データシートのレジスタ応答を再現するフェイクの I2C 機器（BME280・BH1750FVI・SSD1306）。
// 出典は各ドライバのヘッダ（drivers/bme280.hpp・bh1750.hpp・ssd1306.hpp）と同じ。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "drivers/bme280.hpp"
#include "drivers/fake/fake_hal.hpp"
#include "support/fixed_string.hpp"
#include "support/static_vector.hpp"

namespace node::drivers::fake {

/// @brief BME280 のフェイク。チップ ID、補正係数、制御・状態・データのレジスタを持つ。
///
/// 補正係数の既定は BMP280 データシート（BST-BMP280-DS001-26）3.12 節の計算例の値（dig_T1〜dig_P9）と、
/// 湿度は実在しそうな値（dig_H1〜dig_H6。データシートに例がないのでテストの値）。生値の既定も同じ計算例の値。
class FakeBme280Device final : public FakeI2cDevice {
 public:
  /// @brief レジスタへの書き込みの記録。
  struct Write {
    std::uint8_t reg{0};    ///< レジスタのアドレス
    std::uint8_t value{0};  ///< 書いた値
  };

  FakeBme280Device();

  void on_write(std::span<const std::uint8_t> data) override;
  void on_read(std::span<std::uint8_t> out) override;

  /// @brief 補正係数のレジスタ（0x88〜0xA1・0xE1〜0xE7）の中身を決める。
  /// @param k 補正係数
  void set_calibration(const Bme280Calibration& k);
  /// @brief 計測で返す生値を決める。
  /// @param raw 生値
  void set_raw(const Bme280Raw& raw) { raw_ = raw; }
  /// @brief 気温が temp_c になる adc_T を、データシート 8.1 節の倍精度の式で求めて設定する（いまの補正係数で）。
  /// @param temp_c 気温（DegC）
  void set_temperature_c(double temp_c);
  /// @brief チップ ID のレジスタの値を決める（違う機器のふりをする）。
  /// @param id チップ ID
  void set_chip_id(std::uint8_t id) { regs_[Bme280::kRegId] = id; }
  /// @brief 強制モードの計測の後、status の measuring が何回読まれるまで 1 のままか。
  /// @param n 回数
  void set_measuring_polls(std::uint32_t n) { measuring_polls_ = n; }

  /// @brief いまの補正係数。
  /// @return 補正係数
  [[nodiscard]] const Bme280Calibration& calibration() const { return cal_; }
  /// @brief いまの生値。
  /// @return 生値
  [[nodiscard]] const Bme280Raw& raw() const { return raw_; }
  /// @brief レジスタへの書き込みの記録。
  /// @return 書いた順の並び
  [[nodiscard]] std::span<const Write> writes() const { return {writes_.begin(), writes_.size()}; }
  /// @brief 強制モードの計測の回数。
  /// @return 回数
  [[nodiscard]] std::uint32_t measurements() const { return measurements_; }
  /// @brief measuring が 1 の間に status が読まれた回数。
  /// @return 回数
  [[nodiscard]] std::uint32_t status_reads_while_measuring() const { return status_reads_busy_; }

  /// @brief データシート 8.1 節 BME280_compensate_T_double の倍精度の式。
  /// @param k 補正係数
  /// @param adc_T 温度の生値
  /// @param t_fine 気圧・湿度の式に渡す t_fine の書き込み先（要らなければ nullptr）
  /// @return 気温（DegC）
  static double temperature_double(const Bme280Calibration& k, std::int32_t adc_T, std::int32_t* t_fine = nullptr);
  /// @brief データシート 8.1 節 BME280_compensate_P_double の倍精度の式。
  /// @param k 補正係数
  /// @param adc_P 気圧の生値
  /// @param t_fine temperature_double で求めた t_fine
  /// @return 気圧（Pa）
  static double pressure_double(const Bme280Calibration& k, std::int32_t adc_P, std::int32_t t_fine);
  /// @brief データシート 8.1 節 bme280_compensate_H_double の倍精度の式。
  /// @param k 補正係数
  /// @param adc_H 湿度の生値
  /// @param t_fine temperature_double で求めた t_fine
  /// @return 湿度（%RH）
  static double humidity_double(const Bme280Calibration& k, std::int32_t adc_H, std::int32_t t_fine);

 private:
  void complete_measurement();
  void reset();

  std::array<std::uint8_t, 256> regs_{};
  std::uint8_t pointer_{0};
  Bme280Calibration cal_{};
  Bme280Raw raw_{};
  std::uint32_t measuring_polls_{1};
  std::uint32_t measuring_left_{0};
  std::uint32_t measurements_{0};
  std::uint32_t status_reads_busy_{0};
  support::StaticVector<Write, 128> writes_{};
};

/// @brief BH1750FVI のフェイク。命令を受け、連続高分解能モードの結果を返す。
///
/// 命令から 120 ms（tHR の typ。2 ページ）たつまでの読み出しは 0 を返し、early_reads に数える。
class FakeBh1750Device final : public FakeI2cDevice {
 public:
  /// @brief フェイクを作る。
  /// @param timer 計測時間の経過を見る時計
  explicit FakeBh1750Device(const hal::ITimer& timer) : timer_(timer) {}

  void on_write(std::span<const std::uint8_t> data) override;
  void on_read(std::span<std::uint8_t> out) override;

  /// @brief 計測結果（16 ビットのカウント）を決める。
  /// @param count カウント
  void set_count(std::uint16_t count) { count_ = count; }
  /// @brief 照度（lx）から計測結果を決める（count = lx × 1.2）。
  /// @param lux 照度（lx）
  void set_lux(double lux);
  /// @brief Power On の命令を受けたか。
  /// @return 受けていれば true
  [[nodiscard]] bool powered() const { return powered_; }
  /// @brief 連続高分解能モードか。
  /// @return そうなら true
  [[nodiscard]] bool continuous_h() const { return continuous_h_; }
  /// @brief 計測時間がたつ前に読まれた回数。
  /// @return 回数
  [[nodiscard]] std::uint32_t early_reads() const { return early_reads_; }
  /// @brief 受けた命令。
  /// @return 受けた順の並び
  [[nodiscard]] std::span<const std::uint8_t> commands() const { return {commands_.begin(), commands_.size()}; }

 private:
  static constexpr std::uint64_t kTypicalMeasureMs = 120;
  const hal::ITimer& timer_;
  std::uint16_t count_{360};  // 300 lx
  bool powered_{false};
  bool continuous_h_{false};
  std::uint64_t mode_set_ms_{0};
  std::uint32_t early_reads_{0};
  support::StaticVector<std::uint8_t, 64> commands_{};
};

/// @brief SSD1306 のフェイク。受けたコマンドを記録し、GDDRAM（ページアドレッシング）を再現する。
class FakeSsd1306Device final : public FakeI2cDevice {
 public:
  void on_write(std::span<const std::uint8_t> data) override;
  void on_read(std::span<std::uint8_t> out) override;

  /// @brief 表示がオンか（0xAF を受けた後で 0xAE を受けていない）。
  /// @return オンなら true
  [[nodiscard]] bool display_on() const { return display_on_; }
  /// @brief 受けたコマンド（引数のバイトも含む）。
  /// @return 受けた順の並び
  [[nodiscard]] std::span<const std::uint8_t> commands() const { return {commands_.begin(), commands_.size()}; }
  /// @brief GDDRAM の中身（8 ページ × 128 列）。
  /// @return 中身
  [[nodiscard]] const std::array<std::uint8_t, 1024>& gddram() const { return gddram_; }
  /// @brief GDDRAM のページ（行）を 5×7 のフォントで読み戻した文字列（6 ドット幅のセル）。
  /// @param page ページ（0〜7）
  /// @return 読み戻した文字列（読めない形は '?'）
  [[nodiscard]] support::FixedString<21> row_text(std::size_t page) const;
  /// @brief 受けたコマンドの記録を消す。
  void clear_commands() { commands_.clear(); }

 private:
  void command_byte(std::uint8_t b);

  bool display_on_{false};
  std::uint8_t page_{0};
  std::uint8_t column_{0};
  std::uint8_t pending_params_{0};
  std::array<std::uint8_t, 1024> gddram_{};
  support::StaticVector<std::uint8_t, 512> commands_{};
};

}  // namespace node::drivers::fake

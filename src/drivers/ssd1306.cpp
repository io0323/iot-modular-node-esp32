// 出典：Solomon Systech「SSD1306」Rev 1.1（Apr 2008）、「SSD1306 Application Note」Rev 0.4（Jan 2009）。
#include "drivers/ssd1306.hpp"

#include "drivers/font5x7.hpp"
#include "support/static_vector.hpp"

namespace node::drivers {

support::Result<void> Ssd1306::commands(std::span<const std::uint8_t> cmds) const {
  // 制御バイト 00h（Co = 0、D/C# = 0）の後にコマンドを続けて送る（8.1.5 節 5)）
  support::StaticVector<std::uint8_t, 32> tx{};
  if (!tx.push_back(kControlCommands)) {
    return support::make_error(support::ErrorCode::BufferFull);
  }
  for (std::uint8_t const c : cmds) {
    if (!tx.push_back(c)) {
      return support::make_error(support::ErrorCode::BufferFull, "too many commands");
    }
  }
  hal::I2cTransaction t(*dev_.bus, *dev_.mutex);
  return t.write(dev_.addr, std::span<const std::uint8_t>(tx.begin(), tx.size()));
}

support::Result<void> Ssd1306::init() {
  if (auto r = commands(kInitSequence); !r) {
    return r;
  }
  fb_.fill(0);
  return flush();
}

support::Result<void> Ssd1306::set_power(bool on) {
  const std::array<std::uint8_t, 1> cmd{on ? kDisplayOn : kDisplayOff};
  return commands(cmd);
}

void Ssd1306::render(std::span<const std::string_view> lines) {
  fb_.fill(0);
  for (std::size_t row = 0; row < lines.size() && row < kPages; ++row) {
    const std::string_view line = lines[row];
    for (std::size_t col = 0; col < line.size() && col < domain::IDisplay::kColumns; ++col) {
      const auto& g = glyph_of(line[col]);
      for (std::size_t x = 0; x < kFontWidth; ++x) {
        fb_[row * kWidth + col * kCellWidth + x] = g[x];
      }
    }
  }
}

support::Result<void> Ssd1306::flush() {
  // ページアドレッシング（リセット時の既定。10.1.3 節）で、ページごとに先頭の列を決めてから 128 バイトを書く
  for (std::size_t page = 0; page < kPages; ++page) {
    const std::array<std::uint8_t, 3> addr{static_cast<std::uint8_t>(kPageStart + page), kLowerColumn, kHigherColumn};
    if (auto r = commands(addr); !r) {
      return r;
    }
    std::array<std::uint8_t, kWidth + 1> tx{};
    tx[0] = kControlData;
    for (std::size_t x = 0; x < kWidth; ++x) {
      tx[x + 1] = fb_[page * kWidth + x];
    }
    hal::I2cTransaction t(*dev_.bus, *dev_.mutex);
    if (auto r = t.write(dev_.addr, tx); !r) {
      return r;
    }
  }
  return support::ok();
}

support::Result<void> Ssd1306::show(std::span<const std::string_view> lines) {
  render(lines);
  return flush();
}

}  // namespace node::drivers

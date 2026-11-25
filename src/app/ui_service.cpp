#include "app/ui_service.hpp"

namespace node::app {
namespace {

using Line = UiService::Line;

void put_uint(Line& l, std::uint64_t v) {
  std::array<char, 20> tmp{};
  std::size_t n = 0;
  // 0 でも1桁は書く（最初の1回は必ず回る）
  for (bool more = true; more; more = v != 0U) {
    tmp[n++] = static_cast<char>('0' + static_cast<int>(v % 10U));
    v /= 10U;
  }
  while (n > 0) {
    (void)l.push_back(tmp[--n]);
  }
}

/// 1/1000 単位の値を小数1桁で書く（四捨五入）
void put_milli_1(Line& l, std::int32_t milli) {
  std::int64_t v = milli;
  if (v < 0) {
    (void)l.push_back('-');
    v = -v;
  }
  const std::uint64_t tenths = (static_cast<std::uint64_t>(v) + 50U) / 100U;
  put_uint(l, tenths / 10U);
  (void)l.push_back('.');
  (void)l.push_back(static_cast<char>('0' + static_cast<int>(tenths % 10U)));
}

/// 1/1000 単位の値を整数で書く（四捨五入）
void put_milli_0(Line& l, std::int32_t milli) {
  std::int64_t v = milli;
  if (v < 0) {
    (void)l.push_back('-');
    v = -v;
  }
  put_uint(l, (static_cast<std::uint64_t>(v) + 500U) / 1000U);
}

/// 「T   25.0C」の形の行。fault の間は値の代わりに "--"（P3 の決定 #50、#60）
Line reading_line(const domain::ReadingStore& store, std::uint64_t now_ms, std::string_view label,
                  std::string_view channel, std::string_view unit, bool decimals) {
  Line l;
  (void)l.append(label);
  (void)l.append(" ");
  const auto r = store.latest(channel);
  Line value;
  if (!r || r->quality == domain::Quality::Fault) {
    (void)value.append("--");
  } else if (decimals) {
    put_milli_1(value, r->value_milli);
  } else {
    put_milli_0(value, r->value_milli);
  }
  (void)now_ms;
  // 値を右に寄せる（ラベルの後ろ8桁）
  for (std::size_t i = value.size(); i < 8; ++i) {
    (void)l.push_back(' ');
  }
  (void)l.append(value.view());
  (void)l.append(unit);
  return l;
}

constexpr std::string_view mode_label(domain::Mode m) {
  switch (m) {
    case domain::Mode::Auto: return "AUTO";
    case domain::Mode::Manual: return "MANUAL";
    case domain::Mode::PowerSave: return "SAVE";
  }
  return "";
}

}  // namespace

support::Result<void> UiService::init() const {
  if (dev_.mode_button != nullptr) {
    if (auto r = dev_.mode_button->init(); !r) {
      return r;
    }
  }
  if (dev_.action_button != nullptr) {
    if (auto r = dev_.action_button->init(); !r) {
      return r;
    }
  }
  if (dev_.display != nullptr) {
    if (auto r = dev_.display->init(); !r) {
      return r;
    }
  }
  return support::ok();
}

void UiService::handle_action(domain::ButtonEvent ev, std::uint64_t now_ms) {
  // 手動モード以外の ACTION は無視する（ManualControl が NotManualMode を返す）
  if (ev == domain::ButtonEvent::Short) {
    (void)manual_.select_next();
  }
  if (ev == domain::ButtonEvent::Long) {
    (void)manual_.toggle_selected(now_ms);
  }
}

std::array<Line, domain::IDisplay::kRows> UiService::compose(std::uint64_t now_ms, bool connected) const {
  std::array<Line, domain::IDisplay::kRows> lines{};
  (void)lines[0].append(mode_label(modes_.mode()));
  for (std::size_t i = lines[0].size(); i < 7; ++i) {
    (void)lines[0].push_back(' ');
  }
  (void)lines[0].append(connected ? "MQTT:ON" : "MQTT:OFF");
  lines[1] = reading_line(store_, now_ms, "T", "temp_air", "C", true);
  lines[2] = reading_line(store_, now_ms, "H", "humidity", "%", true);
  lines[3] = reading_line(store_, now_ms, "L", "lux", "lx", false);
  lines[4] = reading_line(store_, now_ms, "S", "soil", "%", true);
  lines[5] = reading_line(store_, now_ms, "P", "pressure", "hPa", true);
  // 7行目：アクチュエーターの頭文字と level（例：F1000 L0 P0 B0）
  for (std::size_t i = 0; i < arbiter_.size(); ++i) {
    if (i > 0) {
      (void)lines[6].push_back(' ');
    }
    const std::string_view id = arbiter_.spec(i).id;
    (void)lines[6].push_back(id.empty() ? '?' : static_cast<char>(id[0] - ('a' - 'A')));
    put_uint(lines[6], arbiter_.output(i));
  }
  // 8行目：手動モードのときだけ、選んでいる機器
  if (modes_.mode() == domain::Mode::Manual) {
    (void)lines[7].append("> ");
    (void)lines[7].append(manual_.selected_id());
  }
  return lines;
}

void UiService::refresh_display(std::uint64_t now_ms, bool connected) {
  if (dev_.display == nullptr) {
    return;
  }
  const bool want_on = modes_.mode() != domain::Mode::PowerSave;
  if (want_on != display_on_) {
    if (!dev_.display->set_power(want_on)) {
      ++display_errors_;  // 省電力モードでは表示を消す
    }
    display_on_ = want_on;
  }
  if (!display_on_) {
    return;
  }
  const auto lines = compose(now_ms, connected);
  std::array<std::string_view, domain::IDisplay::kRows> views{};
  for (std::size_t i = 0; i < lines.size(); ++i) {
    views[i] = lines[i].view();
  }
  if (!dev_.display->show(views)) {
    ++display_errors_;
  }
}

UiService::Actions UiService::tick(std::uint64_t now_ms, bool connected) {
  Actions a{};
  if (now_ms >= next_button_ms_) {
    next_button_ms_ = now_ms + kButtonPeriodMs;
    if (dev_.mode_button != nullptr && dev_.mode_button->poll(now_ms) == domain::ButtonEvent::Short) {
      a.mode_pressed = true;
    }
    if (dev_.action_button != nullptr) {
      handle_action(dev_.action_button->poll(now_ms), now_ms);
    }
  }
  // 省電力モードに入ったらすぐに消す。表示の中身は1秒ごと
  const bool power_change = (modes_.mode() != domain::Mode::PowerSave) != display_on_;
  if (now_ms >= next_display_ms_ || power_change) {
    if (now_ms >= next_display_ms_) {
      next_display_ms_ = now_ms + kDisplayPeriodMs;
    }
    refresh_display(now_ms, connected);
  }
  return a;
}

}  // namespace node::app

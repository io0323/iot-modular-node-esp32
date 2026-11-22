#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace node::domain {

/// @brief アクチュエーターの出力（0–1000‰の整数。共通仕様 第2章 level_pm）。
using Level = std::uint16_t;
inline constexpr Level kLevelMax = 1000;

/// @brief 単位（共通仕様 第2章の単位表）。
enum class Unit : std::uint8_t { DegC, PctRh, Pct, LevelPm, HPa, Lux, Bytes, Rpm };

/// @brief データ品質（共通仕様 第5章）。
enum class Quality : std::uint8_t { Good, Stale, Fault };

/// @brief 出力の持ち主（telemetry の actuators[].owner。共通仕様 v0.2.1 #10・#24）。
enum class Owner : std::uint8_t { Interlock, Safety, Manual, Auto, Schedule, Local, None };

/// @brief cmd の優先度（共通仕様 第4章）。
enum class Priority : std::uint8_t { Safety, Manual, Auto, Schedule };

/// @brief 本体のモード（profiles/mcu-cpp-01.yaml の modes）。
enum class Mode : std::uint8_t { Auto, Manual, PowerSave };

/// @brief 単位の文字列。
constexpr std::string_view to_string(Unit u) {
  switch (u) {
    case Unit::DegC: return "degC";
    case Unit::PctRh: return "pct_rh";
    case Unit::Pct: return "pct";
    case Unit::LevelPm: return "level_pm";
    case Unit::HPa: return "hPa";
    case Unit::Lux: return "lux";
    case Unit::Bytes: return "bytes";
    case Unit::Rpm: return "rpm";
  }
  return "";
}

/// @brief 品質の文字列。
constexpr std::string_view to_string(Quality q) {
  switch (q) {
    case Quality::Good: return "good";
    case Quality::Stale: return "stale";
    case Quality::Fault: return "fault";
  }
  return "";
}

/// @brief owner の文字列。
constexpr std::string_view to_string(Owner o) {
  switch (o) {
    case Owner::Interlock: return "interlock";
    case Owner::Safety: return "safety";
    case Owner::Manual: return "manual";
    case Owner::Auto: return "auto";
    case Owner::Schedule: return "schedule";
    case Owner::Local: return "local";
    case Owner::None: return "none";
  }
  return "";
}

/// @brief 優先度の文字列。
constexpr std::string_view to_string(Priority p) {
  switch (p) {
    case Priority::Safety: return "safety";
    case Priority::Manual: return "manual";
    case Priority::Auto: return "auto";
    case Priority::Schedule: return "schedule";
  }
  return "";
}

/// @brief モードの文字列。
constexpr std::string_view to_string(Mode m) {
  switch (m) {
    case Mode::Auto: return "auto";
    case Mode::Manual: return "manual";
    case Mode::PowerSave: return "power_save";
  }
  return "";
}

/// @brief 優先度の文字列を読む。
/// @param s 文字列
/// @return 共通仕様の4値のどれでもなければ std::nullopt
constexpr std::optional<Priority> parse_priority(std::string_view s) {
  if (s == "safety") return Priority::Safety;
  if (s == "manual") return Priority::Manual;
  if (s == "auto") return Priority::Auto;
  if (s == "schedule") return Priority::Schedule;
  return std::nullopt;
}

/// @brief モードの文字列を読む。
/// @param s 文字列
/// @return auto・manual・power_save のどれでもなければ std::nullopt
constexpr std::optional<Mode> parse_mode(std::string_view s) {
  if (s == "auto") return Mode::Auto;
  if (s == "manual") return Mode::Manual;
  if (s == "power_save") return Mode::PowerSave;
  return std::nullopt;
}

/// @brief リース中の上位指令の owner は、その cmd の priority（基本設計書 第6章）。
constexpr Owner owner_of(Priority p) {
  switch (p) {
    case Priority::Safety: return Owner::Safety;
    case Priority::Manual: return Owner::Manual;
    case Priority::Auto: return Owner::Auto;
    case Priority::Schedule: return Owner::Schedule;
  }
  return Owner::None;
}

}  // namespace node::domain

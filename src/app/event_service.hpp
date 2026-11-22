#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app/publisher.hpp"
#include "infra/envelope.hpp"
#include "support/fixed_string.hpp"
#include "support/static_vector.hpp"

namespace node::app {

/// @brief イベントの重大度（共通仕様 第6章）。
enum class Severity : std::uint8_t { Info, Warning, Critical };

/// @brief 重大度の文字列。
constexpr std::string_view to_string(Severity s) {
  switch (s) {
    case Severity::Info: return "info";
    case Severity::Warning: return "warning";
    case Severity::Critical: return "critical";
  }
  return "";
}

/// @brief event の送信。遷移1回につき1件を固定容量（32件）にため、接続中に古い順に送る（基本設計書 第6章、FR-11）。
///
/// あふれたら新しいイベントを捨てて数える（open-questions #8、仮版。P4 で本版）。
class EventService {
 public:
  static constexpr std::size_t kCapacity = 32;
  static constexpr std::size_t kDetailMax = 192;

  /// @brief イベントをためる。
  /// @param code コード（例："BOOT"）
  /// @param severity 重大度
  /// @param detail_json detail のオブジェクト（"{...}"）。空なら detail を付けない
  /// @param stamp 発生時刻
  /// @return あふれて捨てたら false
  bool post(std::string_view code, Severity severity, std::string_view detail_json, const infra::Stamp& stamp);

  /// @brief ためたイベントを送れるだけ送る（未接続なら何もしない）。
  void flush(Publisher& publisher);

  [[nodiscard]] std::size_t pending() const { return queue_.size(); }
  [[nodiscard]] std::uint32_t dropped() const { return dropped_; }

 private:
  struct Pending {
    support::FixedString<32> code{};
    Severity severity{Severity::Info};
    support::FixedString<kDetailMax> detail{};
    infra::Stamp stamp{};
  };
  support::StaticVector<Pending, kCapacity> queue_{};
  std::uint32_t dropped_{0};
};

}  // namespace node::app

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

/// @brief event の保持と送信（基本設計書 FR-11、P4 の決定 #8・#65・#70・#71、open-questions #74）。
///
/// - イベントは発生時の単調時計で記録し、送るときに ts と seq を付ける
/// - 送れない間（未接続・同期前・上限）は最大32件保持する。あふれたら BOOT を除く中で最も古いものを捨てて数える
/// - QoS 1 で送って PUBACK を受ける前に切れた電文は、同じ msg_id・seq・ts のまま保持の先頭に戻し、再接続後に送り直す
/// - 一度接続した後に切れたら、再接続のときに UPSTREAM_LOST（切れた時刻）を先頭に、UPSTREAM_RESTORED（down_ms・dropped）を
///   保持の後ろに入れる。この2件は32件に数えない
class EventService {
 public:
  static constexpr std::size_t kCapacity = 32;     ///< 保持する件数（#8）
  static constexpr std::size_t kDetailMax = 192;   ///< detail の最大長

  /// @brief イベントをためる。あふれたら BOOT を除く中で最も古いものを捨てる。
  /// @param code コード（例："BOOT"）
  /// @param severity 重大度
  /// @param detail_json detail のオブジェクト（"{...}"）。空なら detail を付けない
  /// @param stamp 発生時刻
  /// @param channel event の channel（空なら付けない）
  /// @return 入れられなかったら false（文字列が長すぎる）
  bool post(std::string_view code, Severity severity, std::string_view detail_json, const infra::Stamp& stamp,
            std::string_view channel = {});

  /// @brief ためたイベントを送れるだけ送る。
  /// @param publisher 送信の窓口
  void flush(Publisher& publisher);
  /// @brief PUBACK を受けた（確認が取れた）。
  /// @param token 送ったときのトークン
  void on_delivered(std::uint32_t token);
  /// @brief 接続した（LOST・RESTORED を入れる）。
  /// @param stamp 接続した時刻
  void on_connected(const infra::Stamp& stamp);
  /// @brief 切れた。確認待ちの電文を保持の先頭に戻し、一度接続した後なら切れた時刻を覚える。
  /// @param stamp 切れた時刻
  void on_disconnected(const infra::Stamp& stamp);

  /// @brief 送っていない件数（LOST・RESTORED を含む）。
  /// @return 件数
  [[nodiscard]] std::size_t pending() const { return queue_.size(); }
  /// @brief 送って確認待ちの件数。
  /// @return 件数
  [[nodiscard]] std::size_t inflight() const { return inflight_.size(); }
  /// @brief 捨てた件数の合計。
  /// @return 件数
  [[nodiscard]] std::uint32_t dropped() const { return dropped_total_; }
  /// @brief 最初の接続の前に捨てた件数。
  /// @return 件数
  [[nodiscard]] std::uint32_t dropped_before_first_connect() const { return dropped_before_connect_; }
  /// @brief BOOT を送ったか。
  /// @return 送っていれば true
  [[nodiscard]] bool boot_sent() const { return boot_sent_; }

 private:
  enum class Role : std::uint8_t { Normal, Boot, Lost, Restored };
  struct Pending {
    support::FixedString<32> code;
    Severity severity{Severity::Info};
    support::FixedString<kDetailMax> detail;
    support::FixedString<32> channel;
    infra::Stamp stamp{};
    Role role{Role::Normal};
    bool sent{false};                ///< 一度送った（送り直しは fields のまま作り直す。#71・#116）
    infra::EnvelopeFields fields{};  ///< 送った電文の msg_id・seq・ts
  };
  struct Inflight {
    std::uint32_t token{0};
    Pending item{};
  };

  [[nodiscard]] std::size_t normal_count() const;
  void drop_oldest();
  bool insert_front(const Pending& p);
  static void write_payload(const Pending& p, infra::JsonWriter& w);
  static support::Result<Publisher::Sent> send(Publisher& publisher, const Pending& p);

  support::StaticVector<Pending, kCapacity + 2> queue_{};
  support::StaticVector<Inflight, kCapacity> inflight_{};
  bool ever_connected_{false};
  bool lost_{false};
  infra::Stamp lost_at_{};
  bool boot_sent_{false};
  std::uint32_t dropped_total_{0};
  std::uint32_t dropped_since_restored_{0};
  std::uint32_t dropped_before_connect_{0};
};

}  // namespace node::app

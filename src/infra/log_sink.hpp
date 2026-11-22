#pragma once

#include <string_view>

namespace node::infra {

/// @brief 運用ログ（JSON Lines）の書き出し口。書き方は platform が決める。
class ILogSink {
 public:
  virtual ~ILogSink() = default;
  /// @brief 1行書く。
  /// @param event 種類（boot・connect・disconnect・tx・rx・rx_discard・ack・diag）
  /// @param fields_json 中身の JSON オブジェクト（"{...}"）
  virtual void write(std::string_view event, std::string_view fields_json) = 0;
};

/// @brief 何も書かない ILogSink。
class NullLogSink final : public ILogSink {
 public:
  void write(std::string_view /*event*/, std::string_view /*fields_json*/) override {}
};

}  // namespace node::infra

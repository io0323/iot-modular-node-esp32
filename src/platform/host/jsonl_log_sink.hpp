#pragma once

#include <cstdio>
#include <mutex>
#include <string>

#include "infra/log_sink.hpp"

namespace node::platform {

/// @brief JSON Lines のログをファイルに書く。1行は {"ts":…,"ev":…, 中身の項目}。1行ごとに flush する。
class JsonlLogSink final : public infra::ILogSink {
 public:
  /// @brief ファイルを追記で開く。
  /// @param path ファイルのパス
  explicit JsonlLogSink(const std::string& path);
  ~JsonlLogSink() override;
  JsonlLogSink(const JsonlLogSink&) = delete;
  JsonlLogSink& operator=(const JsonlLogSink&) = delete;
  JsonlLogSink(JsonlLogSink&&) = delete;
  JsonlLogSink& operator=(JsonlLogSink&&) = delete;

  /// @brief ファイルを開けたか。
  /// @return 開けたら true
  [[nodiscard]] bool ok() const { return file_ != nullptr; }
  void write(std::string_view event, std::string_view fields_json) override;

 private:
  std::FILE* file_{nullptr};
  std::mutex mu_;
};

}  // namespace node::platform

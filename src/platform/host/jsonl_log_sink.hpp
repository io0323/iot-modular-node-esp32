#pragma once

#include <cstdio>
#include <mutex>
#include <string>

#include "infra/clock.hpp"
#include "infra/log_sink.hpp"

namespace node::platform {

/// @brief JSON Lines のログをファイルに書く。1行は {"ts":…,"ev":…, 中身の項目}。1行ごとに flush する。
class JsonlLogSink final : public infra::ILogSink {
 public:
  JsonlLogSink(const infra::IClock& clock, const std::string& path);
  ~JsonlLogSink() override;
  JsonlLogSink(const JsonlLogSink&) = delete;
  JsonlLogSink& operator=(const JsonlLogSink&) = delete;

  [[nodiscard]] bool ok() const { return file_ != nullptr; }
  void write(std::string_view event, std::string_view fields_json) override;

 private:
  const infra::IClock& clock_;
  std::FILE* file_{nullptr};
  std::mutex mu_;
};

}  // namespace node::platform

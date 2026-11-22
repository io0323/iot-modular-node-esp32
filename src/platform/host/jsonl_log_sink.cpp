#include "platform/host/jsonl_log_sink.hpp"

#include "infra/formats.hpp"

namespace node::platform {

JsonlLogSink::JsonlLogSink(const infra::IClock& clock, const std::string& path)
    : clock_(clock), file_(std::fopen(path.c_str(), "a")) {}

JsonlLogSink::~JsonlLogSink() {
  if (file_ != nullptr) std::fclose(file_);
}

void JsonlLogSink::write(std::string_view event, std::string_view fields_json) {
  if (file_ == nullptr) return;
  const infra::TsString ts = infra::format_ts(clock_.utc_ms());
  // fields_json は "{...}"。先頭の "{" を外して ts・ev の後ろにつなぐ
  std::string_view body = fields_json;
  if (!body.empty() && body.front() == '{') body.remove_prefix(1);
  const bool empty_body = body == "}";
  std::lock_guard<std::mutex> lock(mu_);
  std::fprintf(file_, "{\"ts\":\"%s\",\"ev\":\"%.*s\"%s%.*s\n", ts.c_str(), static_cast<int>(event.size()),
               event.data(), empty_body ? "" : ",", static_cast<int>(body.size()), body.data());
  std::fflush(file_);
}

}  // namespace node::platform

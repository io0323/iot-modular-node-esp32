#include "platform/host/jsonl_log_sink.hpp"

#include <chrono>

#include "infra/formats.hpp"

namespace node::platform {

// FILE* はこのクラスが持ち、デストラクタで閉じる（docs/quality.md の抑止の表）
JsonlLogSink::JsonlLogSink(const std::string& path)
    : file_(std::fopen(path.c_str(), "a")) {}  // NOLINT(cppcoreguidelines-owning-memory)

JsonlLogSink::~JsonlLogSink() {
  if (file_ != nullptr) {
    (void)std::fclose(file_);  // NOLINT(cppcoreguidelines-owning-memory)：コンストラクタで開いた FILE* を閉じる
  }
}

void JsonlLogSink::write(std::string_view event, std::string_view fields_json) {
  if (file_ == nullptr) {
    return;
  }
  // ログの時刻はホストの時計（電文の ts とは別）
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const infra::TsString ts =
      infra::format_ts(static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count()));
  // fields_json は "{...}"。先頭の "{" を外して ts・ev の後ろにつなぐ
  std::string_view body = fields_json;
  if (!body.empty() && body.front() == '{') {
    body.remove_prefix(1);
  }
  const bool empty_body = body == "}";
  std::lock_guard<std::mutex> const lock(mu_);
  std::string line;
  line.reserve(32 + ts.size() + event.size() + body.size());
  line.append(R"({"ts":")").append(ts.c_str()).append(R"(","ev":")").append(event).append("\"");
  if (!empty_body) {
    line.append(",");
  }
  line.append(body).append("\n");
  (void)std::fwrite(line.data(), 1, line.size(), file_);
  (void)std::fflush(file_);
}

}  // namespace node::platform

#include "app/event_service.hpp"

namespace node::app {

bool EventService::post(std::string_view code, Severity severity, std::string_view detail_json,
                        const infra::Stamp& stamp) {
  Pending p{};
  p.severity = severity;
  p.stamp = stamp;
  if (!p.code.assign(code) || !p.detail.assign(detail_json) || !queue_.push_back(p)) {
    ++dropped_;
    return false;
  }
  return true;
}

void EventService::flush(Publisher& publisher) {
  while (!queue_.empty()) {
    const Pending& p = queue_.front();
    auto r = publisher.publish(
        infra::Kind::Event,
        [&p](infra::JsonWriter& w) {
          w.begin_object();
          w.key("code").string(p.code.view());
          w.key("severity").string(to_string(p.severity));
          if (!p.detail.empty()) w.key("detail").raw(p.detail.view());
          w.end_object();
        },
        &p.stamp);
    if (!r) return;  // 未接続・再送待ちがいっぱいなど。次の機会に送る
    queue_.erase_at(0);
  }
}

}  // namespace node::app

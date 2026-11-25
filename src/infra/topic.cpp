#include "infra/topic.hpp"

namespace node::infra {
namespace {
constexpr std::string_view suffix_of(Kind k) {
  switch (k) {
    case Kind::Telemetry: return "telemetry";
    case Kind::Event: return "event";
    case Kind::State: return "state";
    case Kind::CmdAck: return "cmd/ack";
  }
  return "";
}

bool build(Topics::TopicString& out, std::string_view site, std::string_view device_id,
           std::string_view suffix) {
  out.clear();
  return out.append("iot/v1/") && out.append(site) && out.append("/") && out.append(device_id) &&
         out.append("/") && out.append(suffix);
}
}  // namespace

Topics::Topics(std::string_view site, std::string_view device_id) {
  for (Kind const k : {Kind::Telemetry, Kind::Event, Kind::State, Kind::CmdAck}) {
    ok_ = build(topics_[index_of(k)], site, device_id, suffix_of(k)) && ok_;
  }
  ok_ = build(cmd_, site, device_id, "cmd") && ok_;
}

}  // namespace node::infra

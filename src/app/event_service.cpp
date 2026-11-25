#include "app/event_service.hpp"

#include <array>

namespace node::app {

std::size_t EventService::normal_count() const {
  std::size_t n = 0;
  for (const Pending& p : queue_) {
    n += (p.role == Role::Normal || p.role == Role::Boot) ? 1U : 0U;
  }
  return n;
}

void EventService::drop_oldest() {
  // BOOT・LOST・RESTORED を除く中で最も古いもの（#8）
  for (std::size_t i = 0; i < queue_.size(); ++i) {
    if (queue_[i].role != Role::Normal) {
      continue;
    }
    queue_.erase_at(i);
    ++dropped_total_;
    if (ever_connected_) {
      ++dropped_since_restored_;
    } else {
      ++dropped_before_connect_;
    }
    return;
  }
}

bool EventService::insert_front(const Pending& p) {
  if (queue_.full()) {
    return false;
  }
  if (!queue_.push_back(p)) {
    return false;
  }
  for (std::size_t i = queue_.size() - 1; i > 0; --i) {
    queue_[i] = queue_[i - 1];
  }
  queue_[0] = p;
  return true;
}

bool EventService::post(std::string_view code, Severity severity, std::string_view detail_json,
                        const infra::Stamp& stamp, std::string_view channel) {
  Pending p{};
  p.severity = severity;
  p.stamp = stamp;
  p.role = code == "BOOT" ? Role::Boot : Role::Normal;
  if (!p.code.assign(code) || !p.detail.assign(detail_json) || !p.channel.assign(channel)) {
    return false;
  }
  if (normal_count() >= kCapacity) {
    drop_oldest();
  }
  return queue_.push_back(p);
}

support::Result<Publisher::Sent> EventService::send(Publisher& publisher, const Pending& p) {
  if (p.built) {
    return publisher.republish(infra::Kind::Event, p.message.view(), p.seq);
  }
  return publisher.publish(
      infra::Kind::Event,
      [&p](infra::JsonWriter& w) {
        w.begin_object();
        w.key("code").string(p.code.view());
        w.key("severity").string(to_string(p.severity));
        if (!p.channel.empty()) {
          w.key("channel").string(p.channel.view());
        }
        if (!p.detail.empty()) {
          w.key("detail").raw(p.detail.view());
        }
        w.end_object();
      },
      &p.stamp);
}

void EventService::flush(Publisher& publisher) {
  while (!queue_.empty()) {
    const Pending& p = queue_.front();
    const auto r = send(publisher, p);
    if (!r) {
      return;  // 未接続・同期前・上限など。保持したまま次の機会に送る
    }
    if (p.role == Role::Boot) {
      boot_sent_ = true;
    }
    if (r.value().token != 0 && !inflight_.full()) {
      Inflight f{};
      f.token = r.value().token;
      f.item = p;
      if (!p.built) {
        f.item.built = true;
        f.item.seq = r.value().seq;
        (void)f.item.message.assign(publisher.last_message());
      }
      (void)inflight_.push_back(f);
    }
    queue_.erase_at(0);
  }
}

void EventService::on_delivered(std::uint32_t token) {
  for (std::size_t i = 0; i < inflight_.size(); ++i) {
    if (inflight_[i].token == token) {
      inflight_.erase_at(i);
      return;
    }
  }
}

void EventService::on_disconnected(const infra::Stamp& stamp) {
  // 確認が取れなかった電文は、送った順のまま保持の先頭に戻す（#71）
  for (std::size_t k = inflight_.size(); k > 0; --k) {
    if (normal_count() >= kCapacity) {
      drop_oldest();
    }
    (void)insert_front(inflight_[k - 1].item);
  }
  inflight_.clear();
  if (ever_connected_ && !lost_) {
    lost_ = true;
    lost_at_ = stamp;
  }
}

void EventService::on_connected(const infra::Stamp& stamp) {
  if (lost_) {
    Pending lost{};
    (void)lost.code.assign("UPSTREAM_LOST");
    lost.severity = Severity::Warning;
    lost.stamp = lost_at_;
    lost.role = Role::Lost;
    (void)insert_front(lost);

    Pending restored{};
    (void)restored.code.assign("UPSTREAM_RESTORED");
    restored.severity = Severity::Info;
    restored.stamp = stamp;
    restored.role = Role::Restored;
    std::array<char, 96> buf{};
    infra::JsonWriter w(buf);
    w.begin_object();
    w.key("down_ms").uinteger(stamp.monotonic_ms - lost_at_.monotonic_ms);
    w.key("dropped").uinteger(dropped_since_restored_);
    w.end_object();
    (void)restored.detail.assign(w.ok() ? w.view() : std::string_view{});
    (void)queue_.push_back(restored);
    dropped_since_restored_ = 0;
    lost_ = false;
  }
  ever_connected_ = true;
}

}  // namespace node::app

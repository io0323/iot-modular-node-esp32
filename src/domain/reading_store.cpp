#include "domain/reading_store.hpp"

namespace node::domain {

ReadingStore::Entry* ReadingStore::find(std::string_view channel) {
  for (auto& e : entries_) {
    if (e.reading.channel == channel) return &e;
  }
  return nullptr;
}

const ReadingStore::Entry* ReadingStore::find(std::string_view channel) const {
  for (const auto& e : entries_) {
    if (e.reading.channel == channel) return &e;
  }
  return nullptr;
}

ReadingStore::Entry* ReadingStore::find_or_add(std::string_view channel) {
  if (Entry* e = find(channel)) return e;
  Entry n{};
  n.reading.channel = channel;
  if (!entries_.push_back(n)) return nullptr;
  return &entries_.back();
}

bool ReadingStore::set_period(std::string_view channel, std::uint32_t period_ms) {
  Entry* e = find_or_add(channel);
  if (e == nullptr) return false;
  e->period_ms = period_ms;
  return true;
}

bool ReadingStore::update(const Reading& r, std::uint64_t now_ms) {
  Entry* e = find_or_add(r.channel);
  if (e == nullptr) return false;
  e->reading = r;
  e->has_reading = true;
  if (r.quality == Quality::Good) {
    e->has_good = true;
    e->last_good_ms = now_ms;
  }
  return true;
}

bool ReadingStore::mark_fault(std::string_view channel, std::uint64_t /*now_ms*/) {
  Entry* e = find_or_add(channel);
  if (e == nullptr) return false;
  e->reading.quality = Quality::Fault;
  e->has_reading = true;
  return true;
}

std::optional<Reading> ReadingStore::latest(std::string_view channel) const {
  const Entry* e = find(channel);
  if (e == nullptr || !e->has_reading) return std::nullopt;
  return e->reading;
}

Quality ReadingStore::quality(std::string_view channel, std::uint64_t now_ms) const {
  const Entry* e = find(channel);
  if (e == nullptr || !e->has_reading || !e->has_good) return Quality::Stale;
  if (e->reading.quality == Quality::Fault) return Quality::Fault;
  if (e->reading.quality == Quality::Stale) return Quality::Stale;
  const std::uint64_t limit = static_cast<std::uint64_t>(e->period_ms) * kStaleFactor;
  return (now_ms - e->last_good_ms > limit) ? Quality::Stale : Quality::Good;
}

std::optional<std::int32_t> ReadingStore::good_value(std::string_view channel, std::uint64_t now_ms) const {
  if (quality(channel, now_ms) != Quality::Good) return std::nullopt;
  return find(channel)->reading.value_milli;
}

}  // namespace node::domain

#include "domain/cmd_dedup_cache.hpp"

namespace node::domain {

std::optional<CmdDedupCache::Payload> CmdDedupCache::find(std::string_view cmd_id,
                                                           std::uint64_t now_ms) const {
  for (std::size_t i = 0; i < count_; ++i) {
    const Entry& e = entries_[i];
    if (e.cmd_id == cmd_id && now_ms - e.stored_ms < kRetentionMs) return e.payload;
  }
  return std::nullopt;
}

bool CmdDedupCache::insert(std::string_view cmd_id, std::string_view payload, std::uint64_t now_ms) {
  if (cmd_id.size() > kCmdIdLen || payload.size() > kPayloadMax) return false;
  Entry& e = entries_[next_];
  (void)e.cmd_id.assign(cmd_id);    // 長さは上で確かめた
  (void)e.payload.assign(payload);  // 長さは上で確かめた
  e.stored_ms = now_ms;
  next_ = (next_ + 1) % kCapacity;
  if (count_ < kCapacity) ++count_;
  return true;
}

}  // namespace node::domain

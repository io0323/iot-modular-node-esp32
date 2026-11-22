#include "drivers/sim/sim_sensor.hpp"

namespace node::drivers {

SimSensor::SimSensor(std::string_view id, std::span<const domain::ChannelSpec> channels,
                     std::span<const std::int32_t> values_milli, std::chrono::milliseconds period)
    : id_(id), channels_(channels.first(channels.size() < kMaxChannels ? channels.size() : kMaxChannels)),
      period_(period) {
  for (std::size_t i = 0; i < channels_.size(); ++i) {
    (void)values_.push_back(i < values_milli.size() ? values_milli[i] : 0);
  }
}

support::Result<void> SimSensor::init() {
  ++init_count_;
  if (fault_) return support::make_error(support::ErrorCode::IoError, "sim sensor fault");
  return support::ok();
}

support::Result<std::size_t> SimSensor::read(std::span<domain::Reading> out) {
  if (fault_) return support::make_error(support::ErrorCode::IoError, "sim sensor fault");
  if (fail_next_reads_ > 0) {
    --fail_next_reads_;
    return support::make_error(support::ErrorCode::IoError, "sim sensor transient fault");
  }
  if (out.size() < channels_.size()) {
    return support::make_error(support::ErrorCode::BufferFull, "reading buffer too small");
  }
  for (std::size_t i = 0; i < channels_.size(); ++i) {
    out[i] = domain::Reading{channels_[i].channel, values_[i], channels_[i].unit, quality_};
  }
  return channels_.size();
}

bool SimSensor::set_channel_value(std::string_view channel, std::int32_t value_milli) {
  for (std::size_t i = 0; i < channels_.size(); ++i) {
    if (channels_[i].channel == channel) {
      values_[i] = value_milli;
      return true;
    }
  }
  return false;
}

void SimSensor::set_value(std::size_t index, std::int32_t value_milli) {
  if (index < values_.size()) values_[index] = value_milli;
}

}  // namespace node::drivers

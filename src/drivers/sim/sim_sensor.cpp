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

support::Result<std::size_t> SimSensor::read(std::span<domain::Reading> out) {
  if (out.size() < channels_.size()) {
    return support::make_error(support::ErrorCode::BufferFull, "reading buffer too small");
  }
  for (std::size_t i = 0; i < channels_.size(); ++i) {
    out[i] = domain::Reading{channels_[i].channel, values_[i], channels_[i].unit, quality_};
  }
  return channels_.size();
}

void SimSensor::set_value(std::size_t index, std::int32_t value_milli) {
  if (index < values_.size()) values_[index] = value_milli;
}

}  // namespace node::drivers

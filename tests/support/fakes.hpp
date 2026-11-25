// テスト用のフェイク（時計・乱数・MQTT・ログ）と、電文を読む小さな道具。
#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "infra/clock.hpp"
#include "infra/formats.hpp"
#include "infra/log_sink.hpp"
#include "infra/mqtt_link.hpp"
#include "infra/network.hpp"
#include "infra/random.hpp"
#include "infra/time_sync.hpp"

namespace node::test {

/// 仮想の単調時計（1000ms から始まる）。
class FakeClock final : public infra::IClock {
 public:
  [[nodiscard]] std::uint64_t monotonic_ms() const override { return mono_ms_; }
  void advance(std::uint64_t ms) { mono_ms_ += ms; }
  void set(std::uint64_t ms) { mono_ms_ = ms; }

 private:
  std::uint64_t mono_ms_{1000};
};

/// 仮想の時刻同期。UTC = 単調時計 + offset。既定は同期済みで、単調時計 0 が共通仕様のサンプルの ts
/// 2026-11-20T06:00:12.345Z（FakeClock は 1000ms から始まるので、起動時は 06:00:13.345Z）。
class FakeTimeSync final : public infra::ITimeSync {
 public:
  static constexpr std::int64_t kSampleTs = 1795154412345;  // 2026-11-20T06:00:12.345Z
  [[nodiscard]] bool synced() const override { return synced_; }
  [[nodiscard]] std::int64_t utc_offset_ms() const override { return offset_ms_; }
  void set_synced(bool s) { synced_ = s; }
  void set_offset(std::int64_t ms) { offset_ms_ = ms; }

 private:
  bool synced_{true};
  std::int64_t offset_ms_{kSampleTs};
};

/// 仮想のネットワーク。connect() ですぐに Up（accept_connect が false なら ConnectFailed）。go_down / go_up で落とす・戻す。
class FakeNetwork final : public infra::INetwork {
 public:
  support::Result<void> connect() override {
    ++connects;
    if (accept_connect) {
      up_ = true;
      events.push_back(infra::NetworkEvent::Up);
    } else {
      events.push_back(infra::NetworkEvent::ConnectFailed);
    }
    return support::ok();
  }
  [[nodiscard]] bool up() const override { return up_; }
  infra::NetworkEvent poll_event() override {
    if (events.empty()) return infra::NetworkEvent::None;
    const auto e = events.front();
    events.pop_front();
    return e;
  }
  /// ネットワークが落ちたことにする（戻るまで connect は失敗する）
  void go_down() {
    up_ = false;
    accept_connect = false;
    events.push_back(infra::NetworkEvent::Down);
  }
  /// 次の connect から成功させる
  void go_up() { accept_connect = true; }

  bool accept_connect{true};
  int connects{0};
  std::deque<infra::NetworkEvent> events;

 private:
  bool up_{false};
};

/// 決まった並びの乱数（呼ぶたびに値が変わる）。
class FakeRandom final : public infra::IRandom {
 public:
  explicit FakeRandom(std::uint8_t seed = 0x5A) : next_(seed) {}
  void fill(std::span<std::uint8_t> out) override {
    for (auto& b : out) {
      b = next_;
      next_ = static_cast<std::uint8_t>(next_ * 37U + 11U);
    }
  }

 private:
  std::uint8_t next_;
};

/// ログを覚えるだけの ILogSink。
class CapturingLog final : public infra::ILogSink {
 public:
  struct Line {
    std::string event;
    std::string fields;
  };
  void write(std::string_view event, std::string_view fields_json) override {
    lines.push_back({std::string(event), std::string(fields_json)});
  }
  std::vector<Line> lines;
};

/// メモリ上の IMqttLink。発行した電文を覚え、受信と接続状態の変化をテストから入れる。
class FakeMqttLink final : public infra::IMqttLink {
 public:
  struct Published {
    std::string topic;
    std::string payload;
    infra::Qos qos;
    bool retain;
  };
  struct Connect {
    std::string client_id, username, password, will_topic, will_payload;
    infra::Qos will_qos;
    bool will_retain;
    std::uint16_t keepalive_s;
  };

  support::Result<void> connect(const infra::ConnectOptions& o) override {
    connects.push_back({std::string(o.client_id), std::string(o.username), std::string(o.password),
                        std::string(o.will.topic), std::string(o.will.payload), o.will.qos, o.will.retain,
                        o.keepalive_s});
    if (accept_connect) {
      connected_ = true;
      events.push_back(infra::LinkEvent::Connected);
    } else {
      events.push_back(infra::LinkEvent::ConnectFailed);  // ブローカーが接続を断った
    }
    return support::ok();
  }
  void disconnect(std::uint32_t /*timeout_ms*/) override {
    connected_ = false;
    held_.clear();  // clean session：確認待ちは捨てる
    ++disconnects;
  }
  support::Result<std::uint32_t> publish(std::string_view topic, std::string_view payload, infra::Qos qos,
                                         bool retain) override {
    if (!connected_) return support::make_error(support::ErrorCode::NotConnected);
    std::uint32_t token = 0;
    if (qos == infra::Qos::AtLeastOnce) {
      if (held_.size() >= kMaxInflight) return support::make_error(support::ErrorCode::Busy);
      token = next_token_++;
      if (auto_puback) {
        delivered_.push_back(token);
      } else {
        held_.push_back(token);
      }
    }
    published.push_back({std::string(topic), std::string(payload), qos, retain});
    return token;
  }
  bool poll_delivery(std::uint32_t& token) override {
    if (delivered_.empty()) return false;
    token = delivered_.front();
    delivered_.pop_front();
    return true;
  }
  support::Result<void> subscribe(std::string_view topic, infra::Qos /*qos*/) override {
    subscriptions.emplace_back(topic);
    return support::ok();
  }
  bool poll_message(infra::InboundMessage& out) override {
    if (inbound.empty()) return false;
    (void)out.topic.assign(inbound.front().first);
    (void)out.payload.assign(inbound.front().second);
    inbound.pop_front();
    return true;
  }
  infra::LinkEvent poll_event() override {
    if (events.empty()) return infra::LinkEvent::None;
    const auto e = events.front();
    events.pop_front();
    return e;
  }
  [[nodiscard]] bool connected() const override { return connected_; }
  [[nodiscard]] std::size_t inflight() const override { return held_.size(); }

  /// 接続が切れたことにする（確認待ちの電文は届かなかった扱い）
  void drop() {
    connected_ = false;
    held_.clear();
    events.push_back(infra::LinkEvent::Disconnected);
  }
  /// 確認待ちの電文すべてに PUBACK が来たことにする
  void puback_all() {
    for (const auto t : held_) delivered_.push_back(t);
    held_.clear();
  }
  void set_connected(bool c) { connected_ = c; }

  /// topic の末尾（"telemetry" など）で絞り込む
  [[nodiscard]] std::vector<Published> on(std::string_view suffix) const {
    std::vector<Published> r;
    for (const auto& p : published) {
      const bool match = p.topic.size() >= suffix.size() &&
                         p.topic.compare(p.topic.size() - suffix.size(), suffix.size(), suffix) == 0;
      if (match) r.push_back(p);
    }
    return r;
  }

  bool accept_connect{true};
  bool auto_puback{true};
  int disconnects{0};
  std::vector<Connect> connects;
  std::vector<Published> published;
  std::vector<std::string> subscriptions;
  std::deque<std::pair<std::string, std::string>> inbound;
  std::deque<infra::LinkEvent> events;

 private:
  bool connected_{false};
  std::uint32_t next_token_{1};
  std::deque<std::uint32_t> held_;
  std::deque<std::uint32_t> delivered_;
};

// ---- 電文を読む小さな道具（JsonWriter の出力は空白を含まないので、文字列の検索で足りる）

/// "key": の直後の値（文字列なら引用符の中、ほかは次の , } ] まで）。なければ std::nullopt
inline std::optional<std::string> json_value(std::string_view json, std::string_view key, std::size_t from = 0) {
  const std::string pat = "\"" + std::string(key) + "\":";
  const std::size_t k = json.find(pat, from);
  if (k == std::string_view::npos) return std::nullopt;
  std::size_t i = k + pat.size();
  if (i < json.size() && json[i] == '"') {
    const std::size_t end = json.find('"', i + 1);
    return std::string(json.substr(i + 1, end - i - 1));
  }
  std::size_t end = i;
  int depth = 0;
  while (end < json.size()) {
    const char c = json[end];
    if (c == '{' || c == '[') ++depth;
    if ((c == '}' || c == ']') && depth-- == 0) break;
    if (c == ',' && depth == 0) break;
    ++end;
  }
  return std::string(json.substr(i, end - i));
}

/// payload のオブジェクト（エンベロープの "payload": の値）
inline std::string payload_of(std::string_view message) { return json_value(message, "payload").value_or(""); }

/// telemetry の actuators[] から id の (level, owner) を取り出す
inline std::optional<std::pair<int, std::string>> actuator_in(std::string_view telemetry, std::string_view id) {
  const std::string pat = "{\"id\":\"" + std::string(id) + "\"";
  const std::size_t k = telemetry.find(pat);
  if (k == std::string_view::npos) return std::nullopt;
  const auto level = json_value(telemetry, "level", k);
  const auto owner = json_value(telemetry, "owner", k);
  if (!level || !owner) return std::nullopt;
  return std::make_pair(std::stoi(*level), *owner);
}

/// cmd の電文を作る
inline std::string make_cmd(std::string_view cmd_id, std::string_view op, std::string_view args_json,
                            std::string_view priority = "auto", std::string_view extra = "",
                            std::uint32_t seq = 1) {
  std::string s = R"({"schema":"iot.v1.cmd","msg_id":"01JD3Q8Z5K7M2N4P6R8T0V0099","device_id":"gw-01","seq":)";
  s += std::to_string(seq);
  s += R"(,"boot_id":"7f3a9c01","ts":"2026-11-20T06:00:12.345Z","ts_source":"device","payload":{"cmd_id":")";
  s += cmd_id;
  s += R"(","op":")";
  s += op;
  s += "\"";
  if (!args_json.empty()) {
    s += ",\"args\":";
    s += args_json;
  }
  s += ",\"priority\":\"";
  s += priority;
  s += "\"";
  if (!extra.empty()) {
    s += ",";
    s += extra;
  }
  s += "}}";
  return s;
}

/// テスト用の ULID（末尾2桁を n にする。n は 0〜99）
inline std::string ulid(int n) {
  std::string s = "01JD3R0A5K7M2N4P6R8T0V2X00";
  s[24] = static_cast<char>('0' + n / 10);
  s[25] = static_cast<char>('0' + n % 10);
  return s;
}

}  // namespace node::test

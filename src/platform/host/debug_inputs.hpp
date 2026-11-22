#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "app/node.hpp"
#include "infra/log_sink.hpp"

namespace node::platform {

/// @brief デバッグ入力の入れ先（模擬機器の構成と fakehw の構成で実装が違う）。
class IDebugTarget {
 public:
  virtual ~IDebugTarget() = default;
  /// @brief チャネルの値を変える（単位はそのチャネルの単位）。
  /// @return そのチャネルを扱えなければ false
  virtual bool set_value(std::string_view channel, double value) = 0;
  /// @brief センサーを読めなくする・戻す。
  /// @return そのセンサーを扱えなければ false
  virtual bool set_fault(std::string_view sensor_id, bool fault) = 0;
};

/// @brief ホスト版のデバッグ入力（P2）。起動からの経過時間で、模擬センサーの値・故障、ボタン、手動の出力を入れる。
///
/// - `--sim <channel>=<value>[@<秒>]`：模擬センサーの値を変える（秒を省けば起動時）
/// - `--sim-fault <センサーID>@<開始秒>[-<終了秒>]`：模擬センサーを読めなくする
/// - `--press-at <秒>`：ボタンの短押しを1回入れる
/// - `--manual <id>=<level>@<秒>`：手動の出力を入れる（手動モードでなければ無視してログに残す）
/// 契約テストはこれを使わない。省略時の動きは P1 と同じ。
class DebugInputs {
 public:
  /// @brief 引数を1つ読む。
  /// @param option "--sim" など
  /// @param value その値
  /// @param error 読めなかったときの理由の書き込み先
  /// @return このクラスの引数で、読めたら true。このクラスの引数でなければ false で error は空のまま
  bool parse(std::string_view option, std::string_view value, std::string& error);

  /// @brief 時刻が来た入力を当てる。
  /// @param elapsed_ms 起動からの経過時間
  /// @param node アプリ（ボタン・手動の出力）
  /// @param target 値と故障の入れ先
  /// @param log ログ
  void apply_due(std::uint64_t elapsed_ms, app::Node& node, IDebugTarget& target, infra::ILogSink& log);

  [[nodiscard]] bool empty() const { return actions_.empty(); }

 private:
  enum class Type : std::uint8_t { SetValue, FaultOn, FaultOff, Press, Manual };
  struct Action {
    std::uint64_t at_ms{0};
    Type type{Type::Press};
    std::string target;  ///< チャネル・センサー ID・アクチュエーター ID
    std::int32_t value{0};
    bool done{false};
  };
  std::vector<Action> actions_;
};

}  // namespace node::platform

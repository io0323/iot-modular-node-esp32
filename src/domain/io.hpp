// 計測と本体の UI の、ハードに触る部分との受け渡し（P6 の決定 #96・#110）。
//
// 判断する部分（周期・故障の判定・再初期化の時刻・表示の中身）は app（Node の中の SensorPoller・UiService）に置き、
// ハードに触る部分（センサーの read・init・回復、ボタンの poll、OLED への書き込み）は drivers が実装する。
// ホスト版は drivers の実装をその場で呼ぶ。ターゲット版は platform/esp32 がキューで sense・ui のタスクにつなぐ。
// 受け渡しの型はキューで写せるよう、固定の大きさで、寿命の長い文字列（構成表のチャネル名）だけを指す。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "domain/specs.hpp"
#include "domain/ui.hpp"
#include "support/fixed_string.hpp"
#include "support/result.hpp"

namespace node::domain {

/// @brief センサーへの依頼の種類。
enum class SensorOp : std::uint8_t {
  Read,      ///< 計測する。読めなければその場で続けて読み直し、3回続けて失敗したら回復を1回だけ要求する（#55）
  Reinit,    ///< 故障の間の再初期化：init の後に1回読む（#48）
  FaultDue,  ///< 故障の間に周期が来た（ハードには触らない。fault の計測値を telemetry に入れるため）
};

/// @brief センサーへの依頼（control → sense）。
struct SensorRequest {
  std::uint8_t index{0};          ///< 構成表のセンサーの番号
  SensorOp op{SensorOp::Read};    ///< 種類
  std::uint64_t at_ms{0};         ///< 依頼した時刻（単調時計の ms）
};

/// @brief 依頼の結果（sense → control）。
struct SensorReport {
  static constexpr std::size_t kMaxReadings = 4;  ///< 1台のチャネルの上限

  std::uint8_t index{0};                         ///< 構成表のセンサーの番号
  SensorOp op{SensorOp::Read};                   ///< 依頼の種類
  bool ok{false};                                ///< 読めたか（Reinit は init も成功したか。FaultDue はいつも false）
  bool recovery_requested{false};                ///< 回復を要求したか（Read で3回続けて失敗したとき）
  std::uint8_t read_errors{0};                   ///< 読めなかった回数（読み直しを含む）
  std::uint8_t count{0};                         ///< readings の件数
  std::array<Reading, kMaxReadings> readings{};  ///< 読めた計測値
  std::uint64_t at_ms{0};                        ///< 依頼した時刻（単調時計の ms）
};

/// @brief センサーのハードに触る部分の契約。submit した順に poll_report で結果を返す。
class ISensorIo {
 public:
  virtual ~ISensorIo() = default;
  /// @brief 依頼する。
  /// @param request 依頼
  /// @return 受け付けられなければ false（キューがいっぱい）
  virtual bool submit(const SensorRequest& request) = 0;
  /// @brief 結果を1件取り出す。
  /// @param out 書き込み先
  /// @return 取り出せたら true
  virtual bool poll_report(SensorReport& out) = 0;
};

/// @brief 本体の UI からの入力の種類。
enum class UiInputKind : std::uint8_t {
  ModeButton,    ///< MODE ボタンの判定
  ActionButton,  ///< ACTION ボタンの判定
  DisplayError,  ///< 表示の書き込みに失敗した（診断用に数える）
};

/// @brief 本体の UI からの入力（ui → control）。
struct UiInput {
  UiInputKind kind{UiInputKind::ModeButton};  ///< 種類
  ButtonEvent event{ButtonEvent::None};       ///< ボタンの判定（DisplayError のときは None）
};

/// @brief 表示の1行。
using DisplayLine = support::FixedString<IDisplay::kColumns>;

/// @brief 表示の内容（control → ui）。
struct DisplayFrame {
  bool power_on{true};                              ///< 表示をオンにするか（省電力モードではオフ）
  bool has_lines{false};                            ///< lines を書くか（オフのときは書かない）
  std::array<DisplayLine, IDisplay::kRows> lines{};  ///< 8行
};

/// @brief 本体の UI のハードに触る部分の契約。
class IUiIo {
 public:
  virtual ~IUiIo() = default;
  /// @brief 機器を初期化する。
  /// @return 初期化できなかった機器があれば失敗（ほかの機器は使える）
  virtual support::Result<void> init() = 0;
  /// @brief 入力を1件取り出す（ボタンは 20ms ごとに読む。MODE を ACTION より先に返す）。
  /// @param now_ms いまの単調時計
  /// @param out 書き込み先
  /// @return 取り出せたら true
  virtual bool poll_input(std::uint64_t now_ms, UiInput& out) = 0;
  /// @brief 表示の内容を書く。
  /// @param frame 表示の内容
  virtual void show(const DisplayFrame& frame) = 0;
};

}  // namespace node::domain

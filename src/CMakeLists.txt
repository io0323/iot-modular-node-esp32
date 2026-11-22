# 層ごとのライブラリ。依存の向きは CLAUDE.md「層と依存の向き」のとおり。

# support：Result・Error・固定容量コンテナ（ヘッダだけ）
add_library(node_support INTERFACE)
target_include_directories(node_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

# domain：値・機器の契約・リース・重複排除・ポンプのインターロック
add_library(node_domain STATIC
  domain/cmd_dedup_cache.cpp
  domain/pump_interlock.cpp)
target_link_libraries(node_domain PUBLIC node_support)
node_product_target(node_domain)

# infra：インターフェースと、JSON・エンベロープ・トピック・送信量の制限
add_library(node_infra STATIC
  infra/json_writer.cpp
  infra/formats.cpp
  infra/ulid.cpp
  infra/envelope.cpp
  infra/topic.cpp
  infra/cmd_parser.cpp)
target_link_libraries(node_infra PUBLIC node_support node_domain PRIVATE jsmn_headers)
node_product_target(node_infra)

# app：サービスと調停
add_library(node_app STATIC
  app/control_arbiter.cpp
  app/publisher.cpp
  app/event_service.cpp
  app/state_service.cpp
  app/telemetry_service.cpp
  app/command_service.cpp
  app/node.cpp)
target_link_libraries(node_app PUBLIC node_domain node_infra)
node_product_target(node_app)

# drivers/sim：ホスト用の模擬機器
add_library(node_drivers_sim STATIC
  drivers/sim/sim_sensor.cpp)
target_link_libraries(node_drivers_sim PUBLIC node_domain)
node_product_target(node_drivers_sim)

# config：構成表
add_library(node_config STATIC
  config/devices.cpp)
target_link_libraries(node_config PUBLIC node_domain node_drivers_sim)
node_product_target(node_config)

# platform/host：Paho・時計・乱数・ログ（POSIX・<thread>・<mutex> を使えるのはここだけ）
add_library(node_platform_host STATIC
  platform/host/host_clock.cpp
  platform/host/host_random.cpp
  platform/host/paho_mqtt_link.cpp
  platform/host/jsonl_log_sink.cpp)
target_link_libraries(node_platform_host PUBLIC node_infra PRIVATE paho_async)
node_product_target(node_platform_host)

add_executable(mcu_cpp_01 platform/host/main.cpp)
target_link_libraries(mcu_cpp_01 PRIVATE node_app node_config node_platform_host)
node_product_target(mcu_cpp_01)

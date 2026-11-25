# src/ の機器に依らない層（ホスト版と同じソース。ESP-IDF・FreeRTOS のヘッダは使わない。P6 の原則）
set(S "${NODE_SRC_DIR}")
# 試験用（#132）：QEMU の契約テストのために、フェイクの hal と機器を入れる（製品の構成では入らない）
set(FAKE_HW_SRCS "")
if(CONFIG_NODE_TEST_FAKE_HW)
  set(FAKE_HW_SRCS "${S}/drivers/fake/fake_devices.cpp" "${S}/config/fakehw.cpp")
endif()
idf_component_register(
  SRCS
    "${S}/domain/cmd_dedup_cache.cpp"
    "${S}/domain/reading_store.cpp"
    "${S}/domain/pump_interlock.cpp"
    "${S}/domain/fan_temp_interlock.cpp"
    "${S}/domain/sensor_fault_interlock.cpp"
    "${S}/infra/json_writer.cpp"
    "${S}/infra/formats.cpp"
    "${S}/infra/ulid.cpp"
    "${S}/infra/envelope.cpp"
    "${S}/infra/topic.cpp"
    "${S}/infra/cmd_parser.cpp"
    "${S}/app/control_arbiter.cpp"
    "${S}/app/publisher.cpp"
    "${S}/app/event_service.cpp"
    "${S}/app/state_service.cpp"
    "${S}/app/telemetry_service.cpp"
    "${S}/app/command_service.cpp"
    "${S}/app/mode_manager.cpp"
    "${S}/app/manual_control.cpp"
    "${S}/app/rule_engine.cpp"
    "${S}/app/sensor_poller.cpp"
    "${S}/app/ui_service.cpp"
    "${S}/app/connection_manager.cpp"
    "${S}/app/node.cpp"
    "${S}/drivers/bme280.cpp"
    "${S}/drivers/bh1750.cpp"
    "${S}/drivers/ssd1306.cpp"
    "${S}/drivers/sensor_io.cpp"
    "${S}/drivers/ui_io.cpp"
    "${S}/config/hw.cpp"
    ${FAKE_HW_SRCS}
  INCLUDE_DIRS "${S}")

# jsmn は外部のコードなので警告を当てない（ホスト版と同じく SYSTEM で読む）
target_include_directories(${COMPONENT_LIB} SYSTEM PRIVATE "${NODE_JSMN_DIR}")

# 言語と警告（open-questions #95）。ESP-IDF 本体には当てない
include("${CMAKE_CURRENT_LIST_DIR}/../node_flags.cmake")
node_component_flags(${COMPONENT_LIB})

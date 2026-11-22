cmake_minimum_required(VERSION 3.25)

project(mcu_cpp_01
  VERSION 0.1.0
  DESCRIPTION "作品02 モジュール型スマートデバイス制御システム（ホスト版）"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NODE_SANITIZE "ASan・UBSan を付けてビルドする" OFF)
option(NODE_BUILD_TESTS "テストをビルドする" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(warnings)
include(sanitizers)

# 実行ファイルは build/host/mcu_cpp_01 に置く（契約テストの前提。CLAUDE.md）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(NODE_IOTSPEC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/iot-common-spec")
if(NOT EXISTS "${NODE_IOTSPEC_DIR}/schemas/cmd.schema.json")
  message(FATAL_ERROR "external/iot-common-spec がない。git submodule update --init --recursive を実行する")
endif()

include(dependencies)

add_subdirectory(src)

if(NODE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

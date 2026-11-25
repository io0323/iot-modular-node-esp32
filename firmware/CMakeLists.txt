# 作品02 のターゲット版（ESP-IDF v5.5.6、ESP32-S3）。open-questions #94。
# src/ の層を ESP-IDF のコンポーネントとして読み込む（ソースは写さない）：
#   components/node_core            … support・hal・domain・infra・app・drivers（fake・sim を除く）・config の実機用の表
#   components/node_platform_esp32  … src/platform/esp32/
cmake_minimum_required(VERSION 3.16)

# jsmn（cmd の JSON の字句解析）はホスト版と同じコミットに固定して取り込む（cmake/dependencies.cmake と同じ）
include(FetchContent)
FetchContent_Declare(jsmn
  GIT_REPOSITORY https://github.com/zserge/jsmn.git
  GIT_TAG        25647e692c7906b96ffd2b05ca54c097948e879c
  SOURCE_SUBDIR  no-cmake)
FetchContent_MakeAvailable(jsmn)
set(NODE_JSMN_DIR "${jsmn_SOURCE_DIR}" CACHE INTERNAL "jsmn のヘッダの場所")
set(NODE_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/../src" CACHE INTERNAL "作品02 の src/")

# 秘密は sdkconfig.defaults.local（.gitignore。見本は sdkconfig.defaults.local.example）に書き、あれば重ねる（#102）
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
  set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;sdkconfig.defaults.local")
else()
  set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mcu_cpp_01)

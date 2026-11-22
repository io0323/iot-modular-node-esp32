# 依存ライブラリ（FetchContent。版はすべてタグかコミットに固定し、README に書く）
include(FetchContent)

# CMake 4 系で古い cmake_minimum_required を持つ依存も構成できるようにする
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

# --- Eclipse Paho MQTT C（ホスト版の MQTT クライアント。MQTTAsync、TLS なし、静的ライブラリ）
set(PAHO_WITH_SSL OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(PAHO_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(PAHO_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
FetchContent_Declare(paho_mqtt_c
  GIT_REPOSITORY https://github.com/eclipse-paho/paho.mqtt.c.git
  GIT_TAG        v1.3.16
  GIT_SHALLOW    TRUE
  SYSTEM)

# --- jsmn（cmd の JSON の字句解析。ヘッダだけ、ヒープを使わない）
FetchContent_Declare(jsmn
  GIT_REPOSITORY https://github.com/zserge/jsmn.git
  GIT_TAG        25647e692c7906b96ffd2b05ca54c097948e879c
  SOURCE_SUBDIR  no-cmake
  SYSTEM)

FetchContent_MakeAvailable(paho_mqtt_c jsmn)

add_library(jsmn_headers INTERFACE)
target_include_directories(jsmn_headers SYSTEM INTERFACE "${jsmn_SOURCE_DIR}")

add_library(paho_async INTERFACE)
target_link_libraries(paho_async INTERFACE paho-mqtt3a-static)
target_include_directories(paho_async SYSTEM INTERFACE "${paho_mqtt_c_SOURCE_DIR}/src")

# --- GoogleTest（gMock を含む）
if(NODE_BUILD_TESTS)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.18.0
    GIT_SHALLOW    TRUE
    SYSTEM)
  FetchContent_MakeAvailable(googletest)
endif()

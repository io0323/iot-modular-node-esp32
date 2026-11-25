# gcov のカバレッジ（host-coverage プリセットで有効。GCC で使う。gcovr で集計する：tools/quality.sh coverage）
if(NODE_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "NODE_COVERAGE は GCC で使う（tools/quality.sh coverage は品質のイメージの中で動かす）")
  endif()
  add_compile_options(--coverage -O0)
  add_link_options(--coverage)
endif()

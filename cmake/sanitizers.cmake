# ASan・UBSan（host-debug プリセットで有効）。依存ライブラリも同じ設定でビルドする。
# vptr は RTTI を使うので外す（製品コードは -fno-rtti）。
if(NODE_SANITIZE)
  set(NODE_SANITIZER_FLAGS
    -fsanitize=address,undefined
    -fno-sanitize=vptr
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer)
  add_compile_options(${NODE_SANITIZER_FLAGS})
  add_link_options(-fsanitize=address,undefined)
endif()

# ThreadSanitizer（host-tsan プリセットで有効）。依存ライブラリ（Paho）も同じ設定でビルドする。
if(NODE_SANITIZE_THREAD)
  if(NODE_SANITIZE)
    message(FATAL_ERROR "NODE_SANITIZE と NODE_SANITIZE_THREAD は一緒に使えない")
  endif()
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
endif()

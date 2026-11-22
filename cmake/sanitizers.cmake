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

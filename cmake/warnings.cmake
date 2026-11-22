# 警告設定（CLAUDE.md「コードの決まり」）。外部ライブラリには当てない。
set(NODE_WARNING_FLAGS
  -Wall -Wextra -Werror
  -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
  -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference)

# 製品コード：警告＋例外・RTTI なし
function(node_product_target target)
  target_compile_options(${target} PRIVATE ${NODE_WARNING_FLAGS}
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
endfunction()

# テスト：警告だけ（GoogleTest の都合で例外・RTTI は既定のまま）
function(node_test_target target)
  target_compile_options(${target} PRIVATE ${NODE_WARNING_FLAGS})
endfunction()

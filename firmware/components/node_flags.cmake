# 自分のコンポーネントの言語と警告（open-questions #95。CLAUDE.md「コードの決まり」と同じ警告）。
# 例外と RTTI は ESP-IDF の既定（無効）のまま。-std はここで gnu++20 に決める（ESP-IDF v5.5 の既定は gnu++2b）。
function(node_component_flags target)
  target_compile_options(${target} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++20>
    -Wall -Wextra -Werror
    -Wconversion -Wsign-conversion -Wshadow
    $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual>
    -Wnull-dereference)
endfunction()

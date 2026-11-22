#include "platform/host/host_random.hpp"

#include <unistd.h>

#include <cstdlib>
#if defined(__APPLE__)
#include <sys/random.h>
#endif

namespace node::platform {

void HostRandom::fill(std::span<std::uint8_t> out) {
  // getentropy は1回に256バイトまで
  std::size_t done = 0;
  while (done < out.size()) {
    const std::size_t n = (out.size() - done) < 256U ? (out.size() - done) : 256U;
    if (getentropy(out.data() + done, n) != 0) std::abort();  // 乱数が取れないのは続けられない故障
    done += n;
  }
}

}  // namespace node::platform

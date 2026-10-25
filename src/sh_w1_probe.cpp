#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/thread.h>

namespace {

constexpr int kSamples = 8;

uint32_t Load32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return __builtin_bswap32(v);
}

struct ReadRec {
  uint32_t desc = 0;
  uint32_t buf = 0;
  uint32_t size = 0;
  uint32_t pos = 0;
  uint32_t before[kSamples];
};

thread_local ReadRec tls_read;
std::atomic<int> g_logs{0};
std::atomic<uint32_t> g_reads{0};

void Sample(uint8_t* base, uint32_t buf, uint32_t size, uint32_t* out) {
  for (int i = 0; i < kSamples; ++i) {
    out[i] = Load32(base, buf + ((size / kSamples) * i & ~3u));
  }
}

}  // namespace

REX_HOOK_RAW(__imp__sub_8290D7D8);
REX_HOOK_RAW(__imp__sub_8290D890);

REX_HOOK_RAW(sub_8290D7D8) {
  const uint32_t streamer = ctx.r3.u32;
  const uint32_t state = ctx.r4.u32;
  const uint32_t desc = Load32(base, state + 4);
  auto& r = tls_read;
  r.desc = desc;
  r.buf = desc ? Load32(base, desc + 8) : 0;
  r.size = Load32(base, streamer + 280);
  r.pos = Load32(base, state + 40);
  if (r.buf && r.size >= 0x1000) Sample(base, r.buf, r.size, r.before);
  __imp__sub_8290D7D8(ctx, base);
}

REX_HOOK_RAW(sub_8290D890) {
  const uint32_t streamer = ctx.r3.u32;
  const uint32_t state = ctx.r4.u32;
  __imp__sub_8290D890(ctx, base);
  g_reads.fetch_add(1, std::memory_order_relaxed);
  const uint32_t desc = Load32(base, state + 8);
  auto& r = tls_read;
  if (!desc || desc != r.desc || !r.buf || r.size < 0x1000) return;
  const uint32_t got = Load32(base, desc + 0);
  if (got < r.size) return;
  uint32_t after[kSamples];
  Sample(base, r.buf, r.size, after);
  bool same = true, uniform = true;
  for (int i = 0; i < kSamples; ++i) {
    same &= after[i] == r.before[i];
    uniform &= after[i] == after[0];
  }
  if (same && !uniform && g_logs.fetch_add(1) < 50) {
    REXLOG_ERROR(
        "[w1] read completed but buffer unchanged: streamer {:08X} desc {:08X} buf {:08X} pos {:08X} "
        "size {:X} got {:X} compressed {} read#{} t{}",
        streamer, desc, r.buf, r.pos, r.size, got, base[streamer + 265],
        g_reads.load(), rex::thread::current_thread_id());
  }
}

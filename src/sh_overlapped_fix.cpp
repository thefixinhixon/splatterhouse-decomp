// Splatterhouse (2010) - stale-signal fix for XAPI GetOverlappedResult.
//
// The NIF streaming I/O thread (sub_8290DB88) reads with ReadFile and an
// OVERLAPPED whose hEvent is 0, so GetOverlappedResult (sub_829F0958) waits on
// the handle itself: a manual-reset event owned by the XCTD layer. ReadFile
// clears it (NtClearEvent in sub_829F2370) and the XCTD worker
// (sub_829F3178) writes the IOSB and then sets it. If the I/O thread sees a
// request already complete, skips the wait and issues the next read before
// the worker's NtSetEvent lands, the event stays signalled for the new request
// and GetOverlappedResult returns success with 0 bytes while it is still
// pending. From then on every completion is matched to the wrong request and
// the reader gets a descriptor whose buffer still holds the previous 64 KB
// block (garbage counts in .nif files -> 512 MB allocation -> null write and
// freeze while loading). On the console the worker is never preempted by the
// I/O thread in that window; on a PC it is, most often with thread affinities
// honoured.
//
// A wake-up with the status still pending is never a real completion: clear
// the stale signal and call GetOverlappedResult again. It re-reads the status
// first (the worker writes it before setting the event), so a completion that
// lands around the reset is not lost.

#include <atomic>
#include <cstdint>
#include <thread>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xevent.h>

namespace {
constexpr uint32_t kStatusPending = 0x103;
std::atomic<uint32_t> g_stale{0};
}  // namespace

REX_HOOK_RAW(__imp__sub_829F0958);

REX_HOOK_RAW(sub_829F0958) {
  const uint64_t r3 = ctx.r3.u64, r4 = ctx.r4.u64, r5 = ctx.r5.u64, r6 = ctx.r6.u64;
  const uint32_t overlapped = ctx.r4.u32;
  auto* status = reinterpret_cast<uint32_t*>(base + overlapped);
  for (;;) {
    ctx.r3.u64 = r3;
    ctx.r4.u64 = r4;
    ctx.r5.u64 = r5;
    ctx.r6.u64 = r6;
    __imp__sub_829F0958(ctx, base);
    if (ctx.r3.u32 == 0 ||
        __builtin_bswap32(__atomic_load_n(status, __ATOMIC_ACQUIRE)) != kStatusPending) {
      return;
    }
    const uint32_t event = __builtin_bswap32(*reinterpret_cast<uint32_t*>(base + overlapped + 16));
    auto ev = REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEvent>(event ? event : uint32_t(r3));
    if (ev) {
      ev->Reset();
    } else {
      std::this_thread::yield();
    }
    const uint32_t n = g_stale.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((n & (n - 1)) == 0) {
      REXLOG_WARN("[sh-ovl] GetOverlappedResult woke on a stale signal with the request still pending ({} so far)",
                  n);
    }
  }
}

// Splatterhouse (2010) - race fix for the XAPI XCTD decompression cache.
//
// The cache object keeps a lock at +16, a "no readers" event at +44 and a
// reader count at +48. Readers (sub_829F4918/sub_829F4C80) increment the count
// under the lock and clear the event on 0->1, but decrement without the lock
// and set the event on 1->0. If a reader's decrement is followed by another
// reader's increment+clear before the first reader's NtSetEvent lands, the
// event ends up signalled while a reader is still copying. The writer
// (sub_829F4B58 -> sub_829F4990) then compacts the cache with memmove under
// that reader, which hands corrupted decompressed data to the engine (garbage
// counts in .nif files -> 512 MB allocation -> null write and freeze while
// loading). The window is tiny on the console but easy to hit on a PC.
//
// sub_829F4990 returns holding the lock, so no new reader can start; waiting
// here until the count is really zero closes the race.

#include <thread>

#include <rex/hook.h>
#include <rex/ppc.h>

REX_HOOK_RAW(__imp__sub_829F4990);

REX_HOOK_RAW(sub_829F4990) {
  const uint32_t cache = ctx.r3.u32;
  __imp__sub_829F4990(ctx, base);
  auto* reader_count = reinterpret_cast<uint32_t*>(base + cache + 48);
  while (__atomic_load_n(reader_count, __ATOMIC_ACQUIRE) != 0) {
    std::this_thread::yield();
  }
}

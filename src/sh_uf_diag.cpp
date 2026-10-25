// Splatterhouse (2010) - defensive fix for the "Call to invalid or
// unregistered function at 0x00000000" crash (transition to chapter 6).
//
// Chain resolved with llvm-symbolizer --relative-address:
//   sub_82917918 -> sub_82366CC0
// In sub_82366CC0:
//   lwz r11,48(r4) ; mtctr r11 ; bctrl      (virtual call through [obj+48])
//
// The object arrives CORRECTLY constructed (valid vtable 0x82058AE8) but with
// [obj+48] == 0. That slot is zeroed by the constructor sub_82347100
// (recomp.113.cpp) and the "expected" value would be 0x822BE680, a base virtual
// method that is a no-op (blr). On this path the callback is never assigned, so
// the indirect call ends up jumping to 0x00000000.
//
// In the original this flow isn't reached with a half-initialized object (there
// is a LevelState/LevelManager that doesn't exist yet). This override replicates
// the defensive behavior of the rest of the engine: if the slot is 0, skip the call.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc.h>
#include <rex/rex_app.h>
#include <rex/system/xmemory.h>

REX_HOOK_RAW(__imp__sub_82366CC0);

namespace {

uint32_t GuestLoadU32Offset(rex::memory::Memory* memory, uint32_t addr, uint32_t off) {
  if (!memory || !addr) {
    return 0;
  }
  const uint8_t* p = memory->TranslateVirtual(addr + off);
  if (!p) {
    return 0;
  }
  return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(p));
}

}  // namespace

REX_HOOK_RAW(sub_82366CC0) {
  auto* memory = rex::Runtime::instance()->memory();
  const uint32_t obj = ctx.r4.u32;
  if (!GuestLoadU32Offset(memory, obj, 48)) {
    // Uninitialized virtual slot: skip the call instead of bctrl to 0.
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXLOG_WARN(
          "[sh-uf] sub_82366CC0: [obj+48]==0 (obj=0x{:08X}); skipping the virtual call", obj);
    }
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_82366CC0(ctx, base);
}

// Splatterhouse (2010) - 60 FPS patch (equivalent to the Xenia "60 FPS" patch
// for title 4E4D07F0, author illusion).
//
// The Xenia patch writes three places: two floats (0x82F8FF58/5C: 1/15 and 1/30
// -> 1/60, applied in SplatterhouseApp::ApplyGuestMemoryPatches) and one
// instruction at 0x8247A4AC (stb r30,202(r31) -> stb r11,202(r31)).
//
// Recompiled functions are emitted as weak symbols (DEFINE_REX_FUNC), so this
// file replaces sub_8247A368 with a strong version that calls the original
// (__imp__sub_8247A368) and then replicates the patched behavior of field +202.
// That way the patch survives a regeneration of `generated/`.

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc.h>
#include <rex/rex_app.h>

REXCVAR_DECLARE(bool, sh_60fps);

REX_HOOK_RAW(__imp__sub_8247A368);

REX_HOOK_RAW(sub_8247A368) {
  const uint32_t object = ctx.r3.u32;
  const uint8_t arg_byte = uint8_t(ctx.r7.u32);
  __imp__sub_8247A368(ctx, base);
  if (!REXCVAR_GET(sh_60fps)) {
    return;
  }
  if (auto* memory = rex::Runtime::instance()->memory()) {
    memory->TranslateVirtual(object + 202)[0] = arg_byte;
  }
}

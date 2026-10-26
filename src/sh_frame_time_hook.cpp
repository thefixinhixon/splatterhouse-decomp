// Splatterhouse (2010) - real-time frame step for the 60 FPS patch.
//
// sub_82E76B98 is the per-frame clock update. It takes the game's nominal frame
// time (one presentation interval, so 1/60 with the patch, not a measurement)
// and clamps it to [timer+0, timer+4] of the frame timer at GameState+80, then
// copies the result into the global frame-time block every system reads. The
// bounds are seeded from 0x82F8FF5C (1/30) and 0x82F8FF58 (1/15); the 60 FPS
// patch sets both to 1/60, so the step is always 1/60 and every frame that
// misses a vblank slows the whole game down (40 FPS = 2/3 speed).
//
// Before each update this sets the lower bound to the real time since the
// previous update, rounded to whole 60 Hz vblanks (the guest presents on
// vblanks), and the upper bound to 4 vblanks (the game's original 1/15). At a
// steady 60 FPS the step stays exactly 1/60.

#include <algorithm>
#include <chrono>
#include <cmath>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/memory.h>
#include <rex/ppc.h>
#include <rex/rex_app.h>
#include <rex/system/xmemory.h>

REXCVAR_DECLARE(bool, sh_60fps);
REXCVAR_DECLARE(bool, sh_60fps_realtime);

namespace {

constexpr uint32_t kGameStatePointer = 0x82FFA3AC;
constexpr uint32_t kMinStepOffset = 80 + 0;
constexpr uint32_t kMaxStepOffset = 80 + 4;
constexpr double kMaxVblanks = 4.0;
constexpr double kVblankSeconds = 1.0 / 60.0;

}  // namespace

REX_HOOK_RAW(__imp__sub_82E76B98);

REX_HOOK_RAW(sub_82E76B98) {
  static std::chrono::steady_clock::time_point last;
  auto now = std::chrono::steady_clock::now();
  if (REXCVAR_GET(sh_60fps) && REXCVAR_GET(sh_60fps_realtime) &&
      last != std::chrono::steady_clock::time_point{}) {
    auto* memory = rex::Runtime::instance()->memory();
    uint32_t game_state =
        rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual<const void*>(kGameStatePointer));
    if (game_state) {
      double elapsed = std::chrono::duration<double>(now - last).count();
      double vblanks = std::clamp(std::round(elapsed / kVblankSeconds), 1.0, kMaxVblanks);
      rex::memory::store_and_swap<float>(memory->TranslateVirtual(game_state + kMinStepOffset),
                                         static_cast<float>(vblanks * kVblankSeconds));
      rex::memory::store_and_swap<float>(memory->TranslateVirtual(game_state + kMaxStepOffset),
                                         static_cast<float>(kMaxVblanks * kVblankSeconds));
    }
  }
  last = now;
  __imp__sub_82E76B98(ctx, base);
}

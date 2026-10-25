// Splatterhouse (2010) - dev level warp (sh_warp).
//
// The level request lives in the game-flow object (*(0x831F6ED4)). Its per-frame
// update sub_82EF50A8 calls sub_82EF40C8(flow, index) for the level index stored
// at [flow+152], which is what the frontend writes when the player picks a
// chapter. sub_82EF40C8 takes the level entry [flow+232][index] (each entry
// carries its inline name at +48) and starts the load.
//
// The original implementation of this dev tool lived in a private patch of the
// runtime's VdSwap_entry, which is not in this SDK. Port-side equivalent: the
// guest present function sub_822551A0 (the caller of VdSwap) runs once per
// frame on the guest thread, so the request is issued there with the same
// thread context the game itself uses, and it waits until the flow reaches the
// frontend state (a request made during the boot sequence hangs the game).

#include <cctype>
#include <chrono>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc.h>
#include <rex/rex_app.h>
#include <rex/system/xmemory.h>

REXCVAR_DECLARE(std::string, sh_warp);

REX_EXTERN(__imp__sub_822551A0);

namespace {

constexpr uint32_t kFlowGlobal = 0x831F6ED4;
constexpr uint32_t kRequestedLevelOffset = 152;
constexpr uint32_t kLevelArrayOffset = 232;
constexpr uint32_t kLevelNameOffset = 48;
constexpr uint32_t kFlowStateOffset = 352;
constexpr int32_t kFrontendFlowState = 3;
constexpr int kMaxLevels = 256;

uint32_t ReadU32(uint32_t address) {
  auto* memory = rex::Runtime::instance()->memory();
  if (!memory) {
    return 0;
  }
  return rex::memory::load_and_swap<uint32_t>(memory->TranslateVirtual<const void*>(address));
}

uint32_t LoadGlobal(uint32_t address) { return ReadU32(address); }

std::string LevelEntryName(uint32_t entry) {
  auto* memory = rex::Runtime::instance()->memory();
  if (!memory || !entry) {
    return {};
  }
  const uint32_t nested = ReadU32(entry + 836);
  const uint32_t name =
      nested ? ReadU32(nested + 524) + kLevelNameOffset : entry + kLevelNameOffset;
  const char* text = memory->TranslateVirtual<const char*>(name);
  return text ? std::string(text) : std::string();
}

// The game compares level names with sub_82EEBC30: case-insensitive and with
// spaces treated as underscores.
std::string NormalizeName(const std::string& name) {
  std::string normalized;
  normalized.reserve(name.size());
  for (const char c : name) {
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    normalized.push_back(lower == ' ' ? '_' : lower);
  }
  return normalized;
}

// Level entries are named "lvl<chapter>_act<n>_<chapter name>"
// ("lvl2_act1_shanty") while the chapters the game exposes use "lvl2_shanty",
// so a chapter match picks its first act. 0 = no match, 1 = chapter or exact
// match, 2 = the entry contains the requested name.
int MatchNormalizedName(const std::string& entry, const std::string& wanted) {
  if (entry == wanted) {
    return 1;
  }
  const size_t separator = wanted.find('_');
  if (separator != std::string::npos) {
    const std::string chapter = wanted.substr(0, separator);
    const std::string place = wanted.substr(separator + 1);
    if (entry.rfind(chapter + '_', 0) == 0 && entry.size() > place.size() &&
        entry.compare(entry.size() - place.size() - 1, place.size() + 1, '_' + place) == 0) {
      return 1;
    }
  }
  return entry.find(wanted) != std::string::npos ? 2 : 0;
}

void RequestWarp() {
  const std::string level = rex::cvar::Query<std::string>("sh_warp");
  if (level.empty()) {
    return;
  }
  static auto last_attempt = std::chrono::steady_clock::now() - std::chrono::seconds(1);
  static std::string last_level;
  const auto now = std::chrono::steady_clock::now();
  if (level == last_level && now - last_attempt < std::chrono::seconds(1)) {
    return;
  }
  last_level = level;
  last_attempt = now;

  auto* memory = rex::Runtime::instance()->memory();
  if (!memory) {
    return;
  }
  const uint32_t flow = LoadGlobal(kFlowGlobal);
  if (!flow) {
    return;
  }
  const int32_t state = static_cast<int32_t>(ReadU32(flow + kFlowStateOffset));
  if (state < kFrontendFlowState) {
    return;
  }
  const uint32_t array = ReadU32(flow + kLevelArrayOffset);
  if (!array) {
    return;
  }

  const std::string wanted = NormalizeName(level);
  int index = -1;
  int fallback_index = -1;
  std::string matched;
  std::string fallback;
  for (int i = 0; i < kMaxLevels; ++i) {
    const uint32_t entry = ReadU32(array + i * 4);
    if (!entry) {
      continue;
    }
    if (entry < 0x40000000 || entry >= 0x80000000 || (entry & 3)) {
      break;
    }
    const std::string name = LevelEntryName(entry);
    const int match = MatchNormalizedName(NormalizeName(name), wanted);
    if (match == 1) {
      index = i;
      matched = name;
      break;
    }
    if (match == 2 && fallback_index < 0) {
      fallback_index = i;
      fallback = name;
    }
  }
  if (index < 0 && fallback_index >= 0) {
    index = fallback_index;
    matched = fallback;
  }

  static std::string reported;
  if (index < 0) {
    if (reported != level) {
      reported = level;
      REXLOG_WARN("[sh-warp] '{}' is not in the level list, retrying", level);
    }
    return;
  }
  reported.clear();

  rex::memory::store_and_swap<uint32_t>(
      memory->TranslateVirtual<void*>(flow + kRequestedLevelOffset),
      static_cast<uint32_t>(index));
  rex::cvar::SetFlagByName("sh_warp", "");
  REXLOG_INFO("[sh-warp] requested level '{}' -> '{}' (index {})", level, matched, index);
}

}  // namespace

REX_HOOK_RAW(sub_822551A0) {
  __imp__sub_822551A0(ctx, base);
  RequestWarp();
}

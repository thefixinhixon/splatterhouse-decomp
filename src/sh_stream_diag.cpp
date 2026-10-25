#include <cstdio>
#include <cstdlib>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/thread.h>

namespace {

thread_local uint32_t tls_owner = 0;
thread_local uint32_t tls_stream = 0;
thread_local uint32_t tls_pos = 0;

std::mutex g_mutex;
std::unordered_map<uint32_t, std::vector<uint8_t>> g_data;
std::unordered_map<uint32_t, std::vector<uint8_t>> g_mask;

uint32_t Load32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return __builtin_bswap32(v);
}

std::string Dump(uint8_t* base, uint32_t addr, uint32_t len) {
  std::string out;
  if (addr < 0x1000) {
    return "<null>";
  }
  for (uint32_t i = 0; i < len; i += 4) {
    out += fmt::format("{:08X} ", Load32(base, addr + i));
  }
  return out;
}

std::string OwnerPath(uint8_t* base, uint32_t owner) {
  std::string path;
  for (uint32_t i = 0; i < 260; ++i) {
    char c = char(base[owner + 28 + i]);
    if (!c) break;
    path += c;
  }
  return path;
}

std::string BaseName(const std::string& path) {
  auto p = path.find_last_of("\\/");
  return p == std::string::npos ? path : path.substr(p + 1);
}

void WriteCapture(uint32_t file, const std::string& name, const char* tag) {
  const char* dir = std::getenv("SH_NIF_DUMP_DIR");
  if (!dir) return;
  static std::unordered_map<std::string, size_t> written;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_data.find(file);
  if (it == g_data.end()) return;
  const std::string key = name + tag;
  if (it->second.size() <= written[key] + (1u << 20) && std::string(tag) == "ref") return;
  written[key] = it->second.size();
  for (int k = 0; k < 2; ++k) {
    const auto& buf = k == 0 ? it->second : g_mask[file];
    const std::string path = fmt::format("{}/{}_{}.{}", dir, name, tag, k == 0 ? "bin" : "mask");
    std::ofstream(path + ".tmp", std::ios::binary)
        .write(reinterpret_cast<const char*>(buf.data()), buf.size());
    std::rename((path + ".tmp").c_str(), path.c_str());
  }
}

struct ReqState {
  int state = 0;
  uint32_t thread = 0;
  uint64_t seq = 0;
};
std::mutex g_req_mutex;
std::unordered_map<uint32_t, ReqState> g_req;
uint64_t g_seq = 0;
const char* kStateName[] = {"new", "free", "filled", "reading", "io-taken"};

}  // namespace

REX_HOOK_RAW(__imp__sub_82991488);
REX_HOOK_RAW(__imp__sub_822A4178);
REX_HOOK_RAW(__imp__sub_822A4580);
REX_HOOK_RAW(__imp__sub_8233D480);
REX_HOOK_RAW(__imp__sub_8247FF20);

REX_HOOK_RAW(sub_82991488) {
  const uint32_t file = ctx.r3.u32;
  const uint32_t dst = ctx.r4.u32;
  const bool at_start = Load32(base, file + 16) == 0 && Load32(base, file + 12) == 0;
  __imp__sub_82991488(ctx, base);
  const uint32_t n = ctx.r3.u32;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto& v = g_data[file];
  auto& m = g_mask[file];
  if (at_start) {
    v.clear();
    m.clear();
  }
  const uint32_t pos = uint32_t(v.size());
  if (n == 0 || n > 0x4000000 || pos > 0x8000000) return;
  if (v.size() < pos + n) {
    v.resize(pos + n);
    m.resize(pos + n);
  }
  std::memcpy(v.data() + pos, base + dst, n);
  std::memset(m.data() + pos, 1, n);
}

REX_HOOK_RAW(sub_8233D480) {
  const uint32_t prev_owner = tls_owner, prev_stream = tls_stream, prev_pos = tls_pos;
  const uint32_t owner = ctx.r3.u32;
  tls_owner = owner;
  tls_stream = Load32(base, owner + 392);
  tls_pos = tls_stream ? Load32(base, tls_stream + 4) : 0;
  const uint32_t stream = tls_stream;
  __imp__sub_8233D480(ctx, base);
  if (const char* ref = std::getenv("SH_NIF_REF")) {
    const std::string name = BaseName(OwnerPath(base, owner));
    if (name == ref) {
      WriteCapture(Load32(base, stream + 16), name, "ref");
    }
  }
  tls_owner = prev_owner;
  tls_stream = prev_stream;
  tls_pos = prev_pos;
}

REX_HOOK_RAW(sub_8247FF20) {
  if (ctx.r4.u32 >= 0x01000000 && tls_owner) {
    const uint32_t stream = tls_stream;
    const std::string path = OwnerPath(base, tls_owner);
    REXLOG_ERROR("[sh-stream] huge vector reserve {:08X} entries in '{}', block start {:08X}, now {:08X}",
                 ctx.r4.u32, path, tls_pos, stream ? Load32(base, stream + 4) : 0);
    REXLOG_ERROR("[sh-stream] stream {:08X}: {}", stream, Dump(base, stream, 32));
    WriteCapture(stream ? Load32(base, stream + 16) : 0, BaseName(path), "bad");
    tls_owner = 0;
  }
  __imp__sub_8247FF20(ctx, base);
}

namespace {
struct BufHash {
  uint64_t last = 0;
  uint64_t prev = 0;
  uint32_t size = 0;
};
std::unordered_map<uint32_t, BufHash> g_hash;

uint64_t HashBuf(uint8_t* base, uint32_t v, uint32_t* out_size) {
  const uint32_t size = Load32(base, v + 0);
  const uint32_t buf = Load32(base, v + 8);
  *out_size = size;
  if (!size || size > 0x100000 || buf < 0x40000000 || buf >= 0xA0000000) return 0;
  uint64_t h = 1469598103934665603ull;
  for (uint32_t i = 0; i < size; ++i) h = (h ^ base[buf + i]) * 1099511628211ull;
  return h;
}

std::mutex g_q_mutex;
std::unordered_map<uint32_t, uint32_t> g_where;
std::atomic<int> g_qlog{0};
}

REX_HOOK_RAW(sub_822A4178) {
  const uint32_t q = ctx.r3.u32, v = ctx.r4.u32;
  if (v >= 0x40000000 && v < 0x80000000) {
    std::lock_guard<std::mutex> lock(g_q_mutex);
    auto& where = g_where[v];
    if (where && g_qlog.fetch_add(1) < 200) {
      REXLOG_ERROR("[sh-q] push of {:08X} to {:08X} while still in {:08X}", v, q, where);
    }
    where = q;
    if (false) {
      uint32_t size = 0;
      const uint64_t h = HashBuf(base, v, &size);
      auto& bh = g_hash[v];
      if (h && h == bh.last && size == bh.size) {
        REXLOG_ERROR("[sh-buf] push of {:08X} to {:08X} with unchanged content ({} bytes)", v, q, size);
      }
      bh.prev = bh.last;
      bh.last = h;
      bh.size = size;
    }
  }
  __imp__sub_822A4178(ctx, base);
}

REX_HOOK_RAW(sub_822A4580) {
  const uint32_t q = ctx.r3.u32, out = ctx.r4.u32;
  __imp__sub_822A4580(ctx, base);
  if (ctx.r3.u32 != 0 || !out) return;
  const uint32_t v = Load32(base, out);
  if (v < 0x40000000 || v >= 0x80000000) return;
  std::lock_guard<std::mutex> lock(g_q_mutex);
  auto& where = g_where[v];
  if (where != q && g_qlog.fetch_add(1) < 200) {
    REXLOG_ERROR("[sh-q] pop of {:08X} from {:08X} but it is in {:08X}", v, q, where);
  }
  where = 0;
  {
    const uint32_t size = Load32(base, v + 0);
    const uint32_t buf = Load32(base, v + 8);
    if (size == 0x10000 && buf >= 0x40000000 && buf < 0xA0000000) {
      static std::unordered_map<uint32_t, std::array<uint8_t, 64>> last_sig;
      static std::unordered_map<uint32_t, uint32_t> last_desc;
      std::array<uint8_t, 64> sig;
      std::memcpy(sig.data(), base + buf, 64);
      auto it = last_sig.find(q);
      bool uniform = true;
      for (int k = 1; k < 64; ++k) uniform &= sig[k] == sig[0];
      if (!uniform && it != last_sig.end() && it->second == sig && last_desc[q] != v) {
        REXLOG_ERROR("[sh-stale] q {:08X} popped {:08X} (buf {:08X}) with the same first 64 bytes as the previous {:08X}: {}",
                     q, v, buf, last_desc[q], Dump(base, buf, 16));
      }
      last_sig[q] = sig;
      last_desc[q] = v;
    }
  }
  if (false) {
    uint32_t size = 0;
    const uint64_t h = HashBuf(base, v, &size);
    auto& bh = g_hash[v];
    if (h != bh.last) {
      REXLOG_ERROR("[sh-buf] pop of {:08X} from {:08X}: content changed since push ({} bytes, matches previous fill: {})",
                   v, q, size, h == bh.prev);
    }
  }
}

REX_HOOK_RAW(__imp__sub_8290D9B8);

namespace {
std::mutex g_inf_mutex;
std::unordered_map<uint32_t, uint32_t> g_inflating;
std::atomic<int> g_d1b8_log{0};
}

namespace {
std::unordered_map<uint32_t, uint64_t> g_last_input;
}

REX_HOOK_RAW(sub_8290D9B8) {
  const uint32_t strm = ctx.r4.u32;
  const uint32_t me = rex::thread::current_thread_id();
  {
    std::lock_guard<std::mutex> lock(g_inf_mutex);
    auto& owner = g_inflating[strm];
    if (owner) {
      REXLOG_ERROR("[sh-io] concurrent inflate on stream {:08X}: t{} while t{}", strm, me, owner);
    }
    owner = me;
  }
  __imp__sub_8290D9B8(ctx, base);
  std::lock_guard<std::mutex> lock(g_inf_mutex);
  g_inflating[strm] = 0;
}

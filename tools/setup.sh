#!/usr/bin/env bash
set -e
echo "[setup] Splatterhouse Recompiled - Linux setup"
command -v git >/dev/null || { echo "git not found"; exit 1; }
command -v cmake >/dev/null || { echo "cmake not found"; exit 1; }
command -v ninja >/dev/null || echo "[warn] ninja not found, install ninja-build"
if command -v clang >/dev/null; then
  echo "[ok] clang $(clang --version | head -1)"
  v=$(clang --version | head -1 | sed -n 's/.*version \([0-9]\+\)\..*/\1/p')
  if [ "$v" -lt 18 ]; then
    echo "[warn] ReXGlue requires Clang 18+ (install llvm-20 or newer)"
  fi
else
  echo "[warn] ReXGlue requires Clang 18+ (apt install clang libgtk-3-dev)"
fi
if [ -d .git ]; then
  git submodule update --init extern/rexglue-sdk
  echo "[ok] rexglue-sdk"
fi
echo "To build:"
echo "  cmake --preset linux-amd64-relwithdebinfo && cmake --build out/build/linux-amd64-relwithdebinfo -j\$(nproc)"
echo "To run (Vulkan, no splatterhouse.toml needed):"
echo "  cd out/build/linux-amd64-relwithdebinfo && ./splatterhouse"

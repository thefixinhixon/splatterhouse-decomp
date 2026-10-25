# GPU — Xenos -> Host

Xbox 360 GPU: Xenos (ATI), D3D9-like with a ring command buffer + microcode shaders.

## Now with ReXGlue (v0.2.0)

The SDK already includes a complete Xenos graphics system (`rexgpu-xenos` plugin: command processor, DXBC/SPIR-V shader translator, texture/render target caches, D3D12 and Vulkan backends). It is enabled with `rexglue_setup_target(splatterhouse GPU_PLUGINS xenos)` — with that, a custom D3D9 HLE is **not needed**. The stub D3D12 backend we wrote was left in `legacy/src/gpu/`.

## Fallback strategy (D3D9 HLE)

Splatterhouse (UE3) uses the XDK's `d3d9.dll`. If the Xenos plugin doesn't pan out, there is still the option of:

- Intercepting `D3D9Create`, `CreateDevice`, `DrawIndexedPrimitive`, `SetTexture`, `SetVertexShader`, etc.
- Re-emitting to D3D12/Vulkan with a wrapper (similar to `D3D9On12` or `dxvk`).

## Strategy for this port

Splatterhouse (UE3) uses the XDK's `d3d9.dll`. That's good news: there's no need to emulate all of Xenos, HLE-ing D3D9 is enough:

- Intercept `D3D9Create`, `CreateDevice`, `DrawIndexedPrimitive`, `SetTexture`, `SetVertexShader`, etc.
- Re-emit to D3D12/Vulkan with a wrapper (similar to `D3D9On12` or `dxvk`).

Alternative if fidelity is wanted: use Xenia's command processor (`xenia/gpu/`).

## GPU roadmap

1. **Bootstrap**: empty SDL3 window (done).
2. **D3D9 HLE stubs**: log calls and return a dummy device.
3. **D3D12 backend**: create device/swapchain (`src/gpu/d3d12_backend.cpp`).
4. **Shader translator**: Xenos microcode -> DXIL/SPIR-V (see the `XenonUtils`/`xenia` shader translator).
5. **Texture/format swizzle**: Xenos tiling -> linear.

## References

- Xenia `xenia/gpu/xenos/` and `xenia/ui/d3d12/`
- hedge-dev `XenonUtils` GPU helpers (if they exist)
- RenderDoc to capture game frames in Xenia and compare.

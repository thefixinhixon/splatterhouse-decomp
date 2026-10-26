// splatterhouse - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#if defined(_WIN32)
#include <cstdio>
#include <crtdbg.h>
#include <Windows.h>
#endif
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <rex/cvar.h>
#include <rex/rex_app.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/filesystem.h>
#include <rex/system.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xam/content_manager.h>
#include <rex/system/xmemory.h>
#include <rex/ui/keybinds.h>

#include <fmt/format.h>

#include "graphics_menu.h"

REXCVAR_DECLARE(bool, dump_guest);
REXCVAR_DECLARE(bool, sh_60fps);
REXCVAR_DECLARE(bool, sh_graphics_menu);
REXCVAR_DECLARE(bool, sh_skip_assets_check);
REXCVAR_DECLARE(std::string, sh_aspect);

class SplatterhouseApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<SplatterhouseApp>(new SplatterhouseApp(ctx, "splatterhouse",
        PPCImageConfig));
  }

  // Windows console attached when the app launches: shows the live log.
  void OnPostInitLogging() override {
#if defined(_WIN32)
    if (AllocConsole()) {
      FILE* f = nullptr;
      if (freopen_s(&f, "CONOUT$", "w", stdout) != 0) f = nullptr;
      f = nullptr;
      freopen_s(&f, "CONOUT$", "w", stderr);
      f = nullptr;
      freopen_s(&f, "CONIN$", "r", stdin);
      SetConsoleTitleA("Splatterhouse - Log");
      // With unbuffered stderr, the log appears in real time.
      setvbuf(stdout, nullptr, _IONBF, 0);
      setvbuf(stderr, nullptr, _IONBF, 0);
      // CRT asserts: redirected to a file (no modal MessageBox).
      // Without this an assert aborts in a modal and hangs the XEX load.
      _set_abort_behavior(0, _CALL_REPORTFAULT);
      FILE* crt = nullptr;
      freopen_s(&crt, "crt_report.txt", "w", stderr);
      if (crt) {
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, crt);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, crt);
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_WARN, crt);
      }
      // With unbuffered stderr, the log appears in real time.
      setvbuf(stdout, nullptr, _IONBF, 0);
      setvbuf(stderr, nullptr, _IONBF, 0);
    }
#endif
    REXLOG_INFO("Log console attached");
  }

  // Safe defaults if the user hasn't set them in splatterhouse.toml.
  // Without gpu_plugin there is no GPU (black screen); without "rov" the
  // lighting is wrong (the RTV path approximates the X360 eDRAM).
  bool IsVulkanBackend() const {
#if defined(_WIN32)
    return rex::cvar::Query<std::string>("gpu_backend") == "vulkan";
#else
    return true;
#endif
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }
    const bool vulkan = IsVulkanBackend();
    if (vulkan) {
      REXLOG_INFO("[sh-perf] Vulkan backend selected (gpu_backend=vulkan)");
    }
    // NOTE: `render_target_path_vulkan` / `render_target_path_d3d12` are
    // registered by the GPU plugin, which loads AFTER OnPreSetup; here they would
    // be a no-op. They are set in OnFinalizePaths.
    if (!rex::cvar::HasNonDefaultValue("video_mode_refresh_rate")) {
      rex::cvar::SetFlagByName("video_mode_refresh_rate", "60");
    }
  }

  // Runs AFTER the GPU plugin is loaded (cvars already registered) and BEFORE
  // the runtime is built (GPU init / render target cache). It's the right place
  // to set the render target path, which in OnPreSetup would be a no-op because
  // the plugin hasn't been loaded yet.
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override {
    (void)resume;
    // Equivalent to ROV on D3D12: the host render targets path approximates the
    // X360 eDRAM and leaves the scene almost black (the logos aren't visible).
    // The FSI (fragment shader interlock) path emulates it properly.
    if (IsVulkanBackend()) {
      if (!rex::cvar::HasNonDefaultValue("render_target_path_vulkan")) {
        rex::cvar::SetFlagByName("render_target_path_vulkan", "fsi");
      }
      if (!rex::cvar::HasNonDefaultValue("vulkan_async_skip_incomplete_frames")) {
        rex::cvar::SetFlagByName("vulkan_async_skip_incomplete_frames", "false");
      }
    } else if (!rex::cvar::HasNonDefaultValue("render_target_path_d3d12")) {
      rex::cvar::SetFlagByName("render_target_path_d3d12", "rov");
    }
    // The SDK default (true) drops every CPU-uploaded page at the end of each
    // frame, so all vertex/index data is uploaded again every frame and each
    // upload ends the render pass (combat fell to ~20 FPS). Xenia defaults it to
    // false; guest writes are still caught by the page watches.
    if (!rex::cvar::HasNonDefaultValue("clear_memory_page_state")) {
      rex::cvar::SetFlagByName("clear_memory_page_state", "false");
    }
    return defaults;
  }

  // `vsync` and `d3d12_present_frame_limiter*` are registered by the GPU plugin,
  // which loads AFTER OnPreSetup, so they don't exist there yet. OnPostSetup
  // runs once the plugin is loaded (and the vblank worker reads `vsync` live),
  // so here they do apply.
  //
  // VSync off + present frame limiter at 60: a frame that exceeds the budget no
  // longer drops presentation to half vblank (60 -> 30), which is perceived as
  // "stutters"; the limiter caps the pace at 60 without blocking on vblank.
  void OnPostSetup() override {
    const bool vulkan = IsVulkanBackend();
    if (!vulkan) {
      if (!rex::cvar::HasNonDefaultValue("vsync")) {
        rex::cvar::SetFlagByName("vsync", "false");
      }
      if (!rex::cvar::HasNonDefaultValue("d3d12_present_frame_limiter")) {
        rex::cvar::SetFlagByName("d3d12_present_frame_limiter", "true");
      }
      if (!rex::cvar::HasNonDefaultValue("d3d12_present_frame_limiter_fps")) {
        rex::cvar::SetFlagByName("d3d12_present_frame_limiter_fps", "60");
      }
      // Tearing: with the present synced to vblank, a frame that goes over
      // 16.6 ms waits for the next vblank (33 ms) and the game gets stuck at 30
      // instead of degrading gradually. Default strategy:
      //   - `d3d12_allow_variable_refresh_rate_and_tearing=false` (don't always
      //     tear; at 60 FPS the present is synced and there is no tearing),
      //   - `d3d12_present_adaptive_tearing=true` (the presenter passes
      //     ALLOW_TEARING only on frames that miss the budget, so below 60 the
      //     pace drops gradually: 58, 52, 45...).
      // On a VRR/FreeSync monitor the ideal is to set the first one to true (no
      // tearing and no blocking present).
      if (!rex::cvar::HasNonDefaultValue("d3d12_allow_variable_refresh_rate_and_tearing")) {
        rex::cvar::SetFlagByName("d3d12_allow_variable_refresh_rate_and_tearing", "false");
      }
      if (!rex::cvar::HasNonDefaultValue("d3d12_present_adaptive_tearing")) {
        rex::cvar::SetFlagByName("d3d12_present_adaptive_tearing", "true");
      }
    }
    // The guest video mode sets the aspect of what the game renders. On an
    // ultrawide monitor, if it stays at 16:9 the presenter has to add bars on the
    // sides; matching it to the window/screen aspect makes the game render native
    // 21:9 and fill the screen. Only the WIDTH is touched: the height (the actual
    // internal resolution) is the user's choice.
    ApplyDesiredAspect();
    // Safety net: if the toml has a resolution_scale that pushes the render
    // width above the safe limit with ROV (e.g. 2x at 1080p = 3840 px), the GPU
    // saturates and Windows marks it as "not responding". We reduce it here so
    // it doesn't hang on startup.
    {
      const int32_t width = std::max(1, rex::cvar::Query<int32_t>("video_mode_width"));
      const int32_t scale = rex::cvar::Query<int32_t>("resolution_scale");
      const int32_t max_scale = std::max(1, std::min(4, 2560 / width));
      if (scale > max_scale) {
        REXLOG_WARN("[sh-perf] resolution_scale={} with width {} exceeds the limit ({}x{}); clamping to {}",
                    scale, width, width, scale, max_scale);
        rex::cvar::SetFlagByName("resolution_scale", std::to_string(max_scale));
      }
    }
    if (vulkan) {
      REXLOG_INFO("[sh-perf] Vulkan: vsync={} path={} scale={}",
                  rex::cvar::Query<bool>("vsync"),
                  rex::cvar::Query<std::string>("render_target_path_vulkan"),
                  rex::cvar::Query<int32_t>("resolution_scale"));
    } else {
      REXLOG_INFO("[sh-perf] vsync={} limiter={} fps={} tearing={} scale={}",
                  rex::cvar::Query<bool>("vsync"),
                  rex::cvar::Query<bool>("d3d12_present_frame_limiter"),
                  rex::cvar::Query<double>("d3d12_present_frame_limiter_fps"),
                  rex::cvar::Query<bool>("d3d12_allow_variable_refresh_rate_and_tearing"),
                  rex::cvar::Query<int32_t>("resolution_scale"));
    }
    // What's applied here (and what's loaded from the toml) is already active in
    // this session: don't show it as "pending restart" on the F5 screen.
    rex::cvar::ClearPendingRestartFlags();
  }

  // Matches the ASPECT of the guest video mode to the window/screen.
  // Cvar `sh_aspect`: auto (default), 16:9, 21:9, 32:9 or keep (don't touch).
  void ApplyDesiredAspect() {
    std::string mode = rex::cvar::Query<std::string>("sh_aspect");
    if (mode == "keep") {
      return;
    }
    double target_aspect = 0.0;
    if (mode == "16:9") {
      target_aspect = 16.0 / 9.0;
    } else if (mode == "21:9") {
      target_aspect = 21.0 / 9.0;
    } else if (mode == "32:9") {
      target_aspect = 32.0 / 9.0;
    } else {
      // auto: window aspect; if it has no size yet, the desktop's.
      uint32_t width = 0;
      uint32_t height = 0;
      if (window()) {
        width = window()->GetActualPhysicalWidth();
        height = window()->GetActualPhysicalHeight();
      }
#if defined(_WIN32)
      if (!width || !height) {
        width = uint32_t(std::max(1, GetSystemMetrics(SM_CXSCREEN)));
        height = uint32_t(std::max(1, GetSystemMetrics(SM_CYSCREEN)));
      }
#endif
      if (!width || !height) {
        return;
      }
      target_aspect = double(width) / double(height);
    }
    if (target_aspect <= 0.0) {
      return;
    }
    const int32_t width = std::max(1, rex::cvar::Query<int32_t>("video_mode_width"));
    const int32_t height = std::max(1, rex::cvar::Query<int32_t>("video_mode_height"));
    const double current_aspect = double(width) / double(height);
    // Tolerance to avoid rewriting already-equivalent modes (2560x1080 on a 21:9).
    if (std::fabs(current_aspect - target_aspect) < 0.03) {
      return;
    }
    int32_t new_width = int32_t(std::lround(double(height) * target_aspect / 8.0) * 8.0);
    new_width = std::clamp(new_width, 640, 4095);
    if (new_width <= 0) {
      return;
    }
    rex::cvar::SetFlagByName("video_mode_width", std::to_string(new_width));
    rex::cvar::SetFlagByName("video_mode_height", std::to_string(height));
    REXLOG_INFO("[sh-aspect] {}: internal mode {}x{} (was {}x{}, screen {:.2f}:1)", mode,
                new_width, height, width, height, target_aspect);
  }

  // Game data path: the ISO contents (default.xex + data\) next to the exe or
  // in an equivalent assets\.
  // The installer puts the contents in assets\, but these are also accepted:
  // - the exe's own directory (development layout),
  // - an ISO extracted by hand without flattening: assets\<ISO folder>\.
  //
  // The contents are validated before starting. Without this, if assets are
  // missing the guest runs until the UI, can't find localization/SWF and dies
  // with an opaque AV (black screen + exit), which is what issue #4 reports.
  static bool HasGameXex(const std::filesystem::path& root) {
    std::error_code ec;
    return std::filesystem::is_regular_file(root / "default.xex", ec) ||
           std::filesystem::is_regular_file(root / "default.dec.xex", ec);
  }

  static bool LooksLikeGameDataRoot(const std::filesystem::path& root) {
    std::error_code ec;
    return std::filesystem::is_directory(root, ec) && HasGameXex(root) &&
           std::filesystem::is_directory(root / "data", ec);
  }

  static bool HasLocalizationDatabase(const std::filesystem::path& root) {
    std::error_code ec;
    for (const auto& entry :
         std::filesystem::directory_iterator(root / "data" / "localization", ec)) {
      if (ec) {
        return false;
      }
      if (entry.is_regular_file(ec) && entry.path().extension() == ".xml") {
        return true;
      }
    }
    return false;
  }

  // Returns the root that contains the game, or empty if there is none.
  static std::filesystem::path FindGameDataRoot(const std::filesystem::path& explicit_root) {
    if (!explicit_root.empty()) {
      return explicit_root;
    }
    const std::filesystem::path exe_dir = rex::filesystem::GetExecutableFolder();
    const std::filesystem::path assets_dir = exe_dir / "assets";
    for (const std::filesystem::path& candidate : {assets_dir, exe_dir}) {
      if (LooksLikeGameDataRoot(candidate)) {
        return candidate;
      }
    }
    // ISO extracted inside assets without flattening (up to 2 levels).
    std::vector<std::filesystem::path> pending = {assets_dir};
    for (int depth = 0; depth < 2 && !pending.empty(); ++depth) {
      std::vector<std::filesystem::path> next;
      for (const std::filesystem::path& dir : pending) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
          if (ec) {
            break;
          }
          if (!entry.is_directory(ec)) {
            continue;
          }
          if (LooksLikeGameDataRoot(entry.path())) {
            return entry.path();
          }
          next.push_back(entry.path());
        }
      }
      pending = std::move(next);
    }
    return {};
  }

  // Reason the root is unusable, or an empty string if it's complete.
  static std::string ValidateGameDataRoot(const std::filesystem::path& root) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec)) {
      return "the folder does not exist";
    }
    if (!HasGameXex(root)) {
      return "default.xex is missing (the game executable)";
    }
    if (!std::filesystem::is_directory(root / "data", ec)) {
      return "the data\\ folder is missing";
    }
    if (!HasLocalizationDatabase(root)) {
      return "data\\localization\\<language>.xml is missing";
    }
    if (!std::filesystem::is_directory(root / "data" / "swf", ec)) {
      return "data\\swf\\ is missing (the game UI)";
    }
    return {};
  }

  // Shows the problem and exits. Before this the guest started without data and
  // died with "write of guest 0x00000008" (issue #4); a clear message is better.
  [[noreturn]] static void ReportMissingGameData(const std::filesystem::path& root,
                                                 const std::string& problem) {
    // MessageBoxW doesn't wrap long lines (and clips at high DPI): the text is
    // wrapped by hand so the message is always fully readable.
    auto wrap = [](const std::string& text, size_t max_length) {
      std::string wrapped;
      size_t line_length = 0;
      for (const char c : text) {
        if (c == '\n') {
          wrapped += c;
          line_length = 0;
          continue;
        }
        if (line_length >= max_length) {
          wrapped += "\n  ";
          line_length = 2;
        }
        wrapped += c;
        ++line_length;
      }
      return wrapped;
    };
    // MessageBoxW falls short at high DPI (there is no SDL window or DPI
    // awareness yet), so the dialog gets a short summary and the full detail
    // (paths included) is written next to the exe.
    const std::filesystem::path expected = rex::filesystem::GetExecutableFolder() / "assets";
    const std::string detail = fmt::format(
        "The game data could not be found.\n\n"
        "Folder checked:\n{}\n\n"
        "Problem: {}\n\n"
        "Drag the ISO onto install.bat, or put the ISO contents\n"
        "(default.xex and the data folder) in:\n{}\n\n"
        "You can also set the path in splatterhouse.toml\n"
        "(game_data_root = \"...\") or with --game_data_root.\n",
        wrap(root.string(), 76), wrap(problem, 76), wrap(expected.string(), 76));
    std::ofstream report(rex::filesystem::GetExecutableFolder() / "assets_error.txt",
                         std::ios::trunc);
    if (report) {
      report << detail;
    }
#if defined(_WIN32)
    // The Windows dialog clips the text at high DPI and there is no SDL window
    // yet, so it is shown in a console (which is also where the game log
    // appears).
    if (AllocConsole()) {
      SetConsoleTitleA("Splatterhouse - installation error");
      FILE* out = nullptr;
      if (freopen_s(&out, "CONOUT$", "w", stdout) != 0) {
        out = nullptr;
      }
      FILE* in = nullptr;
      if (freopen_s(&in, "CONIN$", "r", stdin) != 0) {
        in = nullptr;
      }
      std::fputs(detail.c_str(), stdout);
      std::fputs("\nPress Enter to exit...\n", stdout);
      std::fflush(stdout);
      std::getchar();
      std::_Exit(1);
    }
#endif
    rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error,
                              "The Splatterhouse game data could not be found. "
                              "Run install.bat with the ISO, or put default.xex "
                              "and the data folder in assets. Details in "
                              "assets_error.txt, next to the executable.");
    std::_Exit(1);
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    const std::filesystem::path root = FindGameDataRoot(paths.game_data_root);
    if (root.empty()) {
      ReportMissingGameData(rex::filesystem::GetExecutableFolder() / "assets",
                            "neither default.xex nor data\\ found");
    }
    if (!REXCVAR_GET(sh_skip_assets_check)) {
      const std::string problem = ValidateGameDataRoot(root);
      if (!problem.empty()) {
        ReportMissingGameData(root, problem);
      }
    }
    paths.game_data_root = root;
  }

  // Dump of the guest image (loaded XEX) for static analysis:
  // enable with REXCVAR_dump_guest=1 (tools/recompile.py / scripts)
  void OnLoadXexImage(std::string& xex_image) override {
    // The installer generates the decrypted XEX with xextool (default.dec.xex);
    // if present, use it instead of the retail one.
    std::filesystem::path decrypted =
        rex::filesystem::GetExecutableFolder() / "assets" / "default.dec.xex";
    if (std::filesystem::is_regular_file(decrypted)) {
      xex_image = "game:\\default.dec.xex";
    }
    REXLOG_INFO("[sh-trace] pre LoadXexImage: {}", xex_image);
  }

  // Data patches in guest memory (equivalent to the Xenia patches).
  void ApplyGuestMemoryPatches() {
    auto* memory = rex::Runtime::instance()->memory();
    auto store_be32 = [&](u32 addr, u32 value) {
      u32 heap_size = 0;
      auto* heap = memory->LookupHeap(addr);
      if (!heap || !heap->QuerySize(addr, &heap_size) || heap_size < 4) {
        REXLOG_WARN("[sh-60] no committed memory at 0x{:08X}", addr);
        return;
      }
      u8* p = memory->TranslateVirtual(addr);
      p[0] = u8(value >> 24);
      p[1] = u8(value >> 16);
      p[2] = u8(value >> 8);
      p[3] = u8(value);
    };

    if (REXCVAR_GET(sh_60fps)) {
      // Xenia "60 FPS" (4E4D07F0, illusion): the delta-time clamp goes from
      // 1/30..1/15 to 1/60..1/60 (0x3C888889).
      store_be32(0x82F8FF58, 0x3C888889);
      store_be32(0x82F8FF5C, 0x3C888889);
      REXLOG_INFO("[sh-60] 60 FPS data patch applied (0x82F8FF58/5C)");
    }
  }

  void ToggleGraphicsMenu() {
    if (g_graphics_menu) {
      g_graphics_menu->RequestClose();
    } else if (graphics_drawer_) {
      new GraphicsMenuDialog(graphics_drawer_, graphics_config_path_, [this]() { QuitGame(); });
    }
  }

// Game exit requested from the options screen ("Quit game" row).
//
// Do NOT use app_context().RequestDeferredQuit() here. That leaves the message
// loop and leads to ReXApp::OnDestroy(), which destroys the ImGuiDrawer and the
// presenter WHILE the guest thread is still emulating the game, and then does
// module_thread_.join() on a thread that never finishes (the emulated game
// doesn't end on its own) -> crash or hang. That's why the SDK itself exits with
// _Exit(0) in OnClosing instead of the normal teardown.
//
// The correct way is the same path as the window's X button:
// Window::RequestClose() -> OnBeforeClose() -> ReXApp::OnClosing(), which does
// TerminateTitle() + rex::FlushLogging() + std::_Exit(0). The process ends
// inside this call, so nothing is touched afterwards.
void QuitGame() {
  REXLOG_WARN("[sh-ui] exit requested from the options menu");
  auto* win = window();
  if (!win) {
    return;
  }
  win->RequestClose();
}

  // Receives F5 before ImGui (high z-order). LaunchImpl doesn't allow
  // overriding ReXApp::OnKeyDown (it's private), so we use our own listener.
  // It also routes the other keys to the open menu: the ImGuiDrawer (z-order
  // 64) receives them, but its keyboard path isn't reliable here, and with the
  // menu open we don't want the game to see them.
  struct GraphicsKeyListener : public rex::ui::WindowInputListener {
    std::function<void()> toggle;
    void OnKeyDown(rex::ui::KeyEvent& e) override {
      if (e.virtual_key() == rex::ui::VirtualKey::kF5) {
        if (toggle) {
          toggle();
        }
        e.set_handled(true);
        return;
      }
      if (g_graphics_menu) {
        g_graphics_menu->PushKey(e.virtual_key());
        e.set_handled(true);
      }
    }
    void OnKeyUp(rex::ui::KeyEvent& e) override {
      if (g_graphics_menu) {
        g_graphics_menu->ReleaseKey(e.virtual_key());
      }
    }
  };
  GraphicsKeyListener graphics_key_listener_;

  // The port's own options screen (F5 key by default, also rebindable via the
  // "bind_graphics" keybind).
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    graphics_drawer_ = drawer;
    graphics_config_path_ = rex::filesystem::GetExecutableFolder() / "splatterhouse.toml";
    rex::ui::RegisterBind("bind_graphics", "F5", "Toggle options menu",
                          [this]() { ToggleGraphicsMenu(); });
    graphics_key_listener_.toggle = [this]() { ToggleGraphicsMenu(); };
    if (window()) {
      window()->AddInputListener(&graphics_key_listener_, 128);
    }
    if (REXCVAR_GET(sh_graphics_menu)) {
      new GraphicsMenuDialog(drawer, graphics_config_path_, [this]() { QuitGame(); });
    }
  }

  void OnShutdown() override {
    rex::ui::UnregisterBind("bind_graphics");
    if (window()) {
      window()->RemoveInputListener(&graphics_key_listener_);
    }
  }

  rex::ui::ImGuiDrawer* graphics_drawer_ = nullptr;
  std::filesystem::path graphics_config_path_;

  // Installs every STFS package (file starting with CON /LIVE/PIRS) in a folder
  // through ContentManager::InstallContent, skipping packages that are already
  // installed (InstallContent itself re-extracts every time). Only uses the
  // public SDK API. Returns the number of packages installed.
  static uint32_t InstallStfsPackagesFrom(rex::system::xam::ContentManager* manager,
                                          uint32_t title_id, const std::filesystem::path& dir) {
    uint32_t installed = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
      if (ec) {
        break;
      }
      if (!entry.is_regular_file(ec)) {
        continue;
      }
      char magic[4] = {};
      {
        std::ifstream file(entry.path(), std::ios::binary);
        if (!file.read(magic, sizeof(magic))) {
          continue;
        }
      }
      const std::string_view signature(magic, sizeof(magic));
      if (signature != "CON " && signature != "LIVE" && signature != "PIRS") {
        continue;
      }
      rex::system::xam::XCONTENT_AGGREGATE_DATA data;
      data.device_id = static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD);
      data.content_type = rex::system::XContentType::kMarketplaceContent;
      data.title_id = title_id;
      data.xuid = 0;
      data.set_file_name(rex::path_to_utf8(entry.path().filename()));
      if (manager->ContentExists(0, data)) {
        continue;
      }
      if (manager->InstallContent(entry.path()) == rex::X_RESULT(0)) {
        REXLOG_INFO("Installed DLC package {}", entry.path().filename().string());
        ++installed;
      } else {
        REXLOG_WARN("[sh-dlc] Could not install {}", entry.path().string());
      }
    }
    return installed;
  }

  // Installs DLC packages (STFS: CON/LIVE/PIRS) the user has left in the user
  // data folder or in dlc\ next to the exe, so the engine sees them via
  // XamContent. Several locations are searched (the profile xuid's, the common
  // xuid 0000000000000000, and the root).
  void InstallDlcPackages() {
    auto* kernel_state = rex::runtime::current_kernel_state();
    if (!kernel_state || !kernel_state->content_manager()) {
      return;
    }
    const uint32_t title_id = kernel_state->title_id();
    const std::string title_str = fmt::format("{:08X}", title_id);
    auto user_root = rex::filesystem::GetUserFolder() / "splatterhouse";
    std::vector<std::filesystem::path> candidates = {
        user_root / title_str / "00000002",
        user_root / "0000000000000000" / title_str / "00000002",
    };
    // Any first-level subfolder (profile xuid) with <title>/00000002.
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(user_root, ec)) {
      if (ec || !entry.is_directory(ec)) {
        continue;
      }
      candidates.push_back(entry.path() / title_str / "00000002");
    }
    // Easy-to-find folder for the user: <exe>\dlc.
    const std::filesystem::path exe_dlc =
        rex::filesystem::GetExecutableFolder() / "dlc";
    candidates.push_back(exe_dlc);
    // The folders are created so the user knows where to put the packages.
    for (const auto& dir : candidates) {
      std::error_code create_ec;
      std::filesystem::create_directories(dir, create_ec);
    }
    uint32_t total = 0;
    for (const auto& dir : candidates) {
      total += InstallStfsPackagesFrom(kernel_state->content_manager(), title_id, dir);
    }
    if (total) {
      REXLOG_INFO("[sh-dlc] Installed {} DLC package(s)", total);
    } else {
      REXLOG_INFO("[sh-dlc] No DLC packages. Put the packages (CON/PIRS/LIVE) in {}",
                  exe_dlc.string());
    }
  }

  void OnPostLoadXexImage() override {
    ApplyGuestMemoryPatches();
    InstallDlcPackages();
    if (!REXCVAR_GET(dump_guest)) {
      return;
    }
    auto* memory = rex::Runtime::instance()->memory();
    std::filesystem::path out_path =
        rex::filesystem::GetExecutableFolder() / "guest_image.bin";
    std::ofstream f(out_path, std::ios::binary | std::ios::trunc);
    if (!f) {
      REXLOG_ERROR("Could not create guest_image.bin at {}",
                   out_path.string());
      return;
    }
    u32 start = PPCImageConfig.image_base;
    // DATA only (before the module): data is where the vtables the game
    // references live; the analysis looks for pointers there.
    u32 end = PPCImageConfig.code_base;
    for (u32 off = start; off < end; off += 0x1000) {
      // Zero-pad uncommitted regions to keep the layout
      // (address -> file offset).
      auto* heap = memory->LookupHeap(off);
      u32 size_tmp2 = 0;
      if (!heap || !heap->QuerySize(off, &size_tmp2) || size_tmp2 == 0) {
        static const char zeros[0x1000] = {0};
        f.write(zeros, 0x1000);
        continue;
      }
      f.write(reinterpret_cast<const char*>(
                  memory->TranslateVirtual<uint8_t*>(off)),
              0x1000);
    }
    f.flush();
    f.close();
    REXLOG_INFO("Guest image dumped to {} ({} MB)", out_path.string(),
                (end - start) / (1024 * 1024));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
};

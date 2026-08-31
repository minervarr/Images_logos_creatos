// imageslogoscreator_ui_capture - headless screenshots of the real GUI.
// Debug-only dev tooling.
//
// vk_canvas already had every piece (core/headless.hh's VK_EXT_headless_surface
// provider, Renderer::readbackLastFrame, capture/'s stb PNG writer); what the
// app owed was the half that drives LogoWindow with no window: a Host whose
// surface is headless, clicks that never happen, and a readback after the
// ordinary drawFrame(). What lands in the PNG is what the app draws.
//
//   ./ilc_ui_capture                    # every state -> ./ui-shots/
//   ./ilc_ui_capture --out DIR --frame 1280x800 --only terminus --list

#include "logo_view.hh"
#include "app_paths.hh"          // app_shell: exeDir() for the headless host
#include "headless.hh"
#include "wayland_platform.hh"   // FileAssetReader (exe-relative assets/)

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace {

// Every method is either "answer honestly from the capture frame size" or
// "do nothing, there is no window". The one that matters:
// surfaceProvider() hands back a HeadlessSurfaceProvider, so Renderer's
// swapchain/render-pass/pipeline path runs completely unchanged.
class HeadlessHost : public Host {
 public:
  HeadlessHost(int w, int h) : w_(w), h_(h), surface_((uint32_t)w, (uint32_t)h) {}

  std::string exeDir() const override { return app_paths::exeDir(); }
  bool init(AppView*) override { return true; }

  SurfaceProvider& surfaceProvider() override { return surface_; }
  AssetReader&     assetReader()     override { return assets_; }
  AssetReader&     dataReader()      override { return dataReader_; }

  void showWindow() override {}
  MonitorInfo primaryMonitor() const override {
    LayoutRect r{0, 0, w_, h_};
    return {r, r};
  }
  void adaptToCurrentMonitor() override {}
  void snapToEdge(SnapEdge) override {}
  void invalidate() override {}
  void setCursor(CursorShape) override {}
  void setKeepAwake(bool) override {}
  void startTimer(int, int) override {}
  void stopTimer(int) override {}
  void postAppEvent(int, intptr_t, intptr_t) override {}
  void pump(bool) override {}
  bool quitRequested() const override { return false; }
  void showErrorMessage(const std::string& title,
                        const std::string& msg) override {
    fprintf(stderr, "[capture][ERROR] %s: %s\n", title.c_str(), msg.c_str());
  }

 private:
  int w_, h_;
  HeadlessSurfaceProvider surface_;
  FileAssetReader assets_;
  FileByteReader  dataReader_;
};

// The states captureGoTo() knows. Names double as file names (NN-label
// convention, so they sort into reading order).
const char* kStates[] = {
    "10-first-run",
    "11-terminus-style",
    "12-transparent-cutout",
    "13-bundle-applied",
    "14-empty-field",
};

}  // namespace

// LogoWindow::create() references make_host() on the path this tool never
// takes (it always injects its own). app_shell_wayland, which defines the real
// one, owns main() and is deliberately out of this target's source list.
std::unique_ptr<Host> make_host() { return nullptr; }

int main(int argc, char** argv) {
  std::string out = "ui-shots";
  std::string only;
  int frameW = 1280, frameH = 800;
  bool listOnly = false;

  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&](const char* what) -> const char* {
      if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", what); exit(2); }
      return argv[++i];
    };
    if      (a == "--out")   out  = next("--out");
    else if (a == "--only")  only = next("--only");
    else if (a == "--list")  listOnly = true;
    else if (a == "--frame") {
      const char* v = next("--frame");
      if (sscanf(v, "%dx%d", &frameW, &frameH) != 2) {
        fprintf(stderr, "--frame wants WxH, got '%s'\n", v);
        return 2;
      }
    } else {
      fprintf(stderr,
          "usage: %s [--out DIR] [--frame WxH] [--only SUBSTR] [--list]\n",
          argv[0]);
      return 2;
    }
  }

  if (listOnly) {
    for (const char* s : kStates) printf("%s\n", s);
    return 0;
  }

  if (!headless_surface_supported()) {
    fprintf(stderr,
        "[capture] VK_EXT_headless_surface not advertised by this Vulkan "
        "loader/ICD.\n          (Mesa's lavapipe has it.)\n");
    return 1;
  }

  auto hostOwned = std::make_unique<HeadlessHost>(frameW, frameH);
  LogoWindow window;
  window.setCaptureSingleBuffer();
  if (!window.create(std::move(hostOwned))) {
    fprintf(stderr, "[capture] LogoWindow::create() failed\n");
    return 1;
  }

  std::filesystem::create_directories(out);

  int written = 0, failed = 0;
  for (const char* state : kStates) {
    if (!only.empty() && std::string(state).find(only) == std::string::npos)
      continue;
    window.captureGoTo(state);

    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    if (!window.captureFrame(rgba, w, h)) {
      fprintf(stderr, "[capture] %-22s readback failed\n", state);
      failed++;
      continue;
    }
    std::string png = out + "/" + state + ".png";
    if (!stbi_write_png(png.c_str(), (int)w, (int)h, 4, rgba.data(),
                        (int)w * 4)) {
      fprintf(stderr, "[capture] %-22s PNG write failed\n", state);
      failed++;
      continue;
    }
    printf("[capture] %-22s %ux%u -> %s\n", state, w, h, png.c_str());
    written++;
  }
  window.shutdown();
  printf("[capture] %d written, %d failed\n", written, failed);
  return failed ? 1 : 0;
}

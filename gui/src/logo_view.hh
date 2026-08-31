#pragma once
// LogoWindow - the Images Logos Creator GUI.
//
// Immediate-mode UI over app_shell's FrameInputView + vk_canvas, all text in
// Terminus .otb via the font engine's RasterFont. The logo PREVIEW itself is
// produced by il_core's render_px - the exact code the CLI twins run - so the
// picture on screen and the PNG on disk are the same render, not two.

#include "frame_input_view.hh"   // app_shell
#include "host.hh"               // app_shell: the Host the window talks to
#include "canvas.hh"             // vk_canvas: Canvas/Rect/Color
#include "raster_font.hh"        // vk_canvas font engine
#include "renderer.hh"           // vk_canvas
#include "text_field.hh"         // textedit
#include "glyphpng_core.hpp"     // il_core: JVal (bundles), the engine API

#include <memory>
#include <string>
#include <vector>

class LogoWindow : public FrameInputView {
 public:
  bool create(std::unique_ptr<Host> injectedHost = {});
  void run();

  void onHostResized() override { dirty_ = true; rebuildLayout(); }
  // Debug capture launch (Android): the host was handed an "il_capture"
  // intent extra whose value "capture" means "render the UI states
  // off-screen, write PNGs, exit" - no window content ever shows.
  void onHostReady() override;
  void onHostLayoutInvalidated() override { dirty_ = true; }
  void onHostExposed() override { dirty_ = true; }
  void shutdown() override { running_ = false; }

  // ── ui_capture hooks (headless screenshots; see tools/ui_capture) ───────
  // Drive the real app - the states set real settings, the frame goes through
  // the ordinary drawFrame() - so the PNG is what the app draws, not a replica.
  bool captureFrame(std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h);
  void captureGoTo(const std::string& state);
  // The capture host has no compositor; one swapchain image keeps the
  // readback honest. Call before create().
  void setCaptureSingleBuffer() { captureSingleBuffer_ = true; }
  void runHeadlessCapture();

 private:
  void drawFrame();
  void rebuildPreview();
  // APK-asset face -> filesystem path (no-op where FONTDIR is a real dir).
  std::string ensureFontFile(const std::string& rel);
  void rebuildLayout();
  void drawPanel(Canvas& c);
  void save();

  bool button(Canvas& c, Rect r, const std::string& label, bool enabled = true);
  // A "< label >" row; returns -1 / +1 for the arrows, 0 for no click.
  int stepper(Canvas& c, Rect r, const std::string& label);
  void applyBundle();

  std::unique_ptr<Host> host_;
  std::unique_ptr<Renderer> renderer_;
  RasterFont uiFont_;

  std::vector<float> frameCurves_, frameShapes_, msdfQuads_;
  std::vector<ImageDraw> frameImages_, frameImagesFg_;

  // settings
  tedit::TextField field_;
  size_t styleIdx_ = 5;     // STYLES[5] = medieval, the CLI's default
  size_t themeIdx_ = 0;     // THEMES[0] = midnight
  std::string fgName_, bgName_;   // resolved color names for render_px
  int logoSize_ = 640;
  int padding_ = 14;        // percent, the CLI's --padding
  size_t bundleIdx_ = 0;
  std::vector<JVal> bundles_;

  // layout rects
  Rect styleRect_{}, themeRect_{}, sizeRect_{}, padRect_{};
  Rect bundleRect_{}, saveRect_{};
  Rect previewRect_{0, 0, 0, 0};
  float statusY_ = 0.0f;

  // preview
  TextureHandle previewTex_ = 0;
  int previewTexW_ = 0, previewTexH_ = 0;
  bool previewDirty_ = true;

  std::string statusLine_ = "Type, pick a look, then Save PNG.";
  bool running_ = true, dirty_ = true;
  bool captureSingleBuffer_ = false, captureMode_ = false;
  int pendingFrames_ = 1;
};

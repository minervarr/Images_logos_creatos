#include "logo_view.hh"

#include "app_paths.hh"
#include "host.hh"
#include "glyphpng_core.hpp"
#include "ui_fonts.hh"

#include <chrono>
#include <cmath>
#include <cstdio>

using namespace col;

namespace {

std::optional<Rgba> resolveColor(const std::string& name) {
  Rgba c{};
  ColorParse r = parse_color(name, &c);
  if (r == CP_OK) return std::optional<Rgba>(c);
  return std::nullopt;   // CP_NONE (clear) and CP_BAD both mean "no paint"
}

Color toColor(const Rgba& c) {
  return Color{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

double nowSeconds() {
  static const std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  return std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
      .count();
}

}  // namespace

bool LogoWindow::create(std::unique_ptr<Host> injectedHost) {
  host_ = injectedHost ? std::move(injectedHost) : make_host();
  if (!host_) return false;
  if (!host_->init(this)) return false;

  // The engine's path globals: the CLI resolves them from /proc/self/exe; a
  // GUI host does the same job from app_paths. Bundles and the output folder
  // are read/written relative to HERE, exactly like the CLI. On Android
  // exeDir() is empty (font paths name APK assets instead of files), so HERE
  // moves to the writable state dir and FONTDIR names where ensureFontFile()
  // extracts a style's face for render_px, which reads real files.
  const std::string& exeDir = app_paths::exeDir();
  const std::string& stateDir = app_paths::stateDir();
  HERE = !stateDir.empty() ? stateDir : exeDir;
  FONTDIR = (!exeDir.empty() ? exeDir : stateDir) + "fonts";
  PROG = "imageslogoscreator";

  try {
    // Capture pins a single swapchain image: a headless present goes
    // nowhere, and on current Mesa it can retire the image - with one image,
    // what readbackLastFrame() copies is always the frame just rendered. A
    // real window keeps the 3-image mailbox-friendly default.
    renderer_ = std::make_unique<Renderer>(host_->surfaceProvider(),
                                           host_->assetReader(),
                                           captureSingleBuffer_ ? 1 : 3);
    if (captureSingleBuffer_)
        renderer_->setPresentEnabled(false);
  } catch (const std::exception& e) {
    host_->showErrorMessage("Vulkan initialization failed", e.what());
    return false;
  }

  {
    AssetReader& loader = host_->dataReader();
    std::string p = exeDir + ui_fonts::bodyRegular();
    if (!uiFont_.open(loader, p.c_str())) {
      host_->showErrorMessage("Font error", "Could not open the Terminus UI face.");
      return false;
    }
    uiFont_.addStyle(loader, (exeDir + ui_fonts::bodyBold()).c_str(),
                     FontStyle::Bold);
    uiFont_.addStyle(loader, (exeDir + ui_fonts::displayRegular()).c_str(),
                     FontStyle::Italic);   // display slot, see ui_fonts.hh
    uiFont_.addStyle(loader, (exeDir + ui_fonts::displayBold()).c_str(),
                     FontStyle::Math);
    // Bake the ordinary UI text up front at the two native strikes. Anything
    // else a frame asks for arrives through the miss path one frame later.
    std::vector<uint32_t> cps;
    for (uint32_t cp = 0x20; cp <= 0x7E; cp++) cps.push_back(cp);
    for (uint32_t cp : {0xE9u, 0xFCu, 0xF1u, 0xB0u, 0x2192u}) cps.push_back(cp);
    uiFont_.ensureGlyphs(cps, {16, 24});
    if (renderer_) renderer_->initMsdf(uiFont_);
  }

  field_.setPlaceholder("Text to draw (e.g. Nava)");
  field_.setFocused(true);
  field_.setColors(Color{0.09f, 0.09f, 0.12f, 1.0f},
                   Color{0.30f, 0.55f, 0.95f, 1.0f},
                   Color{0.96f, 0.96f, 0.98f, 1.0f},
                   Color{0.30f, 0.55f, 0.95f, 1.0f});

  bundles_ = load_bundles();
  rebuildLayout();
  // A capture launch pins single-buffered, suppressed-present rendering
  // BEFORE the renderer is created; the run itself starts in onHostReady().
  if (host_->launchArgument() == "capture") {
    captureMode_ = true;
    captureSingleBuffer_ = true;
  }
  return true;
}

void LogoWindow::run() {
  while (running_) {
    beginFrame();                       // BEFORE the pump, always
    bool haveWork = renderer_ && pendingFrames_ > 0;
    host_->pump(haveWork);
    if (host_->quitRequested()) { running_ = false; break; }
    // Any input dirties: edges would otherwise be dropped by the next
    // beginFrame() before a later frame could see them.
    if (inputArrived()) dirty_ = true;
    if (renderer_ && dirty_) {
      drawFrame();
      dirty_ = false;
      if (pendingFrames_ > 0) pendingFrames_--;
    }
  }
}

// Android keeps the style faces inside the APK; render_px reads real files.
// Copy the face out of the assets once, then serve it from the state dir.
// Desktop is a no-op: FONTDIR already names a real directory.
std::string LogoWindow::ensureFontFile(const std::string& rel) {
  if (!app_paths::exeDir().empty())
    return join_path(FONTDIR, rel);           // desktop: already a file
  std::string dst = app_paths::stateDir() + "fonts/" + rel;
  if (path_exists(dst)) return dst;
  std::vector<uint8_t> bytes;
  if (!host_->dataReader().read(("fonts/" + rel).c_str(), bytes) ||
      bytes.empty())
    return "";
  std::string dir = dirname_of(dst);
  try {
    makedirs_x(dir);
  } catch (const std::exception&) {
    return "";
  }
  if (!write_bin(dst, bytes)) return "";
  return dst;
}

void LogoWindow::onHostReady() {
  if (!captureMode_ || !renderer_) return;
  runHeadlessCapture();
  shutdown();
}

void LogoWindow::runHeadlessCapture() {
  renderer_->setPresentEnabled(false);
  const char* states[] = {"10-first-run", "11-terminus-style",
                          "12-transparent-cutout", "13-bundle-applied",
                          "14-empty-field"};
  std::string dir = app_paths::stateDir() + "ui-shots";
  makedirs_x(dir);
  int written = 0;
  std::string summary;
  for (const char* state : states) {
    captureGoTo(state);
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    if (!captureFrame(rgba, w, h)) continue;
    std::string png = dir + "/" + state + ".png";
    if (write_png(png, (int)w, (int)h, rgba)) {
      summary += std::string(state) + " " + std::to_string(w) + "x" +
               std::to_string(h) + " -> " + png + "\n";
      written++;
    }
  }
  summary = "capture: " + std::to_string(written) + " written\n" + summary;
  write_bin(dir + "/summary.txt",
            std::vector<uint8_t>(summary.begin(), summary.end()));
}

void LogoWindow::rebuildLayout() {
  const float cw = (float)renderer_->width(), chh = (float)renderer_->height();
  const float pad = std::max(12.0f, cw * 0.02f);
  const float colW = cw - 2 * pad;
  float y = pad + 8;
  Rect r;
  r = {pad, y, colW, 44};          field_.setRect(r); y += 52;
  const float half = (colW - 12) * 0.5f;
  styleRect_ = {pad, y, half, 36};
  themeRect_ = {pad + half + 12, y, half, 36};
  y += 44;
  sizeRect_ = {pad, y, half, 36};
  padRect_ = {pad + half + 12, y, half, 36};
  y += 44;
  bundleRect_ = {pad, y, half, 40};
  saveRect_ = {pad + half + 12, y, half, 40};
  y += 48;
  const float statusH = 24.0f;
  float availH = chh - y - statusH - pad;
  float side = std::min(colW, std::max(availH, 0.0f));
  previewRect_ = {pad + (colW - side) * 0.5f, y + (availH - side) * 0.5f,
                  side, side};
  statusY_ = chh - statusH - 6.0f;
  dirty_ = true;
}

void LogoWindow::rebuildPreview() {
  if (!renderer_) return;
  if (previewTex_) {
    renderer_->destroy_texture(previewTex_);
    previewTex_ = 0;
  }
  const StyleEntry* se = &STYLES[styleIdx_];
  const Theme* th = &THEMES[themeIdx_];
  std::string fontPath = ensureFontFile(se->file);
  std::optional<Rgba> bg = resolveColor(th->bg);
  Rgba fg{255, 255, 255, 255};
  if (auto v = resolveColor(th->fg)) fg = *v;
  bool ok = false;
  std::vector<uint8_t> px =
      render_px(field_.text(), fontPath, 512, fg, bg, padding_ / 100.0, &ok);
  if (!ok || px.empty()) {
    previewTexW_ = previewTexH_ = 0;
    previewDirty_ = false;
    return;   // nothing drew (empty text / no glyphs): keep the old texture gone
  }
  previewTex_ = renderer_->create_texture(px.data(), 512, 512, /*mips=*/false);
  previewTexW_ = previewTexH_ = 512;
  previewDirty_ = false;
}

bool LogoWindow::button(Canvas& c, Rect r, const std::string& label,
                        bool enabled) {
  bool hit = false;
  auto& in = input();
  bool hover = r.contains(in.pointerX, in.pointerY);
  Color fill = enabled ? (hover ? Color{0.24f, 0.26f, 0.34f, 1.0f}
                                : btnIdle)
                       : Color{0.13f, 0.13f, 0.16f, 1.0f};
  c.rect(r.x, r.y, r.w, r.h, fill, 6.0f);
  c.text(label, r.x, r.y + (r.h - ui_fonts::bodySize()) * 0.5f,
         ui_fonts::bodySize(),
         enabled ? text : dim);
  if (enabled && hover && in.pointerWentDown) hit = true;
  return hit;
}

int LogoWindow::stepper(Canvas& c, Rect r, const std::string& label) {
  int out = 0;
  auto& in = input();
  c.rect(r.x, r.y, r.w, r.h, Color{0.13f, 0.13f, 0.17f, 1.0f}, 6.0f);
  const float aw = 30.0f;
  Rect left{r.x, r.y, aw, r.h}, right{r.x + r.w - aw, r.y, aw, r.h};
  c.text("<", left.x + 8, r.y + (r.h - ui_fonts::bodySize()) * 0.5f,
         ui_fonts::bodySize(), text);
  c.text(">", right.x + 10, r.y + (r.h - ui_fonts::bodySize()) * 0.5f,
         ui_fonts::bodySize(), text);
  c.text(label, left.x + aw + 8, r.y + (r.h - ui_fonts::bodySize()) * 0.5f,
         ui_fonts::bodySize(), text);
  if (left.contains(in.pointerX, in.pointerY) && in.pointerWentDown) out = -1;
  if (right.contains(in.pointerX, in.pointerY) && in.pointerWentDown) out = 1;
  return out;
}

void LogoWindow::applyBundle() {
  if (bundles_.empty()) return;
  const JVal& b = bundles_[bundleIdx_ % bundles_.size()];
  try {
    Options o = bundle_to_settings(b, "the bundle");
    for (size_t i = 0; i < STYLES.size(); i++)
      if (STYLES[i].key == o.style) { styleIdx_ = i; break; }
    fgName_ = jstr_or(b, "text-color", jstr_or(b, "fg", ""));
    bgName_ = jstr_or(b, "background", jstr_or(b, "bg", ""));
    const Theme* tf = theme_find(jstr_or(b, "colors", DEFAULT_THEME));
    if (tf) {
      if (fgName_.empty()) fgName_ = tf->fg;
      if (bgName_.empty()) bgName_ = tf->bg;
    }
    if (fgName_.empty()) fgName_ = THEMES[themeIdx_].fg;
    if (bgName_.empty()) bgName_ = THEMES[themeIdx_].bg;
    logoSize_ = o.size;
    padding_ = (int)std::lround(o.margin * 100.0);
    if (padding_ < 0) padding_ = 0;
    if (padding_ > 40) padding_ = 40;
    previewDirty_ = true;
  } catch (const std::exception& e) {
    statusLine_ = std::string("Bundle problem: ") + e.what();
  }
}

void LogoWindow::drawPanel(Canvas& c) {
  const float cw = (float)renderer_->width();
  const float chh = (float)renderer_->height();
  auto& in = input();

  // Focus routing first: a click inside the field focuses it (and raises the
  // soft keyboard where that means something), a click anywhere else drops it.
  if (in.pointerWentDown) {
    bool inField = field_.contains(in.pointerX, in.pointerY);
    if (inField != field_.focused()) {
      field_.setFocused(inField);
      if (inField) host_->showKeyboard(field_.text(), field_.text().size());
      else host_->hideKeyboard();
    }
  }

  // The title, then the controls.
  c.text("Images Logos Creator", 16, 10, ui_fonts::displaySize(), text);

  // text entry
  for (int k : in.keysWentDown)
    if (field_.onKey(k)) { previewDirty_ = true; }
  if (in.textEdited) {
    field_.onTextEdit(in.editedText, in.editedCursorByte);
    previewDirty_ = true;
  }
  for (uint32_t cp : in.typedCodepoints) {
    size_t before = field_.text().size();
    field_.onChar(cp);
    if (field_.text().size() != before) previewDirty_ = true;
  }
  field_.render(c, ui_fonts::bodySize(), nowSeconds());

  // style stepper
  {
    int d = stepper(c, styleRect_,
                    "Style: " + STYLES[styleIdx_].label);
    if (d) {
      size_t n = STYLES.size();
      styleIdx_ = (styleIdx_ + n + (size_t)d) % n;
      previewDirty_ = true;
    }
  }
  // theme stepper
  {
    int d = stepper(c, themeRect_,
                    "Colors: " + THEMES[themeIdx_].label);
    if (d) {
      size_t n = THEMES.size();
      themeIdx_ = (themeIdx_ + n + (size_t)d) % n;
      previewDirty_ = true;
    }
  }
  // size stepper: powers of two from 64 to 2048
  {
    int d = stepper(c, sizeRect_, "Logo size: " + std::to_string(logoSize_));
    if (d) {
      if (d < 0 && logoSize_ > 64) logoSize_ /= 2;
      if (d > 0 && logoSize_ < 2048) logoSize_ *= 2;
      previewDirty_ = true;
    }
  }
  // padding stepper: 0..40 percent, the CLI's --padding
  {
    int d = stepper(c, padRect_, "Padding: " + std::to_string(padding_) + "%");
    if (d) {
      padding_ += d;
      if (padding_ < 0) padding_ = 0;
      if (padding_ > 40) padding_ = 40;
      previewDirty_ = true;
    }
  }
  // bundle stepper + save
  {
    std::string blabel = "Bundle: none";
    if (!bundles_.empty()) {
      const JVal& b = bundles_[bundleIdx_ % bundles_.size()];
      blabel = "Bundle: " + jstr_or(b, "name", "?");
    }
    int d = stepper(c, bundleRect_, blabel);
    if (d && !bundles_.empty()) {
      size_t n = bundles_.size();
      bundleIdx_ = (bundleIdx_ + n + (size_t)d) % n;
      applyBundle();
    }
    std::string label = "Save PNG (" + std::to_string(logoSize_) + ")";
    if (button(c, saveRect_, label)) save();
  }

  // status line
  c.text(statusLine_, 16, statusY_, ui_fonts::bodySize(), dim);

  // the preview
  if (previewDirty_) rebuildPreview();
  if (previewTex_) {
    const Theme* th = &THEMES[themeIdx_];
    if (resolveColor(th->bg).has_value()) {
      // Opaque theme: the preview already carries its own background.
    } else {
      // Transparent background: a checkerboard says "alpha here".
      const float sq = 16.0f;
      for (float gy = 0; gy < previewRect_.h; gy += sq)
        for (float gx = 0; gx < previewRect_.w; gx += sq) {
          bool dark = (fmodf(gx / sq + gy / sq, 2.0f) < 1.0f);
          c.rect(previewRect_.x + gx, previewRect_.y + gy,
                 std::min(sq, previewRect_.w - gx),
                 std::min(sq, previewRect_.h - gy),
                 dark ? Color{0.22f, 0.22f, 0.25f, 1.0f}
                      : Color{0.30f, 0.30f, 0.34f, 1.0f},
                 0.0f);
        }
    }
    c.image(previewTex_, previewRect_.x, previewRect_.y,
            previewRect_.w, previewRect_.h);
  } else if (field_.text().empty()) {
    c.text("Type something to see the logo.",
           previewRect_.x,
           previewRect_.y + previewRect_.h * 0.5f - ui_fonts::bodySize(),
           ui_fonts::bodySize(), dim);
  }
}

void LogoWindow::save() {
  const StyleEntry* se = &STYLES[styleIdx_];
  const Theme* th = &THEMES[themeIdx_];
  std::string fontPath = ensureFontFile(se->file);
  std::optional<Rgba> bg = resolveColor(th->bg);
  Rgba fg{255, 255, 255, 255};
  if (auto v = resolveColor(th->fg)) fg = *v;
  bool ok = false;
  std::vector<uint8_t> px =
      render_px(field_.text(), fontPath, logoSize_, fg, bg, padding_ / 100.0, &ok);
  if (!ok || px.empty()) {
    statusLine_ = "Nothing to save - type some text first.";
    dirty_ = true;
    return;
  }
  try {
    std::string folder = join_path(HERE, "pictures");
    makedirs_x(folder);
    std::string base = field_.text().empty()
                           ? std::string("logo")
                           : safe_name(field_.text());
    std::string out = unique_stem(folder, base);
    write_png_x(out, logoSize_, logoSize_, px);
    statusLine_ = "Saved: " + out;
  } catch (const std::exception& e) {
    statusLine_ = std::string("Save failed: ") + e.what();
  }
  dirty_ = true;
}

void LogoWindow::drawFrame() {  // Glyph misses bake BEFORE any quad is built (the atlas may grow; see the
  // RasterFont contract in the engine).
  if (uiFont_.hasMisses()) {
    if (uiFont_.bakeMisses() > 0 && renderer_) renderer_->initMsdf(uiFont_);
  }
  if (previewDirty_) rebuildPreview();

  frameCurves_.clear();
  frameShapes_.clear();
  frameImages_.clear();
  frameImagesFg_.clear();
  msdfQuads_.clear();
  Canvas canvas(frameCurves_, renderer_->width(), renderer_->height(),
                /*font=*/nullptr, 0.0f, 0.0f, 0.0f, 0.0f);
  canvas.useShapes(&frameShapes_);
  canvas.useImages(&frameImages_);
  canvas.useImagesFg(&frameImagesFg_);
  if (uiFont_.valid()) canvas.useMsdf(&uiFont_, &msdfQuads_);

  canvas.rect(0, 0, (float)renderer_->width(), (float)renderer_->height(),
              Color{0.07f, 0.07f, 0.09f, 1.0f});
  drawPanel(canvas);

  renderer_->draw(frameCurves_, /*overlay_rotation_deg=*/0, frameImages_,
                  frameImagesFg_, msdfQuads_, frameShapes_);
}

// ── ui_capture hooks ─────────────────────────────────────────────────────────

bool LogoWindow::captureFrame(std::vector<uint8_t>& rgba, uint32_t& w,
                              uint32_t& h) {
  if (!renderer_) return false;
  // A cell the layout asks for and does not have is baked by the NEXT
  // drawFrame() (misses are recorded during the draw that missed them), so
  // draw until the cache has stopped asking - one draw is not enough for any
  // state whose glyphs are new.
  for (int i = 0; i < 50; i++) {
    dirty_ = true;
    drawFrame();
    if (!uiFont_.hasMisses()) break;
  }
  return renderer_->readbackLastFrame(rgba, w, h);
}

void LogoWindow::captureGoTo(const std::string& state) {
  if (state.find("empty") != std::string::npos) {
    field_.setText("");
  } else if (state.find("terminus") != std::string::npos) {
    field_.setText("Terminus");
    for (size_t i = 0; i < STYLES.size(); i++)
      if (STYLES[i].key == "terminus") styleIdx_ = i;
    themeIdx_ = 0;
  } else if (state.find("transparent") != std::string::npos) {
    field_.setText("ILC");
    for (size_t i = 0; i < THEMES.size(); i++)
      if (THEMES[i].key == "cutout") themeIdx_ = i;
  } else if (state.find("bundle") != std::string::npos) {
    field_.setText("Nava");
    if (!bundles_.empty()) { bundleIdx_ = 0; applyBundle(); }
  } else {   // the ordinary first-run state
    field_.setText("Ag");
    styleIdx_ = 5;
    themeIdx_ = 0;
  }
  logoSize_ = 640;
  padding_ = 14;
  previewDirty_ = true;
  dirty_ = true;
}

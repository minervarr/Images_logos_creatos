# AGENTS.md

Two halves, one repo:

1. **CLI twins** — `glyphpng.py` and `glyphpng.cpp` turn letters into square PNG logos by parsing font files directly (sfnt/TrueType/CFF charstrings, and since the `terminus` style: OTB bitmap strikes via EBDT/EBLC) and hand-rasterizing them.
2. **The GUI app** (`imageslogoscreator`) — Vulkan 2D canvas app for Linux (Wayland) and Android, structured like Matrix_Player: `core/` (the engine, shared by CLI + GUI), `textedit/` (text field library), `gui/` (app), `android/` (Gradle), with `framework/` = `app_shell` + `vk_canvas` submodules. All UI text uses the Terminus `.otb` bitmap font (`assets/fonts/terminus/`).

## Hard constraints

- **Stdlib only.** The whole point of the codebase: no image or font libraries (no Pillow/fontTools). Only unusual import is `zlib`, required for PNG IDAT compression. Do not add third-party dependencies.
- Font outlines are drawn manually: TrueType quadratics via `quad_contour`, CFF Type2 via the `Type2` interpreter, filled by the nonzero-winding scanline rasterizer in `rasterize()`.
- `glyphpng.cpp` is a **C++17 twin** of the Python script (same flags, menus, messages; links only `-lz`). Its purpose is byte-identical PNGs for identical inputs — verified with `cmp`. When changing render/layout/CLI behavior, port the change to the twin and re-verify parity with:
  ```sh
  make && ./glyphpng A --style modern --colors crimson --no-preview -o /tmp/x.png
  ./glyphpng.py A --style modern --colors crimson --no-preview -o /tmp/y.png
  cmp /tmp/x.png /tmp/y.png
  ```

## Running and verifying

No tests, linter config, CI, or requirements.txt exist. Verification = run the script:

```sh
make                                      # build the C++ twin (needs zlib dev)
./glyphpng.py A --style modern --colors crimson --no-preview -o /tmp/x.png   # smoke test
./glyphpng.py --styles        # valid style keys (includes terminus + terminus-bold)
./glyphpng.py --colors-list   # color pairs/themes and named colors
./scripts/linux/build.sh            # desktop GUI (Release) -> build/linux/gui/imageslogoscreator
./scripts/linux/build.sh --debug    # Debug: adds ilc_ui_capture + textedit_test
cmake --build build/linux_debug --target textedit_test && ./build/linux_debug/textedit_test
./scripts/android/capture.sh        # headless capture on a device (debug APK)
./scripts/android/screenshot.sh     # windowed screencap on a device
```

Python 3.14 works. No args opens an interactive menu; safe when stdin is EOF (exits 0).

After ANY change to render/layout/CLI behavior, re-verify twin parity (byte-identical PNGs across styles x themes x sizes, including `terminus` and transparent-background `cutout` themes).

## The GUI app (Android + Linux)

Structure follows Matrix_Player: `core/` (il_core = the engine, extracted verbatim from glyphpng.cpp; the CLI exe is a thin main over it), `textedit/` (single-line UTF-8 text field library over vk_canvas's Canvas, with its own assert-based test), `gui/src/logo_view.cc` (the whole UI; immediate-mode over app_shell's FrameInputView), `android/` (Gradle), `tools/ui_capture/` (headless screenshots). The UI is 100% Terminus .otb through the font engine's `RasterFont` — four faces on four files (16n/16b via Roman/Bold, 24n/24b via Italic/Math — a bitmap face can only bake at its own strike, so the type roles ARE files; see gui/src/ui_fonts.hh). The logo PREVIEW and Save go through il_core's `render_px`, so the GUI's output is the twins' output.

Android specifics:
- Release APKs are signed with the same bruno.jks as Matrix Player (CN=Bruno Pato); per-ABI splits for armeabi-v7a / arm64-v8a / x86_64, `universalApk false`.
- Text input uses app_shell's `AppShellActivity` (MainActivity subclasses it and does `System.loadLibrary("imageslogoscreator")`); the IME mirrors the whole field buffer through `AppView::onTextEditPortable`.
- The engine's font paths must be REAL FILES for render_px (`read_bin`/ifstream), but APK fonts are assets: `ensureFontFile()` lazily copies a style's face from the APK to the state dir once (desktop is a no-op).
- Debug headless capture: launch with `--es il_capture capture` (scripts/android/capture.sh). The app renders its UI states with `Renderer::setPresentEnabled(false)` (no visible frame), reads pixels back and writes PNGs + summary.txt to its external files dir, then exits.

Submodule patches (framework/vk_canvas, uncommitted — commit via its git_wrapper):
- `Renderer::setPresentEnabled(bool)`: capture hosts skip vkQueuePresentKHR. On current Mesa (3:26.2.1) a headless present recycles the swapchain image and readbackLastFrame() then copies empty memory — this regression also breaks Matrix_Player's matrix_ui_capture on this machine since the Mesa update. The phone/driver matrix for on-device capture is untested; the seam is the same one the engine docs list as future "offscreen capture mode".
- `assets/shaders_prebuilt/*.spv` are the framework's shaders converted from SPIR-V 1.5 to 1.3 (the renderer creates modules under Vulkan 1.1 semantics; the Windows slangc had emitted 1.5, which validation rejects and at least one driver renders as nothing). Rebuild with a native slangc (`vce_compile_slang`) to bypass the prebuilt set.

## Structure

- `assets/fonts/` — bundled OTF/TTF fonts. A style key only exists via the `STYLES` table in `glyphpng.py` (~line 33) plus the mirrored `STYLES` vector in `core/glyphpng_core.cpp`, mapping key → font path relative to `FONTDIR`. Adding a font requires entries in both.
- `pictures/` — default output folder for all PNGs (`--compare` writes into `pictures/compare/` unless `--out-dir`).
- `bundles.json` — saved looks (name/style/colors/text-color/background/size/padding); written by `--new-bundle` and the menus.
- `texts.txt` — labeled text blocks ([label] line, then words, blank line between) drawn in bulk via `--bundle NAME(s) --texts texts.txt`.
- Logo list format (`--from FILE`, e.g. from `--new-list`): `[defaults]` + `[name]` blocks of `key = value`. Different format from `texts.txt`; do not mix them.
- `framework/` — git submodules `app_shell` (Host seam, window/Android plumbing) and `vk_canvas` (Vulkan 2D canvas + `vulkan_font_engine` with vendored FreeType/msdfgen). Terminus `.otb` faces are consumed through the font engine's `RasterFont`; a bitmap face bakes ONLY at its own strike — that is why the GUI's type roles are four separate files (see `gui/src/ui_fonts.hh`).

## clangd (LSP) without a full build

`scripts/clangd.sh` configures with the NDK legacy toolchain purely to emit `compile_commands.json`, copies it to the repo root, and ignores any configure-time failure beyond that step — a successful configure is all clangd needs. Alternative: a plain desktop configure (`cmake -S . -B build/linux -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`). `compile_commands.json` itself is git-ignored; restart clangd after regenerating.

## Quirks

- Help text sometimes shows `./logo`: `PROG` becomes `./logo` if a file named `logo` exists next to the script/binary, else falls back to program name.
- The C++ binary resolves its data dir from `/proc/self/exe`, so it must sit beside `assets/fonts` (i.e., run it from this repo or copy the whole tree). Bundles/texts are also read/written there.
- Multi-line text: in `texts.txt` blank lines separate entries; in list files an empty `text:` takes indented lines below it verbatim.
- Colors accept names from `NAMED`, hex (#rgb/#rrggbb/#rrggbbaa), or `none`/`clear` for transparent background (background only — text must have a visible color).
- `--bundle` may be repeated: with `--texts` it applies every look per entry, with a positional text each look gets its own prefixed PNG (bundle name first). Only one bundle is applied when a single text uses a single `-b`.
- `--compare styles|colors|both [TEXT]` draws the text against every look into `pictures/compare/` (or `--out-dir`); themes whose reversed pair has an invisible foreground (`cutout-r`) are skipped automatically.
- PNG parity gotchas for the twin: sfnt directory needs the searchRange/rangeShift skip (6 bytes after numTables); glyf flag REPEAT must read its count byte once; format 4 cmap skips 6 more bytes before endCode; float pipeline must run with `-ffp-contract=off` to match CPython bit-for-bit.

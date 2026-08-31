#pragma once
// The UI face: Terminus .otb, the pure bitmap OpenType.
//
// Every UI glyph is a native bitmap strike from EBDT - no outlines exist in
// these files, so sizes are not continuous. The RasterFont cache serves a
// (style, size, codepoint) cell from the face registered for that style, and
// FreeType accepts only a face's own strike, so the app's four type roles are
// pinned to four FILES rather than four sizes of one file:
//
//   Roman -> ter-u16n   Bold -> ter-u16b    (body roles)
//   Italic -> ter-u24n  Math -> ter-u24b    (display roles; see displayStyle)
//
// Cells therefore only exist at exactly 16 px and 24 px. Any other size the
// UI asks for would bake-fail and render nothing - that is the engine's
// honest answer for a bitmap face - so drawUI text only through these roles.
// The styles' own names are kept from FontStyle; the mapping lives here so
// the call sites say what they mean.

#include <string>

namespace ui_fonts {

// Paths relative to exeDir(); the build copies assets/fonts next to the exe
// and the APK keeps the same tree under assets/.
inline const char* bodyRegular() { return "fonts/terminus/ter-u16n.otb"; }
inline const char* bodyBold()    { return "fonts/terminus/ter-u16b.otb"; }
inline const char* displayRegular() { return "fonts/terminus/ter-u24n.otb"; }
inline const char* displayBold()    { return "fonts/terminus/ter-u24b.otb"; }

// Native strike of each registered face, for layout constants.
inline constexpr float bodySize()    { return 16.0f; }
inline constexpr float displaySize() { return 24.0f; }

// Every strike the Terminus set ships, for code that wants to offer a size
// choice in px terms (the GUI's logo-size control works on OUTPUT pixels and
// does not need this; kept for future UI work that does).
inline const int* strikes() {
  static const int k[] = {12, 14, 16, 18, 20, 22, 24, 28, 32};
  return k;
}
inline constexpr int strikeCount() { return 9; }

}  // namespace ui_fonts

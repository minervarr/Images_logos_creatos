// Generated equivalent of the min_text_size tool for this app's UI font.
// The floor for an outline font is derived from its thinnest stroke; for a
// bitmap font it is simply the smallest native strike. Terminus .otb ships
// strikes at 12..32 px, so nothing below 12 is readable - and the UI only
// ever draws at native strikes anyway (see ui_fonts.hh).
#pragma once
constexpr float kMinReadableTextSizePx = 12.0f;

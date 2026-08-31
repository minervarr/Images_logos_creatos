#include "text_field.hh"

#include <cmath>

#include "keys.hh"

namespace tedit {

bool TextField::onKey(int keyCode) {
  if (!focused_) return false;
  switch (keyCode) {
    case key::Backspace: buf_.eraseBack(); changed_ = true; return true;
    case key::Delete:    buf_.eraseForward(); changed_ = true; return true;
    case key::Left:      buf_.move(-1); return true;
    case key::Right:     buf_.move(1); return true;
    case key::Home:      buf_.home(); return true;
    case key::End:       buf_.end(); return true;
    case key::Enter:     submitted_ = true; return true;
    default:             return false;
  }
}

void TextField::onChar(uint32_t codepoint) {
  if (!focused_) return;
  if (codepoint < 0x20 || codepoint == 0x7F) return;
  buf_.insert(codepoint);
  changed_ = true;
}

void TextField::onTextEdit(const std::string& text, size_t cursorByte) {
  if (!focused_) return;
  buf_.replaceAll(text, cursorByte);
  changed_ = true;
}

bool TextField::takeChanged() {
  bool c = changed_;
  changed_ = false;
  return c;
}

bool TextField::takeSubmitted() {
  bool s = submitted_;
  submitted_ = false;
  return s;
}

// Codepoint boundaries of s, byte offsets, 0 and size included.
static std::vector<size_t> boundaries(const std::string& s) {
  std::vector<size_t> b;
  b.push_back(0);
  for (size_t i = 1; i < s.size(); i++)
    if (TextBuffer::startsCodepoint((uint8_t)s[i])) b.push_back(i);
  b.push_back(s.size());
  return b;
}

std::string TextField::visibleText(const Canvas& c, float sizePx, float maxW,
                                   size_t caretByte, size_t* startByte) const {
  const std::string& s = buf_.text();
  *startByte = 0;
  if (s.empty()) return s;
  if (c.textWidth(s, sizePx) <= maxW) return s;

  std::vector<size_t> b = boundaries(s);
  auto suffixW = [&](size_t k) {
    return c.textWidth(s.substr(b[k]), sizePx);
  };
  // k0: the maximal suffix that fits. The window ends at the text end while
  // the caret sits inside it, which is the typing-at-the-end case.
  size_t k0 = 0;
  while (k0 + 1 < b.size() && suffixW(k0) > maxW) k0++;
  size_t bc = 0;  // caret's boundary
  for (size_t k = 0; k < b.size(); k++)
    if (b[k] <= caretByte) bc = k;
  if (b[k0] <= caretByte) {
    *startByte = b[k0];
    return s.substr(b[k0]);
  }

  // Caret is left of the window: start at the caret and clip the right at a
  // boundary instead (Canvas has no scissor).
  size_t j = bc;
  while (j + 1 < b.size() &&
         c.textWidth(s.substr(b[bc], b[j + 1] - b[bc]), sizePx) <= maxW)
    j++;
  *startByte = b[bc];
  return s.substr(b[bc], b[j] - b[bc]);
}

void TextField::render(Canvas& c, float textSizePx, double nowSeconds) {
  Color edge = border_;
  if (!focused_) edge.a = 0.4f;
  c.rect(rect_.x, rect_.y, rect_.w, rect_.h, edge, 4.0f);
  c.rect(rect_.x + 2, rect_.y + 2, rect_.w - 4, rect_.h - 4, fill_, 3.0f);

  const float pad = 10.0f;
  const float textX = rect_.x + pad;
  const float innerW = rect_.w - 2 * pad;
  const float textY = rect_.y + (rect_.h - textSizePx) * 0.5f;

  const std::string& s = buf_.text();
  size_t caret = buf_.cursor();

  if (s.empty() && !focused_) {
    if (!placeholder_.empty())
      c.text(placeholder_, textX, textY, textSizePx,
             Color{textCol_.r, textCol_.g, textCol_.b, 0.35f});
    return;
  }

  size_t winStart = 0;
  std::string window = visibleText(c, textSizePx, innerW, caret, &winStart);
  c.text(window, textX, textY, textSizePx, textCol_);

  if (!focused_) return;
  bool on = std::fmod(nowSeconds, 1.2) < 0.8;
  if (!on) return;
  float off = c.textWidth(s.substr(winStart, caret - winStart), textSizePx);
  c.rect(textX + off, textY, 2.0f, textSizePx, caretCol_, 0.0f);
}

}  // namespace tedit

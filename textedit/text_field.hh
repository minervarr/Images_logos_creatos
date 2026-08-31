#pragma once
// TextField - single-line editable text field for a vk_canvas Canvas.
// See text_buffer.hh for the input contract. The host drives focus: click
// inside contains() -> setFocused(true); click elsewhere -> false. The caret
// blinks only while focused, and only from the timestamp render() is given,
// so a fixed `now` produces a deterministic frame (UI capture).

#include "text_buffer.hh"

#include <cstdint>
#include <string>

#include "canvas.hh"

namespace tedit {

class TextField {
 public:
  void setRect(Rect r) { rect_ = r; }
  Rect rect() const { return rect_; }
  bool contains(float px, float py) const { return rect_.contains(px, py); }

  void setText(std::string s) { buf_.setText(std::move(s)); }
  const std::string& text() const { return buf_.text(); }
  void setPlaceholder(const std::string& p) { placeholder_ = std::move(p); }

  void setColors(Color fill, Color border, Color textCol, Color caretCol) {
    fill_ = fill; border_ = border; textCol_ = textCol; caretCol_ = caretCol;
  }

  bool focused() const { return focused_; }
  void setFocused(bool f) { focused_ = f; }

  // True when the key was consumed (arrows/home/end/backspace/delete/enter).
  bool onKey(int keyCode);
  // Printable text; control codes are ignored.
  void onChar(uint32_t codepoint);
  // Whole-buffer replacement from an IME (Android).
  void onTextEdit(const std::string& text, size_t cursorByte);
  bool undo() { return buf_.undo(); }

  // True once after the contents changed since the previous takeChanged().
  bool takeChanged();
  // True once after Enter was pressed while focused.
  bool takeSubmitted();

  void render(Canvas& c, float textSizePx, double nowSeconds);

 private:
  // The window of the text that fits maxW and keeps the caret visible:
  // the whole text if it fits, else a codepoint-aligned slice. Reports the
  // window's byte offset in the text so the caret can be placed inside it.
  std::string visibleText(const Canvas& c, float sizePx, float maxW,
                          size_t caretByte, size_t* startByte) const;

  Rect rect_{0, 0, 0, 0};
  TextBuffer buf_;
  std::string placeholder_;
  Color fill_{0.09f, 0.09f, 0.12f, 1.0f};
  Color border_{0.30f, 0.55f, 0.95f, 1.0f};
  Color textCol_{0.96f, 0.96f, 0.98f, 1.0f};
  Color caretCol_{0.30f, 0.55f, 0.95f, 1.0f};
  bool focused_ = false;
  bool changed_ = false;
  bool submitted_ = false;
};

}  // namespace tedit

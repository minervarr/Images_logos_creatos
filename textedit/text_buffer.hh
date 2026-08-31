#pragma once
// textedit - small reusable text-entry library on top of vk_canvas.
//
// Two pieces:
//   TextBuffer  - a single-line UTF-8 buffer with a boundary-locked byte
//                 cursor and a bounded undo stack. No Vulkan, no Canvas:
//                 unit-testable on its own.
//   TextField   - the widget: box, placeholder, caret, horizontal scroll
//                 for overflow, drawn with whatever TextFont the host gave
//                 its Canvas (the app uses Terminus .otb through RasterFont).
//
// Input contract matches app_shell's AppView seam exactly:
//   onCharPortable(cp)               -> TextField::onChar
//   onTextEditPortable(text, cursor) -> TextField::onTextEdit
//   FrameInput::keysWentDown         -> TextField::onKey (one call per code)
// so the same widget serves the Wayland host, Win32 and the Android IME.

#include <cstdint>
#include <string>
#include <vector>

namespace tedit {

class TextBuffer {
 public:
  void setText(std::string s);            // cursor goes to the end
  void clear();
  const std::string& text() const { return buf_; }
  size_t cursor() const { return cur_; }  // byte offset, on a boundary

  void setCursor(size_t bytePos);         // snapped to a codepoint boundary
  void move(int codepoints);              // +right / -left, clamped
  void home() { setCursor(0); }
  void end() { cur_ = buf_.size(); }

  void insert(uint32_t codepoint);        // one printable character
  void insertText(const std::string& utf8);
  bool eraseBack();                       // backspace; false at the start
  bool eraseForward();                    // delete; false at the end
  void replaceAll(const std::string& utf8, size_t cursorByte);  // IME commit
  bool undo();

  bool dirty() const { return dirty_; }
  void clearDirty() { dirty_ = false; }

  static bool startsCodepoint(uint8_t b) { return (b & 0xC0) != 0x80; }

 private:
  size_t snapBack(size_t p) const;        // round down to a boundary
  size_t snapForward(size_t p) const;
  void pushUndo();

  std::string buf_;
  size_t cur_ = 0;
  struct Snap { std::string text; size_t cur; };
  std::vector<Snap> undo_;
  bool dirty_ = false;
};

}  // namespace tedit

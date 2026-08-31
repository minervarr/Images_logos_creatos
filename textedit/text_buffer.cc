#include "text_buffer.hh"

namespace tedit {

size_t TextBuffer::snapBack(size_t p) const {
  while (p > 0 && !startsCodepoint((uint8_t)buf_[p])) p--;
  return p;
}

size_t TextBuffer::snapForward(size_t p) const {
  while (p < buf_.size() && !startsCodepoint((uint8_t)buf_[p])) p++;
  return p;
}

void TextBuffer::pushUndo() {
  if (undo_.size() >= 64) undo_.erase(undo_.begin());
  undo_.push_back({buf_, cur_});
  dirty_ = true;
}

void TextBuffer::setText(std::string s) {
  buf_ = std::move(s);
  cur_ = buf_.size();
  undo_.clear();
  dirty_ = true;
}

void TextBuffer::clear() {
  pushUndo();
  buf_.clear();
  cur_ = 0;
}

void TextBuffer::setCursor(size_t bytePos) {
  if (bytePos > buf_.size()) bytePos = buf_.size();
  cur_ = bytePos == 0 ? 0 : snapBack(bytePos);
  if (cur_ < bytePos && bytePos < buf_.size()) cur_ = snapForward(bytePos);
}

void TextBuffer::move(int codepoints) {
  while (codepoints > 0 && cur_ < buf_.size()) {
    cur_ = snapForward(cur_ + 1);
    codepoints--;
  }
  while (codepoints < 0 && cur_ > 0) {
    cur_ = snapBack(cur_ - 1);
    codepoints++;
  }
}

void TextBuffer::insert(uint32_t codepoint) {
  // UTF-8 encode, then splice in at the cursor.
  std::string s;
  if (codepoint < 0x80) {
    s.push_back((char)codepoint);
  } else if (codepoint < 0x800) {
    s.push_back((char)(0xC0 | (codepoint >> 6)));
    s.push_back((char)(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    s.push_back((char)(0xE0 | (codepoint >> 12)));
    s.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
    s.push_back((char)(0x80 | (codepoint & 0x3F)));
  } else {
    s.push_back((char)(0xF0 | (codepoint >> 18)));
    s.push_back((char)(0x80 | ((codepoint >> 12) & 0x3F)));
    s.push_back((char)(0x80 | ((codepoint >> 6) & 0x3F)));
    s.push_back((char)(0x80 | (codepoint & 0x3F)));
  }
  insertText(s);
}

void TextBuffer::insertText(const std::string& utf8) {
  if (utf8.empty()) return;
  pushUndo();
  buf_.insert(cur_, utf8);
  cur_ += utf8.size();
}

bool TextBuffer::eraseBack() {
  if (cur_ == 0) return false;
  pushUndo();
  size_t p = snapBack(cur_ - 1);
  buf_.erase(p, cur_ - p);
  cur_ = p;
  return true;
}

bool TextBuffer::eraseForward() {
  if (cur_ >= buf_.size()) return false;
  pushUndo();
  size_t p = snapForward(cur_ + 1);
  buf_.erase(cur_, p - cur_);
  return true;
}

void TextBuffer::replaceAll(const std::string& utf8, size_t cursorByte) {
  pushUndo();
  buf_ = utf8;
  if (cursorByte > buf_.size()) cursorByte = buf_.size();
  cur_ = cursorByte == 0 ? 0 : snapBack(cursorByte);
  if (cur_ < cursorByte && cursorByte < buf_.size()) cur_ = snapForward(cursorByte);
}

bool TextBuffer::undo() {
  if (undo_.empty()) return false;
  Snap s = undo_.back();
  undo_.pop_back();
  buf_ = std::move(s.text);
  cur_ = s.cur;
  return true;
}

}  // namespace tedit

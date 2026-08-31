// Assert-style tests for TextBuffer (pure CPU, no Vulkan). #undef NDEBUG so
// asserts survive a Release build, per the vk_canvas tests' convention.
#undef NDEBUG
#include "text_buffer.hh"

#include <cassert>
#include <cstdio>

using tedit::TextBuffer;

static void test_ascii_basics() {
  TextBuffer b;
  b.insert('H');
  b.insert('i');
  assert(b.text() == "Hi" && b.cursor() == 2);
  assert(b.eraseBack() && b.text() == "H" && b.cursor() == 1);
  b.move(-1);
  assert(b.cursor() == 0);
  assert(b.eraseBack() == false);       // nothing before the start
  assert(b.eraseForward() && b.text().empty());
  assert(b.eraseForward() == false);
}

static void test_utf8_boundaries() {
  TextBuffer b;
  b.setText("aé中\U0001F600");          // 1 + 2 + 3 + 4 bytes
  assert(b.cursor() == b.text().size());
  b.move(-4);                            // to the very start
  assert(b.cursor() == 0);
  b.move(1);
  assert(b.cursor() == 1);               // after 'a'
  b.move(1);
  assert(b.cursor() == 3);               // after 'é' (2 bytes)
  b.move(1);
  assert(b.cursor() == 6);               // after '中' (3 bytes)
  b.move(1);
  assert(b.cursor() == 10);              // after the emoji (4 bytes)
  b.move(1);                             // clamped
  assert(b.cursor() == 10);
  b.move(-10);
  assert(b.cursor() == 0);
  assert(b.eraseForward() && b.text() == "é中\U0001F600");
  assert(b.eraseForward() && b.text() == "中\U0001F600");
  assert(b.eraseForward() && b.text() == "\U0001F600");
  assert(b.eraseForward() && b.text().empty());
}

static void test_cursor_snap() {
  TextBuffer b;
  b.setText("é");                        // 2-byte char
  b.setCursor(1);                        // mid-codepoint: snap to a boundary
  assert(b.cursor() == 0 || b.cursor() == 2);
  b.setCursor(99);
  assert(b.cursor() == 2);
  b.setCursor(0);
  assert(b.cursor() == 0);
}

static void test_undo() {
  TextBuffer b;
  b.insert('a');
  b.insert('b');
  assert(b.text() == "ab");
  assert(b.undo() && b.text() == "a");
  assert(b.undo() && b.text().empty());
  assert(!b.undo());                     // nothing left
  b.setText("xyz");
  b.insert('!');
  b.undo();
  assert(b.text() == "xyz");
}

static void test_replace_all_ime() {
  TextBuffer b;
  b.setText("hello");
  b.replaceAll("héllo!", 3);             // IME commit: whole buffer + caret
  assert(b.text() == "héllo!");
  assert(b.cursor() == 3);               // snapped to a boundary (mid-'é' -> 1)
  assert(b.cursor() != 2);
  b.replaceAll("", 0);
  assert(b.text().empty());
}

static void test_dirty() {
  TextBuffer b;
  b.setText("x");
  assert(b.dirty());
  b.clearDirty();
  assert(!b.dirty());
  b.insert('y');
  assert(b.dirty());
}

int main() {
  test_ascii_basics();
  test_utf8_boundaries();
  test_cursor_snap();
  test_undo();
  test_replace_all_ime();
  test_dirty();
  std::printf("textedit: all tests passed\n");
  return 0;
}

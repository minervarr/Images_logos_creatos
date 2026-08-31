// glyphpng_core.hpp - engine half of the glyphpng.cpp twin.
//
// Split out of glyphpng.cpp so the GUI app (core/, gui/, android/)
// links the exact code that makes the CLI's PNGs. Everything here was
// moved verbatim; only the three path globals' linkage changed
// (static -> external). Byte-parity contract with glyphpng.py stands.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using std::string;
using std::vector;

void say(const string &s = "");
string F(const char *fmt, ...);

struct ListError : std::runtime_error {
  ListError(const string &m) : std::runtime_error(m) {}
};

struct RenderError : std::runtime_error {
  RenderError() : std::runtime_error("nothing drew") {}
};

struct FontError : std::runtime_error {
  FontError(const string &m) : std::runtime_error(m) {}
};

struct Rgba { uint8_t r, g, b, a; };

struct Theme { string key, label, fg, bg; };

struct StyleEntry { string key, label, file; };

struct JVal;
using JObject = vector<std::pair<string, JVal>>;
struct JVal {
  enum T { NUL, BOO, NUM, STR, ARR, OBJ } t = NUL;
  double num = 0;
  bool boo = false;
  string str;
  vector<JVal> arr;
  JObject obj;

  static JVal mknum(double v) { JVal j; j.t = NUM; j.num = v; return j; }
  static JVal mkstr(string v) { JVal j; j.t = STR; j.str = std::move(v); return j; }
  static JVal mkbool(bool v) { JVal j; j.t = BOO; j.boo = v; return j; }
  static JVal nul() { return JVal(); }
};

struct LogoBook {
  string folder;
  vector<std::map<string, string>> logos;
};

struct Options {
  string style;
  string path;
  Rgba fg{};
  std::optional<Rgba> bg;
  int size = 640;
  double margin = 0.14;
};

struct TextEntry { string label, text; };

struct SaveError : std::runtime_error {
  explicit SaveError(const string &w)
      : std::runtime_error(F("Something went wrong while saving (%s).",
                             w.c_str())) {}
};

enum ColorParse { CP_OK, CP_NONE, CP_BAD };

// Shared path globals. The CLI resolves them from /proc/self/exe;
// other hosts (GUI) assign them before any engine call.
extern string HERE;
extern string FONTDIR;
extern string PROG;
extern std::map<string, string> NAMED;
extern vector<Theme> THEMES;
extern vector<StyleEntry> STYLES;
extern const string DEFAULT_STYLE;
extern const string DEFAULT_THEME;

// -- engine API (declarations for everything the CLI shell names) --
string F(const char *fmt, ...);
bool is_dir(const string &p);
bool is_file(const string &p);
bool path_exists(const string &p);
string basename_of(const string &p);
string dirname_of(const string &p);
bool is_abs(const string &p);
string join_path(const string &a, const string &b);
bool read_bin(const string &path, vector<uint8_t> &out);
string read_text_file(const string &path, bool *ok);
bool write_bin(const string &path, const vector<uint8_t> &data);
void makedirs(const string &dir);
string safe_lower_ascii(string s);
uint32_t utf8_decode_at(const string &s, size_t &i);
void utf8_push_cp(string &out, uint32_t cp);
vector<uint32_t> utf8_to_cps(const string &s);
bool cp_is_alnum(uint32_t cp);
string repr_char(uint32_t cp);
string quote_text(const string &s);
string safe_name(const string &name);
string unique_stem(const string &folder, const string &stem);
const StyleEntry *style_find(const string &key);
const Theme *theme_find(const string &key);
void init_themes();
string json_escape(const string &s);
string num_repr(double v);
void json_dump_val(const JVal &v, int depth, string &out);
string json_dump(const JVal &v);
const JVal *jget(const JVal &v, const string &key);
void jset(JVal &v, const string &key, JVal val);
string jstr_or(const JVal &v, const string &key, const string &dflt);
bool jhas_nonnull(const JVal &v, const string &key);
vector<JVal> load_bundles();
void save_bundles(const vector<JVal> &bundles);
ColorParse parse_color(const string &input, Rgba *out);
bool supports_color();
string swatch(const std::optional<Rgba> &rgba);
std::map<int, std::vector<double>> parse_dict(const string &b);
double bias_of(size_t n);
void font_dump_debug(const string &path);
void put_chunk(vector<uint8_t> &out, const char kind[4], const uint8_t *payload, size_t len);
bool write_png(const string &path, int width, int height, const vector<uint8_t> &rgba);
vector<uint8_t> render_px(const string &text, const string &font_path, int size, Rgba fg, std::optional<Rgba> bg, double margin, bool *ok);
void preview(const vector<uint8_t> &px, int size, int cols = 34);
int int_of(const string &raw, bool *ok);
Options look_settings(const std::map<string, string> &item, const string &where);
Options settings_from(const std::map<string, string> &item, const string &where);
std::map<string, string> bundle_to_item(const JVal &bundle);
Options bundle_to_settings(const JVal &bundle, const string &where);
const JVal *find_bundle(const string &name, const vector<JVal> &bundles);
vector<string> split_lines_keep(const string &s);
string replace_all(string s, const string &a, const string &b);
string canon_key(const string &k);
LogoBook read_list(const string &path);
TextEntry entry_from_lines(const vector<string> &lines);
string unescape_line(string line);
vector<TextEntry> read_texts(const string &path);
void write_texts(const string &path, const vector<TextEntry> &entries);
vector<string> unique_labels(const vector<TextEntry> &entries);
void makedirs_x(const string &dir);
void write_png_x(const string &path, int width, int height, const vector<uint8_t> &px);
[[noreturn]] void color_or_quit(const string &value, const char *what);

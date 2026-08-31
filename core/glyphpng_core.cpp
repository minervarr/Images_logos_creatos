// glyphpng_core.cpp - engine half of the glyphpng.cpp twin.
// Moved verbatim from glyphpng.cpp; see glyphpng_core.hpp. The only
// edit is the three path globals' linkage: static -> external, so the
// CLI shell and the GUI share one definition.
#include "glyphpng_core.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <csignal>
#include <unistd.h>
#include <unordered_map>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <zlib.h>

using std::string;
using std::vector;


string HERE;
string FONTDIR;
string PROG = "glyphpng";

void say(const string &s) { std::printf("%s\n", s.c_str()); }

string F(const char *fmt, ...) {
  char buf[4096];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  return string(buf);
}



void makedirs_x(const string &dir);
string F(const char *fmt, ...);
void say(const string &s);

bool is_dir(const string &p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool is_file(const string &p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool path_exists(const string &p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0;
}
string basename_of(const string &p) {
  size_t i = p.find_last_of('/');
  return i == string::npos ? p : p.substr(i + 1);
}
string dirname_of(const string &p) {
  size_t i = p.find_last_of('/');
  if (i == string::npos) return ".";
  if (i == 0) return "/";
  return p.substr(0, i);
}
bool is_abs(const string &p) { return !p.empty() && p[0] == '/'; }
string join_path(const string &a, const string &b) {
  if (a.empty()) return b;
  if (is_abs(b)) return b;
  if (a.back() == '/') return a + b;
  return a + "/" + b;
}
bool read_bin(const string &path, vector<uint8_t> &out) {
  std::ifstream fh(path, std::ios::binary);
  if (!fh) return false;
  out.assign(std::istreambuf_iterator<char>(fh),
             std::istreambuf_iterator<char>());
  return true;
}
string read_text_file(const string &path, bool *ok) {
  std::ifstream fh(path, std::ios::binary);
  if (!fh) {
    if (ok) *ok = false;
    return "";
  }
  std::ostringstream ss;
  ss << fh.rdbuf();
  if (ok) *ok = true;
  return ss.str();
}
bool write_bin(const string &path, const vector<uint8_t> &data) {
  std::ofstream fh(path, std::ios::binary | std::ios::trunc);
  if (!fh) return false;
  fh.write(reinterpret_cast<const char *>(data.data()),
           (std::streamsize)data.size());
  return (bool)fh;
}
void makedirs(const string &dir) {
  string cur;
  for (size_t i = 0; i <= dir.size(); i++) {
    if (i == dir.size() || dir[i] == '/') {
      cur = dir.substr(0, i);
      if (!cur.empty() && !is_dir(cur)) mkdir(cur.c_str(), 0777);
    }
  }
}


string safe_lower_ascii(string s) {
  for (auto &c : s)
    if (c >= 'A' && c <= 'Z') c += 32;
  return s;
}

uint32_t utf8_decode_at(const string &s, size_t &i) {
  unsigned char c = s[i];
  if (c < 0x80) { i++; return c; }
  int len = 0;
  uint32_t cp = 0;
  if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
  else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
  else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
  else { i++; return 0xFFFD; }
  if (i + len > s.size()) { i++; return 0xFFFD; }
  for (int k = 1; k < len; k++) {
    unsigned char cc = s[i + k];
    if ((cc & 0xC0) != 0x80) { i++; return 0xFFFD; }
    cp = (cp << 6) | (cc & 0x3F);
  }
  i += len;
  return cp;
}

void utf8_push_cp(string &out, uint32_t cp) {
  if (cp < 0x80) out.push_back((char)cp);
  else if (cp < 0x800) {
    out.push_back((char)(0xC0 | (cp >> 6)));
    out.push_back((char)(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back((char)(0xE0 | (cp >> 12)));
    out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back((char)(0x80 | (cp & 0x3F)));
  } else {
    out.push_back((char)(0xF0 | (cp >> 18)));
    out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back((char)(0x80 | (cp & 0x3F)));
  }
}

vector<uint32_t> utf8_to_cps(const string &s) {
  vector<uint32_t> out;
  size_t i = 0;
  while (i < s.size()) out.push_back(utf8_decode_at(s, i));
  return out;
}

// Python str.isalnum() approximation: ASCII exact, then exclusion ranges.
bool cp_is_alnum(uint32_t cp) {
  if (cp < 128) return isalnum((int)cp) != 0;
  static const uint32_t excl[] = {
      0xA0, 0xA1, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xAC, 0xAD,
      0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB6, 0xB7, 0xB8,
      0xB9, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xD7, 0xF7};
  for (uint32_t e : excl)
    if (cp == e) return false;
  if (cp >= 0x300 && cp <= 0x36F) return false;
  if (cp >= 0x1AB0 && cp <= 0x1AFF) return false;
  if (cp >= 0x1DC0 && cp <= 0x1DFF) return false;
  if (cp >= 0x2000 && cp <= 0x206F) return false;
  if (cp >= 0x20A0 && cp <= 0x20CF) return false;
  if (cp >= 0x2100 && cp <= 0x214F) return false;
  if (cp >= 0x2150 && cp <= 0x218F) return false;
  if (cp >= 0x2190 && cp <= 0x2BFF) return false;
  if (cp >= 0xFE00 && cp <= 0xFE2F) return false;
  return true;
}

string repr_char(uint32_t cp) {
  string s = "'";
  if (cp == '\n') s += "\\n";
  else if (cp == '\t') s += "\\t";
  else if (cp == '\\') s += "\\\\";
  else if (cp == '\'') s += "\\'";
  else utf8_push_cp(s, cp);
  s += "'";
  return s;
}

string quote_text(const string &s) {
  bool alnum = true;
  for (uint32_t cp : utf8_to_cps(s))
    if (!cp_is_alnum(cp)) { alnum = false; break; }
  return alnum ? s : F("\"%s\"", s.c_str());
}

string safe_name(const string &name) {
  string keep;
  for (uint32_t cp : utf8_to_cps(name)) {
    if (cp_is_alnum(cp)) utf8_push_cp(keep, cp);
    else if (cp == '-' || cp == '_') keep.push_back((char)cp);
    else keep.push_back('-');
  }
  size_t b = keep.find_first_not_of('-');
  size_t e = keep.find_last_not_of('-');
  if (b == string::npos) return "logo";
  return keep.substr(b, e - b + 1);
}

string unique_stem(const string &folder, const string &stem) {
  string cand = join_path(folder, stem + ".png");
  int n = 2;
  while (path_exists(cand))
    cand = join_path(folder, F("%s-%d.png", stem.c_str(), n++));
  return cand;
}

// ------------------------------------------------------------------ colors


std::map<string, string> NAMED = {
    {"black", "#000000"}, {"white", "#ffffff"}, {"ink", "#14161a"},
    {"cream", "#f4efe4"}, {"paper", "#f4efe4"}, {"red", "#c0392b"},
    {"crimson", "#a01f2d"}, {"blue", "#1f4e8c"}, {"sky", "#3d8bd4"},
    {"green", "#16733f"}, {"mint", "#57c99a"}, {"gold", "#c8a34a"},
    {"amber", "#e0913a"}, {"slate", "#2b3440"}, {"charcoal", "#22252b"},
    {"purple", "#5b3a8c"}, {"pink", "#d95f8a"}, {"teal", "#177f83"},
    {"sand", "#e2d3b3"}, {"clear", ""}};

vector<Theme> THEMES = {
    {"midnight", "Cream on near-black", "cream", "ink"},
    {"classic", "Black on cream paper", "#101010", "cream"},
    {"crimson", "White on deep red", "white", "crimson"},
    {"gold", "Gold on slate blue", "gold", "slate"},
    {"mint", "Mint on charcoal", "mint", "charcoal"},
    {"ocean", "Cream on ocean blue", "cream", "blue"},
    {"plum", "Sand on purple", "sand", "purple"},
    {"mono", "White on black", "white", "black"},
    {"cutout", "Black glyph, no background", "ink", "clear"}};

vector<StyleEntry> STYLES = {
    {"classic", "Classic serif", "newcomputermodern/NewCM10-Regular.otf"},
    {"classic-bold", "Classic serif, bold", "newcomputermodern/NewCM10-Bold.otf"},
    {"italic", "Elegant italic", "newcomputermodern/NewCM10-Italic.otf"},
    {"modern", "Clean modern sans", "newcomputermodern/NewCMSans10-Bold.otf"},
    {"typewriter", "Typewriter", "newcomputermodern/NewCMMono10-Bold.otf"},
    {"medieval", "Medieval / uncial", "newcomputermodern/NewCMUncial10-Bold.otf"},
    {"chinese", "Chinese, heavy block", "fandol/FandolHei-Bold.otf"},
    {"chinese-song", "Chinese, classic printed", "fandol/FandolSong-Bold.otf"},
    {"chinese-brush", "Chinese, brush written", "fandol/FandolKai-Regular.otf"},
    {"japanese", "Japanese, extra heavy", "haranoaji/HaranoAjiGothic-Heavy.otf"},
    {"japanese-min", "Japanese, classic printed", "haranoaji/HaranoAjiMincho-Bold.otf"},
    {"poster", "Poster sans, very heavy", "unfonts-core/UnGraphicBold.ttf"},
    {"korean", "Korean, classic printed", "unfonts-core/UnBatangBold.ttf"},
    {"korean-round", "Korean, rounded", "unfonts-core/UnDotumBold.ttf"},
    {"brush", "Brush calligraphy", "unfonts-core/UnGungseo.ttf"},
    {"handwritten", "Handwritten", "unfonts-core/UnPilgiBold.ttf"},
    {"terminus", "Terminus bitmap, 24 px", "terminus/ter-u24n.otb"},
    {"terminus-bold", "Terminus bitmap, bold", "terminus/ter-u24b.otb"},
    {"icons", "Icon symbols", "icons/matrix-icons.otf"}};
const string DEFAULT_STYLE = "medieval";
const string DEFAULT_THEME = "midnight";

const StyleEntry *style_find(const string &key) {
  for (auto &s : STYLES)
    if (s.key == key) return &s;
  return nullptr;
}
const Theme *theme_find(const string &key) {
  for (auto &t : THEMES)
    if (t.key == key) return &t;
  return nullptr;
}

void init_themes() {
  auto base = THEMES;
  for (auto &t : base)
    THEMES.push_back({t.key + "-r", t.label + " - reversed", t.bg, t.fg});
}

// --------------------------------------------------------------------- json


class JsonParser {
 public:
  explicit JsonParser(const string &s) : s_(s) {}
  JVal parse() {
    skip();
    JVal v = value(0);
    skip();
    return v;
  }

 private:
  const string &s_;
  size_t p_ = 0;

  void skip() {
    while (p_ < s_.size() &&
           (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\n' || s_[p_] == '\r'))
      p_++;
  }
  [[noreturn]] void bad(const char *m) {
    throw std::runtime_error(F("JSON: %s at offset %zu", m, p_));
  }
  char peek() {
    if (p_ >= s_.size()) bad("unexpected end");
    return s_[p_];
  }
  void expect(char c) {
    if (peek() != c) bad("unexpected character");
    p_++;
  }
  JVal value(int depth) {
    if (depth > 100) bad("too deep");
    skip();
    char c = peek();
    if (c == '{') return object(depth);
    if (c == '[') return array(depth);
    if (c == '"') { JVal v; v.t = JVal::STR; v.str = str(); return v; }
    if (c == 't' || c == 'f') return boolean();
    if (c == 'n') { literal("null"); return JVal(); }
    return number();
  }
  void literal(const char *lit) {
    size_t n = strlen(lit);
    if (s_.compare(p_, n, lit) != 0) bad("bad literal");
    p_ += n;
  }
  JVal boolean() {
    JVal v;
    v.t = JVal::BOO;
    if (s_.compare(p_, 4, "true") == 0) { v.boo = true; p_ += 4; }
    else if (s_.compare(p_, 5, "false") == 0) { v.boo = false; p_ += 5; }
    else bad("bad literal");
    return v;
  }
  JVal number() {
    size_t start = p_;
    if (peek() == '-') p_++;
    while (p_ < s_.size() &&
           (isdigit((unsigned char)s_[p_]) || s_[p_] == '.' ||
            s_[p_] == 'e' || s_[p_] == 'E' || s_[p_] == '+' ||
            s_[p_] == '-'))
      p_++;
    if (start == p_) bad("bad number");
    JVal v;
    v.t = JVal::NUM;
    v.num = strtod(s_.substr(start, p_ - start).c_str(), nullptr);
    return v;
  }
  string str() {
    expect('"');
    string out;
    while (true) {
      if (p_ >= s_.size()) bad("unterminated string");
      char c = s_[p_++];
      if (c == '"') break;
      if (c != '\\') { out.push_back(c); continue; }
      if (p_ >= s_.size()) bad("unterminated escape");
      char e = s_[p_++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          uint32_t cp = hex4();
          if (cp >= 0xD800 && cp <= 0xDBFF && p_ + 1 < s_.size() &&
              s_[p_] == '\\' && s_[p_ + 1] == 'u') {
            p_ += 2;
            uint32_t lo = hex4();
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          utf8_push_cp(out, cp);
          break;
        }
        default: bad("bad escape");
      }
    }
    return out;
  }
  uint32_t hex4() {
    if (p_ + 4 > s_.size()) bad("bad \\u escape");
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) {
      char c = s_[p_++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= (uint32_t)(c - '0');
      else if (c >= 'a' && c <= 'f') v |= (uint32_t)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= (uint32_t)(c - 'A' + 10);
      else bad("bad \\u digit");
    }
    return v;
  }
  JVal array(int depth) {
    expect('[');
    JVal v;
    v.t = JVal::ARR;
    skip();
    if (peek() == ']') { p_++; return v; }
    while (true) {
      v.arr.push_back(value(depth + 1));
      skip();
      char c = peek();
      if (c == ',') { p_++; continue; }
      if (c == ']') { p_++; break; }
      bad("expected , or ]");
    }
    return v;
  }
  JVal object(int depth) {
    expect('{');
    JVal v;
    v.t = JVal::OBJ;
    skip();
    if (peek() == '}') { p_++; return v; }
    while (true) {
      skip();
      string key = str();
      skip();
      expect(':');
      v.obj.emplace_back(key, value(depth + 1));
      skip();
      char c = peek();
      if (c == ',') { p_++; continue; }
      if (c == '}') { p_++; break; }
      bad("expected , or }");
    }
    return v;
  }
};

string json_escape(const string &s) {
  string out;
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) out += F("\\u%04x", c);
        else out.push_back((char)c);
    }
  }
  return out;
}

string num_repr(double v) {
  if (std::floor(v) == v && std::fabs(v) < 9.2e18)
    return F("%lld", (long long)v);
  char buf[64];
  snprintf(buf, sizeof buf, "%.17g", v);
  string s(buf);
  size_t e = s.find_last_not_of('0');
  if (e != string::npos && s.find('.') != string::npos && s[e] != '.')
    s.erase(e + 1);
  return s;
}

void json_dump_val(const JVal &v, int depth, string &out) {
  string pad(depth * 2, ' '), pad2((depth + 1) * 2, ' ');
  switch (v.t) {
    case JVal::NUL: out += "null"; break;
    case JVal::BOO: out += v.boo ? "true" : "false"; break;
    case JVal::NUM: out += num_repr(v.num); break;
    case JVal::STR: out += "\"" + json_escape(v.str) + "\""; break;
    case JVal::ARR: {
      if (v.arr.empty()) { out += "[]"; break; }
      out += "[\n";
      for (size_t i = 0; i < v.arr.size(); i++) {
        out += pad2;
        json_dump_val(v.arr[i], depth + 1, out);
        out += i + 1 < v.arr.size() ? ",\n" : "\n";
      }
      out += pad + "]";
      break;
    }
    case JVal::OBJ: {
      if (v.obj.empty()) { out += "{}"; break; }
      out += "{\n";
      for (size_t i = 0; i < v.obj.size(); i++) {
        out += pad2 + "\"" + json_escape(v.obj[i].first) + "\": ";
        json_dump_val(v.obj[i].second, depth + 1, out);
        out += i + 1 < v.obj.size() ? ",\n" : "\n";
      }
      out += pad + "}";
      break;
    }
  }
}

string json_dump(const JVal &v) {
  string out;
  json_dump_val(v, 0, out);
  return out;
}

const JVal *jget(const JVal &v, const string &key) {
  if (v.t != JVal::OBJ) return nullptr;
  for (auto &kv : v.obj)
    if (kv.first == key) return &kv.second;
  return nullptr;
}
void jset(JVal &v, const string &key, JVal val) {
  for (auto &kv : v.obj)
    if (kv.first == key) { kv.second = std::move(val); return; }
  v.obj.emplace_back(key, std::move(val));
}
string jstr_or(const JVal &v, const string &key, const string &dflt) {
  const JVal *f = jget(v, key);
  if (!f || f->t == JVal::NUL) return dflt;
  if (f->t == JVal::STR) return f->str;
  if (f->t == JVal::NUM) return num_repr(f->num);
  return dflt;
}
bool jhas_nonnull(const JVal &v, const string &key) {
  const JVal *f = jget(v, key);
  return f && f->t != JVal::NUL;
}

vector<JVal> load_bundles() {
  bool ok = false;
  string raw = read_text_file(join_path(HERE, "bundles.json"), &ok);
  if (!ok) return {};
  try {
    JVal doc = JsonParser(raw).parse();
    if (doc.t != JVal::ARR) return {};
    return doc.arr;
  } catch (...) {
    return {};
  }
}

void save_bundles(const vector<JVal> &bundles) {
  JVal doc;
  doc.t = JVal::ARR;
  doc.arr = bundles;
  std::ofstream fh(join_path(HERE, "bundles.json"),
                   std::ios::binary | std::ios::trunc);
  fh << json_dump(doc) << "\n";
}

// -------------------------------------------------------------- color parse

ColorParse parse_color(const string &input, Rgba *out) {
  string s = input;
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == string::npos) return CP_NONE;
  size_t e = s.find_last_not_of(" \t\r\n");
  s = s.substr(b, e - b + 1);
  string low = safe_lower_ascii(s);
  if (low == "none" || low == "transparent" || low == "clear" || low.empty())
    return CP_NONE;
  auto it = NAMED.find(low);
  if (it != NAMED.end()) {
    if (it->second.empty()) return CP_NONE;
    s = it->second;
  }
  size_t start = (!s.empty() && s[0] == '#') ? 1 : 0;
  s = s.substr(start);
  if (s.size() == 3) {
    string exp;
    for (char c : s) { exp.push_back(c); exp.push_back(c); }
    s = exp;
  }
  if (s.size() == 6) s += "ff";
  if (s.size() != 8) return CP_BAD;
  uint8_t v[4];
  for (int k = 0; k < 4; k++) {
    unsigned n = 0;
    for (int j = 0; j < 2; j++) {
      char c = s[k * 2 + j];
      n <<= 4;
      if (c >= '0' && c <= '9') n |= (unsigned)(c - '0');
      else if (c >= 'a' && c <= 'f') n |= (unsigned)(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') n |= (unsigned)(c - 'A' + 10);
      else return CP_BAD;
    }
    v[k] = (uint8_t)n;
  }
  out->r = v[0]; out->g = v[1]; out->b = v[2]; out->a = v[3];
  return CP_OK;
}

[[noreturn]] void color_or_quit(const string &value, const char *what) {
  say(F("Sorry, I don't recognise the %s color %s.", what,
        F("\"%s\"", value.c_str()).c_str()));
  say("Use a color name, a hex code like #c0392b, or 'none'.");
  string names;
  bool first = true;
  for (auto &kv : NAMED) {
    if (!first) names += ", ";
    names += kv.first;
    first = false;
  }
  say("Names available: " + names);
  std::exit(1);
}

bool supports_color() {
  return isatty(1) && getenv("TERM") && strcmp(getenv("TERM"), "dumb") != 0;
}

string swatch(const std::optional<Rgba> &rgba) {
  if (!supports_color()) return "  ";
  if (!rgba) return "\x1b[90m::\x1b[0m";
  return F("\x1b[48;2;%d;%d;%dm  \x1b[0m", rgba->r, rgba->g, rgba->b);
}

// ------------------------------------------------------------ font reading

class Reader {
 public:
  Reader(const vector<uint8_t> &d, size_t pos = 0) : d(d), p(pos) {}
  void seek(size_t q) { p = q; }
  size_t tell() const { return p; }
  uint8_t u8() { bounds(1); return d[p++]; }  uint16_t u16() { bounds(2); uint16_t v = (uint16_t)((d[p] << 8) | d[p + 1]); p += 2; return v; }
  int16_t s16() { return (int16_t)u16(); }
  uint32_t u32() {
    bounds(4);
    uint32_t v = ((uint32_t)d[p] << 24) | ((uint32_t)d[p + 1] << 16) |
                 ((uint32_t)d[p + 2] << 8) | (uint32_t)d[p + 3];
    p += 4;
    return v;
  }
  double f2dot14() { return s16() / 16384.0; }

  const vector<uint8_t> &d;
  size_t p;

 private:
  void bounds(int n) const {
    if (p + n > d.size()) throw FontError("truncated font");
  }
};

struct PtOn { double x, y; bool on; };
using Contour = vector<std::pair<double, double>>;

vector<Contour> quad_contour(const vector<PtOn> &pts, int steps = 12) {
  vector<Contour> out;
  int n = (int)pts.size();
  if (n == 0) return out;
  vector<PtOn> seq;
  int idx = -1;
  for (int i = 0; i < n; i++)
    if (pts[i].on) { idx = i; break; }
  if (idx < 0) {
    double x0 = (pts[0].x + pts[n - 1].x) / 2.0;
    double y0 = (pts[0].y + pts[n - 1].y) / 2.0;
    seq.push_back({x0, y0, true});
    for (auto &p : pts) seq.push_back(p);
  } else {
    for (int i = idx; i < n; i++) seq.push_back(pts[i]);
    for (int i = 0; i < idx; i++) seq.push_back(pts[i]);
  }
  seq.push_back(seq[0]);

  Contour res;
  double cx = seq[0].x, cy = seq[0].y;
  res.push_back({cx, cy});
  int i = 1;
  while (i < (int)seq.size()) {
    double x = seq[i].x, y = seq[i].y;
    if (seq[i].on) {
      res.push_back({x, y});
      cx = x; cy = y;
      i++;
      continue;
    }
    PtOn next = (i + 1 < (int)seq.size()) ? seq[i + 1] : seq[0];
    double nx, ny;
    int step;
    if (!next.on) {
      nx = (x + next.x) / 2.0;
      ny = (y + next.y) / 2.0;
      step = 1;
    } else {
      nx = next.x; ny = next.y;
      step = 2;
    }
    for (int s = 1; s <= steps; s++) {
      double t = (double)s / steps;
      double u = 1 - t;
      res.push_back({u * u * cx + 2 * u * t * x + t * t * nx,
                     u * u * cy + 2 * u * t * y + t * t * ny});
    }
    cx = nx; cy = ny;
    i += step;
  }
  out.push_back(res);
  return out;
}

class Type2;
std::map<int, std::vector<double>> parse_dict(const string &b) {
  std::map<int, std::vector<double>> out;
  vector<double> ops;
  size_t i = 0;
  while (i < b.size()) {
    uint8_t v = (uint8_t)b[i];
    if (v <= 21) {
      int key;
      if (v == 12) {
        if (i + 1 >= b.size()) break;
        key = (12 << 8) | (uint8_t)b[i + 1];
        i += 2;
      } else {
        key = v;
        i += 1;
      }
      out[key] = ops;
      ops.clear();
    } else if (v == 28) {
      if (i + 2 >= b.size()) { i++; continue; }
      ops.push_back((int16_t)(((uint16_t)(uint8_t)b[i + 1] << 8) |
                              (uint16_t)(uint8_t)b[i + 2]));
      i += 3;
    } else if (v == 29) {
      if (i + 4 >= b.size()) { i++; continue; }
      uint32_t raw = ((uint32_t)(uint8_t)b[i + 1] << 24) |
                     ((uint32_t)(uint8_t)b[i + 2] << 16) |
                     ((uint32_t)(uint8_t)b[i + 3] << 8) |
                     (uint32_t)(uint8_t)b[i + 4];
      ops.push_back((int32_t)raw);
      i += 5;
    } else if (v == 30) {
      string s;
      bool done = false;
      i++;
      while (i < b.size() && !done) {
        for (int nib : {(b[i] >> 4) & 15, b[i] & 15}) {
          if (nib <= 9) s.push_back((char)('0' + nib));
          else if (nib == 10) s.push_back('.');
          else if (nib == 11) s.push_back('E');
          else if (nib == 12) { s += "E-"; }
          else if (nib == 14) s.push_back('-');
          else if (nib == 15) { done = true; break; }
        }
        i++;
      }
      double val = 0.0;
      if (!s.empty()) {
        char *end = nullptr;
        val = strtod(s.c_str(), &end);
        if (end == s.c_str()) val = 0.0;
      }
      ops.push_back(val);
    } else if (v >= 32 && v <= 246) {
      ops.push_back((int)v - 139);
      i += 1;
    } else if (v >= 247 && v <= 250) {
      if (i + 1 < b.size())
        ops.push_back(((int)v - 247) * 256 + (int)(uint8_t)b[i + 1] + 108);
      i += 2;
    } else if (v >= 251 && v <= 254) {
      if (i + 1 < b.size())
        ops.push_back(-((int)v - 251) * 256 - (int)(uint8_t)b[i + 1] - 108);
      i += 2;
    } else {
      i += 1;
    }
  }
  return out;
}

class CFF;

class FontC {
 public:
  explicit FontC(const string &path);
  ~FontC();
  uint32_t gid(uint32_t cp) const {
    auto it = cmap.find(cp);
    return it == cmap.end() ? 0 : it->second;
  }
  vector<Contour> contours(uint32_t g, int depth = 0);
  int advance_of(uint32_t g) const {
    if (advances.empty()) return units_per_em / 2;
    if ((size_t)g < advances.size()) return advances[g];
    return last_advance;
  }
  const uint8_t *at(size_t off) const { return data.data() + off; }

  vector<uint8_t> data;
  std::map<string, std::pair<size_t, size_t>> tables;
  int units_per_em = 1000;
  int index_to_loc = 1;
  int num_glyphs = 0;
  vector<int> advances;
  int last_advance = 0;
  std::unordered_map<uint32_t, uint32_t> cmap;
  CFF *cff = nullptr;
  vector<long long> loca;

 private:
  void read_hmtx();
  void read_loca();
  void read_cmap();
  uint32_t r_u32(Reader &rd) const;
  vector<Contour> glyf(uint32_t gidv, int depth);
  vector<Contour> composite(Reader &r, int depth);
};

double bias_of(size_t n) {
  return n < 1240 ? 107 : (n < 33900 ? 1131 : 32768);
}

class Type2 {
 public:
  Type2(const vector<string> &cs, const vector<string> &gs,
        const vector<string> &ss)
      : charstrings(cs), gsubrs(gs), subrs(ss) {
    gbias = bias_of(gs.size());
    lbias = bias_of(ss.size());
  }
  static const int CURVE_STEPS = 14;
  vector<double> stack;
  vector<Contour> contours;
  Contour cur;
  double x = 0, y = 0;
  int nstems = 0;
  bool width_done = false;

  bool run(const string &code, int depth = 0) {
    if (depth > 10) return true;
    size_t i = 0;
    while (i < code.size()) {
      uint8_t b0 = (uint8_t)code[i];
      if (b0 >= 32 || b0 == 28) {
        if (b0 == 28) {
          stack.push_back((int16_t)(((uint16_t)(uint8_t)code[i + 1] << 8) |
                                    (uint16_t)(uint8_t)code[i + 2]));
          i += 3;
        } else if (b0 <= 246) {
          stack.push_back((int)b0 - 139);
          i += 1;
        } else if (b0 <= 250) {
          stack.push_back((b0 - 247) * 256 + (uint8_t)code[i + 1] + 108);
          i += 2;
        } else if (b0 <= 254) {
          stack.push_back(-((int)b0 - 251) * 256 - (uint8_t)code[i + 1] - 108);
          i += 2;
        } else {
          uint32_t raw = ((uint32_t)(uint8_t)code[i + 1] << 24) |
                         ((uint32_t)(uint8_t)code[i + 2] << 16) |
                         ((uint32_t)(uint8_t)code[i + 3] << 8) |
                         (uint32_t)(uint8_t)code[i + 4];
          stack.push_back((int32_t)raw / 65536.0);
          i += 5;
        }
        continue;
      }
      i++;
      switch (b0) {
        case 1: case 3: case 18: case 23:  // stem
          take_width(true);
          nstems += (int)stack.size() / 2;
          stack.clear();
          break;
        case 19: case 20: {  // hintmask / cntrmask
          take_width(true);
          nstems += (int)stack.size() / 2;
          stack.clear();
          i += (nstems + 7) / 8;
          break;
        }
        case 21: {  // rmoveto
          take_width(true);
          if (stack.size() >= 2) moveto(x + stack[stack.size() - 2],
                                        y + stack[stack.size() - 1]);
          stack.clear();
          break;
        }
        case 22: {  // hmoveto
          take_width(false);
          if (!stack.empty()) moveto(x + stack.back(), y);
          stack.clear();
          break;
        }
        case 4: {  // vmoveto
          take_width(false);
          if (!stack.empty()) moveto(x, y + stack.back());
          stack.clear();
          break;
        }
        case 5: {  // rlineto
          for (size_t j = 0; j + 1 < stack.size(); j += 2)
            lineto(x + stack[j], y + stack[j + 1]);
          stack.clear();
          break;
        }
        case 6: case 7: {  // hlineto / vlineto
          bool horiz = (b0 == 6);
          for (double v : stack) {
            if (horiz) lineto(x + v, y);
            else lineto(x, y + v);
            horiz = !horiz;
          }
          stack.clear();
          break;
        }
        case 8: {  // rrcurveto
          for (size_t j = 0; j + 5 < stack.size(); j += 6)
            rc(stack[j], stack[j + 1], stack[j + 2], stack[j + 3],
               stack[j + 4], stack[j + 5]);
          stack.clear();
          break;
        }
        case 24: {  // rcurveline
          size_t j = 0;
          while (stack.size() - j >= 8) {
            rc(stack[j], stack[j + 1], stack[j + 2], stack[j + 3],
               stack[j + 4], stack[j + 5]);
            j += 6;
          }
          if (j + 1 < stack.size()) lineto(x + stack[j], y + stack[j + 1]);
          stack.clear();
          break;
        }
        case 25: {  // rlinecurve
          size_t j = 0;
          while (stack.size() - j >= 8) {
            lineto(x + stack[j], y + stack[j + 1]);
            j += 2;
          }
          if (j + 5 < stack.size())
            rc(stack[j], stack[j + 1], stack[j + 2], stack[j + 3],
               stack[j + 4], stack[j + 5]);
          stack.clear();
          break;
        }
        case 26: case 27: {  // vvcurveto / hhcurveto
          size_t j = 0;
          double d1 = 0;
          if (stack.size() % 4) { d1 = stack[0]; j = 1; }
          while (j + 3 < stack.size()) {
            double a = stack[j], b = stack[j + 1], c = stack[j + 2],
                   d = stack[j + 3];
            if (b0 == 26) rc(d1, a, b, c, 0, d);
            else rc(a, d1, b, c, d, 0);
            d1 = 0;
            j += 4;
          }
          stack.clear();
          break;
        }
        case 30: case 31: {  // vhcurveto / hvcurveto
          bool horiz = (b0 == 31);
          size_t j = 0;
          while (j + 3 < stack.size()) {
            bool last = (stack.size() - j == 5);
            double a = stack[j], b = stack[j + 1], c = stack[j + 2],
                   d = stack[j + 3];
            double e = last ? stack[j + 4] : 0.0;
            if (horiz) rc(a, 0, b, c, e, d);
            else rc(0, a, b, c, d, e);
            horiz = !horiz;
            j += 4;
          }
          stack.clear();
          break;
        }
        case 10: {  // callsubr
          if (!stack.empty()) {
            long idx = (long)stack.back();
            stack.pop_back();
            idx += (long)lbias;
            if (idx >= 0 && (size_t)idx < subrs.size() &&
                run(subrs[idx], depth + 1))
              return true;
          }
          break;
        }
        case 29: {  // callgsubr
          if (!stack.empty()) {
            long idx = (long)stack.back();
            stack.pop_back();
            idx += (long)gbias;
            if (idx >= 0 && (size_t)idx < gsubrs.size() &&
                run(gsubrs[idx], depth + 1))
              return true;
          }
          break;
        }
        case 11: return false;  // return
        case 14:                // endchar
          take_width(true);
          stack.clear();
          return true;
        case 12: {  // escape
          if (i >= code.size()) break;
          uint8_t b1 = (uint8_t)code[i];
          i++;
          if (b1 == 35 && stack.size() >= 12) {  // flex
            flex6(0);
            flex6(6);
          } else if (b1 == 34 && stack.size() >= 7) {  // hflex
            double y0 = y;
            rc(stack[0], 0, stack[1], stack[2], stack[3], 0);
            rc(stack[4], 0, stack[5], y0 - y, stack[6], 0);
          } else if (b1 == 36 && stack.size() >= 9) {  // hflex1
            double y0 = y;
            rc(stack[0], stack[1], stack[2], stack[3], stack[4], 0);
            rc(stack[5], 0, stack[6], stack[7], stack[8], y0 - y);
          } else if (b1 == 37 && stack.size() >= 11) {  // flex1
            double x0 = x, y0 = y;
            double dx = stack[0] + stack[2] + stack[4] + stack[6] +
                        stack[8];
            double dy = stack[1] + stack[3] + stack[5] + stack[7] +
                        stack[9];
            rc(stack[0], stack[1], stack[2], stack[3], stack[4], stack[5]);
            rc(stack[6], stack[7], stack[8], stack[9],
               x0 + dx + stack[10] - x - stack[6] - stack[8],
               y0 + dy - y - stack[7] - stack[9]);
          }
          stack.clear();
          break;
        }
        default:
          stack.clear();
          break;
      }
    }
    return false;
  }

  void close() {
    if (cur.size() > 1) contours.push_back(cur);
    cur.clear();
  }

 private:
  const vector<string> &charstrings;
  const vector<string> &gsubrs;
  const vector<string> &subrs;
  double gbias = 0, lbias = 0;

  void take_width(bool even) {
    if (width_done) return;
    width_done = true;
    size_t want = even ? 1 : 0;
    if (stack.size() % 2 == want && !stack.empty()) stack.erase(stack.begin());
  }
  void moveto(double nx, double ny) {
    close();
    x = nx; y = ny;
    cur.push_back({nx, ny});
  }
  void lineto(double nx, double ny) {
    x = nx; y = ny;
    cur.push_back({nx, ny});
  }
  void curveto(double x1, double y1, double x2, double y2, double x3,
               double y3) {
    double x0 = x, y0 = y;
    for (int s = 1; s <= CURVE_STEPS; s++) {
      double t = (double)s / CURVE_STEPS;
      double u = 1 - t;
      double a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t,
             d = t * t * t;
      cur.push_back({a * x0 + b * x1 + c * x2 + d * x3,
                     a * y0 + b * y1 + c * y2 + d * y3});
    }
    x = x3; y = y3;
  }
  void rc(double a0, double a1, double a2, double a3, double a4, double a5) {
    double x1 = x + a0, y1 = y + a1;
    double x2 = x1 + a2, y2 = y1 + a3;
    curveto(x1, y1, x2, y2, x2 + a4, y2 + a5);
  }
  void flex6(size_t o) {
    rc(stack[o], stack[o + 1], stack[o + 2], stack[o + 3], stack[o + 4],
       stack[o + 5]);
  }
};

std::map<int, std::vector<double>> parse_dict(const string &b);

class CFF {
 public:
  CFF(const vector<uint8_t> &data_, size_t base) : data(data_) {
    Reader r(data, base);
    r.u8(); r.u8();
    uint8_t hdr_size = r.u8();
    r.u8();
    size_t p = base + hdr_size;
    p = skip_index(p);
    auto top_dicts = index(p);
    p = top_dicts.second;
    p = skip_index(p);  // String INDEX
    auto gsub = index(p);
    for (auto &it : gsub.first) push_bytes(gsubrs, it);
    auto top = parse_dict(top_dicts.first[0]);
    auto ch = index(base + (size_t)dict_int(top, op(0, 17)));
    for (auto &it : ch.first) push_bytes(charstrings, it);

    if (top.count(op(0, 18))) {
      auto pv = top.at(op(0, 18));
      auto priv = parse_dict(
          slice(base + (size_t)pv[1], base + (size_t)pv[1] + (size_t)pv[0]));
      if (priv.count(op(0, 19))) {
        auto si = index(base + (size_t)pv[1] + (size_t)priv.at(op(0, 19))[0]);
        for (auto &it : si.first) push_bytes(subrs, it);
      }
    }
    if (top.count(op(12, 7))) {
      auto m = top.at(op(12, 7));
      for (int k = 0; k < 6; k++) font_matrix[k] = m[k];
    }
    if (top.count(op(12, 36))) {  // CID-keyed FDArray/FDSelect
      auto fda = index(base + (size_t)top.at(op(12, 36))[0]);
      for (auto &fdraw : fda.first) {
        auto d = parse_dict(fdraw);
        vector<string> fsub;
        if (d.count(op(0, 18))) {
          auto pv = d.at(op(0, 18));
          auto priv = parse_dict(slice(base + (size_t)pv[1],
                                       base + (size_t)pv[1] + (size_t)pv[0]));
          if (priv.count(op(0, 19))) {
            auto si = index(base + (size_t)pv[1] +
                            (size_t)priv.at(op(0, 19))[0]);
            for (auto &it : si.first) push_bytes(fsub, it);
          }
        }
        fd_subrs.push_back(fsub);
      }
      if (top.count(op(12, 37)))
        read_fdselect(base + (size_t)top.at(op(12, 37))[0]);
    }
  }

  vector<Contour> glyph(uint32_t gidv) {
    vector<Contour> result;
    if (gidv >= charstrings.size()) return result;
    const vector<string> *use = &subrs;
    if (!fd_subrs.empty()) {
      int fd = fdselect.count((int)gidv) ? fdselect.at((int)gidv) : 0;
      if (fd < (int)fd_subrs.size()) use = &fd_subrs[fd];
    }
    Type2 t(charstrings, gsubrs, *use);
    t.run(charstrings[gidv]);
    t.close();
    double m0 = font_matrix[0], m1 = font_matrix[1];
    double m2 = font_matrix[2], m3 = font_matrix[3];
    double m4 = font_matrix[4], m5 = font_matrix[5];
    if (fabs(m0 - 0.001) > 1e-9 || m1 != 0 || m2 != 0 ||
        fabs(m3 - 0.001) > 1e-9) {
      const double k = 1000.0;
      for (auto &c : t.contours) {
        Contour cc;
        for (auto &pt : c)
          cc.push_back({(pt.first * m0 + pt.second * m2 + m4) * k,
                        (pt.first * m1 + pt.second * m3 + m5) * k});
        result.push_back(cc);
      }
    } else {
      result = t.contours;
    }
    return result;
  }

  vector<string> charstrings, gsubrs, subrs;
  vector<vector<string>> fd_subrs;

 private:
  const vector<uint8_t> &data;
  double font_matrix[6] = {0.001, 0, 0, 0.001, 0, 0};
  std::map<int, int> fdselect;

  static uint16_t op(int a, int b) {
    return a == 12 ? (uint16_t)((12 << 8) | b) : (uint16_t)b;
  }
  static double dict_int(const std::map<int, std::vector<double>> &d,
                         int key) {
    auto it = d.find(key);
    return it == d.end() ? 0 : it->second[0];
  }
  string slice(size_t a, size_t b) {
    if (a > data.size()) a = data.size();
    if (b > data.size()) b = data.size();
    return string(reinterpret_cast<const char *>(data.data()) + a, b - a);
  }
  static void push_bytes(vector<string> &dst, const string &bytes) {
    dst.push_back(bytes);
  }
  std::pair<vector<string>, size_t> index(size_t pos) {
    Reader r(data, pos);
    int count = r.u16();
    if (count == 0) return {{}, pos + 2};
    int osize = r.u8();
    vector<long long> offs(count + 1);
    for (int i = 0; i <= count; i++) {
      long long v = 0;
      for (int k = 0; k < osize; k++) v = (v << 8) | r.u8();
      offs[i] = v;
    }
    size_t basep = (size_t)r.tell() - 1;
    vector<string> items;
    for (int i = 0; i < count; i++)
      items.push_back(slice(basep + (size_t)offs[i],
                            basep + (size_t)offs[i + 1]));
    return {items, basep + (size_t)offs[count]};
  }
  size_t skip_index(size_t pos) { return index(pos).second; }
  void read_fdselect(size_t off) {
    Reader r(data, off);
    int fmt = r.u8();
    if (fmt == 0) {
      for (int g = 0; g < (int)charstrings.size(); g++)
        fdselect[g] = r.u8();
    } else if (fmt == 3) {
      int nr = r.u16();
      int first = r.u16();
      for (int i = 0; i < nr; i++) {
        int fd = r.u8();
        int nxt = r.u16();
        for (int g = first; g < nxt; g++) fdselect[g] = fd;
        first = nxt;
      }
    }
  }
  friend class FontC;
};

FontC::~FontC() { delete cff; }

// debug helper used during porting verification
void font_dump_debug(const string &path) {
  FontC f(path);
  fprintf(stderr, "upem=%d glyphs=%d tables=%zu cmap=%zu A=%u Z=%u\n",
          f.units_per_em, f.num_glyphs, f.tables.size(), f.cmap.size(),
          f.gid('A'), f.gid('Z'));
}

FontC::FontC(const string &path) {
  if (!read_bin(path, data)) throw FontError("cannot read " + path);
  Reader r(data);
  uint32_t tag = r.u32();
  if (tag == 0x74746366) {  // 'ttcf'
    r.u32();
    r.u32();
    r.seek(r.u32());
    r.u32();
  }
  int num = (int)r.u16();
  r.p += 6;  // searchRange, entrySelector, rangeShift
  for (int i = 0; i < num; i++) {
    string name;
    for (int k = 0; k < 4; k++) name.push_back((char)r.u8());
    r.u32();
    uint32_t off = r.u32(), length = r.u32();
    tables[name] = {(size_t)off, (size_t)length};
  }
  auto ht = tables.find("head");
  if (ht != tables.end()) {
    Reader h(data, ht->second.first);
    h.seek(h.tell() + 18);
    units_per_em = h.u16();
    h.seek(ht->second.first + 50);
    index_to_loc = h.s16();
  }
  auto mt = tables.find("maxp");
  if (mt != tables.end()) {
    Reader m(data, mt->second.first + 4);
    num_glyphs = m.u16();
  }
  read_hmtx();
  read_cmap();
  auto ct = tables.find("CFF ");
  if (ct != tables.end()) cff = new CFF(data, ct->second.first);
  else if (tables.count("loca")) read_loca();
}

void FontC::read_hmtx() {
  auto hh = tables.find("hhea"), hx = tables.find("hmtx");
  if (hh == tables.end() || hx == tables.end()) return;
  Reader h(data, hh->second.first + 34);
  int n = h.u16();
  Reader rd(data, hx->second.first);
  int adv = 0;
  for (int i = 0; i < n; i++) {
    adv = rd.u16();
    rd.s16();
    advances.push_back(adv);
  }
  last_advance = adv;
}

void FontC::read_loca() {
  size_t off = tables["loca"].first;
  Reader rd(data, off);
  long long n = num_glyphs + 1;
  loca.clear();
  if (index_to_loc == 0)
    for (long long i = 0; i < n; i++) loca.push_back(rd.u16() * 2LL);
  else
    for (long long i = 0; i < n; i++) loca.push_back((long long)r_u32(rd));
}

uint32_t FontC::r_u32(Reader &rd) const { return rd.u32(); }

vector<Contour> FontC::contours(uint32_t g, int depth) {
  if (cff) return cff->glyph(g);
  return glyf(g, depth);
}

vector<Contour> FontC::glyf(uint32_t gidv, int depth) {
  vector<Contour> out;
  if ((long long)(gidv + 1) >= (long long)loca.size() || depth > 5)
    return out;
  size_t goff = tables["glyf"].first + (size_t)loca[gidv];
  size_t glen = (size_t)(loca[gidv + 1] - loca[gidv]);
  if (glen == 0) return out;
  Reader r(data, goff);
  int ncont = r.s16();
  // skip bbox: min/max x/y as two s16 pairs, matching python's r.p += 8
  for (int k = 0; k < 4; k++) r.s16();
  if (ncont < 0) return composite(r, depth);
  vector<int> ends;
  for (int i = 0; i < ncont; i++) ends.push_back(r.u16());
  int npts = ends.empty() ? 0 : ends.back() + 1;
  int instr_len = r.u16();
  r.p += instr_len;  // hinting instructions
  vector<uint8_t> flags;
  while ((int)flags.size() < npts) {
    uint8_t f = r.u8();
    flags.push_back(f);
    if (f & 8) {
      int rep_count = r.u8();
      for (int k = 0; k < rep_count; k++) flags.push_back(f);
    }
  }
  vector<int> xs, ys;
  int v = 0;
  for (uint8_t f : flags) {
    if (f & 2) {
      int d = r.u8();
      v += (f & 16) ? d : -d;
    } else if (!(f & 16)) {
      v += r.s16();
    }
    xs.push_back(v);
  }
  v = 0;
  for (uint8_t f : flags) {
    if (f & 4) {
      int d = r.u8();
      v += (f & 32) ? d : -d;
    } else if (!(f & 32)) {
      v += r.s16();
    }
    ys.push_back(v);
  }

  int start = 0;
  for (int e : ends) {
    vector<PtOn> pts;
    for (int i = start; i <= e && i < npts; i++)
      pts.push_back({(double)xs[i], (double)ys[i],
                     bool(flags[i] & 1)});
    start = e + 1;
    if (!pts.empty())
      for (auto &c : quad_contour(pts)) out.push_back(c);
  }
  return out;
}

vector<Contour> FontC::composite(Reader &r, int depth) {
  vector<Contour> out;
  while (true) {
    int flags = r.u16(), sub_gid = r.u16();
    double a1, a2;
    if (flags & 1) { a1 = r.s16(); a2 = r.s16(); }
    else {
      a1 = (int8_t)(uint8_t)r.u8();
      a2 = (int8_t)(uint8_t)r.u8();
    }
    double xx = 1.0, yy = 1.0, xy = 0.0, yx = 0.0;
    if (flags & 8) { xx = yy = r.f2dot14(); }
    else if (flags & 0x40) { xx = r.f2dot14(); yy = r.f2dot14(); }
    else if (flags & 0x80) {
      xx = r.f2dot14(); yx = r.f2dot14();
      xy = r.f2dot14(); yy = r.f2dot14();
    }
    double dx = 0, dy = 0;
    if (flags & 2) { dx = a1; dy = a2; }
    for (auto &c : contours(sub_gid, depth + 1)) {
      Contour cc;
      for (auto &pt : c)
        cc.push_back({pt.first * xx + pt.second * xy + dx,
                      pt.first * yx + pt.second * yy + dy});
      out.push_back(cc);
    }
    if (!(flags & 0x20)) break;
  }
  return out;
}

void FontC::read_cmap() {
  auto ct = tables.find("cmap");
  if (ct == tables.end()) return;
  size_t base = ct->second.first;
  Reader rd(data, base + 2);
  int n = rd.u16();
  long best = -1;
  int best_score = -1;
  std::map<std::pair<int, int>, int> score_map = {
      {{3, 10}, 5}, {{3, 1}, 4}, {{0, 4}, 4}, {{0, 3}, 3},
      {{0, 6}, 3}, {{3, 0}, 2}, {{1, 0}, 1}};
  for (int i = 0; i < n; i++) {
    int pid = rd.u16(), eid = rd.u16();
    uint32_t off = rd.u32();
    auto sc = score_map.find({pid, eid});
    int score = sc == score_map.end() ? 0 : sc->second;
    if (score > best_score) { best_score = score; best = base + off; }
  }
  if (best < 0) return;
  Reader s(data, (size_t)best);
  int fmt = s.u16();
  if (fmt == 4) {
    s.u16(); s.u16();
    int seg = s.u16() / 2;
    s.p += 6;
    vector<int> end(seg), start(seg), delta(seg), rng(seg);
    for (int i = 0; i < seg; i++) end[i] = s.u16();
    s.u16();
    for (int i = 0; i < seg; i++) start[i] = s.u16();
    for (int i = 0; i < seg; i++) delta[i] = s.s16();
    size_t ro_pos = s.p;
    for (int i = 0; i < seg; i++) rng[i] = s.u16();
    for (int i = 0; i < seg; i++)
      for (int c = start[i]; c <= std::min(end[i], 0xFFFF); c++) {
        int g;
        if (rng[i] == 0) {
          g = (c + delta[i]) & 0xFFFF;
        } else {
          size_t gp = ro_pos + (size_t)i * 2 + (size_t)rng[i] +
                      (size_t)(c - start[i]) * 2;
          if (gp + 1 >= data.size()) continue;
          g = (data[gp] << 8) | data[gp + 1];
          if (!g) continue;
          g = (g + delta[i]) & 0xFFFF;
        }
        if (g) cmap[(uint32_t)c] = (uint32_t)g;
      }
  } else if (fmt == 12) {
    s.u16(); s.u32(); s.u32();
    uint32_t ngroups = s.u32();
    for (uint32_t gi = 0; gi < ngroups; gi++) {
      uint32_t a = s.u32(), b2 = s.u32(), g = s.u32();
      if (b2 - a > 0x10000) b2 = a + 0x10000;
      for (uint32_t c = a; c <= b2; c++)
        cmap[c] = g + (c - a);
    }
  } else if (fmt == 6) {
    s.u16(); s.u16();
    int first = s.u16(), cnt = s.u16();
    for (int i = 0; i < cnt; i++) cmap[(uint32_t)(first + i)] = s.u16();
  } else if (fmt == 0) {
    s.u16(); s.u16();
    for (int c = 0; c < 256; c++) cmap[(uint32_t)c] = s.u8();
  }
}

// -------------------------------------------------------------- rasterizer

vector<uint8_t> rasterize(const vector<Contour> &contours, int width,
                          int height, int ss = 4) {
  struct Edge { double y0, y1, x0, slope; };
  vector<Edge> edges;
  for (auto &c : contours) {
    size_t n = c.size();
    for (size_t i = 0; i < n; i++) {
      double x0 = c[i].first, y0 = c[i].second;
      auto &nx = c[(i + 1) % n];
      if (y0 != nx.second)
        edges.push_back({y0, nx.second, x0,
                         (nx.first - x0) / (nx.second - y0)});
    }
  }
  vector<uint8_t> cov((size_t)width * height, 0);
  if (edges.empty()) return cov;

  double ymin_d = 1e30, ymax_d = -1e30;
  for (auto &e : edges) {
    ymin_d = std::min(ymin_d, std::min(e.y0, e.y1));
    ymax_d = std::max(ymax_d, std::max(e.y0, e.y1));
  }
  long ymin = (long)(int)(ymin_d * ss);
  ymin = std::max(ymin, 0L);
  long ymax = (long)(int)(ymax_d * ss) + 1;
  ymax = std::min(ymax, (long)(height * ss) - 1);

  vector<double> acc(width + 2, 0.0);
  const double inv = 255.0 / (ss * ss);
  for (long sy = ymin; sy <= ymax; sy++) {
    double y = ((double)sy + 0.5) / ss;
    vector<std::pair<double, int>> xs;
    for (auto &e : edges)
      if ((e.y0 <= y && y < e.y1) || (e.y1 <= y && y < e.y0))
        xs.push_back({e.x0 + (y - e.y0) * e.slope,
                      e.y1 > e.y0 ? 1 : -1});
    if (xs.empty()) continue;
    std::sort(xs.begin(), xs.end());
    int wind = 0;
    bool open_span = false;
    double start_x = 0;
    vector<std::pair<double, double>> spans;
    for (auto &cross : xs) {
      int prev = wind;
      wind += cross.second;
      if (prev == 0 && wind != 0) {
        start_x = cross.first;
        open_span = true;
      } else if (prev != 0 && wind == 0) {
        spans.push_back({start_x, cross.first});
        open_span = false;
      }
    }
    (void)open_span;
    if (spans.empty()) continue;
    size_t row = (size_t)(sy / ss) * width;
    for (auto &sp : spans) {
      long ia = (long)(int)(sp.first * ss + 0.5);
      long ib = (long)(int)(sp.second * ss + 0.5);
      if (ib <= ia) continue;
      ia = std::max(ia, 0L);
      ib = std::min(ib, (long)(width * ss));
      long pa = ia / ss, pb = (ib - 1) / ss;
      if (pa == pb) {
        if (pa >= 0 && pa < width) acc[pa] += (double)(ib - ia);
      } else {
        if (pa >= 0 && pa < width)
          acc[pa] += (double)((pa + 1) * ss - ia);
        for (long px = pa + 1; px < pb; px++)
          if (px >= 0 && px < width) acc[px] += ss;
        if (pb >= 0 && pb < width) acc[pb] += (double)(ib - pb * ss);
      }
    }
    for (int px = 0; px < width; px++) {
      if (acc[px]) {
        int v = cov[row + px] + (int)(acc[px] * inv + 0.5);
        cov[row + px] = v > 255 ? 255 : (uint8_t)v;
        acc[px] = 0;
      }
    }
  }
  return cov;
}

// ------------------------------------------------------------------ png out

void put_chunk(vector<uint8_t> &out, const char kind[4],
               const uint8_t *payload, size_t len) {
  uint32_t n = (uint32_t)len;
  out.push_back((n >> 24) & 0xFF); out.push_back((n >> 16) & 0xFF);
  out.push_back((n >> 8) & 0xFF); out.push_back(n & 0xFF);
  vector<uint8_t> crcbuf(kind, kind + 4);
  crcbuf.insert(crcbuf.end(), payload, payload + len);
  uint32_t crc = (uint32_t)crc32(crc32(0L, Z_NULL, 0), crcbuf.data(),
                                 (uInt)crcbuf.size());
  out.insert(out.end(), crcbuf.begin(), crcbuf.end());
  out.push_back((crc >> 24) & 0xFF); out.push_back((crc >> 16) & 0xFF);
  out.push_back((crc >> 8) & 0xFF); out.push_back(crc & 0xFF);
}

bool write_png(const string &path, int width, int height,
               const vector<uint8_t> &rgba) {
  vector<uint8_t> raw;
  raw.reserve((size_t)(width * 4 + 1) * height);
  int stride = width * 4;
  for (int y = 0; y < height; y++) {
    raw.push_back(0);  // filter type None
    raw.insert(raw.end(), rgba.begin() + (size_t)y * stride,
               rgba.begin() + (size_t)(y + 1) * stride);
  }
  uLongf comp_len = compressBound((uLong)raw.size());
  vector<uint8_t> comp(comp_len);
  if (compress2(comp.data(), &comp_len, raw.data(), (uLong)raw.size(),
                9) != Z_OK)
    return false;
  comp.resize(comp_len);

  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  vector<uint8_t> out(sig, sig + 8);
  uint8_t ihdr[13];
  ihdr[0] = (width >> 24) & 0xFF; ihdr[1] = (width >> 16) & 0xFF;
  ihdr[2] = (width >> 8) & 0xFF; ihdr[3] = width & 0xFF;
  ihdr[4] = (height >> 24) & 0xFF; ihdr[5] = (height >> 16) & 0xFF;
  ihdr[6] = (height >> 8) & 0xFF; ihdr[7] = height & 0xFF;
  ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  put_chunk(out, "IHDR", ihdr, 13);
  put_chunk(out, "IDAT", comp.data(), comp.size());
  put_chunk(out, "IEND", nullptr, 0);
  return write_bin(path, out);
}

// ------------------------------------------------------------------- layout

struct LayoutResult {
  vector<Contour> shapes;
  vector<uint32_t> missing;
};

LayoutResult layout(FontC &font, const string &text_utf8) {
  LayoutResult res;
  vector<string> lines_raw;
  {
    string cur;
    for (char ch : text_utf8) {
      if (ch == '\n') { lines_raw.push_back(cur); cur.clear(); }
      else cur.push_back(ch);
    }
    lines_raw.push_back(cur);
  }
  // tabs become four spaces
  for (auto &ln : lines_raw) {
    string repl;
    for (char c : ln) {
      if (c == '\t') repl += "    ";
      else repl.push_back(c);
    }
    ln = repl;
  }

  int upem = font.units_per_em ? font.units_per_em : 1000;
  const double line_spacing = 1.25;
  double line_height = upem * line_spacing;
  uint32_t space_gid = font.gid(' ');
  int space_adv =
      space_gid ? (int)font.advance_of(space_gid) : upem / 3;

  struct Row { vector<Contour> shapes; double width; };
  vector<Row> rows;
  for (auto &line : lines_raw) {
    Row row;
    row.width = 0.0;
    double pen = 0.0;
    size_t i = 0;
    while (i < line.size()) {
      uint32_t cp = utf8_decode_at(line, i);
      if (cp == ' ') { pen += space_adv; continue; }
      uint32_t g = font.gid(cp);
      bool in_cmap = font.cmap.count(cp) != 0;
      if (g == 0 && !in_cmap) {
        bool seen = false;
        for (uint32_t m : res.missing)
          if (m == cp) { seen = true; break; }
        if (!seen) res.missing.push_back(cp);
        continue;
      }
      for (Contour c : font.contours(g)) {
        Contour shifted;
        for (auto &pt : c) shifted.push_back({pt.first + pen, pt.second});
        row.shapes.push_back(shifted);
      }
      pen += font.advance_of(g);
    }
    row.width = pen;
    rows.push_back(row);
  }

  int i = 0;
  for (auto &row : rows) {
    double dx = -row.width / 2.0, dy = -(double)i * line_height;
    for (Contour c : row.shapes) {
      Contour moved;
      for (auto &pt : c) moved.push_back({pt.first + dx, pt.second + dy});
      res.shapes.push_back(moved);
    }
    i++;
  }
  return res;
}

// -------------------------------------------------------------------- render

// A bitmap-only OpenType (OTB): glyph bitmaps straight from EBDT/EBLC, no
// outlines. Terminus ships in this shape. Index subtable formats 1 (explicit
// offsets) and 2 (constant-size slots) cover every layout these files use.
struct BitmapGlyph {
  int w = 0, h = 0, bx = 0, by = 0, adv = 0;
  size_t rows_off = 0;   // bit rows within the font data; 0 when none
};

struct BitmapRange {
  int first = 0, last = 0, image_fmt = 0;
  vector<size_t> slots;  // absolute data offset per gid
};

struct BitmapStrike {
  int cell = 0, asc = 0, desc = 0;
  vector<BitmapRange> ranges;
};

class BitmapFontC {
 public:
  explicit BitmapFontC(const string &path) : base(path) {    auto lc = base.tables.find("EBLC");
    auto dtp = base.tables.find("EBDT");
    if (lc == base.tables.end() || dtp == base.tables.end()) return;
    size_t ebdt = dtp->second.first;
    size_t eblc = lc->second.first;
    Reader r(base.data, eblc);
    r.u32();  // version
    uint32_t num_sizes = r.u32();
    for (uint32_t si = 0; si < num_sizes; si++) {
      size_t st = r.p;
      uint32_t idx_off = r.u32();
      r.u32();          // indexTablesSize
      uint32_t num_idx = r.u32();
      r.u32();          // colorRef
      BitmapStrike strike;
      strike.asc = base.data[st + 16];
      strike.desc = (int8_t)base.data[st + 17];
      strike.cell = strike.asc - strike.desc;
      r.seek(st + 48);  // hori+vert metrics live at st+16..st+40; skip to end
      for (uint32_t k = 0; k < num_idx; k++) {
        Reader s(base.data, eblc + idx_off + 8 * k);
        int first = s.u16(), last = s.u16();
        size_t sub = eblc + idx_off + s.u32();
        Reader t(base.data, sub);
        int index_fmt = t.u16(), image_fmt = t.u16();
        uint32_t image_off = t.u32();
        if (index_fmt != 1 && index_fmt != 2) continue;
        BitmapRange rng;
        rng.first = first;
        rng.last = last;
        rng.image_fmt = image_fmt;
        if (index_fmt == 2) {
          uint32_t image_size = t.u32();
          for (int g = first; g <= last; g++)
            rng.slots.push_back(ebdt + image_off +
                                (size_t)image_size * (size_t)(g - first));
        } else {
          for (int g = first; g <= last; g++)
            rng.slots.push_back(ebdt + image_off + t.u32());
        }
        strike.ranges.push_back(rng);
      }
      strikes.push_back(strike);
    }
  }

  uint32_t gid(uint32_t cp) const { return base.gid(cp); }
  bool has_cp(uint32_t cp) const { return base.cmap.count(cp) != 0; }

  // False when g sits outside the range. rows_off is 0 when the glyph carries
  // no bitmap (wrong image format, or empty ink).
  bool bitmap(const BitmapStrike &st, const BitmapRange &rng, uint32_t g,
              BitmapGlyph &out) const {
    (void)st;  // the slot carries everything; the strike only scopes ranges
    if (g < (uint32_t)rng.first || g > (uint32_t)rng.last) return false;
    size_t off = rng.slots[g - rng.first];
    Reader r(base.data, off);
    out.h = r.u8();
    out.w = r.u8();
    out.bx = (int8_t)r.u8();
    out.by = (int8_t)r.u8();
    out.adv = r.u8();
    out.rows_off = (rng.image_fmt == 1 && out.w > 0 && out.h > 0) ? off + 5 : 0;
    return true;
  }

  vector<BitmapStrike> strikes;

  FontC base;   // public: the blit walks its raw font bytes
};

// True when the sfnt table directory lists an EBDT table. Same shape as the
// Python twin's is_bitmap_font: the directory is scanned from the fixed
// header positions and a ttcf wrapper is not honoured.
static bool file_has_ebdt(const string &path) {
  vector<uint8_t> head;
  if (!read_bin(path, head)) return false;
  if (head.size() < 12) return false;
  size_t num = ((size_t)head[4] << 8) | head[5];
  for (size_t i = 0; i < num; i++) {
    size_t off = 12 + 16 * i;
    if (off + 16 > head.size()) break;
    if (head[off] == 'E' && head[off + 1] == 'B' && head[off + 2] == 'D' &&
        head[off + 3] == 'T')
      return true;
  }
  return false;
}

static vector<uint8_t> render_px_bitmap(const string &text,
                                        const string &font_path, int size,
                                        Rgba fg, std::optional<Rgba> bg,
                                        double margin, bool *ok) {
  BitmapFontC font(font_path);
  if (font.strikes.empty()) return {};

  string body;
  {
    string expanded;
    for (char ch : text) {
      if (ch == '\t') expanded += "    ";
      else expanded.push_back(ch);
    }
    body = expanded;
  }

  double box_req = size * (1.0 - 2 * margin);
  const BitmapStrike *strike = nullptr;
  const BitmapRange *range = nullptr;
  double best = 0.0;
  for (auto &st : font.strikes)
    for (auto &rg : st.ranges) {
      double d = std::fabs((double)st.cell - box_req);
      if (!strike || d < best) {
        best = d;
        strike = &st;
        range = &rg;
      }
    }
  if (!strike) return {};
  int cell = strike->cell;
  double line_height = cell * 1.25;

  uint32_t sg = font.gid(' ');
  BitmapGlyph sbm;
  bool have_space = sg && font.bitmap(*strike, *range, sg, sbm);
  int space_adv = have_space ? sbm.adv : 0;
  if (!space_adv) space_adv = cell / 3;

  vector<uint32_t> missing;
  struct Rect { double x, by; uint32_t g; int w, h; };
  vector<Rect> rects;
  vector<vector<Rect>> pen_rects;
  vector<double> pen_widths;
  {
    vector<string> lines;
    {
      string cur;
      for (char ch : body) {
        if (ch == '\n') { lines.push_back(cur); cur.clear(); }
        else cur.push_back(ch);
      }
      lines.push_back(cur);
    }
    for (auto &line : lines) {
      double pen = 0.0;
      rects.clear();
      size_t i = 0;
      while (i < line.size()) {
        uint32_t cp = utf8_decode_at(line, i);
        if (cp == ' ') { pen += space_adv; continue; }
        uint32_t g = font.gid(cp);
        if (g == 0 && !font.has_cp(cp)) {
          bool seen = false;
          for (uint32_t m : missing)
            if (m == cp) { seen = true; break; }
          if (!seen) missing.push_back(cp);
          continue;
        }
        BitmapGlyph bm;
        if (!font.bitmap(*strike, *range, g, bm)) {
          bool seen = false;
          for (uint32_t m : missing)
            if (m == cp) { seen = true; break; }
          if (!seen) missing.push_back(cp);
          continue;
        }
        if (bm.w > 0 && bm.h > 0)
          rects.push_back({pen + bm.bx, (double)bm.by, g, bm.w, bm.h});
        pen += bm.adv;
      }
      pen_rects.push_back(rects);
      pen_widths.push_back(pen);
    }
  }

  struct Placed { double x, y; uint32_t g; int w, h; };
  vector<Placed> placed;
  for (size_t i = 0; i < pen_rects.size(); i++) {
    double dx = -pen_widths[i] / 2.0;
    double baseline = (double)i * line_height;
    for (auto &rc : pen_rects[i])
      placed.push_back({rc.x + dx, baseline - rc.by, rc.g, rc.w, rc.h});
  }

  if (!missing.empty()) {
    string names;
    for (size_t i = 0; i < missing.size(); i++) {
      if (i) names += " ";
      names += repr_char(missing[i]);
    }
    say(F("Note: this style has no drawing for %s, so it was left out.",
          names.c_str()));
  }
  if (placed.empty()) return {};

  // one 1-bit mask at strike resolution, bounds from the rounded rects
  struct Cell { int gx, gy, w, h; uint32_t g; };
  vector<Cell> cells;
  for (auto &p : placed)
    cells.push_back({(int)std::floor(p.x + 0.5), (int)std::floor(p.y + 0.5),
                     p.w, p.h, p.g});
  int min_x = cells[0].gx, min_y = cells[0].gy;
  for (auto &c : cells) {
    min_x = std::min(min_x, c.gx);
    min_y = std::min(min_y, c.gy);
  }
  int bw = min_x, bh = min_y;
  for (auto &c : cells) {
    bw = std::max(bw, c.gx + c.w);
    bh = std::max(bh, c.gy + c.h);
  }
  bw -= min_x;
  bh -= min_y;
  vector<uint8_t> mask((size_t)bw * bh, 0);
  for (auto &c : cells) {
    BitmapGlyph bm;
    if (!font.bitmap(*strike, *range, c.g, bm) || !bm.rows_off) continue;
    int rowsz = (c.w + 7) / 8;
    for (int r = 0; r < c.h; r++) {
      size_t base = (size_t)(c.gy - min_y + r) * bw + (c.gx - min_x);
      for (int cc = 0; cc < c.w; cc++)
        if ((font.base.data[bm.rows_off + (size_t)r * rowsz + cc / 8] >>
             (7 - (cc % 8))) & 1)
          mask[base + cc] = 255;
    }
  }

  // nearest-neighbour into the same inner box the vector path fills
  double w = bw, h = bh;
  double box = size * (1.0 - 2 * margin);
  double scale = box / std::max(w, h);
  int tw = std::max((int)(w * scale + 0.5), 1);
  int th = std::max((int)(h * scale + 0.5), 1);
  int ox = (size - tw) / 2;
  int oy = (size - th) / 2;
  vector<uint8_t> cov((size_t)size * size, 0);
  for (int ty = 0; ty < th; ty++) {
    long long sy = (long long)ty * bh / th;
    size_t srow = (size_t)sy * bw;
    size_t orow = (size_t)(oy + ty) * size + ox;
    for (int tx = 0; tx < tw; tx++)
      if (mask[srow + (size_t)((long long)tx * bw / tw)])
        cov[orow + tx] = 255;
  }

  vector<uint8_t> px((size_t)size * size * 4);
  uint8_t br, bgc, bb, ba;
  if (bg) { br = bg->r; bgc = bg->g; bb = bg->b; ba = bg->a; }
  else { br = fg.r; bgc = fg.g; bb = fg.b; ba = 0; }
  const uint8_t fr = fg.r, fgc = fg.g, fb = fg.b, fa = fg.a;

  for (int i = 0; i < size * size; i++) {
    int a = cov[i];
    size_t j = (size_t)i * 4;
    if (a == 0) {
      px[j] = br; px[j+1] = bgc; px[j+2] = bb; px[j+3] = ba;
    } else if (a == 255 && fa == 255) {
      px[j] = fr; px[j+1] = fgc; px[j+2] = fb; px[j+3] = 255;
    } else {
      double t = a / 255.0 * (fa / 255.0);
      double u = 1 - t;
      double oa = ba / 255.0 * u + t;
      if (oa <= 0) {
        px[j] = 0; px[j+1] = 0; px[j+2] = 0; px[j+3] = 0;
      } else {
        px[j]   = (uint8_t)(int)((br * (ba / 255.0) * u + fr * t) / oa);
        px[j+1] = (uint8_t)(int)((bgc * (ba / 255.0) * u + fgc * t) / oa);
        px[j+2] = (uint8_t)(int)((bb * (ba / 255.0) * u + fb * t) / oa);
        px[j+3] = (uint8_t)(int)(oa * 255 + 0.5);
      }
    }
  }
  *ok = true;
  return px;
}

vector<uint8_t> render_px(const string &text, const string &font_path,
                          int size, Rgba fg, std::optional<Rgba> bg,
                          double margin, bool *ok) {
  *ok = false;
  if (!path_exists(font_path)) return {};
  if (file_has_ebdt(font_path))
    return render_px_bitmap(text, font_path, size, fg, bg, margin, ok);
  FontC font(font_path);
  LayoutResult lay = layout(font, text);
  vector<Contour> shapes = lay.shapes;
  if (!lay.missing.empty()) {
    string names;
    for (size_t i = 0; i < lay.missing.size(); i++) {
      if (i) names += " ";
      names += repr_char(lay.missing[i]);
    }
    say(F("Note: this style has no drawing for %s, so it was left out.",
          names.c_str()));
  }
  if (shapes.empty()) return {};

  double x0 = 1e30, x1 = -1e30, y0 = 1e30, y1 = -1e30;
  for (auto &c : shapes)
    for (auto &p : c) {
      x0 = std::min(x0, p.first); x1 = std::max(x1, p.first);
      y0 = std::min(y0, p.second); y1 = std::max(y1, p.second);
    }
  double w = std::max(x1 - x0, 1e-6), h = std::max(y1 - y0, 1e-6);
  double box = size * (1.0 - 2 * margin);
  double scale = box / std::max(w, h);
  double ox = (size - w * scale) / 2.0 - x0 * scale;
  double oy = (size - h * scale) / 2.0 - y0 * scale;

  vector<Contour> dev;
  for (auto &c : shapes) {
    Contour cd;
    for (auto &p : c)
      cd.push_back({p.first * scale + ox,
                    size - (p.second * scale + oy)});
    dev.push_back(cd);
  }

  vector<uint8_t> cov = rasterize(dev, size, size);
  vector<uint8_t> px((size_t)size * size * 4);
  uint8_t br, bgc, bb, ba;
  if (bg) { br = bg->r; bgc = bg->g; bb = bg->b; ba = bg->a; }
  else { br = fg.r; bgc = fg.g; bb = fg.b; ba = 0; }
  const uint8_t fr = fg.r, fgc = fg.g, fb = fg.b, fa = fg.a;

  for (int i = 0; i < size * size; i++) {
    int a = cov[i];
    size_t j = (size_t)i * 4;
    if (a == 0) {
      px[j] = br; px[j+1] = bgc; px[j+2] = bb; px[j+3] = ba;
    } else if (a == 255 && fa == 255) {
      px[j] = fr; px[j+1] = fgc; px[j+2] = fb; px[j+3] = 255;
    } else {
      double t = a / 255.0 * (fa / 255.0);
      double u = 1 - t;
      double oa = ba / 255.0 * u + t;
      if (oa <= 0) {
        px[j] = 0; px[j+1] = 0; px[j+2] = 0; px[j+3] = 0;
      } else {
        px[j]   = (uint8_t)(int)((br * (ba / 255.0) * u + fr * t) / oa);
        px[j+1] = (uint8_t)(int)((bgc * (ba / 255.0) * u + fgc * t) / oa);
        px[j+2] = (uint8_t)(int)((bb * (ba / 255.0) * u + fb * t) / oa);
        px[j+3] = (uint8_t)(int)(oa * 255 + 0.5);
      }
    }
  }
  *ok = true;
  return px;
}

void preview(const vector<uint8_t> &px, int size, int cols) {
  if (!supports_color()) return;
  int rows = cols;
  double step = size / (double)cols;
  vector<string> lines;
  for (int ry = 0; ry < rows; ry += 2) {
    string line;
    for (int rx = 0; rx < cols; rx++) {
      auto grab = [&](int r) -> Rgba {
        int y = std::min(size - 1, (int)((r + 0.5) * step));
        int x = std::min(size - 1, (int)((rx + 0.5) * step));
        size_t i = ((size_t)y * size + x) * 4;
        Rgba c{px[i], px[i+1], px[i+2], px[i+3]};
        if (c.a < 255) {
          double t = c.a / 255.0;
          c.r = (uint8_t)(int)(c.r * t + 128 * (1 - t));
          c.g = (uint8_t)(int)(c.g * t + 128 * (1 - t));
          c.b = (uint8_t)(int)(c.b * t + 128 * (1 - t));
        }
        return c;
      };
      Rgba top = grab(ry), bot = grab(ry + 1);
      line += F("\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm", top.r, top.g,
                top.b, bot.r, bot.g, bot.b);
      line += "\xE2\x96\x80";
    }
    lines.push_back("   " + line + "\x1b[0m");
  }
  say();
  for (auto &l : lines) say(l);
  say();
}

// ------------------------------------------------------------- look settings

string prog_hint(const char *flag) {
  return F("%s %s  to see the choices.", PROG.c_str(), flag);
}

int int_of(const string &raw, bool *ok) {
  *ok = false;
  size_t b = raw.find_first_not_of(" \t\r\n");
  if (b == string::npos) return 0;
  size_t e = raw.find_last_not_of(" \t\r\n");
  string s = raw.substr(b, e - b + 1);
  size_t i = 0;
  bool neg = false;
  if (!s.empty() && (s[i] == '+' || s[i] == '-')) { neg = s[i] == '-'; i++; }
  if (i >= s.size()) return 0;
  long long v = 0;
  for (; i < s.size(); i++) {
    if (!isdigit((unsigned char)s[i])) return 0;
    v = v * 10 + (s[i] - '0');
    if (v > 1000000000LL) v = 1000000000LL;
  }
  *ok = true;
  return (int)(neg ? -v : v);
}

Options look_settings(const std::map<string, string> &item,
                      const string &where) {
  Options o;
  auto get = [&](const string &k) -> string {
    auto it = item.find(k);
    return it == item.end() ? string() : it->second;
  };
  o.style = get("style");
  if (o.style.empty()) o.style = DEFAULT_STYLE;
  const StyleEntry *se = style_find(o.style);
  if (!se)
    throw ListError(F("%s: there is no style called \"%s\".\n  Run  %s",
                      where.c_str(), o.style.c_str(),
                      prog_hint("--styles").c_str()));
  string theme = get("colors");
  if (theme.empty()) theme = DEFAULT_THEME;
  const Theme *th = theme_find(theme);
  if (!th)
    throw ListError(F("%s: there is no color pair called \"%s\".\n  Run  %s",
                      where.c_str(), theme.c_str(),
                      prog_hint("--colors-list").c_str()));
  string fg_name = get("fg");
  if (fg_name.empty()) fg_name = th->fg;
  string bg_name = get("bg");
  if (bg_name.empty()) bg_name = th->bg;

  Rgba fgv{}, bgv{};
  ColorParse fr = parse_color(fg_name, &fgv);
  if (fr == CP_BAD)
    throw ListError(
        F("%s: I don't recognise the color \"%s\".\n"
          "  Use a color name, a hex code like #c0392b, or 'none'.",
          where.c_str(), fg_name.c_str()));
  if (fr == CP_NONE)
    throw ListError(F("%s: the letters need a visible color; 'none' only "
                      "works for the background.",
                      where.c_str()));

  ColorParse br = parse_color(bg_name, &bgv);
  if (br == CP_BAD)
    throw ListError(
        F("%s: I don't recognise the color \"%s\".\n"
          "  Use a color name, a hex code like #c0392b, or 'none'.",
          where.c_str(), bg_name.c_str()));
  std::optional<Rgba> bgv_opt =
      br == CP_OK ? std::optional<Rgba>(bgv) : std::optional<Rgba>();
  o.fg = fgv;
  o.bg = bgv_opt;

  auto number = [&](const char *key, int dflt, int low, int high) -> int {
    string raw = get(key);
    int v = dflt;
    if (!raw.empty()) {
      bool ok = false;
      v = int_of(raw, &ok);
      if (!ok)
        throw ListError(F("%s: %s should be a whole number, not \"%s\".",
                          where.c_str(), key, raw.c_str()));
    }
    if (v < low || v > high)
      throw ListError(F("%s: %s should be between %d and %d.",
                        where.c_str(), key, low, high));
    return v;
  };
  o.size = number("size", 640, 16, 4096);
  int padding = number("padding", 14, 0, 40);
  o.path = join_path(FONTDIR, se->file);
  o.margin = padding / 100.0;
  return o;
}

Options settings_from(const std::map<string, string> &item,
                      const string &where) {
  Options o = look_settings(item, where);
  return o;
}

std::map<string, string> bundle_to_item(const JVal &bundle) {
  std::map<string, string> item;
  auto put_if = [&](const char *keys[], const char *dst) {
    for (const char **k = keys; *k; k++) {
      const JVal *f = jget(bundle, *k);
      if (f && f->t == JVal::STR && !f->str.empty()) {
        item[dst] = f->str;
        return;
      }
      if (f && f->t == JVal::NUM) {
        item[dst] = num_repr(f->num);
        return;
      }
    }
  };
  const char *k_style[] = {"style", nullptr};
  const char *k_colors[] = {"colors", nullptr};
  const char *k_fg[] = {"text-color", "fg", nullptr};
  const char *k_bg[] = {"background", "bg", nullptr};
  const char *k_size[] = {"size", nullptr};
  const char *k_padding[] = {"padding", nullptr};
  put_if(k_style, "style");
  put_if(k_colors, "colors");
  put_if(k_fg, "fg");
  put_if(k_bg, "bg");
  put_if(k_size, "size");
  put_if(k_padding, "padding");
  return item;
}

Options bundle_to_settings(const JVal &bundle, const string &where) {
  return look_settings(bundle_to_item(bundle), where);
}

const JVal *find_bundle(const string &name, const vector<JVal> &bundles) {
  string low = safe_lower_ascii(name);
  for (auto &b : bundles) {
    const JVal *f = jget(b, "name");
    if (f && f->t == JVal::STR &&
        safe_lower_ascii(f->str) == low)
      return &b;
  }
  return nullptr;
}

// ------------------------------------------------------------------ lists

vector<string> split_lines_keep(const string &s) {
  vector<string> lines;
  string cur;
  for (char c : s) {
    if (c == '\n') { lines.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  lines.push_back(cur);
  return lines;
}

string replace_all(string s, const string &a, const string &b) {
  if (a.empty()) return s;
  size_t pos = 0;
  while ((pos = s.find(a, pos)) != string::npos) {
    s.replace(pos, a.size(), b);
    pos += b.size();
  }
  return s;
}

string canon_key(const string &k) {
  static const std::map<string, string> KEYS = {
      {"style", "style"}, {"colors", "colors"}, {"color", "colors"},
      {"text-color", "fg"}, {"textcolor", "fg"}, {"text color", "fg"},
      {"background", "bg"}, {"background-color", "bg"},
      {"size", "size"}, {"padding", "padding"}, {"text", "text"},
      {"folder", "folder"}, {"name", "name"}};
  auto it = KEYS.find(k);
  return it == KEYS.end() ? string() : it->second;
}


LogoBook read_list(const string &path) {
  bool ok = false;
  string raw = read_text_file(path, &ok);
  if (!ok)
    throw ListError(F("I couldn't open %s (%s).", path.c_str(),
                      strerror(errno)));

  std::map<string, string> defaults;
  vector<std::map<string, string>> blocks;
  std::map<string, string> *current = nullptr;
  auto lines = split_lines_keep(raw);
  int lineno = 0;
  size_t idx = 0;
  while (idx < lines.size()) {
    string line = lines[idx];
    idx++;
    lineno++;
    string bare;
    {
      size_t b = line.find_first_not_of(" \t\r");
      bare = b == string::npos ? "" : line.substr(b);
    }
    if (bare.empty() || bare[0] == '#') continue;

    if (bare.size() >= 2 && bare.front() == '[' && bare.back() == ']') {
      string name = bare.substr(1, bare.size() - 2);
      // trim spaces
      size_t b = name.find_first_not_of(" \t");
      size_t e = name.find_last_not_of(" \t");
      name = b == string::npos ? "" : name.substr(b, e - b + 1);
      if (name.empty())
        throw ListError(F("Line %d: a block needs a name, like [logo1].",
                          lineno));
      if (safe_lower_ascii(name) == "defaults") current = &defaults;
      else {
        blocks.emplace_back();
        current = &blocks.back();
        (*current)["name"] = name;
      }
      continue;
    }

    if (!current)
      throw ListError(
          F("Line %d: this setting comes before any [name] block.\n"
            "  Start a logo with a name in brackets first, like [logo1].",
            lineno));

    long sep = -1;
    long p1 = (long)bare.find('='), p2 = (long)bare.find(':');
    if (p1 > 0 && p2 > 0) sep = std::min(p1, p2);
    else if (p1 > 0) sep = p1;
    else if (p2 > 0) sep = p2;
    if (sep < 0)
      throw ListError(F("Line %d: I expected  setting = value  here,\n"
                        "  but found: %s",
                        lineno, bare.c_str()));
    string key = bare.substr(0, (size_t)sep);
    string value = bare.substr((size_t)sep + 1);
    {
      size_t kb = key.find_first_not_of(" \t");
      size_t ke = key.find_last_not_of(" \t");
      key = kb == string::npos ? ""
                               : safe_lower_ascii(key.substr(kb, ke - kb + 1));
      size_t vb = value.find_first_not_of(" \t");
      size_t ve = value.find_last_not_of(" \t\r");
      value = vb == string::npos ? "" : value.substr(vb, ve - vb + 1);
    }
    string canon = canon_key(key);
    if (canon.empty())
      throw ListError(
          F("Line %d: I don't know the setting \"%s\".\n"
            "  You can use: style, colors, text-color, background, "
            "size, padding, text, folder",
            lineno, key.c_str()));

    if (canon == "text" && value.empty()) {
      vector<string> body;
      while (idx < lines.size()) {
        string nxt = lines[idx];
        string t;
        size_t nb = nxt.find_first_not_of(" \t\r");
        t = nb == string::npos ? "" : nxt.substr(nb);
        if (!t.empty() && !isspace((unsigned char)nxt[0])) break;
        body.push_back(nxt);
        idx++;
      }
      while (!body.empty() &&
             body.front().find_first_not_of(" \t\r") == string::npos)
        body.erase(body.begin());
      while (!body.empty() &&
             body.back().find_first_not_of(" \t\r") == string::npos)
        body.pop_back();
      if (body.empty())
        throw ListError(F("Line %d: 'text:' is empty, and no indented "
                          "lines follow it.",
                          lineno));
      int cut = INT_MAX;
      for (auto &bl : body) {
        size_t nb = bl.find_first_not_of(" \t");
        if (nb != string::npos) cut = std::min(cut, (int)nb);
      }
      if (cut == INT_MAX) cut = 0;
      string joined;
      for (size_t bi = 0; bi < body.size(); bi++) {
        const string &bl = body[bi];
        string piece =
            (int)bl.size() >= cut ? bl.substr((size_t)cut) : bl;
        if (bi) joined.push_back('\n');
        joined += piece;
      }
      value = joined;
    } else if (canon == "text") {
      value = replace_all(value, "\\n", "\n");
    }

    (*current)[canon] = value;
  }

  LogoBook book;
  book.folder = "pictures";
  {
    auto it = defaults.find("folder");
    if (it != defaults.end()) {
      book.folder = it->second;
      defaults.erase(it);
    }
  }
  for (auto &block : blocks) {
    std::map<string, string> item = defaults;
    item.erase("name");
    item.erase("folder");
    for (auto &kv : block) item[kv.first] = kv.second;
    string name = item.count("name") ? item["name"] : "?";
    auto it = item.find("text");
    if (it == item.end() || it->second.empty()) {
      string up2 = name.substr(0, std::min<size_t>(2, name.size()));
      for (auto &c : up2) c = (char)toupper((unsigned char)c);
      throw ListError(
          F("The logo [%s] has no text to draw.\n"
            "  Add a line such as:  text = %s",
            name.c_str(), up2.c_str()));
    }
    book.logos.push_back(item);
  }
  if (book.logos.empty())
    throw ListError(F("There are no logos in %s yet.\n"
                      "  A logo looks like:\n\n    [my-logo]\n"
                      "    style = modern\n    text  = AB",
                      path.c_str()));
  return book;
}

// ------------------------------------------------------------------- texts


TextEntry entry_from_lines(const vector<string> &lines) {
  TextEntry e;
  vector<string> body = lines;
  if (!body.empty()) {
    string first = body[0];
    {
      size_t b = first.find_first_not_of(" \t");
      size_t ee = first.find_last_not_of(" \t\r");
      first = b == string::npos ? "" : first.substr(b, ee - b + 1);
    }
    if (first.size() >= 2 && first.front() == '[' && first.back() == ']') {
      e.label = safe_name(first.substr(1, first.size() - 2));
      body.erase(body.begin());
    }
  }
  if (e.label.empty())
    e.label = !body.empty() ? safe_name(body[0]) : "text";
  string text;
  for (size_t i = 0; i < body.size(); i++) {
    if (i) text.push_back('\n');
    text += body[i];
  }
  e.text = text;
  return e;
}

string unescape_line(string line) {
  line = replace_all(line, "\\\\", "\x01");
  line = replace_all(line, "\\.", ".");
  line = replace_all(line, "\x01", "\\");
  line = replace_all(line, "\\t", "\t");
  return line;
}

vector<TextEntry> read_texts(const string &path) {
  bool ok = false;
  string raw = read_text_file(path, &ok);
  if (!ok)
    throw ListError(F("I couldn't open %s (%s).", path.c_str(),
                      strerror(errno)));
  raw = replace_all(raw, "\r\n", "\n");
  vector<TextEntry> entries;
  size_t pos = 0;
  while (pos <= raw.size()) {
    size_t nx = raw.find("\n\n", pos);
    string block;
    if (nx == string::npos) {
      block = raw.substr(pos);
      pos = raw.size() + 1;
    } else {
      block = raw.substr(pos, nx - pos);
      pos = nx + 2;
    }
    vector<string> lines;
    for (auto &ln : split_lines_keep(block)) {
      string rt = ln;
      while (!rt.empty() && (rt.back() == ' ' || rt.back() == '\t' ||
                             rt.back() == '\r'))
        rt.pop_back();
      size_t nb = rt.find_first_not_of(" \t");
      if (rt.empty() || rt[nb] == '#') continue;
      lines.push_back(rt);
    }
    if (lines.empty()) continue;
    TextEntry e = entry_from_lines(lines);
    if (!e.text.empty()) entries.push_back(e);
  }
  if (entries.empty())
    throw ListError(F("There are no texts in %s yet.\n"
                      "  A text is just the words; a blank line starts the\n"
                      "  next one. Add some with:  %s --add-texts %s",
                      path.c_str(), PROG.c_str(),
                      basename_of(path).c_str()));
  return entries;
}

void write_texts(const string &path, const vector<TextEntry> &entries) {
  string out;
  out += "# A list of texts for the logo maker.\n";
  out += "# Run it with:   " + PROG + " --bundle NAME --texts " +
         basename_of(path) + "\n";
  out += "#\n";
  out += "# Each text starts with its [label] - that becomes the file name.\n";
  out += "# One line = one picture; several lines = one picture with several\n";
  out += "# lines. A blank line starts the next text.\n";
  out += "\n";
  for (auto &e : entries) {
    out += "[" + e.label + "]\n";
    out += e.text;
    out += "\n\n";
  }
  std::ofstream fh(path, std::ios::binary | std::ios::trunc);
  fh << out;
}

vector<string> unique_labels(const vector<TextEntry> &entries) {
  std::set<string> used;
  vector<string> names;
  for (auto &e : entries) {
    string base = safe_name(e.label);
    string name = base;
    int n = 2;
    while (used.count(safe_lower_ascii(name))) {
      name = F("%s-%d", base.c_str(), n++);
    }
    used.insert(safe_lower_ascii(name));
    names.push_back(name);
  }
  return names;
}


void makedirs_x(const string &dir) {
  string cur;
  for (size_t i = 0; i <= dir.size(); i++) {
    if (i == dir.size() || dir[i] == '/') {
      cur = dir.substr(0, i);
      if (!cur.empty() && !is_dir(cur) && mkdir(cur.c_str(), 0777) != 0 &&
          errno != EEXIST)
        throw SaveError(strerror(errno));
    }
  }
}

void write_png_x(const string &path, int width, int height,
                 const vector<uint8_t> &px) {
  if (!write_png(path, width, height, px))
    throw SaveError(strerror(errno));
}


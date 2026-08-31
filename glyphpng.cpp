// C++ twin of glyphpng.py - same features, stdlib + zlib only.
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

#include "core/glyphpng_core.hpp"

static void init_paths(char **argv) {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n > 0) {
    buf[n] = 0;
    HERE = dirname_of(string(buf));
  } else {
    string a0 = argv[0] ? argv[0] : "glyphpng";
    HERE = is_abs(a0) ? dirname_of(a0) : getcwd(nullptr, 0) ? getcwd(nullptr, 0)
                                                            : ".";
  }
  FONTDIR = join_path(HERE, "assets/fonts");
  PROG = basename_of(argv[0] ? argv[0] : "glyphpng");
  if (is_file(join_path(HERE, "logo"))) PROG = "./logo";
}

void run_list(const string &path, const string &out_dir) {
  LogoBook book = read_list(path);
  string folder = out_dir.empty() ? book.folder : out_dir;
  if (!is_abs(folder)) folder = join_path(HERE, folder);

  vector<std::pair<std::map<string, string>, Options>> plan;
  for (auto &item : book.logos)
    plan.push_back({item, look_settings(
                              item, F("the logo [%s]",
                                      item.at("name").c_str()))});
  std::set<string> seen_names;
  for (auto &pr : plan) {
    string f = safe_name(pr.first.at("name"));
    if (seen_names.count(f))
      throw ListError(F("Two logos would be saved as %s.png.\n"
                        "  Give [%s] a different name.",
                        f.c_str(), pr.first.at("name").c_str()));
    seen_names.insert(f);
  }

  makedirs_x(folder);
  say();
  say(F("Reading %s - %zu logo%s to draw.", path.c_str(), plan.size(),
        plan.size() == 1 ? "" : "s"));
  say(F("Writing into %s", folder.c_str()));
  say();

  int made = 0;
  for (auto &pr : plan) {
    string out = join_path(folder, safe_name(pr.first.at("name")) + ".png");
    bool ok = false;
    string text = pr.first.count("text") ? pr.first.at("text") : "";
    vector<uint8_t> px =
        render_px(text, pr.second.path, pr.second.size, pr.second.fg,
                  pr.second.bg, pr.second.margin, &ok);
    if (!ok) throw RenderError();
    write_png(out, pr.second.size, pr.second.size, px);
    string shown = text;
    {
      size_t nl = shown.find('\n');
      if (nl != string::npos) shown = shown.substr(0, nl) + " ...";
    }
    say(F("  %-22s %4d x %-4d  %-14s %s", basename_of(out).c_str(),
          pr.second.size, pr.second.size, pr.second.style.c_str(),
          shown.c_str()));
    made++;
  }
  say();
  say(F("Done - %d picture%s in %s", made, made == 1 ? "" : "s",
        folder.c_str()));
}

string texts_path() { return join_path(HERE, "texts.txt"); }

// -------------------------------------------------------------- batch draws

int render_entries(const vector<TextEntry> &entries,
                   const vector<JVal> &bundles, const string &folder) {
  vector<string> names = unique_labels(entries);
  bool many = bundles.size() > 1;

  std::set<string> seen;
  struct Plan { string stem, text; Options opts; };
  vector<Plan> plan;
  for (auto &b : bundles) {
    string bname = "bundle";
    if (const JVal *f = jget(b, "name"); f && f->t == JVal::STR)
      bname = f->str;
    Options opts = bundle_to_settings(
        b, F("the bundle [%s]", bname.c_str()));
    for (size_t i = 0; i < entries.size(); i++) {
      string stem = many ? safe_name(bname) + "-" + names[i] : names[i];
      if (seen.count(stem))
        throw ListError(F("Two results would be saved as %s.png.\n"
                          "  Give them different names.",
                          stem.c_str()));
      seen.insert(stem);
      plan.push_back({stem, entries[i].text, opts});
    }
  }

  int made = 0;
  for (auto &p : plan) {
    string out = join_path(folder, p.stem + ".png");
    bool ok = false;
    vector<uint8_t> px =
        render_px(p.text, p.opts.path, p.opts.size, p.opts.fg, p.opts.bg,
                  p.opts.margin, &ok);
    if (!ok) throw RenderError();
    write_png(out, p.opts.size, p.opts.size, px);
    string shown = p.text;
    {
      size_t nl = shown.find('\n');
      if (nl != string::npos) shown = shown.substr(0, nl) + " ...";
    }
    say(F("  %-24s %4d x %-4d  %-14s %s", basename_of(out).c_str(),
          p.opts.size, p.opts.size, p.opts.style.c_str(), shown.c_str()));
    made++;
  }
  return made;
}

void run_texts(const string &path, const vector<JVal> &bundles,
               const string &out_dir) {
  vector<TextEntry> entries = read_texts(path);
  string folder = out_dir.empty() ? join_path(HERE, "pictures") : out_dir;
  if (!is_abs(folder)) folder = join_path(HERE, folder);

  makedirs_x(folder);
  say();
  say(F("Reading %s - %zu text%s.", path.c_str(), entries.size(),
        entries.size() == 1 ? "" : "s"));
  say(F("Drawing with %zu bundle%s.", bundles.size(),
        bundles.size() == 1 ? "" : "s"));
  say(F("Writing into %s", folder.c_str()));
  say();

  int made = render_entries(entries, bundles, folder);

  say();
  say(F("Done - %d picture%s in %s", made, made == 1 ? "" : "s",
        folder.c_str()));
}

struct Args;
void draw_multi_bundle(const Args &a);

// ------------------------------------------------------------------ editors

vector<string> find_editor_cmd() {
  for (const char *var : {"VISUAL", "EDITOR"}) {
    const char *v = getenv(var);
    if (v && *v) {
      vector<string> parts;
      std::istringstream ss(v);
      string tok;
      while (ss >> tok) parts.push_back(tok);
      if (!parts.empty()) return parts;
    }
  }
  static const char *CANDIDATES[] = {"nvim",   "vim",     "vi",
                                     "nano",   "micro",   "emacs",
                                     "l3afpad", "leafpad", "gedit",
                                     "notepad"};
  const char *path_env = getenv("PATH");
  vector<string> dirs;
  {
    string p = path_env ? path_env : "/usr/bin:/bin";
    size_t s = 0;
    while (s <= p.size()) {
      size_t c = p.find(':', s);
      if (c == string::npos) { dirs.push_back(p.substr(s)); break; }
      dirs.push_back(p.substr(s, c - s));
      s = c + 1;
    }
  }
  for (const char *cand : CANDIDATES)
    for (auto &dir : dirs) {
      string full = dir.empty() ? cand : dir + "/" + cand;
      if (access(full.c_str(), X_OK) == 0) return {full};
    }
  return {};
}

bool open_in_editor(const vector<string> &cmd, const string &path) {
  if (cmd.empty()) {
    say("  I couldn't find an editor (nvim, nano, micro, ...).");
    say("  Install one, or set your own:  export EDITOR=nano");
    return false;
  }
  pid_t pid = fork();
  if (pid < 0) {
    say(F("  I couldn't start %s (%s).", cmd[0].c_str(),
          strerror(errno)));
    return false;
  }
  if (pid == 0) {
    vector<char *> argv;
    for (auto &c : cmd)
      argv.push_back(const_cast<char *>(c.c_str()));
    argv.push_back(const_cast<char *>(path.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return true;
}

std::optional<string> edit_text_in_editor(const string &text) {
  string tmpl = "/tmp/logo-text-XXXXXX.txt";
  vector<char> buf(tmpl.begin(), tmpl.end());
  buf.push_back(0);
  int fd = mkstemps(buf.data(), 4);
  if (fd < 0) return std::nullopt;
  string tmp(buf.data());
  auto readback = [&]() -> string {
    bool ok2 = false;
    return read_text_file(tmp, &ok2);
  };
  std::optional<string> result;
  do {
    ssize_t w = write(fd, (text + "\n").data(), text.size() + 1);
    (void)w;
    close(fd);
    string before = readback();
    vector<string> cmd = find_editor_cmd();
    if (!open_in_editor(cmd, tmp)) break;
    string content = readback();
    if (content == before) {
      std::printf("  Save & close the editor, then press Enter... ");
      std::fflush(stdout);
      string dummy;
      std::getline(std::cin, dummy);
      content = readback();
    }
    content = replace_all(content, "\r\n", "\n");
    size_t b = content.find_first_not_of("\n");
    size_t e = content.find_last_not_of("\n");
    if (b == string::npos) break;
    result = content.substr(b, e - b + 1);
  } while (false);
  unlink(tmp.c_str());
  return result;
}

const char *TEXT_TEMPLATE =
    "# A list of texts for the logo maker.\n"
    "# Run it with:   ./%P --bundle NAME --texts %F\n"
    "#\n"
    "# Each text starts with its [label] - that becomes the file name.\n"
    "# One line = one picture; several lines = one picture with several\n"
    "# lines. A blank line starts the next text.\n"
    "#\n"
    "# A bundle (--bundle) supplies the style, colors, size and padding,\n"
    "# so all you write here are the words. Edit this file from the app:\n"
    "#     ./%P --edit-texts\n"
    "\n"
    "[whatsapp]\n"
    "N\n"
    "\n"
    "[poster]\n"
    "GOOD\n"
    "NIGHT\n"
    "\n"
    "OK\n";

bool create_texts_file(const string &target) {
  std::ofstream fh(target, std::ios::binary | std::ios::trunc);
  if (!fh) {
    say(F("I couldn't write %s (%s).", target.c_str(), strerror(errno)));
    return false;
  }
  string body = TEXT_TEMPLATE;
  body = replace_all(body, "%F", basename_of(target));
  body = replace_all(body, "%P", PROG);
  fh << body;
  return true;
}

vector<TextEntry> read_texts_batch() {
  say();
  say("  Type your texts below.");
  say("  Start with [label] to choose the file name; otherwise");
  say("  the first line becomes it.");
  say("  Press Enter for a new line in the SAME picture.");
  say("  A blank line starts a NEW picture.");
  say("  Type a single . on its own line when you are done.");
  say("  (A line that should be just a dot: type \\.)");
  say();
  vector<string> lines;
  while (true) {
    std::printf("  > ");
    std::fflush(stdout);
    string line;
    if (!std::getline(std::cin, line)) break;
    size_t b = line.find_first_not_of(" \t\r");
    if (b != string::npos && line[b] == '.' &&
        line.find_first_not_of(" \t\r", b + 1) == string::npos)
      break;
    lines.push_back(unescape_line(line));
  }
  say();

  vector<vector<string>> blocks;
  vector<string> block;
  for (auto &ln : lines) {
    size_t nb = ln.find_first_not_of(" \t\r");
    if (nb != string::npos) block.push_back(ln);
    else if (!block.empty()) {
      blocks.push_back(block);
      block.clear();
    }
  }
  if (!block.empty()) blocks.push_back(block);
  vector<TextEntry> out;
  for (auto &b : blocks) {
    TextEntry e = entry_from_lines(b);
    if (!e.text.empty()) out.push_back(e);
  }
  return out;
}

void add_texts_interactive(const string &path) {
  vector<TextEntry> existing;
  if (path_exists(path)) {
    try {
      existing = read_texts(path);
    } catch (ListError &) {
      say(F("I couldn't read %s, so I'll start fresh.", path.c_str()));
      existing.clear();
    }
  }
  say();
  say(F("  Add texts to %s", path.c_str()));
  vector<TextEntry> fresh = read_texts_batch();
  if (fresh.empty()) {
    say("  Nothing added.");
    return;
  }
  existing.insert(existing.end(), fresh.begin(), fresh.end());
  write_texts(path, existing);
  say(F("  Saved %zu new text%s:", fresh.size(),
        fresh.size() == 1 ? "" : "s"));
  for (auto &e : fresh) {
    string first = e.text.substr(0, e.text.find('\n'));
    say(F("    %-16s %s", e.label.c_str(), first.c_str()));
  }
  say(F("  Draw them:  %s --bundle NAME --texts %s", PROG.c_str(),
        basename_of(path).c_str()));
  say();
}

void cmd_new_texts(const string &target) {
  if (path_exists(target)) {
    say(F("%s already exists, so I left it alone.", target.c_str()));
    say(F("Add to it with:  %s --add-texts %s", PROG.c_str(),
          target.c_str()));
    std::exit(1);
  }
  if (!create_texts_file(target)) std::exit(1);
  say(F("Created %s with three example texts inside.", target.c_str()));
  say(F("Add more easily with:  %s --add-texts %s", PROG.c_str(),
        target.c_str()));
  say(F("Edit it anytime with:  %s --edit-texts %s", PROG.c_str(),
        target.c_str()));
}

// -------------------------------------------------------- interactive core

struct Cancelled {};

string ask(const string &prompt, const std::optional<string> &dflt) {
  string suffix = dflt ? F(" [%s]: ", dflt->c_str()) : ": ";
  std::printf("%s%s", prompt.c_str(), suffix.c_str());
  std::fflush(stdout);
  string answer;
  if (!std::getline(std::cin, answer)) throw Cancelled();
  while (!answer.empty() &&
         (answer.back() == '\r' || answer.back() == ' ' ||
          answer.back() == '\t'))
    answer.pop_back();
  size_t b = answer.find_first_not_of(" \t");
  answer = b == string::npos ? "" : answer.substr(b);
  if (answer.empty() && dflt) return *dflt;
  return answer;
}

string pick_style(const string &prompt,
                  const std::optional<string> &default_key) {
  vector<string> keys;
  for (auto &s : STYLES) keys.push_back(s.key);
  while (true) {
    string ans = safe_lower_ascii(ask(prompt, default_key));
    bool digit = !ans.empty() &&
                 ans.find_first_not_of("0123456789") == string::npos;
    int n = digit ? atoi(ans.c_str()) : -1;
    if (digit && n >= 1 && n <= (int)keys.size()) return keys[n - 1];
    for (auto &k : keys)
      if (ans == k) return k;
    say(F("  Please enter a number from 1 to %zu, or one of the names "
          "above.",
          keys.size()));
  }
}

int pick_number(const string &prompt, int count, int dflt) {
  while (true) {
    string ans = ask(prompt, std::to_string(dflt));
    bool digit = !ans.empty() &&
                 ans.find_first_not_of("0123456789") == string::npos;
    int n = digit ? atoi(ans.c_str()) : -1;
    if (n >= 1 && n <= count) return n;
    say(F("  Pick a number from 1 to %d.", count));
  }
}

void show_styles() {
  say();
  say("Lettering styles:");
  say();
  for (size_t i = 0; i < STYLES.size(); i++) {
    const char *star =
        STYLES[i].key == DEFAULT_STYLE ? " (default)" : "";
    say(F("  %2zu. %-14s %s%s", i + 1, STYLES[i].key.c_str(),
          STYLES[i].label.c_str(), star));
  }
  say();
}

void show_colors(bool full) {
  say();
  say("Color pairs:");
  say();
  for (size_t i = 0; i < THEMES.size(); i++) {
    Rgba bg{}, fg{};
    ColorParse b = parse_color(THEMES[i].bg, &bg);
    ColorParse f = parse_color(THEMES[i].fg, &fg);
    const char *star =
        THEMES[i].key == DEFAULT_THEME ? " (default)" : "";
    say(F("  %2zu. %s%s %-13s %-32s text=%s bg=%s%s", i + 1,
          swatch(b == CP_OK ? std::optional<Rgba>(bg)
                            : std::optional<Rgba>())
              .c_str(),
          swatch(f == CP_OK ? std::optional<Rgba>(fg)
                            : std::optional<Rgba>())
              .c_str(),
          THEMES[i].key.c_str(), THEMES[i].label.c_str(),
          THEMES[i].fg.c_str(), THEMES[i].bg.c_str(), star));
  }
  say();
  if (!full) return;
  {
    size_t n = 0;
    string names;
    for (auto &kv : NAMED) {
      if (n++ >= 6) break;
      if (n > 1) names += ", ";
      names += kv.first;
    }
    say("Or set your own with --text-color and --background,");
    say(F("using a name (%s, ...)", names.c_str()));
  }
  say("or a hex code like #c0392b. Use 'none' for a transparent background.");
  say();
}

void show_bundles() {
  vector<JVal> bundles = load_bundles();
  say();
  if (bundles.empty()) {
    say("You have no bundles yet.");
    say(F("Make one:  %s --new-bundle NAME", PROG.c_str()));
    say();
    return;
  }
  say("Your bundles:");
  say();
  for (size_t i = 0; i < bundles.size(); i++) {
    string name = jstr_or(bundles[i], "name", "?");
    string style_key = jstr_or(bundles[i], "style", DEFAULT_STYLE);
    const StyleEntry *se = style_find(style_key);
    string s_label = se ? se->label : style_key;
    string theme = jstr_or(bundles[i], "colors", DEFAULT_THEME);
    say(F("  %2zu.  %-14s %-18s %s", i + 1, name.c_str(),
          s_label.c_str(), theme.c_str()));
  }
  say();
  say(F("Use one:  %s --bundle NAME --texts texts.txt", PROG.c_str()));
  say(F("Or mix several:  %s --bundle A --bundle B --texts texts.txt",
        PROG.c_str()));
  say();
}

// ----------------------------------------------------------------- bundles

JVal default_look() {
  JVal b;
  b.t = JVal::OBJ;
  b.obj.emplace_back("name", JVal::mkstr("default"));
  b.obj.emplace_back("style", JVal::mkstr(DEFAULT_STYLE));
  b.obj.emplace_back("colors", JVal::mkstr(DEFAULT_THEME));
  b.obj.emplace_back("text-color", JVal::nul());
  b.obj.emplace_back("background", JVal::nul());
  b.obj.emplace_back("size", JVal::mknum(640));
  b.obj.emplace_back("padding", JVal::mknum(14));
  return b;
}

void pick_colors(JVal &bundle) {
  show_colors(false);
  int default_num = 1;
  for (size_t i = 0; i < THEMES.size(); i++)
    if (THEMES[i].key == DEFAULT_THEME) default_num = (int)i + 1;
  while (true) {
    string ans = ask("Number, or C to mix your own colors",
                     std::to_string(default_num));
    ans = safe_lower_ascii(ans);
    if (ans == "c") {
      string fg_in = ask("Text color (name or #hex)", string("white"));
      string bg_in =
          ask("Background color (name, #hex, or none)", string("ink"));
      Rgba tmp{};
      if (parse_color(fg_in, &tmp) == CP_BAD ||
          parse_color(bg_in, &tmp) == CP_BAD) {
        say("  I don't recognise one of those; names like gold,");
        say("  hex like #c0392b, or none for transparent.");
        continue;
      }
      jset(bundle, "colors", JVal::mkstr(DEFAULT_THEME));
      jset(bundle, "text-color", JVal::mkstr(fg_in));
      jset(bundle, "background",
           parse_color(bg_in, &tmp) == CP_NONE ? JVal::mkstr(bg_in)
                                               : JVal::mkstr(bg_in));
      return;
    }
    bool digit = !ans.empty() &&
                 ans.find_first_not_of("0123456789") == string::npos;
    int n = digit ? atoi(ans.c_str()) : -1;
    if (digit && n >= 1 && n <= (int)THEMES.size()) {
      jset(bundle, "colors", JVal::mkstr(THEMES[n - 1].key));
      jset(bundle, "text-color", JVal::nul());
      jset(bundle, "background", JVal::nul());
      return;
    }
    say(F("  Pick a number from 1 to %zu, or C.", THEMES.size()));
  }
}

JVal bundle_wizard(const string &name) {
  JVal b;
  b.t = JVal::OBJ;
  b.obj.emplace_back("name", JVal::mkstr(name));
  say();
  say(F("  New bundle: %s", name.c_str()));
  say("  A bundle is a look - style, colors, size, padding - no words.");
  show_styles();
  string style = pick_style("Choose a style", DEFAULT_STYLE);
  const StyleEntry *se = style_find(style);
  say(F("  -> %s", se ? se->label : style.c_str()));
  b.obj.emplace_back("style", JVal::mkstr(style));
  pick_colors(b);

  while (true) {
    string raw = ask("Size in pixels", string("640"));
    bool ok = false;
    int v = int_of(raw, &ok);
    if (!ok) { say("  Please type a number."); continue; }
    if (v >= 16 && v <= 4096) { jset(b, "size", JVal::mknum(v)); break; }
    say("  Pick a number between 16 and 4096.");
  }
  while (true) {
    string raw = ask("Padding (14 is a good default, 0 to 40)",
                     string("14"));
    bool ok = false;
    int v = int_of(raw, &ok);
    if (!ok) { say("  Please type a number."); continue; }
    if (v >= 0 && v <= 40) { jset(b, "padding", JVal::mknum(v)); break; }
    say("  Pick a number between 0 and 40.");
  }
  return b;
}

void cmd_new_bundle(const string &given_name, const Args &a);

// forward declarations referenced by menus
void menu_edit_texts_file(const string &path);

void menu_add_texts() { add_texts_interactive(texts_path()); }

JVal pick_bundle() {
  vector<JVal> bundles = load_bundles();
  if (bundles.empty()) {
    say("  (No bundles yet - using the default look.)");
    return default_look();
  }
  say();
  say("  Pick a look:");
  say();
  for (size_t i = 0; i < bundles.size(); i++) {
    string name = jstr_or(bundles[i], "name", "?");
    string sk = jstr_or(bundles[i], "style", DEFAULT_STYLE);
    const StyleEntry *se = style_find(sk);
    say(F("    %2zu.  %-14s %s", i + 1, name.c_str(),
          se ? se->label : sk.c_str()));
  }
  say(F("     %2zu.  %s", bundles.size() + 1, "Default look"));
  say();
  int n = pick_number("  Number", (int)bundles.size() + 1,
                      (int)bundles.size() + 1);
  if (n <= (int)bundles.size()) return bundles[n - 1];
  return default_look();
}

void menu_pick_style() {
  say();
  say("  Browse styles");
  say("  Press Enter to see the next one, Q to go back.");
  say();
  int total = (int)STYLES.size();
  int idx = 0;
  while (idx < total) {
    auto &st = STYLES[idx];
    string font_path = join_path(FONTDIR, st.file);
    if (!path_exists(font_path)) { idx++; continue; }
    say(F("  Style %d of %d:  %s", idx + 1, total, st.label.c_str()));
    say(F("  (%s)", st.key.c_str()));
    try {
      bool ok = false;
      vector<uint8_t> px = render_px(
          "Ag", font_path, 320, Rgba{255, 255, 255, 255},
          Rgba{20, 22, 26, 255}, 0.14, &ok);
      if (ok) preview(px, 320, 24);
    } catch (...) {
      say("  (no preview available for this style)");
    }
    std::printf("  Enter = next, Q = back: ");
    std::fflush(stdout);
    string answer;
    if (!std::getline(std::cin, answer)) { say(); return; }
    answer = safe_lower_ascii(answer);
    if (answer == "q" || answer == "quit" || answer == "exit") return;
    idx++;
  }
  say();
  say("  That was the last style.  Back to the menu.");
  say();
}

// ------------------------------------------------------------- error notes

string color_swatch_of(const string &name) {
  Rgba c{};
  return swatch(parse_color(name, &c) == CP_OK
                    ? std::optional<Rgba>(c)
                    : std::optional<Rgba>());
}

std::pair<string, string> bundle_colors(const JVal &bundle) {
  auto tf = theme_find(jstr_or(bundle, "colors", DEFAULT_THEME));
  string f = tf ? tf->fg : "";
  string b = tf ? tf->bg : "";
  if (jhas_nonnull(bundle, "text-color")) f = jstr_or(bundle, "text-color", "");
  if (jhas_nonnull(bundle, "background")) b = jstr_or(bundle, "background", "");
  return {f, b};
}

// ------------------------------------------------------------------ menus

void menu_edit_texts_file_str(const string &path_in) {
  string path = path_in.empty() ? texts_path() : path_in;
  if (!path_exists(path)) {
    if (create_texts_file(path)) say(F("  Created %s with examples inside.", path.c_str()));
  }
  open_in_editor(find_editor_cmd(), path);
  try {
    size_t n = read_texts(path).size();
    say();
    say(F("  %zu text%s ready in %s.", n, n == 1 ? "" : "s", path.c_str()));
  } catch (ListError &e) {
    say();
    say(e.what());
  }
  say();
}

void menu_edit_texts_file() { menu_edit_texts_file_str(""); }

void menu_my_texts() {
  string path = texts_path();
  vector<TextEntry> entries;
  try {
    entries = read_texts(path);
  } catch (ListError &) {
    say();
    say("  You don't have any texts yet.");
    say("  Add some with option 1, then they show up here.");
    say();
    return;
  }

  say();
  say("  Your texts:");
  say();
  for (size_t i = 0; i < entries.size(); i++) {
    string first = entries[i].text.substr(0, entries[i].text.find('\n'));
    if (first.size() > 20) first = first.substr(0, 18) + "..";
    // %-16s counts bytes; pad by hand for utf8 labels
    string lbl = entries[i].label;
    int vis = 0;
    for (size_t k = 0; k < lbl.size();) {
      size_t s0 = k;
      utf8_decode_at(lbl, k);
      if (k - s0 >= 4) vis += 2; else vis++;
    }
    string padded = lbl;
    for (int p = vis; p < 16; p++) padded += " ";
    say(F("    %2zu.  %s %s", i + 1, padded.c_str(), first.c_str()));
  }
  say();
  say("    Pick a number, or Enter to go back.");

  string ans = ask("  Number", string());
  if (ans.empty()) return;
  bool digit = ans.find_first_not_of("0123456789") == string::npos &&
               !ans.empty();
  int n = digit ? atoi(ans.c_str()) : -1;
  if (!digit || n < 1 || n > (int)entries.size()) {
    say("  That's not a valid number.");
    return;
  }
  TextEntry &entry = entries[n - 1];

  string one_line = replace_all(entry.text, "\n", " / ");
  say();
  say(F("  [%s]  \"%s\"", entry.label.c_str(), one_line.c_str()));
  say();
  say("    1.  Draw it");
  say("    2.  Edit it here");
  say("    3.  Edit it in my editor");
  say("    4.  Remove it");
  say("    5.  Back");
  say();
  string choice = ask("  Number", string("1"));
  if (choice == "1") {
    JVal bundle = pick_bundle();
    string folder = join_path(HERE, "pictures");
    makedirs(folder);
    say();
    say("  Drawing ...");
    try {
      render_entries({entry}, {bundle}, folder);
    } catch (RenderError &) {
      say("  Sorry, that text doesn't work with that look.");
    }
    say();
  } else if (choice == "2") {
    say();
    say(F("  Type the new words for [%s].", entry.label.c_str()));
    say("  Enter = new line in the SAME picture. A lone . finishes.");
    vector<string> lines;
    while (true) {
      std::printf("  > ");
      std::fflush(stdout);
      string line;
      if (!std::getline(std::cin, line)) break;
      size_t b = line.find_first_not_of(" \t\r");
      if (b != string::npos && line[b] == '.' &&
          line.find_first_not_of(" \t\r", b + 1) == string::npos)
        break;
      lines.push_back(unescape_line(line));
    }
    while (!lines.empty()) {
      size_t nb = lines.back().find_first_not_of(" \t\r");
      if (nb == string::npos) lines.pop_back();
      else break;
    }
    string text;
    for (size_t i = 0; i < lines.size(); i++) {
      if (i) text.push_back('\n');
      text += lines[i];
    }
    {
      size_t nb = text.find_first_not_of(" \t\r\n");
      if (nb == string::npos) text.clear();
    }
    if (text.empty()) {
      say("  Kept the old words.");
    } else {
      entry.text = text;
      write_texts(path, entries);
      say(F("  Updated [%s].", entry.label.c_str()));
    }
    say();
  } else if (choice == "3") {
    auto fresh = edit_text_in_editor(entry.text);
    if (!fresh) say("  Kept the old words.");
    else {
      entry.text = *fresh;
      write_texts(path, entries);
      say(F("  Updated [%s].", entry.label.c_str()));
    }
    say();
  } else if (choice == "4") {
    entries.erase(entries.begin() + (n - 1));
    write_texts(path, entries);
    say(F("  Removed [%s].", entry.label.c_str()));
    say();
  }
}

void menu_draw_all() {
  string path = texts_path();
  try {
    read_texts(path);
  } catch (ListError &) {
    say();
    say("  You don't have any texts yet - add some with option 1 first.");
    say();
    return;
  }
  JVal bundle = pick_bundle();
  try {
    run_texts(path, {bundle}, "");
  } catch (ListError &e) {
    say();
    say(e.what());
  } catch (SaveError &e) {
    say(e.what());
  }
}

void menu_edit_bundle(vector<JVal> &bundles, size_t idx) {
  JVal &b = bundles[idx];
  while (true) {
    string name = jstr_or(b, "name", "?");
    string sk = jstr_or(b, "style", DEFAULT_STYLE);
    const StyleEntry *se = style_find(sk);
    string s_label = se ? se->label : sk;
    auto fc = bundle_colors(b);
    say();
    say(F("  [%s]  %s  %s%s", name.c_str(), s_label.c_str(),
          color_swatch_of(fc.second).c_str(),
          color_swatch_of(fc.first).c_str()));
    say(F("  text=%s  bg=%s", fc.first.c_str(), fc.second.c_str()));
    try {
      Options opts = bundle_to_settings(
          b, F("the bundle [%s]", name.c_str()));
      bool ok = false;
      vector<uint8_t> px =
          render_px("Ag", opts.path, 240, opts.fg, opts.bg,
                    opts.margin, &ok);
      if (ok) preview(px, 240, 22);
    } catch (...) {
      say("  (no preview available for this style)");
    }
    say("    1.  Change the colors");
    say("    2.  Change the style");
    say("    3.  Change size or padding");
    say("    4.  Rename it");
    say("    5.  Delete it");
    say("    6.  Done");
    say();
    string choice = ask("  Number", string("6"));
    if (choice == "1") {
      pick_colors(b);
      save_bundles(bundles);
    } else if (choice == "2") {
      say();
      int current = 1;
      for (size_t i = 0; i < STYLES.size(); i++)
        if (STYLES[i].key == jstr_or(b, "style", "")) current = (int)i + 1;
      for (size_t i = 0; i < STYLES.size(); i++)
        say(F("    %2zu.  %s%s", i + 1, STYLES[i].label.c_str(),
              STYLES[i].key == jstr_or(b, "style", "") ? " <--" : ""));
      int n = pick_number("  Number", (int)STYLES.size(), current);
      jset(b, "style", JVal::mkstr(STYLES[n - 1].key));
      save_bundles(bundles);
    } else if (choice == "3") {
      while (true) {
        string raw = ask("  Size in pixels",
                         jstr_or(b, "size", "640"));
        bool ok = false;
        int v = int_of(raw, &ok);
        if (!ok) { say("  Please type a number."); continue; }
        if (v >= 16 && v <= 4096) { jset(b, "size", JVal::mknum(v)); break; }
        say("  Pick a number between 16 and 4096.");
      }
      while (true) {
        string raw = ask("  Padding (0 to 40)",
                         jstr_or(b, "padding", "14"));
        bool ok = false;
        int v = int_of(raw, &ok);
        if (!ok) { say("  Please type a number."); continue; }
        if (v >= 0 && v <= 40) { jset(b, "padding", JVal::mknum(v)); break; }
        say("  Pick a number between 0 and 40.");
      }
      save_bundles(bundles);
    } else if (choice == "4") {
      string fresh = ask("  New name", name);
      if (fresh.empty() || safe_lower_ascii(fresh) ==
                               safe_lower_ascii(name))
        continue;
      bool clash = false;
      for (size_t i = 0; i < bundles.size() && !clash; i++) {
        if (i == idx) continue;
        const JVal *f = jget(bundles[i], "name");
        clash = f && f->t == JVal::STR &&
                safe_lower_ascii(f->str) == safe_lower_ascii(fresh);
      }
      if (clash) {
        say(F("  You already have a bundle called \"%s\".",
              fresh.c_str()));
        continue;
      }
      jset(b, "name", JVal::mkstr(fresh));
      save_bundles(bundles);
      say(F("  Renamed to \"%s\".", fresh.c_str()));
    } else if (choice == "5") {
      std::printf("  Delete \"%s\"?  Type yes to confirm: ", name.c_str());
      std::fflush(stdout);
      string confirm;
      try {
        if (!std::getline(std::cin, confirm)) { say(); return; }
      } catch (...) { say(); return; }
      if (confirm == "yes") {
        bundles.erase(bundles.begin() + idx);
        save_bundles(bundles);
        say("  Deleted.");
        say();
        return;
      }
      say("  Kept it.");
    } else {
      return;
    }
  }
}

void menu_bundles() {
  while (true) {
    vector<JVal> bundles = load_bundles();
    say();
    say("  Bundles (a look: style, colors, size, padding - no words)");
    say();
    if (bundles.empty()) say("    You have no bundles yet.");
    else
      for (size_t i = 0; i < bundles.size(); i++) {
        string name = jstr_or(bundles[i], "name", "?");
        string sk = jstr_or(bundles[i], "style", DEFAULT_STYLE);
        const StyleEntry *se = style_find(sk);
        string s_label = se ? se->label : sk;
        auto fc = bundle_colors(bundles[i]);
        string theme = jstr_or(bundles[i], "colors", DEFAULT_THEME);
        say(F("    %2zu.  %s%s %-12s %-22s %s", i + 1,
              color_swatch_of(fc.second).c_str(),
              color_swatch_of(fc.first).c_str(), name.c_str(),
              s_label.c_str(), theme.c_str()));
      }
    say();
    say("    N.  New bundle");
    say("    B.  Back");
    say();
    string choice = safe_lower_ascii(ask("  Number to edit, N, or B",
                                         string("B")));
    if (choice.empty() || choice == "b" || choice == "back") return;
    if (choice == "n" || choice == "new") {
      string name = ask("  Bundle name", string());
      if (name.empty()) continue;
      if (find_bundle(name, bundles)) {
        say(F("  You already have a bundle called \"%s\".", name.c_str()));
        continue;
      }
      JVal bundle = bundle_wizard(name);
      try {
        bundle_to_settings(bundle, F("the bundle [%s]", name.c_str()));
      } catch (ListError &e) {
        say();
        say(e.what());
        continue;
      }
      bundles.push_back(bundle);
      save_bundles(bundles);
      say();
      say(F("  Saved bundle \"%s\".", name.c_str()));
      say();
      continue;
    }
    bool digit = choice.find_first_not_of("0123456789") == string::npos &&
                 !choice.empty();
    int n = digit ? atoi(choice.c_str()) : -1;
    if (n >= 1 && n <= (int)bundles.size()) {
      menu_edit_bundle(bundles, (size_t)(n - 1));
      continue;
    }
    say("  Pick a bundle number, N, or B.");
  }
}

// ------------------------------------------------------------ main menus

void menu_texts() {
  while (true) {
    say();
    say("  Texts");
    say();
    say("    1.  Add texts");
    say("    2.  My texts");
    say("    3.  Edit texts file");
    say("    4.  Back");
    say();
    string choice = safe_lower_ascii(ask("  Pick a number", string()));
    if (choice == "1") menu_add_texts();
    else if (choice == "2") menu_my_texts();
    else if (choice == "3") menu_edit_texts_file();
    else if (choice == "4" || choice == "b" || choice == "back") return;
    else say("  Pick a number from 1 to 4.");
  }
}

void menu_main() {
  while (true) {
    say();
    say("  +---------------------------------------+");
    say("  |           Logo Creator                |");
    say("  +---------------------------------------+");
    say();
    say("    1.  Texts");
    say("    2.  Bundles");
    say("    3.  Draw all texts");
    say("    4.  Browse styles");
    say("    5.  Quit");
    say();
    string choice = ask("  Pick a number", string("1"));
    if (choice == "1") menu_texts();
    else if (choice == "2") menu_bundles();
    else if (choice == "3") menu_draw_all();
    else if (choice == "4") menu_pick_style();
    else if (choice == "5") {
      say();
      say("  See you next time!");
      say();
      return;
    } else {
      say("  Pick a number from 1 to 5.");
    }
  }
}

const char *EXAMPLE_LIST =
    "# A list of logos. Run it with:   ./logo --from logos.txt\n"
    "#\n"
    "# Anything after a # is a note and is ignored.\n"
    "# Each logo starts with a name in [brackets]. That name becomes the\n"
    "# file name, so the picture below lands in pictures/whatsapp.png\n"
    "#\n"
    "# Settings you can use in any block:\n"
    "#   style       one of the names from  ./logo --styles\n"
    "#   colors      one of the pairs from  ./logo --colors-list\n"
    "#   text-color  a color name or #hex, overrides the pair\n"
    "#   background  a color name, #hex, or none for transparent\n"
    "#   size        width and height in pixels\n"
    "#   padding     empty space around the letters, 0 to 40 percent\n"
    "#   text        what to draw\n"
    "\n"
    "# Anything in [defaults] applies to every logo below, unless that\n"
    "# logo says otherwise. 'folder' chooses where the pictures are\n"
    "# written.\n"
    "[defaults]\n"
    "folder = pictures\n"
    "size   = 640\n"
    "\n"
    "[whatsapp]\n"
    "style  = medieval\n"
    "colors = midnight\n"
    "text   = N\n"
    "\n"
    "[work-avatar]\n"
    "style  = modern\n"
    "colors = crimson\n"
    "text   = JD\n"
    "\n"
    "# For text with spaces, tabs or several lines, leave 'text:' empty\n"
    "# and indent the lines below it. They are drawn exactly as you typed\n"
    "# them.\n"
    "[poster]\n"
    "style   = poster\n"
    "colors  = gold\n"
    "size    = 1024\n"
    "padding = 8\n"
    "text:\n"
    "    GOOD\n"
    "    NIGHT\n"
    "\n"
    "[sticker]\n"
    "style      = brush\n"
    "text-color = ink\n"
    "background = none\n"
    "text       = OK\n";

// ------------------------------------------------------------- comparisons

vector<std::pair<char, string>> compare_variants(const string &what) {
  vector<std::pair<char, string>> pairs;
  if (what == "styles" || what == "both")
    for (auto &s : STYLES) pairs.push_back({'s', s.key});
  if (what == "colors" || what == "both")
    for (auto &t : THEMES) {
      Rgba tmp{};
      ColorParse r = parse_color(t.fg, &tmp);
      if (r != CP_NONE) pairs.push_back({'c', t.key});
    }
  return pairs;
}

void cmd_compare(const string &what, const string &text,
                 const string &out_dir, bool show_preview) {
  if (what != "styles" && what != "colors" && what != "both") {
    say("I can only compare 'styles', 'colors', or 'both'.");
    std::exit(1);
  }
  string folder =
      out_dir.empty() ? join_path(HERE, "pictures/compare") : out_dir;
  if (!is_abs(folder)) folder = join_path(HERE, folder);

  struct CPlan { char cat; string key; Options opts; };
  vector<CPlan> plan;
  vector<string> skipped;
  for (auto &pv : compare_variants(what)) {
    std::map<string, string> item;
    bool is_style = pv.first == 's';
    item["style"] = is_style ? pv.second : DEFAULT_STYLE;
    item["colors"] = is_style ? string("classic") : pv.second;
    Options opts = look_settings(
        item,
        F("the comparison %s '%s'",
          is_style ? "style" : "color", pv.second.c_str()));
    if (!path_exists(opts.path)) { skipped.push_back(pv.second); continue; }
    plan.push_back({pv.first, pv.second, opts});
  }

  if (plan.empty()) {
    say("Nothing to compare - none of those looks has a font I can use.");
    std::exit(1);
  }

  makedirs(folder);
  say();
  say(F("Comparing looks for \"%s\" - %d variation%s.",
        quote_text(text).c_str(), (int)plan.size(),
        plan.size() == 1 ? "" : "s"));
  say(F("Writing into %s", folder.c_str()));
  say();

  int made = 0;
  for (auto &cp : plan) {
    string stem =
        F("%s-%s", cp.cat == 's' ? "style" : "color",
          safe_name(cp.key).c_str());
    string out = unique_stem(folder, stem);
    vector<uint8_t> px;
    try {
      bool ok = false;
      px = render_px(text, cp.opts.path, cp.opts.size, cp.opts.fg,
                     cp.opts.bg, cp.opts.margin, &ok);
      if (!ok) throw RenderError();
    } catch (RenderError &) {
      say(F("  Skipped %s %s: nothing drew there.",
            cp.cat == 's' ? "style" : "color", cp.key.c_str()));
      continue;
    }
    write_png(out, cp.opts.size, cp.opts.size, px);
    if (show_preview) preview(px, cp.opts.size, 24);
    say(F("  %-24s %4d x %-4d  %-14s %s", basename_of(out).c_str(),
          cp.opts.size, cp.opts.size, cp.opts.style.c_str(),
          cp.key.c_str()));
    made++;
  }

  say();
  say(F("Done - %d picture%s in %s", made, made == 1 ? "" : "s",
        folder.c_str()));
  if (!skipped.empty()) {
    string joined;
    for (size_t i = 0; i < skipped.size(); i++) {
      if (i) joined += ", ";
      joined += skipped[i];
    }
    say("(left out, font file missing: " + joined + ")");
  }
}

// -------------------------------------------------------------------- args

struct Args {
  string text;
  bool text_given = false;
  std::optional<string> style, colors, text_color, background;
  std::optional<int> size, padding;
  string save;
  bool save_given = false;
  string font_file;
  string from_file;
  string out_dir;
  bool has_new_list = false;
  string new_list = "logos.txt";
  vector<string> bundle;
  bool bundles_flag = false;
  bool has_new_bundle = false;
  string new_bundle;
  string texts;
  bool has_texts = false;
  bool has_new_texts = false;
  string new_texts = "texts.txt";
  string add_texts;
  bool has_add_texts = false;
  bool has_edit_texts = false;
  string edit_texts = "";
  bool styles_flag = false, colors_list_flag = false;
  bool no_preview = false;
  bool has_compare = false;
  string compare = "both";
};

void usage_short() {
  say(F("usage: %s [-h] [-s NAME] [-c NAME] [--text-color COLOR] "
        "[--background COLOR]",
        PROG.c_str()));
  say("            [--size PIXELS] [--padding PERCENT] [-o FILE] "
      "[--font-file FILE]");
  say("            [--from FILE] [--out-dir FOLDER] [--new-list [FILE]] "
      "[-b NAME]");
  say("            [--bundles] [--new-bundle NAME] [--texts FILE] "
      "[--new-texts [FILE]]");
  say("            [--add-texts FILE] [--edit-texts [FILE]] [--styles] "
      "[--colors-list]");
  say("            [--no-preview] [--compare [WHAT]] [text]");
}

[[noreturn]] void arg_error(const string &msg) {
  usage_short();
  std::fprintf(stderr, "%s: error: %s\n", PROG.c_str(), msg.c_str());
  std::exit(2);
}

void show_help_and_exit() {
  say(F("usage: %s [-h] [-s NAME] [-c NAME] [--text-color COLOR] "
        "[--background COLOR]\n"
        "            [--size PIXELS] [--padding PERCENT] [-o FILE] "
        "[--font-file FILE]\n"
        "            [--from FILE] [--out-dir FOLDER] [--new-list [FILE]] "
        "[-b NAME]\n"
        "            [--bundles] [--new-bundle NAME] [--texts FILE] "
        "[--new-texts [FILE]]\n"
        "            [--add-texts FILE] [--edit-texts [FILE]] [--styles] "
        "[--colors-list]\n"
        "            [--no-preview] [--compare [WHAT]] [text]",
        PROG.c_str()));
  say("Turn letters into a square picture for a profile photo, logo, "
      "or chat icon.");
  say("");
  say("positional arguments:");
  say("  text                  the letter or letters to draw, e.g. A or \"JD\"");
  say("");
  say("options:");
  say("  -h, --help            show this help message and exit");
  say("  -s, --style NAME      lettering style; see --styles");
  say("  -c, --colors NAME     a ready-made color pair; see --colors-list");
  say("  --text-color, --fg COLOR");
  say("                        color of the letters, e.g. gold or #c8a34a");
  say("  --background, --bg COLOR");
  say("                        color behind the letters, or 'none' for transparent");
  say("  --size PIXELS         width and height in pixels (default: 640)");
  say("  --padding PERCENT     empty space around the letters, 0-40 (default: 14)");
  say("  -o, --save FILE       where to save the picture; a folder that already");
  say("                        exists receives it under an automatic name");
  say("  --font-file FILE      use your own .otf or .ttf file instead of a style");
  say("  --from FILE           draw every logo listed in a file, all at once");
  say("  --out-dir FOLDER      folder for --from/--texts pictures");
  say("  --new-list [FILE]     write a ready-to-edit example list (default: logos.txt)");
  say("  -b, --bundle NAME     a saved bundle to use; repeat it to try several");
  say("                        looks at once");
  say("  --bundles             list your saved bundles and exit");
  say("  --new-bundle NAME     save a look as a bundle (no words)");
  say("  --texts FILE          draw every text in this file, using --bundle");
  say("  --new-texts [FILE]    write a ready-to-edit texts file (default: texts.txt)");
  say("  --add-texts FILE      add texts to a file, several at once");
  say("  --edit-texts [FILE]   open a texts file in your editor (default: texts.txt)");
  say("  --styles              list the lettering styles and exit");
  say("  --colors-list         list the color pairs and color names and exit");
  say("  --no-preview          skip the preview drawn in the terminal");
  say("  --compare [WHAT]      draw the text once per lettering style and/or color");
  say("");
  say("examples:");
  say(F("  %s                       ask me step by step (easiest)", PROG.c_str()));
  say(F("  %s A                     one letter, using the default look", PROG.c_str()));
  say(F("  %s \"JD\" --style modern --colors crimson", PROG.c_str()));
  say(F("  %s N --fg gold --bg slate", PROG.c_str()));
  say(F("  %s A --background none --save logo.png", PROG.c_str()));
  say(F("  %s --compare both AB   draw AB in every style and color pair", PROG.c_str()));
  say(F("  %s --new-list            create an example list file to edit", PROG.c_str()));
  say(F("  %s --from logos.txt      draw every logo in that file at once", PROG.c_str()));
  say(F("  %s --styles              show every lettering style", PROG.c_str()));
  say(F("  %s --colors-list         show every color pair and color name", PROG.c_str()));
  say("");
  say("bundles (a saved look: style, colors, size, padding - no words):");
  say(F("  %s --new-bundle retro    save a look, asking step by step", PROG.c_str()));
  say(F("  %s --bundles             list your saved bundles", PROG.c_str()));
  say("");
  say("texts (just the words, drawn with a bundle):");
  say(F("  %s --bundle retro --texts texts.txt          draw every text", PROG.c_str()));
  say(F("  %s --bundle retro --bundle flat --texts texts.txt   try both looks", PROG.c_str()));
  say("");
  say("The picture is always square and saved as a PNG with no quality loss.");
  std::exit(0);
}

bool take_value(int argc, char **argv, int &i, const string &inline_val,
                string &out) {
  if (!inline_val.empty()) { out = inline_val; return true; }
  if (i + 1 >= argc) return false;
  out = argv[++i];
  return true;
}

Args parse_args(int argc, char **argv) {
  Args a;
  auto opt_str = [&](std::optional<string> &slot, bool *given,
                     char **argv, int argc, int &i,
                     const string &inl) {
    string v;
    if (!take_value(argc, argv, i, inl, v))
      arg_error(F("argument: expected one argument"));
    slot = v;
    if (given) *given = true;
  };
  for (int i = 1; i < argc; i++) {
    string arg = argv[i];
    string inl;
    size_t eq = arg.find('=');
    if (arg.rfind("--", 0) == 0 && eq != string::npos) {
      inl = arg.substr(eq + 1);
      arg = arg.substr(0, eq);
    }
    if (arg == "-h" || arg == "--help") {
      show_help_and_exit();
    } else if (arg == "-s" || arg == "--style") {
      opt_str(a.style, nullptr, argv, argc, i, inl);
    } else if (arg == "-c" || arg == "--colors") {
      opt_str(a.colors, nullptr, argv, argc, i, inl);
    } else if (arg == "--text-color" || arg == "--fg") {
      opt_str(a.text_color, nullptr, argv, argc, i, inl);
    } else if (arg == "--background" || arg == "--bg") {
      opt_str(a.background, nullptr, argv, argc, i, inl);
    } else if (arg == "--size") {
      string v;
      if (!take_value(argc, argv, i, inl, v)) arg_error("expected value");
      bool ok = false;
      int n = int_of(v, &ok);
      if (!ok) arg_error(F("invalid int value: '%s'", v.c_str()));
      a.size = n;
    } else if (arg == "--padding") {
      string v;
      if (!take_value(argc, argv, i, inl, v)) arg_error("expected value");
      bool ok = false;
      int n = int_of(v, &ok);
      if (!ok) arg_error(F("invalid int value: '%s'", v.c_str()));
      a.padding = n;
    } else if (arg == "-o" || arg == "--save") {
      string v;
      if (!take_value(argc, argv, i, inl, v)) arg_error("expected value");
      a.save = v;
      a.save_given = true;
    } else if (arg == "--font-file") {
      string v;
      if (!take_value(argc, argv, i, inl, v)) arg_error("expected value");
      a.font_file = v;
    } else if (arg == "--from") {
      string v;
      if (!take_value(argc, argv, i, inl, v)) arg_error("expected value");
      a.from_file = v;
    } else if (arg == "--out-dir") {
      string v;
      if (!take_value(argc, argv, i, inl, v)) arg_error("expected value");
      a.out_dir = v;
    } else if (arg == "--new-list") {
      a.has_new_list = true;
      if (!inl.empty()) { a.new_list = inl; continue; }
      if (i + 1 < argc && argv[i + 1][0] != '-')
        a.new_list = argv[++i];
    } else if (arg == "-b" || arg == "--bundle") {
      string v;
      if (!take_value(argc, argv, i, inl, v)) arg_error("expected value");
      a.bundle.push_back(v);
    } else if (arg == "--bundles") {
      a.bundles_flag = true;
    } else if (arg == "--new-bundle") {
      string v;
      if (!take_value(argc, argv, i, inl, v)) v = "";
      a.new_bundle = v;
      a.has_new_bundle = true;
    } else if (arg == "--texts") {
      string v;
      if (!take_value(argc, argv, i, inl, v)) arg_error("expected value");
      a.texts = v;
      a.has_texts = true;
    } else if (arg == "--new-texts") {
      a.has_new_texts = true;
      if (!inl.empty()) { a.new_texts = inl; continue; }
      if (i + 1 < argc && argv[i + 1][0] != '-')
        a.new_texts = argv[++i];
    } else if (arg == "--add-texts") {
      string v;
      if (!take_value(argc, argv, i, inl, v)) arg_error("expected value");
      a.add_texts = v;
      a.has_add_texts = true;
    } else if (arg == "--edit-texts") {
      a.has_edit_texts = true;
      a.edit_texts = "";
      if (!inl.empty()) { a.edit_texts = inl; continue; }
      if (i + 1 < argc && argv[i + 1][0] != '-')
        a.edit_texts = argv[++i];
    } else if (arg == "--styles") {
      a.styles_flag = true;
    } else if (arg == "--colors-list") {
      a.colors_list_flag = true;
    } else if (arg == "--no-preview") {
      a.no_preview = true;
    } else if (arg == "--compare") {
      string v;
      if (!inl.empty()) {
        v = inl;
      } else if (i + 1 < argc && argv[i + 1][0] != '-') {
        v = argv[++i];
      }
      if (!v.empty() && v != "styles" && v != "colors" && v != "both")
        arg_error(F("argument --compare: invalid choice: '%s' "
                    "(choose from 'styles', 'colors', 'both')",
                    v.c_str()));
      a.compare = v.empty() ? "both" : v;
      a.has_compare = true;
    } else if (arg.size() >= 2 && arg[0] == '-') {
      arg_error(F("unrecognized arguments: %s", arg.c_str()));
    } else {
      if (a.text_given)
        arg_error(F("unrecognized arguments: %s", arg.c_str()));
      a.text = arg;
      a.text_given = true;
    }
  }
  return a;
}

// ------------------------------------------------------- multi-bundle draw

JVal default_bundle_from_flags(const Args &a) {
  JVal b;
  b.t = JVal::OBJ;
  b.obj.emplace_back("name", JVal::mkstr("default"));
  b.obj.emplace_back("style",
                     JVal::mkstr(a.style ? *a.style : DEFAULT_STYLE));
  b.obj.emplace_back("colors",
                     JVal::mkstr(a.colors ? *a.colors : DEFAULT_THEME));
  b.obj.emplace_back(
      "text-color",
      a.text_color ? JVal::mkstr(*a.text_color) : JVal::nul());
  b.obj.emplace_back(
      "background",
      a.background ? JVal::mkstr(*a.background) : JVal::nul());
  b.obj.emplace_back("size", JVal::mknum(a.size ? *a.size : 640));
  b.obj.emplace_back("padding", JVal::mknum(a.padding ? *a.padding : 14));
  return b;
}

void draw_multi_bundle(const Args &a) {
  string text = a.text;
  vector<JVal> saved = load_bundles();
  vector<JVal> picks;
  for (auto &bn : a.bundle) {
    const JVal *b = find_bundle(bn, saved);
    if (!b) {
      say(F("There is no bundle called \"%s\".", bn.c_str()));
      say(F("Run  %s --bundles  to see them.", PROG.c_str()));
      std::exit(1);
    }
    picks.push_back(*b);
  }

  string folder = join_path(HERE, "pictures");
  string prefix;
  bool use_prefix = false;
  if (a.save_given) {
    if (is_dir(a.save)) folder = a.save;
    else {
      string base = basename_of(a.save);
      size_t dot = base.rfind('.');
      prefix = dot == string::npos || dot == 0 ? base : base.substr(0, dot);
      use_prefix = true;
      folder = dirname_of(a.save);
    }
  }

  // python: "".join(c if c.isalnum() else "_" ...)
  string safe;
  for (uint32_t cp : utf8_to_cps(text))
    utf8_push_cp(safe, cp_is_alnum(cp) ? cp : '_');
  if (safe.empty()) safe = "letters";

  struct MPlan { string stem; Options opts; string style_key; };
  vector<MPlan> plan;
  for (auto &b : picks) {
    string bname = jstr_or(b, "name", "bundle");
    std::map<string, string> item;
    auto put2 = [&](const char *key, std::optional<string> cli,
                    const char *jkey) {
      if (cli) { item[key] = *cli; return; }
      const JVal *f = jget(b, jkey);
      if (f && f->t == JVal::STR) item[key] = f->str;
    };
    put2("style", a.style, "style");
    put2("colors", a.colors, "colors");
    put2("fg", a.text_color, "text-color");
    put2("bg", a.background, "background");
    item["size"] = a.size ? std::to_string(*a.size)
                          : jstr_or(b, "size", "640");
    item["padding"] = a.padding ? std::to_string(*a.padding)
                                : jstr_or(b, "padding", "14");
    try {
      Options opts = look_settings(item,
                                   F("the bundle [%s]", bname.c_str()));
      string stem =
          F("%s-%s", safe_name(use_prefix ? prefix : bname).c_str(),
            safe.c_str());
      plan.push_back({stem, opts, opts.style});
    } catch (ListError &e) {
      say();
      say(e.what());
      std::exit(1);
    }
  }

  std::set<string> seen;
  for (auto &p : plan)
    if (!seen.insert(p.stem).second) {
      say(F("Two results would be saved as %s.png.", p.stem.c_str()));
      say("Give your bundles different names.");
      std::exit(1);
    }

  makedirs(folder);
  say();
  say(F("Drawing \"%s\" with %zu looks.",
        text.substr(0, text.find('\n')).c_str(), plan.size()));
  say(F("Writing into %s", folder.c_str()));
  say();

  int made = 0;
  for (auto &p : plan) {
    string out = unique_stem(folder, p.stem);
    vector<uint8_t> px;
    try {
      bool ok = false;
      px = render_px(text, p.opts.path, p.opts.size, p.opts.fg, p.opts.bg,
                     p.opts.margin, &ok);
      if (!ok) throw RenderError();
    } catch (RenderError &) {
      say(F("  Skipped \"%s\": nothing drew with style %s.",
            p.stem.c_str(), p.style_key.c_str()));
      continue;
    }
    write_png(out, p.opts.size, p.opts.size, px);
    if (!a.no_preview) preview(px, p.opts.size);
    string shown = text.substr(0, text.find('\n'));
    say(F("  %-24s %4d x %-4d  %-14s %s", basename_of(out).c_str(),
          p.opts.size, p.opts.size, p.opts.style.c_str(),
          shown.c_str()));
    made++;
  }
  say();
  say(F("Done - %d picture%s in %s", made, made == 1 ? "" : "s",
        folder.c_str()));
}

// ------------------------------------------------------------ new bundles

JVal make_bundle(const string &name, const Args &a) {
  bool explicit_flags = a.style || a.colors || a.text_color ||
                        a.background;
  JVal b = explicit_flags ? default_bundle_from_flags(a) : bundle_wizard(name);
  jset(b, "name", JVal::mkstr(name));
  return b;
}

void cmd_new_bundle(const string &given_name, const Args &a) {
  string name = given_name;
  {
    size_t b = name.find_first_not_of(" \t\r\n");
    size_t e = name.find_last_not_of(" \t\r\n");
    name = b == string::npos ? "" : name.substr(b, e - b + 1);
  }
  if (name.empty()) {
    try {
      name = ask("Name this bundle (e.g. retro, flat)", string());
    } catch (Cancelled &) { name.clear(); }
    if (name.empty()) {
      say("Cancelled - nothing was saved.");
      std::exit(0);
    }
  }
  vector<JVal> bundles = load_bundles();
  if (find_bundle(name, bundles)) {
    say(F("You already have a bundle called \"%s\".", name.c_str()));
    std::exit(1);
  }
  JVal bundle = make_bundle(name, a);
  try {
    bundle_to_settings(bundle, F("the bundle [%s]", name.c_str()));
  } catch (ListError &e) {
    say();
    say(e.what());
    std::exit(1);
  }
  bundles.push_back(bundle);
  save_bundles(bundles);
  say();
  say(F("Saved bundle \"%s\".", name.c_str()));
  say(F("Use it:  %s --bundle %s --texts texts.txt", PROG.c_str(),
        safe_name(name).c_str()));
  say();
}

// ------------------------------------------------------------------- main

int run_main(Args a) {
  if (a.styles_flag) { show_styles(); return 0; }
  if (a.colors_list_flag) { show_colors(true); return 0; }
  if (a.bundles_flag) { show_bundles(); return 0; }
  if (a.has_new_bundle) { cmd_new_bundle(a.new_bundle, a); return 0; }
  if (a.has_new_texts) { cmd_new_texts(a.new_texts); return 0; }
  if (a.has_add_texts) { add_texts_interactive(a.add_texts); return 0; }
  if (a.has_edit_texts) { menu_edit_texts_file_str(a.edit_texts); return 0; }

  if (a.has_compare) {
    cmd_compare(a.compare, a.text_given ? a.text : "Ag", a.out_dir,
                !a.no_preview);
    return 0;
  }

  if (a.has_texts) {
    vector<JVal> bundles;
    if (!a.bundle.empty()) {
      vector<JVal> saved = load_bundles();
      for (auto &bn : a.bundle) {
        const JVal *b = find_bundle(bn, saved);
        if (!b) {
          say(F("There is no bundle called \"%s\".", bn.c_str()));
          say(F("Run  %s --bundles  to see them.", PROG.c_str()));
          std::exit(1);
        }
        bundles.push_back(*b);
      }
    } else {
      bundles.push_back(default_bundle_from_flags(a));
    }
    try {
      run_texts(a.texts, bundles, a.out_dir);
    } catch (ListError &e) {
      say();
      say(e.what());
      std::exit(1);
    } catch (SaveError &e) {
      say(e.what());
      std::exit(1);
    }
    return 0;
  }

  if (a.has_new_list) {
    string target = a.new_list;
    if (path_exists(target)) {
      say(F("%s already exists, so I left it alone.", target.c_str()));
      say("Choose another name, for example:  ./logo --new-list "
          "my-logos.txt");
      std::exit(1);
    }
    std::ofstream fh(target, std::ios::binary | std::ios::trunc);
    if (!fh) {
      say(F("I couldn't write %s (%s).", target.c_str(), strerror(errno)));
      std::exit(1);
    }
    fh << EXAMPLE_LIST;
    say(F("Created %s with four example logos inside.", target.c_str()));
    say("Open it in any text editor, change it however you like, then:");
    say(F("  ./logo --from %s", target.c_str()));
    return 0;
  }

  if (a.from_file.size()) {
    try {
      run_list(a.from_file, a.out_dir);
    } catch (ListError &e) {
      say();
      say(e.what());
      std::exit(1);
    } catch (SaveError &e) {
      say(e.what());
      std::exit(1);
    }
    return 0;
  }

  if (!a.text_given && a.font_file.empty() && a.bundle.empty()) {
    menu_main();
    return 0;
  }

  if (!a.text_given || a.text.empty()) {
    say(F("Please tell me what to draw, for example:  %s A", PROG.c_str()));
    std::exit(1);
  }
  string text = a.text;

  string style = a.style.value_or("");
  int size = a.size.value_or(-1);
  string theme = a.colors.value_or("");
  std::optional<string> fg_name = a.text_color;
  std::optional<string> bg_name = a.background;
  int padding = a.padding.value_or(-1);

  if (!a.bundle.empty()) {
    if (a.bundle.size() > 1) { draw_multi_bundle(a); return 0; }
    vector<JVal> saved = load_bundles();
    const JVal *b = find_bundle(a.bundle[0], saved);
    if (!b) {
      say(F("There is no bundle called \"%s\".", a.bundle[0].c_str()));
      say(F("Run  %s --bundles  to see them.", PROG.c_str()));
      std::exit(1);
    }
    if (style.empty()) style = jstr_or(*b, "style", "");
    if (size < 0) {
      bool num_ok = true;
      size = int_of(jstr_or(*b, "size", "640"), &num_ok);
      if (!num_ok) size = 640;
    }
    if (theme.empty()) theme = jstr_or(*b, "colors", "");
    if (!fg_name)
      if (jhas_nonnull(*b, "text-color"))
        fg_name = jstr_or(*b, "text-color", "");
    if (!bg_name)
      if (jhas_nonnull(*b, "background"))
        bg_name = jstr_or(*b, "background", "");
    if (padding < 0) {
      bool pad_ok = true;
      padding = int_of(jstr_or(*b, "padding", "14"), &pad_ok);
      if (!pad_ok) padding = 14;
    }
  }

  if (style.empty()) style = DEFAULT_STYLE;
  if (size < 0) size = 640;
  if (theme.empty()) theme = DEFAULT_THEME;
  if (padding < 0) padding = 14;

  const Theme *th = theme_find(theme);
  if (!th) {
    say(F("There is no color pair called \"%s\".", theme.c_str()));
    say(F("Run  %s --colors-list  to see the choices.", PROG.c_str()));
    std::exit(1);
  }
  if (!fg_name || fg_name->empty()) fg_name = th->fg;
  if (!bg_name || bg_name->empty()) bg_name = th->bg;
  double margin = padding / 100.0;

  // ------------------------------------------------ check everything first
  string path, style_label;
  if (a.font_file.size()) {
    path = a.font_file;
    style_label = basename_of(a.font_file);
    if (!path_exists(path)) {
      say(F("I can't find that font file: %s", path.c_str()));
      std::exit(1);
    }
  } else {
    const StyleEntry *se = style_find(style);
    if (!se) {
      say(F("There is no style called \"%s\".", style.c_str()));
      say(F("Run  %s --styles  to see the choices.", PROG.c_str()));
      std::exit(1);
    }
    path = join_path(FONTDIR, se->file);
    style_label = se->label;
    if (!path_exists(path)) {
      say(F("The font for the %s style is missing from:", style.c_str()));
      say(F("  %s", path.c_str()));
      std::exit(1);
    }
  }

  if (size < 16) {
    say("The size needs to be at least 16 pixels. 640 is a good choice.");
    std::exit(1);
  }
  if (size > 4096) {
    say("That size is very large; please choose 4096 pixels or less.");
    std::exit(1);
  }
  if (!(margin >= 0 && margin <= 0.4)) {
    say("Padding should be between 0 and 40 percent.");
    std::exit(1);
  }

  Rgba fgv{}, bgv{};
  ColorParse fr = parse_color(*fg_name, &fgv);
  if (fr == CP_BAD) color_or_quit(*fg_name, "text");
  ColorParse br = parse_color(*bg_name, &bgv);
  if (br == CP_BAD) color_or_quit(*bg_name, "background");
  std::optional<Rgba> bg_opt =
      br == CP_OK ? std::optional<Rgba>(bgv) : std::optional<Rgba>();
  if (fr == CP_NONE) {
    say("The letters need a visible color; 'none' only works for the "
        "background.");
    std::exit(1);
  }

  string out = a.save;
  string auto_stem;
  for (uint32_t cp : utf8_to_cps(text))
    utf8_push_cp(auto_stem, cp_is_alnum(cp) ? cp : '_');
  if (auto_stem.empty()) auto_stem = "letters";
  string suffix = a.font_file.size() ? "custom" : style;
  if (!out.empty() && is_dir(out)) {
    out = unique_stem(out, auto_stem + "-" + suffix);
  } else if (out.empty()) {
    out = unique_stem(join_path(HERE, "pictures"), auto_stem + "-" + suffix);
  }
  string folder = dirname_of(out.size() ? out : ".");
  makedirs(folder);
  if (!is_dir(folder)) {
    say(F("I couldn't create the folder %s (%s).", folder.c_str(),
          strerror(errno)));
    std::exit(1);
  }

  vector<uint8_t> px;
  try {
    bool ok = false;
    px = render_px(text, path, size, fgv, bg_opt, margin, &ok);
    if (!ok) throw RenderError();
  } catch (RenderError &) {
    say(F("None of those characters exist in the %s style.",
          style_label.c_str()));
    say("Try a different style with --style, or --styles to see them all.");
    std::exit(1);
  }

  if (!write_png(out, size, size, px)) {
    say(F("I couldn't save the picture (%s).", strerror(errno)));
    std::exit(1);
  }

  if (!a.no_preview) preview(px, size);
  say(F("Saved: %s", out.c_str()));
  say(F("       %d x %d pixels, %s style, ready to use as a picture.",
        size, size, style_label.c_str()));
  return 0;
}

int main(int argc, char **argv) {
  init_paths(argv);
  init_themes();
  signal(SIGINT, [](int) {
    say();
    say("Cancelled - nothing was saved.");
    _exit(0);
  });
  Args a = parse_args(argc, argv);
  try {
    return run_main(std::move(a));
  } catch (Cancelled &) {
    say();
    say("Cancelled - nothing was saved.");
    return 0;
  } catch (ListError &e) {
    fprintf(stderr, "%s\n", e.what());
    return 1;
  } catch (FontError &e) {
    fprintf(stderr, "Sorry - I couldn't read that font (%s)\n", e.what());
    return 1;
  } catch (std::exception &e) {
    fprintf(stderr, "Unexpected problem: %s\n", e.what());
    return 1;
  }
}
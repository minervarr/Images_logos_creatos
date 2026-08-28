# AGENTS.md

Single-file Python CLI: `glyphpng.py` turns letters into square PNG logos by parsing font files directly (sfnt/TrueType/CFF charstrings) and hand-rasterizing them. Not a git repo.

## Hard constraints

- **Stdlib only.** The whole point of the codebase: no image or font libraries (no Pillow/fontTools). Only unusual import is `zlib`, required for PNG IDAT compression. Do not add third-party dependencies.
- Font outlines are drawn manually: TrueType quadratics via `quad_contour`, CFF Type2 via the `Type2` interpreter, filled by the nonzero-winding scanline rasterizer in `rasterize()`.
- `glyphpng.cpp` is a **C++17 twin** of the Python script (same flags, menus, messages; links only `-lz`). Its purpose is byte-identical PNGs for identical inputs — verified with `cmp`. When changing render/layout/CLI behavior, port the change to the twin and re-verify parity with:
  ```sh
  make && ./glyphpng A --style modern --colors crimson --no-preview -o /tmp/x.png
  ./glyphpng.py A --style modern --colors crimson --no-preview -o /tmp/y.png
  cmp /tmp/x.png /tmp/y.png
  ```

## Running and verifying

No tests, linter config, CI, or requirements.txt exist. Verification = run the script:

```sh
make                                      # build the C++ twin (needs zlib dev)
./glyphpng.py A --style modern --colors crimson --no-preview -o /tmp/x.png   # smoke test
./glyphpng.py --styles        # valid style keys
./glyphpng.py --colors-list   # color pairs/themes and named colors
```

Python 3.14 works. No args opens an interactive menu; safe when stdin is EOF (exits 0).

## Structure

- `assets/fonts/` — bundled OTF/TTF fonts. A style key only exists via the `STYLES` table in `glyphpng.py` (~line 33) plus the mirrored `STYLES` vector in `glyphpng.cpp`, mapping key → font path relative to `FONTDIR`. Adding a font requires entries in both.
- `pictures/` — default output folder for all PNGs (`--compare` writes into `pictures/compare/` unless `--out-dir`).
- `bundles.json` — saved looks (name/style/colors/text-color/background/size/padding); written by `--new-bundle` and the menus.
- `texts.txt` — labeled text blocks ([label] line, then words, blank line between) drawn in bulk via `--bundle NAME(s) --texts texts.txt`.
- Logo list format (`--from FILE`, e.g. from `--new-list`): `[defaults]` + `[name]` blocks of `key = value`. Different format from `texts.txt`; do not mix them.

## Quirks

- Help text sometimes shows `./logo`: `PROG` becomes `./logo` if a file named `logo` exists next to the script/binary, else falls back to program name.
- The C++ binary resolves its data dir from `/proc/self/exe`, so it must sit beside `assets/fonts` (i.e., run it from this repo or copy the whole tree). Bundles/texts are also read/written there.
- Multi-line text: in `texts.txt` blank lines separate entries; in list files an empty `text:` takes indented lines below it verbatim.
- Colors accept names from `NAMED`, hex (#rgb/#rrggbb/#rrggbbaa), or `none`/`clear` for transparent background (background only — text must have a visible color).
- `--bundle` may be repeated: with `--texts` it applies every look per entry, with a positional text each look gets its own prefixed PNG (bundle name first). Only one bundle is applied when a single text uses a single `-b`.
- `--compare styles|colors|both [TEXT]` draws the text against every look into `pictures/compare/` (or `--out-dir`); themes whose reversed pair has an invisible foreground (`cutout-r`) are skipped automatically.
- PNG parity gotchas for the twin: sfnt directory needs the searchRange/rangeShift skip (6 bytes after numTables); glyf flag REPEAT must read its count byte once; format 4 cmap skips 6 more bytes before endCode; float pipeline must run with `-ffp-contract=off` to match CPython bit-for-bit.

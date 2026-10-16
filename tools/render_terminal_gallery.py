#!/usr/bin/env python3
"""Render captured terminal cells for inspection without running an interactive UI."""
import argparse
import html
import json
import unicodedata
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--cjk-font", type=Path)
    args = parser.parse_args()
    mono = ImageFont.truetype("/usr/share/fonts/noto/NotoSansMono-Regular.ttf", 16)
    cjk = ImageFont.truetype(str(args.cjk_font), 18) if args.cjk_font else mono
    images = []
    for source in sorted(args.directory.glob("*.json")):
        frame = json.loads(source.read_text())
        picture = Image.new("RGB", (frame["columns"] * 10, frame["rows"] * 22), (12, 12, 12))
        draw = ImageDraw.Draw(picture)
        for column, row, glyph, foreground, background in frame["cells"]:
            bg = tuple(max(0, value) for value in background)
            draw.rectangle((column * 10, row * 22, column * 10 + 9, row * 22 + 21), fill=bg)
        # Paint backgrounds first so continuation cells cannot erase half a CJK glyph.
        for column, row, glyph, foreground, background in frame["cells"]:
            if glyph and glyph != " ":
                font = cjk if any(unicodedata.east_asian_width(ch) in ("W", "F") for ch in glyph) else mono
                draw.text((column * 10, row * 22), glyph, font=font,
                          fill=tuple(max(0, value) for value in foreground))
        picture.save(source.with_suffix(".png"))
        images.append(source.with_suffix(".png"))
    cards = "".join(f'<figure><figcaption>{html.escape(p.stem)}</figcaption>'
                    f'<a href="{html.escape(p.name)}"><img loading="lazy" src="{html.escape(p.name)}"></a></figure>' for p in images)
    page = '<!doctype html><meta charset="utf-8"><title>TerMino UI previews</title>' \
           '<style>body{background:#151515;color:#ddd;font:16px sans-serif;margin:24px}' \
           'main{display:grid;grid-template-columns:repeat(auto-fit,minmax(360px,1fr));gap:20px}' \
           'figure{margin:0;background:#222;padding:12px}img{width:100%;height:auto}' \
           'figcaption{margin-bottom:8px}a{color:inherit}</style>' \
           f'<h1>TerMino UI previews</h1><p>{len(images)} captured terminal frames</p><main>{cards}</main>'
    (args.directory / "index.html").write_text(page)
    print(f"Rendered {len(images)} previews: {args.directory / 'index.html'}")


if __name__ == "__main__":
    main()

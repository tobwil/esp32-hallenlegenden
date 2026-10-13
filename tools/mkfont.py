import sys, io
from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont
src = sys.argv[1]; out = sys.argv[2]
chars = [chr(c) for c in range(32, 127)] + list('ÄÖÜäöüß')
fonts = []
for f in src.split(','):
    t = TTFont(f); b = io.BytesIO(); t.flavor = None; t.save(b); b.seek(0); fonts.append((t, b.getvalue()))
def glyph(ch):
    for t, data in fonts:
        if ord(ch) in t.getBestCmap():
            fnt = ImageFont.truetype(io.BytesIO(data), 8)
            im = Image.new('L', (16, 16), 0); ImageDraw.Draw(im).text((0, 0), ch, font=fnt, fill=255)
            return im
    raise SystemExit('missing ' + ch)
# Baseline-Lage aus 'H' bestimmen
rows = []
lines = ['// Erzeugt von tools/mkfont.py aus Press Start 2P (SIL Open Font License 1.1, © CodeMan38).', '// 8x8 Pixel pro Zeichen, Bit 7 = linke Spalte.', '#pragma once', '#include <cstdint>', 'namespace font {', 'static const char32_t kExtra[] = {0xC4,0xD6,0xDC,0xE4,0xF6,0xFC,0xDF};', 'static const uint8_t kGlyphs[][8] = {']
h = glyph('H'); bb = h.getbbox(); top = bb[1]
for ch in chars:
    im = glyph(ch); bs = []
    for y in range(8):
        v = 0
        for x in range(8):
            if im.getpixel((x, y + top)) > 127: v |= 0x80 >> x
        bs.append(v)
    lines.append('  {%s}, // %s' % (','.join('0x%02X' % v for v in bs), repr(ch)))
lines += ['};', '}  // namespace font']
open(out, 'w').write('\n'.join(lines) + '\n')
print('ok', len(chars), 'top', top)

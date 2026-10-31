"""Build per-mode contact sheets from consecutive decoded frames (verifier observation aid, no scoring)."""
import sys, os
from PIL import Image, ImageDraw

src = sys.argv[1]   # frames dir
out = sys.argv[2]   # output dir
os.makedirs(out, exist_ok=True)
modes = ['off', 'A_weak_s20', 'A_strong_s60', 'B_display_s60']
# crops in original 2400x1080 coordinates
LINES = (325, 395, 870, 750)     # vertical + horizontal 1px line blocks
TEXT = (325, 0, 1000, 150)       # thin Korean/Latin text lines
CHECK = (1660, 400, 2150, 760)   # checkerboard
COUNTER = (1795, 985, 2180, 1075)
for m in modes:
    frames = [Image.open(os.path.join(src, f'{m}_{i:02d}.png')).convert('RGB') for i in (1, 2, 3, 4)]
    cols = []
    for f in frames:
        parts = [f.crop(COUNTER), f.crop(TEXT), f.crop(LINES), f.crop(CHECK)]
        w = max(p.width for p in parts)
        h = sum(p.height for p in parts) + 8 * len(parts)
        col = Image.new('RGB', (w, h), (40, 40, 40))
        y = 0
        for p in parts:
            col.paste(p, (0, y)); y += p.height + 8
        cols.append(col)
    W = sum(c.width for c in cols) + 10 * len(cols)
    H = max(c.height for c in cols) + 30
    sheet = Image.new('RGB', (W, H), (20, 20, 20))
    d = ImageDraw.Draw(sheet)
    d.text((5, 5), f'{m}: 4 consecutive decoded output frames (screenrecord), left->right', fill=(255, 255, 0))
    x = 0
    for c in cols:
        sheet.paste(c, (x, 30)); x += c.width + 10
    sheet.save(os.path.join(out, f'sheet_{m}.png'))
    print(m, sheet.size)

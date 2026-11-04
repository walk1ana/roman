#!/usr/bin/env python3
"""Рендерит JSONL-лог тестового бэкенда в PNG (для проверки раскладки без Windows)."""
import json, sys, os, struct
from PIL import Image, ImageDraw, ImageFont
W, H = 450, 800
def load_assets(path):
    d = open(path, 'rb').read(); n = struct.unpack('<I', d[4:8])[0]; imgs = []
    for i in range(n):
        e = d[8+i*40:8+i*40+40]; name = e[:24].rstrip(b'\0').decode(); typ, a, b, off = struct.unpack('<IIII', e[24:])
        if typ == 1: imgs.append((name, Image.frombytes('RGBA', (a, b), d[off:off+a*b*4])))
    return dict(imgs)
A = load_assets(os.path.join(os.path.dirname(__file__), '..', 'assets', 'assets.bin'))
IMG_ORDER = {}
def font(sz, bold):
    p = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf' if bold else '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    return ImageFont.truetype(p, max(6, int(sz)))
def render(path, out):
    S = 2
    img = Image.new('RGBA', (W*S, H*S), (16, 16, 16, 255))
    for line in open(path, encoding='utf-8'):
        c = json.loads(line); col = tuple(c['c'][:3]); al = c['c'][3]
        mask_clip = None
        if c['clip']:
            mask_clip = Image.new('L', img.size, 255)
            for poly in c['clip']:
                m = Image.new('L', img.size, 0); ImageDraw.Draw(m).polygon([(x*S, y*S) for x, y in poly], fill=255)
                from PIL import ImageChops; mask_clip = ImageChops.multiply(mask_clip, m)
        layer = Image.new('RGBA', img.size, (0, 0, 0, 0)); d = ImageDraw.Draw(layer); k = c['k']
        if k == 'poly': d.polygon([(x*S, y*S) for x, y in c['p']], fill=col + (255,))
        elif k == 'line':
            pts = [(x*S, y*S) for x, y in c['p']]
            if c['cl']: pts = pts + [pts[0]]
            d.line(pts, fill=col + (255,), width=max(1, int(c['lw']*S)), joint='curve')
        elif k == 'img':
            pass
        elif k == 'txt':
            s = c['s']; sz = c['sz']; f = font(sz*S, c['wt'] >= 600)
            if all(ord(ch) > 0x2000 or ch in ' ' for ch in s) and s.strip():   # эмодзи — цветная метка
                w = sz*S*1.1*len(s); x = c['x']*S - (w/2 if c['al'] == 1 else 0); y = c['y']*S
                d.ellipse([x+w*.1, y-sz*S*.5*(1 if c['va']==1 else 1.6), x+w*.9, y+sz*S*.5*(1 if c['va']==1 else 0)], fill=(255, 190, 60, 255), outline=(120, 80, 0, 255))
            else:
                bb = d.textbbox((0, 0), s, font=f); tw = bb[2]-bb[0]
                x = c['x']*S - (tw/2 if c['al'] == 1 else tw if c['al'] == 2 else 0); y = c['y']*S
                anchor = {0: 'ls', 1: 'lm', 2: 'lt'}[c['va']]
                d.text((x, y), s, font=f, fill=col + (255,), anchor=anchor)
        if k == 'img':
            im = A.get(IMG_NAMES[c['id']] if 'IMG_NAMES' in globals() and c['id'] < len(IMG_NAMES) else None)
            if im is not None:
                sx, sy, sw, sh = c['src']; crop = im.crop((int(sx), int(sy), int(sx+sw), int(sy+sh)))
                p = c['p']; x0, y0 = p[0]; x1, y1 = p[2]
                import math
                ang = math.degrees(math.atan2(p[1][1]-p[0][1], p[1][0]-p[0][0]))
                w_ = math.hypot(p[1][0]-p[0][0], p[1][1]-p[0][1]); h_ = math.hypot(p[3][0]-p[0][0], p[3][1]-p[0][1])
                if w_ > 0.5 and h_ > 0.5:
                    t = crop.resize((max(1, int(w_*S)), max(1, int(h_*S))))
                    if abs(ang) > 0.5:
                        t = t.rotate(-ang, expand=True, resample=Image.BICUBIC)
                        cx = sum(q[0] for q in p)/4*S; cy = sum(q[1] for q in p)/4*S
                        layer.paste(t, (int(cx - t.width/2), int(cy - t.height/2)), t)
                    else:
                        layer.paste(t, (int(min(q[0] for q in p)*S), int(min(q[1] for q in p)*S)), t)
        if al < 1: layer.putalpha(layer.getchannel('A').point(lambda v: int(v*al)))
        if mask_clip is not None:
            from PIL import ImageChops; layer.putalpha(ImageChops.multiply(layer.getchannel('A'), mask_clip))
        img = Image.alpha_composite(img, layer)
    img.convert('RGB').resize((W, H), Image.LANCZOS).save(out)
if __name__ == '__main__':
    # порядок картинок в логе = порядок первого появления; сопоставляем по размерам при каждом файле
    IMG_NAMES = None
    for p in sys.argv[1:]:
        # определяем имена картинок по эвристике размеров (id -> имя) по первому упоминанию
        seen = []
        for line in open(p, encoding='utf-8'):
            if '"k":"img"' in line:
                c = json.loads(line)
                if c['id'] not in [s[0] for s in seen]: seen.append((c['id'], c['src']))
        IMG_NAMES = [None]*(max([s[0] for s in seen])+1 if seen else 0)
        for i, src in seen:
            for n, im in A.items():
                if (abs(src[2]-im.width) < 1 and abs(src[3]-im.height) < 1) or (n.startswith('face') and src[2] <= im.width and src[3] <= im.height and src[2] == src[3]):
                    IMG_NAMES[i] = n; break
        render(p, p.replace('.jsonl', '.png'))

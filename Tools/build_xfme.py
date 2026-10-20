#!/usr/bin/env python3
"""elysia xfilter Mastering Edition (19", 1U).
Podklad: ostra celni fotka standardniho 1U xfilteru (stejny panel), na ktere se:
  - odstrani knoby (vykresli se vlastni stribrne knoby, otacene podle hodnoty),
  - zhasnou LED (sviceni kresli plugin),
  - prekresli popisky stupnic podle Mastering Edition (gain +-5 dB),
  - doplni napis MASTERING EDITION.
Vystup: Assets/xfilterme.jpg, Assets/xfilterme_knob.png, work/geo_xfilterme.json"""
import cv2, numpy as np, json, math, sys
from PIL import Image, ImageDraw, ImageFont
sys.path.insert(0, '/home/claude/panels')
from build_elysia import knob_sprite, shadow, draw_led_off, hex_bgr

WORK = '/home/claude/panels/work'
OUT = '/home/claude/RackMemory/Assets'
SRC = cv2.imread(f'{WORK}/xfme_crop.png')          # 2411 x 215, panel z xfilter_rack_home.png (x 76..2487, y 179.5..394)
H0, W0 = SRC.shape[:2]
TW, TH = 2400, 214                                  # vysledny panel (px profilu)
FONT_B = '/usr/share/fonts/truetype/crosextra/Carlito-Bold.ttf'
FONT_R = '/usr/share/fonts/truetype/crosextra/Carlito-Regular.ttf'

AXY = 109.5
LEFT_X = [321.9, 477.3, 631.2, 789.0]
AXES = LEFT_X + [W0 - x for x in reversed(LEFT_X)]   # osy knobu (souradnice vyrezu)
CAPS = [310, 466, 622, 780, 1628, 1786, 1944, 2088]  # stredy vyrenderovanych cel (jen kvuli mazani)
KNOB_R = 47.0                                        # polomer knobu (sukne) ve vyrezu

# polohy popisku vuci ose (prumer ze 4 levych knobu, prava - nezakryta - strana); uhel -> (dx, dy)
RIGHT_SLOTS = {14: (15.9, -60.2), 39: (41.3, -47.0), 65: (58.0, -23.9), 91: (62.4, 4.9), 118: (52.8, 32.2), 146: (32.1, 52.0)}
def slots(style):
    """style 'L' = knob v leve polovine (slovo Gain/Freq vlevo nahore), 'R' = zrcadlove."""
    right = {a: d for a, d in RIGHT_SLOTS.items()}
    left = {-a: (-d[0], d[1]) for a, d in RIGHT_SLOTS.items() if a != 39}
    s = {**left, **right}
    if style == 'R':
        s = {-a: (-d[0], d[1]) for a, d in s.items()}
    return dict(sorted(s.items()))

import os
STANDARD = os.environ.get('XF_EDITION', 'me') == 'standard'     # standardni 1U xfilter (+-16 / +-13 dB)
NAME = 'xfilter1u' if STANDARD else 'xfilterme'
KNOBS = [  # id, skupina, nazev, styl, popisky po smeru hodin, min, max, stred
    ('ls_gain', 'Low Shelf', 'Gain', 'L', ['-5.0', '-4.8', '-4.2', '-3.5', '-0.5', '0.5', '2.5', '3.5', '4.2', '4.8', '5.0'], '-5', '+5', '0'),
    ('ls_freq', 'Low Shelf', 'Freq', 'L', ['20', '25', '30', '40', '80', '100', '150', '200', '400', '600', '900'], '20', '900', ''),
    ('lm_gain', 'Low Mid', 'Gain', 'L', ['-5.0', '-4.6', '-3.9', '-2.5', '-0.3', '0.3', '1.2', '2.5', '3.9', '4.6', '5.0'], '-5', '+5', '0'),
    ('lm_freq', 'Low Mid', 'Freq', 'L', ['45', '50', '70', '100', '200', '300', '400', '600', '1k0', '1k5', '2k2'], '45', '2k2', ''),
    ('hm_gain', 'High Mid', 'Gain', 'R', ['-5.0', '-4.8', '-3.9', '-2.5', '-1.2', '-0.3', '0.3', '2.5', '3.9', '4.8', '5.0'], '-5', '+5', '0'),
    ('hm_freq', 'High Mid', 'Freq', 'R', ['300', '350', '500', '1k0', '1k5', '2k0', '2k5', '4k5', '8k0', '12k', '16k'], '300', '16k', ''),
    ('hs_gain', 'High Shelf', 'Gain', 'R', ['-5.0', '-4.8', '-3.9', '-2.5', '-1.5', '-0.3', '0.3', '2.5', '3.9', '4.8', '5.0'], '-5', '+5', '0'),
    ('hs_freq', 'High Shelf', 'Freq', 'R', ['700', '800', '1k0', '2k0', '3k0', '4k0', '5k0', '10k', '16k', '22k', '28k'], '700', '28k', ''),
]
if STANDARD:
    # potisk standardniho xfilteru (cteno z celni fotky elysia a z fotky uzivatele 20.10.2026)
    STD = {'ls_gain': (['-16', '-14', '-12', '-8.0', '-1.0', '1.0', '4.0', '8.0', '12', '14', '16'], '-16', '+16'),
           'lm_gain': (['-13', '-11', '-9.0', '-5.0', '-1.0', '1.0', '3.0', '5.0', '9.0', '11', '13'], '-13', '+13'),
           'hm_gain': (['-13', '-11', '-9.0', '-5.0', '-3.0', '-1.0', '1.0', '5.0', '9.0', '11', '13'], '-13', '+13'),
           'hs_gain': (['-16', '-14', '-12', '-8.0', '-4.0', '-1.0', '1.0', '8.0', '12', '14', '16'], '-16', '+16')}
    KNOBS = [(k[0], k[1], k[2], k[3], STD[k[0]][0], STD[k[0]][1], STD[k[0]][2], k[7]) if k[0] in STD else k for k in KNOBS]
LEDS = [  # id, nazev, skupina, x LED, barva, poznamka
    ('hitit', 'Hit It!', 'Switches', 964.0, '#ffd21a', 'ZAP = aktivní, VYP = hardwire bypass.'),
    ('low_cut', 'Low Cut', 'Low Shelf', 1021.8, '#ff2a1a', 'VYP = shelf, ZAP = low cut s rezonancí.'),
    ('lm_q', 'Low Mid Narrow Q', 'Low Mid', 1079.2, '#ff2a1a', 'VYP = široké Q (0.5), ZAP = úzké Q (1.0).'),
    ('hm_q', 'High Mid Narrow Q', 'High Mid', 1374.5, '#ff2a1a', 'VYP = široké Q (0.5), ZAP = úzké Q (1.0).'),
    ('high_cut', 'High Cut', 'High Shelf', 1432.0, '#ff2a1a', 'VYP = shelf, ZAP = high cut s rezonancí.'),
    ('passive', 'Passive Massage', 'Switches', 1490.0, '#ff2a1a', 'Pevný pasivní LC filtr (vysoké pásmo).'),
]
LED_Y, LED_R = 108.5, 4.6


def build():
    img = SRC.copy()
    g = cv2.cvtColor(SRC, cv2.COLOR_BGR2GRAY)
    hsv = cv2.cvtColor(SRC, cv2.COLOR_BGR2HSV)
    yy, xx = np.mgrid[0:H0, 0:W0].astype(np.float32)

    # --- co zachovat: slova Gain/Freq a dB/Hz (zkopiruji se zpet po vymazani)
    keep = []
    bright = ((g > 70) & (hsv[..., 1] < 80)).astype(np.uint8)
    n, lab, st, cen = cv2.connectedComponentsWithStats(cv2.dilate(bright, np.ones((2, 5), np.uint8)))
    for (ax, (kid, grp, nm, style, *_)) in zip(AXES, KNOBS):
        for k in range(1, n):
            cx, cy = cen[k]
            r = math.hypot(cx - ax, cy - AXY)
            a = math.degrees(math.atan2(cx - ax, -(cy - AXY)))
            want = (-52 < a < -30) if style == 'L' else (30 < a < 52)
            if (want and 55 < r < 90 and st[k, 2] > 18) or (abs(abs(a) - 180) < 8 and 45 < r < 64):
                x, y, w, h = st[k, 0], st[k, 1], st[k, 2], st[k, 3]
                keep.append((x - 1, y - 1, w + 2, h + 2))

    # --- vymazat knoby + stare popisky stupnic
    erase = np.zeros((H0, W0), np.uint8)
    for ax, cx in zip(AXES, CAPS):
        cv2.circle(erase, (int(round(ax)), int(round(AXY))), 80, 1, -1)
        cv2.circle(erase, (int(round(cx)), 118), 50, 1, -1)
    for (x, y, w, h) in keep:
        erase[y:y + h, x:x + w] = 0
    # svitici LED
    for (_, _, _, lx, _, _) in LEDS:
        cv2.circle(erase, (int(round(lx)), int(round(LED_Y))), 9, 1, -1)
    # ramecky LOW SHELF apod. (dole) nechat
    erase[172:, :] = 0
    img = cv2.inpaint(img, erase, 6, cv2.INPAINT_TELEA)
    # inpaint muze zanechat skvrny: srovnat jas mazanych oblasti s okolim (jemne rozmazat jen uvnitr)
    soft = cv2.GaussianBlur(img, (0, 0), 3)
    m = cv2.GaussianBlur(erase.astype(np.float32), (0, 0), 2)[..., None]
    img = (img.astype(np.float32) * (1 - m) + soft.astype(np.float32) * m)
    for (x, y, w, h) in keep:
        img[y:y + h, x:x + w] = SRC[y:y + h, x:x + w]

    # --- novy potisk (supersampling 4x)
    ss = 4
    lay = Image.new('L', (W0 * ss, H0 * ss), 0)
    dr = ImageDraw.Draw(lay)
    fnum = ImageFont.truetype(FONT_B, int(13.2 * ss))
    for ax, (kid, grp, nm, style, labels, *_rest) in zip(AXES, KNOBS):
        sl = slots(style)
        assert len(sl) == len(labels), (kid, len(sl))
        for (a, (dx, dy)), t in zip(sl.items(), labels):
            x, y = (ax + dx) * ss, (AXY + dy) * ss
            dr.text((x, y), t, font=fnum, fill=255, anchor='mm')
    ft = ImageFont.truetype(FONT_R, int(16.5 * ss))
    for i, t in enumerate([] if STANDARD else ['MASTERING', 'EDITION']):
        dr.text((2251 * ss, (91 + 20 * i) * ss), t, font=ft, fill=255, anchor='mm')
    a = np.asarray(lay.resize((W0, H0), Image.LANCZOS), np.float32) / 255.0
    ink = np.array([226, 226, 224], np.float32)
    img = img * (1 - a[..., None]) + ink * a[..., None]

    # --- zhasnute LED
    for (_, _, _, lx, col, _) in LEDS:
        draw_led_off(img, lx, LED_Y, LED_R, hex_bgr(col))

    # --- prevod na velikost profilu
    sx, sy = TW / W0, TH / H0
    img = cv2.resize(np.clip(img, 0, 255).astype(np.uint8), (TW, TH), interpolation=cv2.INTER_AREA).astype(np.float32)
    R = KNOB_R * sx
    for ax in AXES:
        shadow(img, ax * sx, AXY * sy, R, 0.55, 3, 6, 6)
    cv2.imwrite(f'{OUT}/{NAME}.jpg', np.clip(img, 0, 255).astype(np.uint8), [cv2.IMWRITE_JPEG_QUALITY, 93])
    spr = knob_sprite(R)
    cv2.imwrite(f'{OUT}/{NAME}_knob.png', spr)

    geo = {'w': TW, 'h': TH, 'image': NAME + '.jpg', 'knobs': [], 'buttons': []}
    for ax, (kid, grp, nm, style, labels, mn, mx, ctr) in zip(AXES, KNOBS):
        geo['knobs'].append({'id': kid, 'group': grp, 'name': nm, 'x': round(ax * sx, 1), 'y': round(AXY * sy, 1), 'r': round(R, 1),
                             'sprite': NAME + '_knob.png', 'spriteR': round(spr.shape[0] / 2.0, 1),
                             'labels': labels, 'min': mn, 'max': mx, 'centre': ctr})
    for (bid, nm, grp, lx, col, note) in LEDS:
        geo['buttons'].append({'id': bid, 'name': nm, 'group': grp, 'note': note,
                               'x': round((lx - 29) * sx, 1), 'y': round(109 * sy, 1), 'r': round(16 * sx, 1),
                               'led': {'x': round(lx * sx, 1), 'y': round(LED_Y * sy, 1), 'r': round(LED_R * sx, 1), 'colour': col}})
    json.dump(geo, open(f'{WORK}/geo_{NAME}.json', 'w'), indent=1, ensure_ascii=False)

    # nahled s knoby
    prev = img.copy()
    for i, ax in enumerate(AXES):
        ang = -150 + 300 * i / 7
        M = cv2.getRotationMatrix2D((spr.shape[1] / 2, spr.shape[0] / 2), -ang, 1.0)
        rs = cv2.warpAffine(spr, M, (spr.shape[1], spr.shape[0]), flags=cv2.INTER_CUBIC)
        x0 = int(round(ax * sx - spr.shape[1] / 2)); y0 = int(round(AXY * sy - spr.shape[0] / 2))
        al = rs[..., 3:4].astype(np.float32) / 255
        prev[y0:y0 + rs.shape[0], x0:x0 + rs.shape[1]] = prev[y0:y0 + rs.shape[0], x0:x0 + rs.shape[1]] * (1 - al) + rs[..., :3] * al
    os.makedirs('/tmp/claude-0/t', exist_ok=True); cv2.imwrite(f'/tmp/claude-0/t/{NAME}_prev.png', prev.astype(np.uint8))


if __name__ == '__main__':
    build()

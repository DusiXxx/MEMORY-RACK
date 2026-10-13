#!/usr/bin/env python3
"""Sklada celni panely elysia 500 z oficialnich recall sheetu (presna geometrie a potisk)
+ renderovane knoby / tlacitka. Vystup: Assets/<id>.jpg, sprite PNG, <id>.json (geometrie)."""
import cv2, numpy as np, json, os, sys

WORK = '/home/claude/panels/work'
OUT = '/home/claude/RackMemory/Assets'
os.makedirs(OUT, exist_ok=True)
TW = 900
S = TW / 1619.0
TH = int(round(2835 * S))

PANEL_TOP = np.array([30, 20, 16], np.float32)      # BGR, tmave namorni cerna
PANEL_BOT = np.array([22, 15, 12], np.float32)
PRINT = np.array([236, 234, 232], np.float32)


def disc_mask(h, w, cx, cy, r, feather=1.0):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    d = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
    return np.clip((r - d) / feather + 0.5, 0, 1)


def blend(img, colour, alpha):
    a = alpha[..., None]
    img[:] = img * (1 - a) + np.array(colour, np.float32) * a


def shadow(img, cx, cy, r, strength=0.55, dx=5, dy=9, blur=13):
    h, w = img.shape[:2]
    m = disc_mask(h, w, cx + dx, cy + dy, r * 1.04, 2.0)
    m = cv2.GaussianBlur(m, (0, 0), blur) * strength
    img *= (1 - m[..., None])


def radial(h, w, cx, cy):
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    dx, dy = xx - cx, yy - cy
    return np.sqrt(dx * dx + dy * dy), np.arctan2(dx, -dy), dx, dy


def knob_sprite(R, ss=4):
    """Stribrny elysia knob pri pohledu zepredu (podle fotek vyrobce): tmavsi kuzelova sukne,
    svetle celo s prohlubni a vyfrezovanym zarezem na okraji. Zarez miri nahoru. Vraci BGRA."""
    n = int(np.ceil(R * 2 + 8)) * ss
    c = n / 2.0
    r = R * ss
    d, th, dx, dy = radial(n, n, c, c)
    light = (-dx * 0.55 - dy * 0.83)

    # sukne (kuzel): stredne seda, svetlo zleva shora, k okraji tmavne
    skirt = 148 + 34 * light / r + 8 * np.cos(2 * th + 0.6)
    skirt *= np.clip(1.0 - 0.45 * np.clip((d - r * 0.90) / (r * 0.10), 0, 1) ** 2, 0, 1)
    img = np.repeat(skirt[..., None], 3, axis=2).astype(np.float32)

    # celo
    rc = r * 0.74
    mcap = np.clip((rc - d) / ss + 0.5, 0, 1)
    sh = cv2.GaussianBlur(disc_mask(n, n, c + 0.03 * r, c + 0.05 * r, rc * 1.04, ss), (0, 0), ss * 2.0)
    img *= (1 - 0.30 * sh * (1 - mcap))[..., None]
    cap = 226 + 12 * light / rc
    # prohluben uprostred cela
    rd = rc * 0.72
    dish = 232 - 10 * light / rd
    mdish = np.clip((rd - d) / ss + 0.5, 0, 1)
    edge = np.clip(1.0 - np.abs(d - rd) / (ss * 1.6), 0, 1)
    cap = cap * (1 - mdish) + dish * mdish
    cap = cap - 16 * edge
    img[:] = img * (1 - mcap[..., None]) + np.clip(cap, 0, 250)[..., None] * mcap[..., None]

    # zarez na okraji cela: svetla drazka s tmavymi hranami
    half = np.radians(4.6)
    ang = np.abs(th)
    inband = (d > rd * 1.0) & (d < rc * 1.0)
    slot = np.clip((half - ang) / np.radians(0.9) + 0.5, 0, 1) * inband
    edges = np.clip(1.0 - np.abs(ang - half) / np.radians(1.1), 0, 1) * inband
    slot = cv2.GaussianBlur(slot.astype(np.float32), (0, 0), ss * 0.5)
    edges = cv2.GaussianBlur(edges.astype(np.float32), (0, 0), ss * 0.5)
    img[:] = img * (1 - 0.55 * slot[..., None]) + 246 * 0.55 * slot[..., None]
    img[:] = img * (1 - 0.75 * edges[..., None]) + 96 * 0.75 * edges[..., None]

    # znacka na sukni v prodlouzeni zarezu (na skutecnem knobu tmava tecka u okraje)
    mark = ((np.abs(dx) < r * 0.03) & (dy < -r * 0.80) & (dy > -r * 0.985)).astype(np.float32)
    mark = cv2.GaussianBlur(mark, (0, 0), ss * 0.6)
    img[:] = img * (1 - 0.9 * mark[..., None]) + 30 * 0.9 * mark[..., None]

    img[..., 0] *= 1.015
    img[..., 2] *= 0.985

    alpha = np.clip((r - d) / ss + 0.5, 0, 1)
    out = np.dstack([np.clip(img, 0, 255), alpha * 255])
    out = cv2.resize(out, (n // ss, n // ss), interpolation=cv2.INTER_AREA)
    return out.astype(np.uint8)


def draw_button(img, cx, cy, r):
    h, w = img.shape[:2]
    shadow(img, cx, cy, r, 0.5, 2, 4, 5)
    d, th, dx, dy = radial(h, w, cx, cy)
    m = np.clip((r - d) + 0.5, 0, 1)
    body = 16 + 22 * np.clip((-dx * 0.5 - dy * 0.8) / r, -1, 1) + 14 * np.clip(1 - d / (r * 0.55), 0, 1)
    body = np.clip(body, 4, 70)
    col = np.dstack([body * 1.05, body, body * 0.97])
    img[:] = img * (1 - m[..., None]) + col * m[..., None]
    ring = np.clip(1.5 - np.abs(d - r), 0, 1) * 0.5
    img[:] = img * (1 - ring[..., None]) + 5 * ring[..., None]


def draw_ring_button(img, cx, cy, r):
    """xmax: cerne tlacitko v kovovem krouzku (krouzek sviti az v pluginu)."""
    h, w = img.shape[:2]
    shadow(img, cx, cy, r * 1.25, 0.45, 2, 4, 5)
    d, th, dx, dy = radial(h, w, cx, cy)
    ro = r * 1.25
    m = np.clip((ro - d) + 0.5, 0, 1)
    ringc = 62 + 34 * np.clip((-dx * 0.5 - dy * 0.8) / ro, -1, 1)
    img[:] = img * (1 - m[..., None]) + np.dstack([ringc * 0.95, ringc * 0.92, ringc * 0.92]) * m[..., None]
    draw_button_plain(img, cx, cy, r * 0.98)


def draw_button_plain(img, cx, cy, r):
    h, w = img.shape[:2]
    d, th, dx, dy = radial(h, w, cx, cy)
    m = np.clip((r - d) + 0.5, 0, 1)
    body = np.clip(14 + 20 * np.clip((-dx * 0.5 - dy * 0.8) / r, -1, 1) + 12 * np.clip(1 - d / (r * 0.55), 0, 1), 4, 64)
    img[:] = img * (1 - m[..., None]) + np.dstack([body, body, body]) * m[..., None]


def draw_screw(img, cx, cy, r):
    h, w = img.shape[:2]
    d, th, dx, dy = radial(h, w, cx, cy)
    m = np.clip((r - d) + 0.5, 0, 1)
    v = 175 + 45 * np.clip((-dx * 0.5 - dy * 0.8) / r, -1, 1)
    v = np.where(d < r * 0.55, v * 0.55, v)
    v = np.where(d < r * 0.30, 40, v)
    img[:] = img * (1 - m[..., None]) + np.dstack([v, v, v]) * m[..., None]


def draw_badge(img, cx, cy, r, text_alpha):
    h, w = img.shape[:2]
    shadow(img, cx, cy, r, 0.5, 4, 8, 10)
    d, th, dx, dy = radial(h, w, cx, cy)
    m = np.clip((r - d) + 0.5, 0, 1)
    chrome = 150 + 95 * np.cos(th * 2 + 0.9)
    chrome = np.clip(chrome, 40, 250)
    img[:] = img * (1 - m[..., None]) + np.dstack([chrome, chrome, chrome]) * m[..., None]
    ri = r * 0.86
    mi = np.clip((ri - d) + 0.5, 0, 1)
    face = 236 + 14 * np.clip((-dy) / ri, -1, 1) - 30 * np.clip((d / ri) ** 6, 0, 1)
    img[:] = img * (1 - mi[..., None]) + np.dstack([face * 0.99, face, face]) * mi[..., None]
    a = (text_alpha * mi)[..., None]
    img[:] = img * (1 - a) + np.array([58, 52, 48], np.float32) * a


def draw_led_off(img, cx, cy, r, colour):
    h, w = img.shape[:2]
    d, th, dx, dy = radial(h, w, cx, cy)
    m = np.clip((r - d) + 0.5, 0, 1)
    c = np.array(colour, np.float32) * 0.22 + 12
    img[:] = img * (1 - m[..., None]) + c * m[..., None]


def hex_bgr(h):
    h = h.lstrip('#')
    return (int(h[4:6], 16), int(h[2:4], 16), int(h[0:2], 16))


def build(name, dev):
    p = cv2.imread(f'{WORK}/p_{name}.png', 0).astype(np.float32)
    H, W = p.shape
    ink = 255 - p

    # text loga pred smazanim
    bx, by, br = dev['badge']
    badge_ink = ink.copy()
    badge_ink *= disc_mask(H, W, bx, by, br - 16, 2)

    # smazat obrys panelu, kruznice soucastek, pripadne dalsi oblasti
    erase = np.zeros((H, W), np.float32)
    b = dev.get('border', 22)
    erase[:b, :] = 1; erase[-b:, :] = 1; erase[:, :b] = 1; erase[:, -b:] = 1
    for (x, y, r) in dev['erase']:
        erase = np.maximum(erase, disc_mask(H, W, x, y, r, 2))
    for (x0, y0, x1, y1) in dev.get('erase_rects', []):
        erase[y0:y1, x0:x1] = 1
    ink *= (1 - erase)

    a = cv2.resize(ink, (TW, TH), interpolation=cv2.INTER_AREA) / 255.0
    a = np.clip(a * 1.25, 0, 1)
    ba = cv2.resize(badge_ink, (TW, TH), interpolation=cv2.INTER_AREA) / 255.0

    t = np.linspace(0, 1, TH, dtype=np.float32)[:, None, None]
    img = np.ones((TH, TW, 3), np.float32) * (PANEL_TOP * (1 - t) + PANEL_BOT * t)
    # jemny lesk
    xx = np.linspace(-1, 1, TW, dtype=np.float32)[None, :, None]
    img *= (1.0 + 0.10 * np.exp(-((xx + 0.35) ** 2) / 0.5))
    blend(img, PRINT, a)

    sc = lambda v: v * S
    for (x, y, r) in dev['screws']:
        draw_screw(img, sc(x), sc(y), sc(r))
    draw_badge(img, sc(bx), sc(by), sc(br), ba)
    for (x, y, r, col) in dev['leds_static']:
        draw_led_off(img, sc(x), sc(y), sc(r), col)
    for bt in dev['buttons']:
        if dev.get('ring_buttons'):
            draw_ring_button(img, sc(bt['x']), sc(bt['y']), sc(bt['r']))
        else:
            draw_button(img, sc(bt['x']), sc(bt['y']), sc(bt['r']))
            lx, ly, lr = bt['led']
            draw_led_off(img, sc(lx), sc(ly), sc(lr), hex_bgr(bt['ledColour']))
    for k in dev['knobs']:
        shadow(img, sc(k['x']), sc(k['y']), sc(dev['knobR']), 0.6, 6, 11, 12)

    # hrana panelu
    cv2.rectangle(img, (0, 0), (TW - 1, TH - 1), (150, 150, 150), 2, cv2.LINE_AA)
    cv2.rectangle(img, (2, 2), (TW - 3, TH - 3), (60, 60, 60), 1, cv2.LINE_AA)

    cv2.imwrite(f'{OUT}/{name}.jpg', np.clip(img, 0, 255).astype(np.uint8), [cv2.IMWRITE_JPEG_QUALITY, 92])
    spr = knob_sprite(sc(dev['knobR']))
    cv2.imwrite(f'{OUT}/{name}_knob.png', spr)

    geo = {'w': TW, 'h': TH, 'image': f'{name}.jpg', 'knobs': {}, 'buttons': {}}
    for k in dev['knobs']:
        geo['knobs'][k['id']] = {'x': round(sc(k['x']), 1), 'y': round(sc(k['y']), 1), 'r': round(sc(dev['knobR']), 1),
                                 'sprite': f'{name}_knob.png', 'spriteR': round(spr.shape[0] / 2.0, 1)}
    for bt in dev['buttons']:
        g = {'x': round(sc(bt['x']), 1), 'y': round(sc(bt['y']), 1), 'r': round(sc(bt['r']), 1)}
        if dev.get('ring_buttons'):
            g['glow'] = {'shape': 'ring', 'x': g['x'], 'y': g['y'], 'r': round(sc(bt['r']) * 1.13, 1),
                         'w': round(sc(bt['r']) * 0.26, 1), 'colour': bt['ledColour']}
        else:
            lx, ly, lr = bt['led']
            g['glow'] = {'shape': 'dot', 'x': round(sc(lx), 1), 'y': round(sc(ly), 1), 'r': round(sc(lr), 1), 'colour': bt['ledColour']}
        geo['buttons'][bt['id']] = g
    json.dump(geo, open(f'{WORK}/geo_{name}.json', 'w'), indent=1)
    # nahled s knoby
    prev = np.clip(img, 0, 255).astype(np.uint8).copy().astype(np.float32)
    for i, k in enumerate(dev['knobs']):
        ang = (-150 + 300 * (i / max(1, len(dev['knobs']) - 1)))
        M = cv2.getRotationMatrix2D((spr.shape[1] / 2, spr.shape[0] / 2), -ang, 1.0)
        rs = cv2.warpAffine(spr, M, (spr.shape[1], spr.shape[0]), flags=cv2.INTER_CUBIC)
        x0 = int(round(sc(k['x']) - spr.shape[1] / 2)); y0 = int(round(sc(k['y']) - spr.shape[0] / 2))
        al = rs[..., 3:4].astype(np.float32) / 255
        prev[y0:y0 + rs.shape[0], x0:x0 + rs.shape[1]] = prev[y0:y0 + rs.shape[0], x0:x0 + rs.shape[1]] * (1 - al) + rs[..., :3] * al
    cv2.imwrite(f'/tmp/claude-0/prev_{name}.jpg', prev.astype(np.uint8))


RED, YEL = '#ff2a1a', '#ffd21a'
RED_BGR, YEL_BGR, BEIGE = (40, 40, 230), (40, 200, 240), (150, 200, 220)

KX = [450, 1260]
KY = [428, 1089, 1747, 2406]

def knobs(ids):
    return [{'id': i, 'x': x, 'y': y} for (i, x, y) in ids]

def btns(defs, r=76):
    return [{'id': i, 'x': 850, 'y': y, 'r': r, 'led': (850, y - 123, 26), 'ledColour': c} for (i, y, c) in defs]

def common_erase(kn, R, bt, badge, screws):
    e = [(k['x'], k['y'], R + 8) for k in kn]
    e += [(b['x'], b['y'], b['r'] + 12) for b in bt]
    e += [(b['led'][0], b['led'][1], b['led'][2] + 9) for b in bt if 'led' in b]
    e += [(badge[0], badge[1], badge[2] + 10)]
    e += [(x, y, r + 14) for (x, y, r) in screws]
    return e

SCREWS_XN = [(404, 84, 38), (1214, 84, 38), (404, 2750, 38), (1214, 2750, 38)]

# ---- xfilter 500 -------------------------------------------------------------
xf_k = knobs([('ls_gain', 450, 428), ('hm_gain', 1260, 428), ('ls_freq', 450, 1089), ('hm_freq', 1260, 1089),
              ('lm_gain', 450, 1747), ('hs_gain', 1260, 1747), ('lm_freq', 450, 2406), ('hs_freq', 1260, 2406)])
xf_b = btns([('passive', 334, RED), ('low_cut', 578, RED), ('hm_q', 822, RED), ('lm_q', 2078, RED), ('high_cut', 2322, RED), ('hitit', 2566, YEL)])
XF = {'knobs': xf_k, 'knobR': 195, 'buttons': xf_b, 'badge': (850, 1418, 195), 'screws': SCREWS_XN, 'leds_static': []}
XF['erase'] = common_erase(xf_k, 195, xf_b, XF['badge'], SCREWS_XN)

# ---- nvelope 500 -------------------------------------------------------------
nv_k = knobs([('ch1_attack', 450, 428), ('ch2_attack', 1260, 428), ('ch1_freq_a', 450, 1089), ('ch2_freq_a', 1260, 1089),
              ('ch1_sustain', 450, 1747), ('ch2_sustain', 1260, 1747), ('ch1_freq_s', 450, 2406), ('ch2_freq_s', 1260, 2406)])
nv_b = btns([('autogain', 334, RED), ('ch2_fullrange', 578, RED), ('ch2_eqmode', 822, RED), ('ch2_on', 1066, YEL),
             ('link', 1834, RED), ('ch1_fullrange', 2078, RED), ('ch1_eqmode', 2322, RED), ('ch1_on', 2566, YEL)])
NV = {'knobs': nv_k, 'knobR': 195, 'buttons': nv_b, 'badge': (850, 1418, 195), 'screws': SCREWS_XN, 'leds_static': []}
NV['erase'] = common_erase(nv_k, 195, nv_b, NV['badge'], SCREWS_XN)

# ---- xmax 500 ----------------------------------------------------------------
XY = [284, 736, 1187, 1638, 2094, 2546]
xm_k = knobs([('link', 326, XY[0]), ('thr_mid', 326, XY[1]), ('thr_side', 326, XY[2]), ('thr_low', 326, XY[3]), ('xfreq', 326, XY[4]), ('release', 326, XY[5]),
              ('tone', 1148, XY[0]), ('gain_mid', 1148, XY[1]), ('gain_side', 1148, XY[2]), ('gain_low', 1148, XY[3]), ('sclip', 1148, XY[4]), ('level', 1148, XY[5])])
xm_b = [{'id': 'lowmo', 'x': 738, 'y': 1888, 'r': 66, 'ledColour': RED}, {'id': 'punch', 'x': 738, 'y': 2232, 'r': 66, 'ledColour': RED},
        {'id': 'hitit', 'x': 738, 'y': 2580, 'r': 66, 'ledColour': '#ffb000'}]
xm_screws = [(408, 84, 46), (1234, 84, 46), (408, 2750, 46), (1234, 2750, 46)]
leds = []
for x in (650, 822):
    for y in (188, 232):
        leds.append((x, y, 15, YEL_BGR))
    for i in range(7):
        leds.append((x, 364 + 45.3 * i, 15, RED_BGR if i < 4 else BEIGE))
    for i in range(7):
        leds.append((x, 766 + 45.2 * i, 15, (RED_BGR if i < 4 else BEIGE) if x == 650 else YEL_BGR))
XM = {'knobs': xm_k, 'knobR': 157, 'buttons': xm_b, 'ring_buttons': True, 'badge': (738, 1416, 222), 'screws': xm_screws, 'leds_static': leds,
      'erase_rects': [(1560, 0, 1619, 2835)], 'border': 44}
XM['erase'] = [(k['x'], k['y'], 157 + 8) for k in xm_k] + [(b['x'], b['y'], 77 + 12) for b in xm_b] + [(738, 1416, 262)] \
              + [(x, y, 58) for (x, y, r) in xm_screws] + [(x, y, 24) for (x, y, r, c) in leds]

if __name__ == '__main__':
    build('xfilter500', XF)
    build('nvelope500', NV)
    build('xmax500', XM)
    print('ok')

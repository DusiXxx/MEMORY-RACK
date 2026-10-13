#!/usr/bin/env python3
"""Pripravi panely z realnych celnich fotek (SPL, Manley, WesAudio): pozadi + vyrezy knobu + geometrie."""
import cv2, numpy as np, json, os

RAW = '/home/claude/panels/raw'
WORK = '/home/claude/panels/work'
OUT = '/home/claude/RackMemory/Assets'
os.makedirs(OUT, exist_ok=True)


def fit_circle(pts):
    pts = np.array(pts, np.float64)
    A = np.c_[2 * pts[:, 0], 2 * pts[:, 1], np.ones(len(pts))]
    b = (pts ** 2).sum(1)
    cx, cy, c = np.linalg.lstsq(A, b, rcond=None)[0]
    return cx, cy, np.sqrt(c + cx * cx + cy * cy)


def pointer_angle(bgr, cx, cy, rmax, thresh):
    """Uhel rysky: hlavni osa svetlych bodu (nezavisi na presnem stredu), strana podle teziste."""
    g = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY).astype(np.float32)
    h, w = g.shape
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    dx, dy = xx - cx, yy - cy
    m = (np.hypot(dx, dy) < rmax) & (g > thresh)
    if m.sum() < 5:
        return None
    wgt = g[m] - thresh
    mx, my = (dx[m] * wgt).sum() / wgt.sum(), (dy[m] * wgt).sum() / wgt.sum()
    sxx = (wgt * (dx[m] - mx) ** 2).sum(); syy = (wgt * (dy[m] - my) ** 2).sum(); sxy = (wgt * (dx[m] - mx) * (dy[m] - my)).sum()
    th = 0.5 * np.arctan2(2 * sxy, sxx - syy)
    ax, ay = np.cos(th), np.sin(th)
    if ax * mx + ay * my < 0: ax, ay = -ax, -ay
    return float(np.degrees(np.arctan2(ax, -ay)))


def circle_sprite(bgr, cx, cy, r, feather=2.5):
    n = int(np.ceil(r)) * 2 + 2
    M = np.float32([[1, 0, n / 2 - cx], [0, 1, n / 2 - cy]])
    patch = cv2.warpAffine(bgr, M, (n, n), flags=cv2.INTER_CUBIC, borderMode=cv2.BORDER_REPLICATE)
    yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
    d = np.hypot(xx - n / 2 + 0.5, yy - n / 2 + 0.5)
    a = np.clip((r - d) / feather, 0, 1) * 255
    return np.dstack([patch, a.astype(np.uint8)])


def refine_centre(gray, cx, cy, rmin, rmax, win=None):
    win = win or int(rmax * 1.8)
    x0, y0 = int(cx - win), int(max(0, cy - win))
    roi = cv2.medianBlur(gray[y0:int(cy + win), x0:int(cx + win)], 5)
    c = cv2.HoughCircles(roi, cv2.HOUGH_GRADIENT, dp=1, minDist=win, param1=110, param2=25, minRadius=int(rmin), maxRadius=int(rmax))
    if c is None:
        return cx, cy, None
    x, y, r = c[0][0]
    return x + x0, y + y0, r


def dots_centre(bgr, cx, cy, r0, r1):
    """Stred stupnice = stred kruznice prolozene bilymi teckami kolem knobu."""
    g = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
    h, w = g.shape
    yy, xx = np.mgrid[0:h, 0:w]
    d = np.hypot(xx - cx, yy - cy)
    m = ((d > r0) & (d < r1) & (g > 170)).astype(np.uint8)
    n, lab, st, cen = cv2.connectedComponentsWithStats(m)
    pts = [cen[i] for i in range(1, n) if 3 <= st[i, 4] <= 40]
    if len(pts) < 6:
        return cx, cy
    fx, fy, fr = fit_circle(pts)
    if np.hypot(fx - cx, fy - cy) > 12:
        return cx, cy
    return fx, fy


# =============================================================================
def vitalizer():
    im = cv2.imread(f'{RAW}/vitalizer_front.png', cv2.IMREAD_UNCHANGED)
    bgr = im[:, :, :3].copy()
    if im.shape[2] == 4:   # pruhledne rohy -> cerna
        a = im[:, :, 3:4].astype(np.float32) / 255
        bgr = (bgr * a).astype(np.uint8)
    TW = 2400
    s = TW / bgr.shape[1]
    bg = cv2.resize(bgr, (TW, int(round(bgr.shape[0] * s))), interpolation=cv2.INTER_AREA)
    gray = cv2.cvtColor(bg, cv2.COLOR_BGR2GRAY)

    ids = ['drive', 'bass', 'basscomp', 'midhi', 'process', 'lceq', 'intensity', 'expander']
    approx_x = [406, 726, 1045, 1365, 1687, 2003, 2320, 2636]
    geo = {'w': bg.shape[1], 'h': bg.shape[0], 'image': 'vitalizer.jpg', 'knobs': {}, 'buttons': {}}
    dbgpts = []
    for i, x in zip(ids, approx_x):
        sx, sy = dots_centre(bg, x * s, 171 * s, 56, 74)
        # knob je vysoky: cepicka s ryskou je na siroke fotce posunuta od stredu stupnice (paralaxa)
        cx, cy = sx + 0.0065 * (sx - bg.shape[1] / 2), sy
        R = 39.0
        ang = pointer_angle(bg, cx, cy, 38, 120)
        spr = circle_sprite(bg, cx, cy, R)
        cv2.imwrite(f'{OUT}/vitalizer_{i}.png', spr)
        geo['knobs'][i] = {'x': round(float(cx), 1), 'y': round(float(cy), 1), 'r': R, 'sprite': f'vitalizer_{i}.png',
                           'spriteR': spr.shape[0] / 2.0, 'refAngle': round(ang or 0.0, 1)}
        print('vitalizer', i, round(float(cx), 1), round(float(cy), 1), 'angle', ang); dbgpts.append((cx, cy, R, ang or 0))
    # ON: podsviceny ctverec (na fotce sviti)
    geo['buttons']['on'] = {'x': round(3320 * s, 1), 'y': round(226 * s, 1), 'r': round(44 * s, 1),
                            'glow': {'shape': 'rect', 'x': round(3320 * s, 1), 'y': round(226 * s, 1), 'w': round(64 * s, 1), 'h': round(70 * s, 1),
                                     'colour': '#ff8a1a', 'litInPhoto': True}}
    # ryska patri k otocnemu vyrezu; z pozadi se odstrani, aby pod pootocenym knobem neprosvitala puvodni
    gg = cv2.cvtColor(bg, cv2.COLOR_BGR2GRAY)
    mask = np.zeros_like(gg)
    for (cx, cy, R, a) in dbgpts:
        yy, xx = np.mgrid[0:bg.shape[0], 0:bg.shape[1]]
        mask[(np.hypot(xx - cx, yy - cy) < R + 3) & (gg > 95)] = 255
    mask = cv2.dilate(mask, np.ones((5, 5), np.uint8))
    bg_clean = cv2.inpaint(bg, mask, 4, cv2.INPAINT_TELEA)
    cv2.imwrite(f'{OUT}/vitalizer.jpg', bg_clean, [cv2.IMWRITE_JPEG_QUALITY, 93])
    json.dump(geo, open(f'{WORK}/geo_vitalizer.json', 'w'), indent=1)
    dbg = bg.copy()
    for (cx, cy, R, a) in dbgpts:
        cv2.circle(dbg, (int(round(cx)), int(round(cy))), int(R), (0, 255, 0), 1, cv2.LINE_AA)
        a = np.radians(a); cv2.line(dbg, (int(cx), int(cy)), (int(cx + 60 * np.sin(a)), int(cy - 60 * np.cos(a))), (0, 0, 255), 1, cv2.LINE_AA)
    cv2.imwrite('/tmp/claude-0/pv_vitdbg.jpg', np.vstack([cv2.resize(dbg[:, :1000], None, fx=1.9, fy=1.9), cv2.resize(dbg[:, 1000:2000], None, fx=1.9, fy=1.9)]))
    return bg, geo


# =============================================================================
def manley():
    im = cv2.imread(f'{RAW}/manley_mastering_front.png', cv2.IMREAD_UNCHANGED)
    bgr = im[:, :, :3].copy()
    if im.shape[2] == 4:
        a = im[:, :, 3:4].astype(np.float32) / 255
        bgr = (bgr * a + np.array([20, 18, 18]) * (1 - a)).astype(np.uint8)
    bg = bgr
    knobs = {'ch1_recovery': (846, 172), 'ch1_output': (1040, 180), 'input': (1240, 180), 'ch2_output': (1436, 180), 'ch2_recovery': (1635, 178),
             'ch1_threshold': (847, 358), 'ch1_attack': (1041, 355), 'ch2_attack': (1435, 355), 'ch2_threshold': (1635, 358)}
    geo = {'w': bg.shape[1], 'h': bg.shape[0], 'image': 'manley.jpg', 'knobs': {}, 'buttons': {}, 'toggles': {}}
    for i, (x, y) in knobs.items():
        R = 50.0
        ang = pointer_angle(bg, x, y, 44, 175)
        spr = circle_sprite(bg, x, y, R, 4.0)
        cv2.imwrite(f'{OUT}/manley_{i}.png', spr)
        geo['knobs'][i] = {'x': x, 'y': y, 'r': R, 'sprite': f'manley_{i}.png', 'spriteR': spr.shape[0] / 2.0, 'refAngle': round(ang or 0.0, 1)}
        print('manley', i, 'angle', ang)
    # packove prepinace: (x, y, osa, stav na fotce = index polohy)
    toggles = {'ch1_bypass': (304, 386, 'v', 1), 'ch1_mode': (681, 305, 'v', 1), 'ch1_hpsc': (1141, 293, 'v', 0),
               'link': (1236, 292, 'h', 0), 'ch2_hpsc': (1340, 292, 'v', 0), 'ch2_mode': (1806, 306, 'v', 1), 'ch2_bypass': (2187, 387, 'v', 1)}
    for i, (x, y, axis, state) in toggles.items():
        w, h = (58, 74) if axis == 'v' else (78, 58)
        geo['toggles'][i] = {'x': x, 'y': y, 'w': w, 'h': h, 'axis': axis, 'photoState': state, 'r': 30}
    cv2.imwrite(f'{OUT}/manley.jpg', bg, [cv2.IMWRITE_JPEG_QUALITY, 94])
    json.dump(geo, open(f'{WORK}/geo_manley.json', 'w'), indent=1)
    return bg, geo


# =============================================================================
def rhea():
    im = cv2.imread(f'{RAW}/rheaimg-004.png')
    g = cv2.cvtColor(im, cv2.COLOR_BGR2GRAY)
    H, W = g.shape
    P = lambda x, y: (x / 2.6 + 262, y / 2.6 + 570)     # souradnice ze zvetseneho vyrezu -> original

    # --- 1) maska popisovych car manualu (tenke tmave cary) + koncove tecky
    dark = (g < 120).astype(np.uint8) * 255
    hl = cv2.morphologyEx(dark, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_RECT, (26, 1)))
    hthick = cv2.morphologyEx(dark, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_RECT, (26, 6)))
    hl[cv2.dilate(hthick, np.ones((9, 9), np.uint8)) > 0] = 0
    vl = cv2.morphologyEx(dark, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_RECT, (1, 26)))
    vthick = cv2.morphologyEx(dark, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_RECT, (6, 26)))
    vl[cv2.dilate(vthick, np.ones((9, 9), np.uint8)) > 0] = 0
    mask = cv2.dilate(cv2.max(hl, vl), np.ones((5, 5), np.uint8))
    blobs = [P(750, 80), P(165, 210), P(1388, 210), P(750, 633), P(157, 757), P(1403, 752), P(578, 1133), P(1082, 1133),
             P(710, 1200), P(163, 1255), P(1465, 1250), P(945, 1325), P(948, 1388), (425, 290)]
    for (x, y) in blobs:
        cv2.circle(mask, (int(round(x)), int(round(y))), 8, 255, -1)
    # jen v pasech kolem znamych popisovych car (jinak by se poskodily ikony elektronek apod.)
    band = np.zeros_like(mask)
    Hs = [(264, 551, 600.8), (264, 326, 650.8), (795, 858, 650.8), (264, 551, 813.5), (264, 323, 861.2), (801, 858, 859.2),
          (483, 858, 1005.8), (264, 326, 1052.7), (824, 858, 1050.8), (264, 426, 290), (494, 536, 1031.5), (603, 627, 1079.6)]
    Vs = [(494.7, 1031, 1212), (550.5, 1108, 1212), (603.5, 1079, 1212), (626.6, 1110, 1212)]
    for (x0, x1, y) in Hs:
        cv2.rectangle(band, (int(x0), int(y - 6)), (int(x1), int(y + 6)), 255, -1)
    for (x, y0, y1) in Vs:
        cv2.rectangle(band, (int(x - 6), int(y0)), (int(x + 6), int(y1)), 255, -1)
    for (x, y) in blobs:
        cv2.circle(band, (int(round(x)), int(round(y))), 9, 255, -1)
    mask &= band
    clean = cv2.inpaint(im, mask, 5, cv2.INPAINT_TELEA)

    # --- 2) LED krouzky kolem enkoderu
    def ring_dots(cx0, cy0, r):
        yy, xx = np.mgrid[0:H, 0:W]
        d = np.hypot(xx - cx0, yy - cy0)
        m = ((d > r + 8) & (d < r + 27) & (g < 110)).astype(np.uint8)
        n, lab, st, cen = cv2.connectedComponentsWithStats(m)
        return [tuple(cen[i]) for i in range(1, n) if 15 <= st[i, 4] <= 34]

    thr = ring_dots(559.5, 668.5, 47.1)
    tcx, tcy, tr = fit_circle(thr)
    thr_angles = sorted(np.degrees(np.arctan2(x - tcx, -(y - tcy))) for (x, y) in thr)
    # 16 LED rovnomerne mezi krajnimi body
    a0, a1 = thr_angles[0], thr_angles[-1]
    # kraje jsou zakryte popisky MIN/MAX jen castecne; pouzijeme symetricky rozsah
    amax = (abs(a0) + abs(a1)) / 2
    step = 2 * amax / 15.0
    ring_angles = [-amax + step * i for i in range(16)]
    print('rhea ring', round(tcx, 1), round(tcy, 1), round(tr, 1), 'amax', round(amax, 1), 'n', len(thr))

    rings = {}
    caps = {'input': (365.5, 701.5), 'threshold': (559.5, 668.5), 'output': (753.5, 702.5), 'mix': (560.5, 883.5)}
    off = (tcx - 559.5, tcy - 668.5)
    for name, (kx, ky) in caps.items():
        dots = ring_dots(kx, ky, 48)
        if len(dots) >= 8:
            cx, cy, r = fit_circle(dots)
            if abs(r - tr) > 4 or np.hypot(cx - kx, cy - ky) > 14:
                cx, cy, r = kx + off[0], ky + off[1], tr
        else:
            cx, cy, r = kx + off[0], ky + off[1], tr
        rings[name] = (cx, cy, r)
        print('  ring', name, round(cx, 1), round(cy, 1), round(r, 1), len(dots))

    panel_white = lambda x, y: clean[int(y), int(x)].astype(np.float32)
    led_positions = {}
    for name, (cx, cy, r) in rings.items():
        pts = []
        for a in ring_angles:
            x = cx + r * np.sin(np.radians(a)); y = cy - r * np.cos(np.radians(a))
            pts.append((x, y))
        # smazat puvodni tecky v mezikruzi (i zbytky po popiskach) a nakreslit je jednotne
        yy, xx = np.mgrid[0:H, 0:W]
        dd = np.hypot(xx - cx, yy - cy)
        aa = np.degrees(np.arctan2(xx - cx, -(yy - cy)))
        gc = cv2.cvtColor(clean, cv2.COLOR_BGR2GRAY)
        em = ((np.abs(dd - r) < 6) & (np.abs(aa) < amax + 2.5) & (gc < 170)).astype(np.uint8) * 255
        em = cv2.dilate(em, np.ones((3, 3), np.uint8))
        clean[:] = cv2.inpaint(clean, em, 4, cv2.INPAINT_TELEA)
        for (x, y) in pts:
            cv2.circle(clean, (int(round(x)), int(round(y))), 3, (38, 38, 38), -1, cv2.LINE_AA)
        led_positions[name] = pts

    att_pts = [P(113, 840), P(157, 757), P(243, 708), P(338, 707), P(420, 755), P(468, 838)]
    rel_pts = [P(1093, 838), P(1140, 752), P(1222, 706), P(1318, 706), P(1403, 752), P(1450, 835)]
    for (x, y) in (att_pts[1], rel_pts[4]):      # tecky, ktere zakryl popisek
        cv2.circle(clean, (int(round(x)), int(round(y))), 5, (30, 30, 30), -1, cv2.LINE_AA)
    led_positions['attack'] = att_pts
    led_positions['release'] = rel_pts

    # DATA / H-LINK LED: zkopirovat cistou LED (MED)
    mx, my = P(305, 1302)
    patch = clean[int(my) - 9:int(my) + 10, int(mx) - 9:int(mx) + 10].copy()
    for (x, y) in (P(948, 1325), P(948, 1388)):
        clean[int(y) - 9:int(y) + 10, int(x) - 9:int(x) + 10] = patch

    # --- 3) vyrez panelu a zvetseni
    X0, Y0, X1, Y1 = 266, 162, 856, 1212
    crop = clean[Y0:Y1, X0:X1].copy()
    crop[:, :7] = crop[:, 7:8]
    crop[:, -7:] = crop[:, -8:-7]
    K = 1.6
    bg = cv2.resize(crop, None, fx=K, fy=K, interpolation=cv2.INTER_LANCZOS4)
    T = lambda p: (round((p[0] - X0) * K, 1), round((p[1] - Y0) * K, 1))

    geo = {'w': bg.shape[1], 'h': bg.shape[0], 'image': 'rhea.jpg', 'knobs': {}, 'buttons': {}, 'rings': {}, 'leds': {}}
    for name, (kx, ky) in {**caps, 'attack': (367.5, 920.5), 'release': (752.5, 916.5)}.items():
        x, y = T((kx, ky))
        geo['knobs'][name] = {'x': x, 'y': y, 'r': round(48 * K, 1)}
        geo['rings'][name] = [list(T(p)) for p in led_positions[name]]
    sq = lambda p: {'x': T(p)[0], 'y': T(p)[1], 'r': round(17 * K, 1)}
    geo['buttons'] = {'a': sq(P(537, 1203)), 'bypass': sq(P(782, 1203)), 'b': sq(P(1025, 1203)), 'thd': sq(P(185, 1332)), 'scfilter': sq(P(1382, 1330))}
    geo['leds'] = {'a': [list(T(P(537, 1107)))], 'bypass': [list(T(P(780, 1107)))], 'b': [list(T(P(1025, 1107)))],
                   'thd': [list(T(P(305, 1302))), list(T(P(305, 1365)))],
                   'scfilter': [list(T(P(1258, 1363))), list(T(P(1258, 1302))), list(T(P(1258, 1240)))]}
    geo['ledR'] = round(6.5 * K, 1)
    geo['ringLedR'] = round(3.6 * K, 1)
    cv2.imwrite(f'{OUT}/rhea.jpg', bg, [cv2.IMWRITE_JPEG_QUALITY, 93])
    json.dump(geo, open(f'{WORK}/geo_rhea.json', 'w'), indent=1)
    return bg, geo


if __name__ == '__main__':
    b, g = vitalizer(); cv2.imwrite('/tmp/claude-0/pv_vit.jpg', b)
    b, g = manley()
    dbg = b.copy()
    for k in g['knobs'].values():
        cv2.circle(dbg, (int(k['x']), int(k['y'])), int(k['r']), (0, 255, 0), 1)
        a = np.radians(k['refAngle']); cv2.line(dbg, (int(k['x']), int(k['y'])), (int(k['x'] + 50 * np.sin(a)), int(k['y'] - 50 * np.cos(a))), (0, 0, 255), 1)
    for t in g['toggles'].values():
        cv2.rectangle(dbg, (int(t['x'] - t['w'] / 2), int(t['y'] - t['h'] / 2)), (int(t['x'] + t['w'] / 2), int(t['y'] + t['h'] / 2)), (0, 255, 255), 1)
    cv2.imwrite('/tmp/claude-0/pv_man.jpg', dbg[:, 180:2320])
    b, g = rhea()
    dbg = b.copy()
    for k in g['buttons'].values():
        cv2.circle(dbg, (int(k['x']), int(k['y'])), int(k['r']), (0, 0, 255), 1)
    for l in g['leds'].values():
        for (x, y) in l: cv2.circle(dbg, (int(x), int(y)), 11, (255, 0, 0), 1)
    for n in ('attack', 'release'):
        for (x, y) in g['rings'][n]: cv2.circle(dbg, (int(x), int(y)), 10, (0, 160, 0), 1)
    cv2.imwrite('/tmp/claude-0/pv_rhea.jpg', dbg)

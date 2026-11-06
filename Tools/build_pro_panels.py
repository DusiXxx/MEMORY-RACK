# Vlastni kresby panelu + profily nejpouzivanejsiho studioveho hardwaru (0.13):
#   Universal Audio 1176LN, Universal Audio Teletronix LA-2A, Empirical Labs Distressor EL8,
#   API 2500+, Warm Audio EQP-WA
# Hodnoty (rozsahy, polohy prepinacu, poradi) VYHRADNE podle manualu vyrobce - zdroj je v kazdem profilu.
# Co manual nerika (potisk stupnic, presne rozmisteni), se NEvymysli: uklada se poloha (hodiny) a je to
# v openQuestions. Rozmisteni je orientacni -> layoutVerified = False (fotka tyto kresby zatim neprecte,
# hodnoty se zadavaji mysi nebo psanim).
#
#   python3 Tools/build_pro_panels.py      # nakresli obrazky a doplni / nahradi profily v Assets/profiles.json
import json, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw
from build_new_panels import font, text_c, ticks, vu, rack_holes, save, OUT

def knob(cid, name, x, y, r, note, colour='#202020', style='skirt', minLabel='', maxLabel='', centreLabel='', verified=True, a0=-150, a1=150, scale=None):
    d = dict(id=cid, name=name, group='', type='knob', minLabel=minLabel, maxLabel=maxLabel, verified=verified, note=note,
             x=x, y=y, r=r, angleMin=a0, angleMax=a1, draw=dict(kind='vknob', style=style, colour=colour))
    if centreLabel: d['centreLabel'] = centreLabel
    if scale: d['scale'] = scale
    return d

def rotary(cid, name, x, y, r, options, note, colour='#202020', a0=-135, a1=135, verified=True):
    return dict(id=cid, name=name, group='', type='stepped', steps=len(options), options=options, verified=verified, note=note,
                x=x, y=y, r=r, angleMin=a0, angleMax=a1, draw=dict(kind='vknob', style='chicken', colour=colour))

def leds(cid, name, options, points, mapping, note, colour, verified=True, w=26, h=26):
    xs = [p[0] for p in points]; ys = [p[1] for p in points]
    return dict(id=cid, name=name, group='', type='selector', options=options, verified=verified, note=note,
                x=sum(xs) / len(xs), y=sum(ys) / len(ys), r=30,
                draw=dict(kind='vbuttons', points=points, w=w, h=h, colour=colour, map=mapping))

def button(cid, name, x, y, note, colour='#ff3b2a', group='Switches', w=60, h=44):
    return dict(id=cid, name=name, group=group, type='button', note=note, verified=True, x=x, y=y, r=30,
                draw=dict(kind='vbuttons', points=[[x, y]], w=w, h=h, colour=colour, map=[-1, 0]))

def knob_ticks(d, x, y, r, n, ink, a0=-150, a1=150, w=3, L=10):
    ticks(d, x, y, r, n, a0, a1, ink, w, L)

def rotary_labels(d, x, y, r, labels, ink, a0=-135, a1=135, size=22):
    n = len(labels)
    for i, t in enumerate(labels):
        a = math.radians(a0 + (a1 - a0) * i / max(1, n - 1))
        text_c(d, (x + math.sin(a) * (r + 36), y - math.cos(a) * (r + 30)), t, font(size), ink)

def led_row(d, pts, labels, ink, size=18, below=True):
    for (x, y), t in zip(pts, labels):
        d.ellipse((x - 9, y - 9, x + 9, y + 9), fill=(40, 10, 10), outline=(90, 90, 90), width=2)
        text_c(d, (x, y + (24 if below else -24)), t, font(size), ink)

def push(d, x, y, label, ink, w=60, h=44, below=True):
    d.rounded_rectangle((x - w / 2, y - h / 2, x + w / 2, y + h / 2), 6, fill=(16, 16, 16), outline=(110, 110, 110), width=2)
    if label: text_c(d, (x, y + (h / 2 + 18 if below else -h / 2 - 18)), label, font(22), ink)

# ---------------------------------------------------------------------------------- UA 1176LN (2U)
def ua1176ln():
    W, H = 2400, 442
    img = Image.new('RGB', (W, H), (22, 22, 24)); d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W - 1, H - 1), outline=(70, 70, 74), width=3)
    rack_holes(d, W, H, (8, 8, 8), (80, 80, 84))
    ink = (225, 225, 220)
    c = []
    for cid, name, x, note in (('input', 'INPUT', 420, 'Vstupní úroveň: určuje práh a míru komprese, doprava = víc komprese (manuál UA). Manuál neuvádí čísla stupnice – ukládá se poloha.'),
                               ('output', 'OUTPUT', 860, 'Výstupní úroveň (zisk výstupního zesilovače), nastavuje se podle měřáku +4/+8 (manuál UA). Čísla stupnice manuál neuvádí – ukládá se poloha.')):
        knob_ticks(d, x, 230, 100, 13, ink, w=4, L=14)
        text_c(d, (x, 395), name, font(40), ink)
        c.append(knob(cid, name, x, 230, 100, note, colour='#c8c8c8'))
    for cid, name, y, lab_y, note in (('attack', 'ATTACK', 150, 40, '20 µs až 800 µs; zcela doprava = nejrychlejší; zcela doleva = komprese vypnutá (manuál UA 1176LN).'),
                                      ('release', 'RELEASE', 320, 405, '50 ms až 1,1 s; zcela doprava = nejrychlejší (manuál UA 1176LN).')):
        knob_ticks(d, 1240, y, 50, 7, ink, -135, 135)
        text_c(d, (1240, lab_y), name, font(30), ink)
        c.append(knob(cid, name, 1240, y, 50, note, colour='#1a1a1a', style='small', a0=-135, a1=135,
                      minLabel='OFF' if cid == 'attack' else '', maxLabel='FAST'))
    text_c(d, (1480, 62), 'RATIO', font(30), ink)
    ry = [118, 182, 246, 310]
    for lab, y in zip(['20', '12', '8', '4'], ry):
        text_c(d, (1415, y), lab, font(30), ink); push(d, 1480, y, '', ink)
    c.append(leds('ratio', 'RATIO', ['4', '8', '12', '20'], [[1480, 310], [1480, 246], [1480, 182], [1480, 118]], [0, 1, 2, 3],
                  'Tlačítka 20:1, 12:1, 8:1, 4:1 (vlevo od měřáku). 4 a 8 = komprese, 12 a 20 = limiter (manuál UA). Kombinace tlačítek manuál nepopisuje.',
                  '#f2f2ee', w=60, h=44))
    vu(d, 1640, 90, 2030, 330, (236, 226, 196), (30, 30, 30), 'VU')
    text_c(d, (1835, 372), '1176LN  LIMITING AMPLIFIER', font(30), ink)
    text_c(d, (1835, 410), 'UNIVERSAL AUDIO', font(26), (200, 200, 200))
    text_c(d, (2190, 62), 'METER', font(30), ink)
    for lab, y in zip(['GR', '+8', '+4', 'OFF'], ry):
        text_c(d, (2268, y), lab, font(28), ink); push(d, 2190, y, '', ink)
    c.append(leds('meter', 'METER', ['GR', '+8', '+4', 'OFF'], [[2190, y] for y in ry], [0, 1, 2, 3],
                  'GR = zeslabení v dB, +8 / +4 = výstupní úroveň, OFF = přístroj vypnutý; jiné tlačítko než OFF ho zapne (manuál UA).', '#f2f2ee', w=60, h=44))
    save(img, 'ua1176ln.jpg')
    return dict(id='ua.1176ln', name='Universal Audio 1176LN', manufacturer='Universal Audio', formFactor='19" 2U', category='mixing', kind='compressor',
                w=W, h=H, image='ua1176ln.jpg', profileVersion=1, layoutVerified=False, anchor='',
                imageNote='Vlastní kresba (ne fotografie výrobce). Ovladače a rozsahy podle manuálu UA 1176LN, rozmístění orientační.',
                sources=['Universal Audio 1176LN Owner\'s Manual (c3.zzounds.com/media/1176LN_manual-….pdf)'],
                openQuestions=['Manuál neuvádí čísla na stupnicích INPUT/OUTPUT/ATTACK/RELEASE – ukládá se poloha.',
                               'Rozmístění ovladačů orientační – pošli fotku zepředu.'],
                controls=c)

# ---------------------------------------------------------------------------------- Teletronix LA-2A (3U)
def la2a():
    W, H = 2400, 663
    img = Image.new('RGB', (W, H), (178, 180, 182)); d = ImageDraw.Draw(img)
    for y in range(H):
        v = 176 + int(5 * math.sin(y * 0.8) + 3 * math.sin(y * 0.11))
        d.line((0, y, W, y), fill=(v, v + 1, v + 3))
    d.rectangle((0, 0, W - 1, H - 1), outline=(120, 120, 124), width=3)
    rack_holes(d, W, H, (60, 60, 62), (110, 110, 114))
    ink = (20, 20, 22)
    c = []
    for cid, name, x, note in (('gain', 'GAIN', 560, 'Výstupní úroveň; kompresi neovlivňuje, nastavuje se až po PEAK REDUCTION (manuál UA). Čísla stupnice manuál neuvádí – ukládá se poloha.'),
                               ('peak', 'PEAK REDUCTION', 1840, 'Práh a míra komprese, nastavuje se první (manuál UA). Čísla stupnice manuál neuvádí – ukládá se poloha.')):
        knob_ticks(d, x, 300, 140, 11, ink, w=4, L=16)
        text_c(d, (x, 520), name, font(44), ink)
        c.append(knob(cid, name, x, 300, 140, note, colour='#1c1c1c'))
    vu(d, 980, 120, 1420, 400, (246, 238, 214), (30, 30, 30), 'VU')
    text_c(d, (1200, 470), 'TELETRONIX  LEVELING AMPLIFIER  MODEL LA-2A', font(30), ink)
    # LIMIT / COMPRESS + METER (pod VU)
    text_c(d, (1010, 560), 'COMPRESS', font(24), ink); text_c(d, (1010, 640), 'LIMIT', font(24), ink)
    d.ellipse((1060, 580, 1100, 620), fill=(140, 140, 144), outline=(70, 70, 74), width=2)
    c.append(dict(id='mode', name='LIMIT / COMPRESS', group='', type='selector', options=['COMPRESS', 'LIMIT'], verified=False,
                  note='LIMIT = vyšší poměr komprese, COMPRESS = mírnější (manuál UA). Která poloha páčky je nahoře, manuál neuvádí.',
                  x=1080, y=600, r=36, draw=dict(kind='vlever', points=[[1080, 562], [1080, 638]], colour='#e8e8e8')))
    for lab, a in (('+10', -60), ('GR', 0), ('+4', 60)):
        r = math.radians(a)
        text_c(d, (1400 + math.sin(r) * 80, 610 - math.cos(r) * 64), lab, font(24), ink)
    c.append(dict(id='meter', name='METER', group='', type='selector', options=['+10', 'GAIN REDUCTION', '+4'], verified=False,
                  note='Prostřední poloha = gain reduction; krajní = výstup +4 a +10 (manuál UA). Které krajní je vlevo, manuál neuvádí.',
                  x=1400, y=610, r=36, angleMin=-60, angleMax=60, draw=dict(kind='vknob', style='chicken', colour='#202020')))
    save(img, 'la2a.jpg')
    return dict(id='ua.la2a', name='Teletronix LA-2A (Universal Audio)', manufacturer='Universal Audio', formFactor='19" 3U', category='mixing', kind='compressor',
                w=W, h=H, image='la2a.jpg', profileVersion=1, layoutVerified=False, anchor='',
                imageNote='Vlastní kresba (ne fotografie výrobce). Ovladače podle manuálu UA LA-2A, rozmístění orientační.',
                sources=['Universal Audio LA-2A Manual (barryrudolph.com/recall/manuals/uaaudiola2.pdf)'],
                openQuestions=['Manuál neuvádí čísla stupnic GAIN a PEAK REDUCTION – ukládá se poloha.',
                               'Pořadí krajních poloh přepínače METER (+4 / +10) a poloha páčky LIMIT/COMPRESS neověřeny.',
                               'R37 (pre-emphasis) a R3 jsou trimry, ne ovladače na panelu – neukládají se.',
                               'Výška 3U podle běžného provedení – ověřit.'],
                controls=c)

# ---------------------------------------------------------------------------------- Empirical Labs Distressor EL8 (1U)
def distressor():
    W, H = 2400, 221
    img = Image.new('RGB', (W, H), (34, 36, 40)); d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W - 1, H - 1), outline=(80, 80, 86), width=3)
    rack_holes(d, W, H, (8, 8, 8), (90, 90, 94))
    ink = (230, 230, 226)
    c = []
    # knoby 0..10 (manual: cislovane, jdou az do 10 1/2)
    nums = [[i / 10.5, str(i)] for i in range(11)]
    for cid, name, x, note in (('input', 'INPUT', 300, 'Vstup: víc = víc komprese; číslovaný knob 0 až 10½ (manuál Empirical Labs).'),
                               ('attack', 'ATTACK', 620, 'Náběh 50 µs – 30 ms; nižší číslo = rychlejší, k 10 = pomalejší (manuál).'),
                               ('release', 'RELEASE', 940, 'Návrat 0,05 – 3,5 s (v režimu 10:1 Opto až cca 20 s); nižší číslo = rychlejší (manuál).'),
                               ('output', 'OUTPUT', 1260, 'Výstup, kalibrovaný: 8 = +4 (páskový level), 6,5 = −10 zařízení (manuál).')):
        knob_ticks(d, x, 106, 62, 11, ink, -150, 136, 3, 9)
        text_c(d, (x, 196), name, font(26), ink)
        c.append(knob(cid, name, x, 106, 62, note, colour='#2b2b2b', style='small', scale=nums, minLabel='0'))
    # RATIO: 8 LED + tlacitko
    rlab = ['1:1', '2:1', '3:1', '4:1', '6:1', '10:1', '20:1', 'NUKE']
    rpts = [[1440 + i * 44, 70] for i in range(8)]
    led_row(d, rpts, rlab, ink, 15)
    d.rounded_rectangle((1566, 146, 1622, 180), 6, fill=(16, 16, 16), outline=(110, 110, 110), width=2)
    text_c(d, (1594, 202), 'RATIO', font(20), ink)
    c.append(leds('ratio', 'RATIO', rlab, rpts, list(range(8)),
                  'Tlačítko cyklicky: 1:1 (bez komprese, jen „hřejivé“ obvody), 2:1, 3:1, 4:1, 6:1 (žlutá LED), 10:1 Opto, 20:1, Nuke; po Nuke zpět (manuál). '
                  'Kolik LED má panel, manuál neuvádí – kresba předpokládá jednu na polohu.', '#ffcc33', verified=False, w=20, h=20))
    # DETECTOR: 3 LED (HP zelena, Band Emphasis zluta, Stereo Link cervena)
    dpts = [[1870, 70], [1930, 70], [1990, 70]]
    led_row(d, dpts, ['HP', 'BAND', 'LINK'], ink, 15)
    d.rounded_rectangle((1902, 146, 1958, 180), 6, fill=(16, 16, 16), outline=(110, 110, 110), width=2)
    text_c(d, (1930, 202), 'DETECTOR', font(20), ink)
    for (cid, nm, p, col, note) in (('det_hp', 'DET HP', dpts[0], '#3dff6e', 'Detektor: horní propust v postranním řetězci (zelená LED).'),
                                    ('det_band', 'DET BAND', dpts[1], '#ffd21a', 'Detektor: Band Emphasis 6 kHz (žlutá LED).'),
                                    ('link', 'LINK', dpts[2], '#ff3b2a', 'Stereo Link (červená LED).')):
        c.append(button(cid, nm, p[0], p[1], note + ' Tlačítko DETECTOR cyklicky prochází 8 kombinací (manuál).', col, group='Detector', w=20, h=20))
    # AUDIO: HP + Dist 2 / Dist 3
    apts = [[2110, 70], [2170, 70], [2230, 70]]
    led_row(d, apts, ['HP', 'DIST 2', 'DIST 3'], ink, 15)
    d.rounded_rectangle((2142, 146, 2198, 180), 6, fill=(16, 16, 16), outline=(110, 110, 110), width=2)
    text_c(d, (2170, 202), 'AUDIO', font(20), ink)
    c.append(button('audio_hp', 'AUDIO HP', apts[0][0], apts[0][1], 'Audio HP: Besselova horní propust cca 80 Hz (zelená LED). Tlačítko AUDIO cyklicky: Norm, HP, Dist 2, Dist 2 & HP, Dist 3, Dist 3 & HP (manuál).',
                    '#3dff6e', group='Audio', w=20, h=20))
    c.append(leds('dist', 'DIST', ['NORM', 'DIST 2', 'DIST 3'], [apts[1], apts[2]], [-1, 0, 1],
                  'Zkreslení: Dist 2 zdůrazní 2. harmonickou (žlutá LED), Dist 3 třetí (červená LED) (manuál).', '#ffaa22', w=20, h=20))
    d.rounded_rectangle((1750, 146, 1800, 180), 6, fill=(16, 16, 16), outline=(110, 110, 110), width=2)
    text_c(d, (1775, 202), 'BYPASS', font(18), ink)
    c.append(button('bypass', 'BYPASS', 1775, 163, 'Bypass, červená LED svítí v bypassu (manuál).', '#ff3b2a', w=50, h=34))
    text_c(d, (1700, 30), 'EMPIRICAL LABS  DISTRESSOR', font(22), (220, 180, 120))
    save(img, 'distressor.jpg')
    return dict(id='empiricallabs.distressor', name='Empirical Labs Distressor EL8', manufacturer='Empirical Labs', formFactor='19" 1U', category='mixing', kind='compressor',
                w=W, h=H, image='distressor.jpg', profileVersion=1, layoutVerified=False, anchor='',
                imageNote='Vlastní kresba (ne fotografie výrobce). Ovladače a hodnoty podle manuálu Distressor, rozmístění orientační.',
                sources=['Empirical Labs Distressor EL8 Manual (barryrudolph.com/recall/manuals/distressor1.pdf)'],
                openQuestions=['Úhel stupnice 0–10½ na knobech neověřen (čísla ano, poloha na panelu podle kresby).',
                               'Počet a pořadí LED u RATIO manuál neuvádí.',
                               'Rozmístění ovladačů orientační – pošli fotku zepředu.'],
                controls=c)

# ---------------------------------------------------------------------------------- API 2500+ (1U)
def api2500():
    W, H = 2400, 221
    img = Image.new('RGB', (W, H), (60, 64, 72)); d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W - 1, H - 1), outline=(100, 104, 112), width=3)
    rack_holes(d, W, H, (12, 12, 12), (110, 110, 116))
    ink = (235, 235, 235)
    c = []
    vu(d, 100, 46, 250, 140, (240, 232, 205), (30, 30, 30), 'L')
    vu(d, 286, 46, 436, 140, (240, 232, 205), (30, 30, 30), 'R')
    y = 104; r = 34
    knob_ticks(d, 540, y, r, 9, ink, w=2, L=7)
    text_c(d, (540, 200), 'THRESH', font(20), ink)
    c.append(knob('thresh', 'THRESH', 540, y, r, 'Práh plynule +20 dBu až −20 dBu; červená LED = signál nad prahem (manuál API 2500+). Směr stupnice manuál neuvádí – ukládá se poloha.', colour='#1a1a1a', style='small'))
    for cid, nm, x, opts, note in (('ratio', 'RATIO', 720, ['1.5:1', '2:1', '3:1', '4:1', '6:1', '10:1', '∞:1'], '7 poloh: 1.5:1, 2:1, 3:1, 4:1, 6:1, 10:1, ∞:1 (manuál).'),
                                   ('attack', 'ATTACK', 910, ['0.03 ms', '0.1 ms', '0.3 ms', '1 ms', '3 ms', '10 ms', '30 ms'], '7 poloh: 0,03 / 0,1 / 0,3 / 1 / 3 / 10 / 30 ms (manuál).'),
                                   ('release', 'RELEASE', 1100, ['0.05 s', '0.1 s', '0.2 s', '0.5 s', '1 s', '2 s', 'VAR'], '6 pevných poloh 0,05 / 0,1 / 0,2 / 0,5 / 1 / 2 s + VAR (plynule 50 ms – 3 s knobem VAR) (manuál).')):
        rotary_labels(d, x, y, r, [o.replace(':1', '').replace(' ms', '').replace(' s', '') for o in opts], ink, size=15)
        text_c(d, (x, 200), nm, font(20), ink)
        c.append(rotary(cid, nm, x, y, r, opts, note))
    knob_ticks(d, 1240, y, 26, 7, ink, w=2, L=6)
    text_c(d, (1240, 200), 'VAR', font(18), ink)
    c.append(knob('release_var', 'RELEASE VAR', 1240, y, 26, 'Plynulý release 50 ms – 3 s, platí jen v poloze RELEASE = VAR (manuál). Čísla stupnice neuvedena – ukládá se poloha.', colour='#1a1a1a', style='small'))
    def cyc(cid, nm, x, opts, colour, note, mapping=None, lit=None, span=None):
        lit = lit if lit is not None else opts
        n = len(lit)
        span = span if span is not None else 30 * (n - 1)
        pts = [[x - span / 2 + span * i / max(1, n - 1), 64] for i in range(n)]
        for i, (p, o) in enumerate(zip(pts, lit)):
            d.ellipse((p[0] - 7, p[1] - 7, p[0] + 7, p[1] + 7), fill=(40, 12, 12), outline=(90, 90, 90), width=2)
            text_c(d, (p[0], 36 if i % 2 == 0 or n < 4 else 22), o, font(14), ink)
        d.rounded_rectangle((x - 24, 116, x + 24, 150), 6, fill=(20, 20, 20), outline=(120, 120, 120), width=2)
        text_c(d, (x, 200), nm, font(18), ink)
        c.append(leds(cid, nm, opts, pts, mapping if mapping else list(range(n)), note, colour, w=16, h=16))
    cyc('knee', 'KNEE', 1360, ['HARD', 'MED', 'SOFT'], '#ffd21a', 'HARD (červená), MED (žlutá), SOFT (zelená) (manuál).', span=80)
    cyc('thrust', 'THRUST', 1490, ['NORM', 'MED', 'LOUD'], '#ffd21a', 'NORM (zelená, bez filtru), MED (žlutá), LOUD (červená) (manuál).', span=80)
    cyc('type', 'TYPE', 1600, ['NEW', 'OLD'], '#ffd21a', 'NEW = feed-forward (žlutá), OLD = feed-back (zelená) (manuál).', span=50)
    cyc('link', 'L/R LINK', 1740, ['IND', '50', '60', '70', '80', '90', '100'], '#3dff6e', 'IND (nezávisle), 50 / 60 / 70 / 80 / 90 / 100 % (manuál).', span=150)
    cyc('shape', 'SHAPE', 1880, ['FAST', 'SLOW', 'BOTH'], '#3dff6e', 'FAST (peak), SLOW (RMS), BOTH = svítí obě zelené LED (manuál).', mapping=[0, 1, 99], lit=['FAST', 'SLOW'], span=50)
    d.rounded_rectangle((1956, 116, 2004, 150), 6, fill=(20, 20, 20), outline=(120, 120, 120), width=2); text_c(d, (1980, 200), 'MANUAL', font(16), ink)
    c.append(button('manual', 'MANUAL', 1980, 133, 'Ruční make-up zisk (jinak automatický) (manuál).', '#3dff6e', group='Output', w=48, h=34))
    knob_ticks(d, 2070, y, 28, 7, ink, w=2, L=6); text_c(d, (2070, 200), 'GAIN', font(18), ink)
    c.append(knob('gain', 'GAIN', 2070, y, 28, 'Ruční make-up 0 až +24 dB, jen když je zapnuté MANUAL (manuál). Mezilehlé značky neuvedeny – ukládá se poloha.',
                  colour='#1a1a1a', style='small', minLabel='0', maxLabel='+24'))
    cyc('blend', 'BLEND', 2160, ['100%', 'X', '||'], '#3dff6e', '100 % komprese (bez LED), X = crossfade, || = paralelně (zelené LED) (manuál).', mapping=[-1, 0, 1], lit=['X', '||'], span=30)
    knob_ticks(d, 2215, y, 26, 5, ink, w=2, L=6); text_c(d, (2215, 200), 'MIX', font(16), ink)
    c.append(knob('mix', 'MIX', 2215, y, 26, 'Poměr mixu, aretace v 50 %; platí jen v BLEND X nebo || (manuál).', colour='#1a1a1a', style='small', centreLabel='50%'))
    for cid, nm, yy, col, note in (('in', 'IN', 133, '#3dff6e', 'Tiché zapnutí/vypnutí komprese (zelená LED) (manuál).'),
                                   ('byp', 'BYP', 64, '#ff3b2a', 'Tvrdý bypass relé (červená LED); sepne i při vypnutí (manuál).')):
        d.rounded_rectangle((2290, yy - 17, 2320, yy + 17), 6, fill=(20, 20, 20), outline=(120, 120, 120), width=2)
        text_c(d, (2268, yy), nm, font(14), ink)
        c.append(button(cid, nm, 2305, yy, note, col, group='Output', w=30, h=34))
    mpts = [[470, 66], [470, 94], [470, 122]]
    for p, o in zip(mpts, ['GR', 'OUT', 'IN']):
        d.ellipse((p[0] - 6, p[1] - 6, p[0] + 6, p[1] + 6), fill=(40, 12, 12), outline=(90, 90, 90), width=2)
    text_c(d, (470, 36), 'METER', font(13), ink)
    c.append(leds('meter', 'METER', ['GR', 'OUT', 'IN'], mpts, [0, 1, 2], 'Měřáky: GR (červená), OUT (žlutá), IN (zelená); 0 VU = +4 dBu (manuál).', '#ff8833', w=14, h=14))
    text_c(d, (268, 186), 'API 2500+', font(22), (230, 210, 150))
    save(img, 'api2500plus.jpg')
    return dict(id='api.2500plus', name='API 2500+', manufacturer='API', formFactor='19" 1U', category='mastering', kind='compressor',
                w=W, h=H, image='api2500plus.jpg', profileVersion=1, layoutVerified=False, anchor='',
                imageNote='Vlastní kresba (ne fotografie výrobce). Ovladače a polohy podle manuálu API 2500+, rozmístění orientační.',
                sources=['API 2500+ User Manual (apiaudio.com/docs/manuals/2500+_user_23-01-09.pdf)', 'apiaudio.com – rozměry 19" × 1,75"'],
                openQuestions=['Manuál nepopisuje rozmístění na panelu – kresba je orientační, pošli fotku zepředu.',
                               'Starší API 2500 (bez +) nemá BLEND/MIX ani některé polohy – pro něj zatím nepoužívat beze změn.'],
                controls=c)

# ---------------------------------------------------------------------------------- Warm Audio EQP-WA (2U)
def eqpwa():
    W, H = 2400, 442
    img = Image.new('RGB', (W, H), (52, 66, 92)); d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W - 1, H - 1), outline=(90, 100, 120), width=3)
    rack_holes(d, W, H, (10, 10, 12), (100, 104, 116))
    ink = (236, 236, 230)
    c = []
    y = 190
    lf = ['20', '30', '60', '100', '200', '400', '800']
    rotary_labels(d, 260, y, 70, lf, ink, size=22)
    text_c(d, (260, 330), 'LOW FREQ (Hz)', font(26), ink)
    c.append(rotary('low_freq', 'LOW FREQ', 260, y, 70, [f + ' Hz' for f in lf], 'Kmitočet dolního pásma (boost i cut): 20, 30, 60, 100, 200, 400, 800 Hz (manuál Warm Audio).'))
    for cid, nm, x, note, mn, mx in (('low_boost', 'BOOST', 540, 'Zdvih basů 0 (jednotkový zisk) až +12 dB (manuál).', '0', '+12 dB'),
                                     ('low_cut', 'ATTEN', 800, 'Útlum basů 0 až −18 dB (manuál).', '0', '-18 dB'),
                                     ('bandwidth', 'BANDWIDTH', 1080, 'Šířka výškového zdvihu: 0 = úzká (sharp), 10 = široká (broad) (manuál).', 'SHARP', 'BROAD'),
                                     ('high_boost', 'BOOST', 1360, 'Zdvih výšek 0 až +18 dB (manuál).', '0', '+18 dB'),
                                     ('high_cut', 'ATTEN', 1900, 'Útlum výšek 0 až −14 dB (manuál).', '0', '-14 dB')):
        knob_ticks(d, x, y, 80, 11, ink, w=3, L=10)
        text_c(d, (x, 330), nm, font(30), ink)
        scale = [[i / 10.0, str(i)] for i in range(11)] if cid == 'bandwidth' else None
        c.append(knob(cid, nm, x, y, 80, note + ('' if cid == 'bandwidth' else ' Mezilehlé značky stupnice manuál neuvádí – ukládá se poloha.'),
                      colour='#1d1d1d', minLabel=mn if cid != 'bandwidth' else '0', maxLabel=mx if cid != 'bandwidth' else '10', scale=scale))
    hf = ['3', '4', '5', '8', '10', '12', '16']
    rotary_labels(d, 1630, y, 70, hf, ink, size=22)
    text_c(d, (1630, 330), 'HIGH FREQ (kHz)', font(26), ink)
    c.append(rotary('high_freq', 'HIGH FREQ', 1630, y, 70, [f + ' kHz' for f in hf], 'Kmitočet výškového zdvihu: 3, 4, 5, 8, 10, 12, 16 kHz (manuál).'))
    af = ['3', '4', '5', '10', '20']
    rotary_labels(d, 2160, y, 70, af, ink, size=22)
    text_c(d, (2160, 330), 'ATTEN SEL (kHz)', font(26), ink)
    c.append(rotary('high_cut_freq', 'ATTEN SEL', 2160, y, 70, [f + ' kHz' for f in af], 'Kmitočet výškového útlumu: 3, 4, 5, 10, 20 kHz (manuál Warm Audio).'))
    d.ellipse((150, 375, 190, 415), fill=(150, 150, 154), outline=(80, 80, 84), width=2); text_c(d, (240, 395), 'BYPASS', font(22), ink)
    c.append(dict(id='bypass', name='BYPASS', group='', type='button', note='Přepínač bypass vlevo dole (není „true bypass“) (manuál).', verified=True,
                  x=170, y=395, r=26, draw=dict(kind='vbuttons', points=[[170, 395]], w=40, h=40, colour='#ff3b2a', map=[-1, 0])))
    text_c(d, (1200, 400), 'WARM AUDIO  EQP-WA  PROGRAM EQUALIZER', font(26), (230, 200, 140))
    save(img, 'eqpwa.jpg')
    return dict(id='warmaudio.eqpwa', name='Warm Audio EQP-WA', manufacturer='Warm Audio', formFactor='19" 2U', category='mixing', kind='eq',
                w=W, h=H, image='eqpwa.jpg', profileVersion=1, layoutVerified=False, anchor='',
                imageNote='Vlastní kresba (ne fotografie výrobce). Ovladače a hodnoty podle manuálu EQP-WA, rozmístění orientační.',
                sources=['Warm Audio EQP-WA User Manual (gear4music download_148361_1.pdf)', 'scan.co.uk – rozměry 19" × 3,5" (2U)'],
                openQuestions=['Manuál uvádí rozsahy v dB, ne čísla potisku 0–10 u BOOST/ATTEN – ukládá se poloha.',
                               'Rozmístění ovladačů orientační (manuál ho popisuje jen obrázkem) – pošli fotku zepředu.',
                               'Originální Pultec EQP-1A má jiné kmitočty (LOW 20/30/60/100, ATTEN SEL 5/10/20) – tento profil platí pro EQP-WA.'],
                controls=c)

def pro_profiles():
    return [ua1176ln(), la2a(), distressor(), api2500(), eqpwa()]

if __name__ == '__main__':
    path = os.path.join(OUT, 'profiles.json')
    data = json.load(open(path, encoding='utf-8'))
    new = pro_profiles()
    ids = {p['id'] for p in new}
    data['profiles'] = [p for p in data['profiles'] if p['id'] not in ids] + new
    json.dump(data, open(path, 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
    for p in new: print(p['id'], len(p['controls']))

# Vlastni kresby panelu (zadne fotografie vyrobcu) + profily pro:
#   Warm Audio WA76, Warm Audio WA-2A, Samson S-patch Plus, Neutrik NYS-SPP-L1
# Popisky podle manualu / specifikaci vyrobce (zdroje v profilech). Rozmisteni ovladacu je orientacni
# (podle fotek z obchodu) -> layoutVerified = False a otevrene otazky v profilu.
# Ovladace (knoby, packy, tlacitka) se na obrazek NEkresli - kresli je plugin podle ulozene hodnoty.
import math, os
from PIL import Image, ImageDraw, ImageFont

OUT = os.environ.get('RM_OUT', '/home/claude/RackMemory/Assets')
F = '/usr/share/fonts/truetype/crosextra/Carlito-Bold.ttf'
FR = '/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf'
def font(sz, bold=True): return ImageFont.truetype(F if bold else FR, sz)

def text_c(d, xy, s, f, fill):
    b = d.textbbox((0, 0), s, font=f)
    d.text((xy[0] - (b[2] - b[0]) / 2 - b[0], xy[1] - (b[3] - b[1]) / 2 - b[1]), s, font=f, fill=fill)

def rack_holes(d, W, H, col_hole, col_rim):
    for x in (46, W - 46):
        for y in ((H * 0.22, H * 0.78) if H > 300 else (H * 0.5,)):
            d.rounded_rectangle((x - 22, y - 12, x + 22, y + 12), 12, fill=col_hole, outline=col_rim, width=2)

def ticks(d, cx, cy, r, n, a0, a1, col, w=4, L=16):
    for i in range(n):
        a = math.radians(a0 + (a1 - a0) * i / (n - 1))
        x0, y0 = cx + math.sin(a) * (r + 10), cy - math.cos(a) * (r + 10)
        x1, y1 = cx + math.sin(a) * (r + 10 + L), cy - math.cos(a) * (r + 10 + L)
        d.line((x0, y0, x1, y1), fill=col, width=w)

def vu(d, x0, y0, x1, y1, face, ink, label):
    d.rounded_rectangle((x0 - 14, y0 - 14, x1 + 14, y1 + 14), 16, fill=(20, 20, 20))
    d.rectangle((x0, y0, x1, y1), fill=face)
    cx, cy, r = (x0 + x1) / 2, y1 + (y1 - y0) * 0.35, (y1 - y0) * 0.95
    for i in range(11):
        a = math.radians(-48 + 96 * i / 10)
        d.line((cx + math.sin(a) * r * 0.86, cy - math.cos(a) * r * 0.86, cx + math.sin(a) * r * 0.95, cy - math.cos(a) * r * 0.95), fill=ink, width=4)
    d.arc((cx - r * 0.9, cy - r * 0.9, cx + r * 0.9, cy + r * 0.9), 270 - 48, 270 + 48, fill=ink, width=3)
    text_c(d, ((x0 + x1) / 2, y1 - (y1 - y0) * 0.22), label, font(30), ink)
    # rucicka v klidu
    a = math.radians(-46)
    rs = (cy - y1) / math.cos(a) + 4          # rucicka jen uvnitr okenka
    d.line((cx + math.sin(a) * rs, cy - math.cos(a) * rs, cx + math.sin(a) * r * 0.92, cy - math.cos(a) * r * 0.92), fill=(30, 30, 30), width=4)

def save(img, name):
    img.convert('RGB').save(f'{OUT}/{name}', quality=92)

# ---------------------------------------------------------------------------------- WA76 (2U, cerny)
def wa76():
    W, H = 2400, 442
    img = Image.new('RGB', (W, H), (24, 24, 26)); d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W - 1, H - 1), outline=(60, 60, 64), width=3)
    rack_holes(d, W, H, (8, 8, 8), (80, 80, 84))
    white = (232, 232, 228)
    c = []
    for cid, name, x in (('input', 'INPUT', 420), ('output', 'OUTPUT', 860)):
        ticks(d, x, 230, 100, 13, -150, 150, white)
        text_c(d, (x, 395), name, font(40), white)
        c.append(dict(id=cid, name=name, group='', type='knob', minLabel='', maxLabel='', verified=True,
                      note='Stupnice potisku zatím neověřená – hodnota se ukládá jako poloha (hodiny).',
                      x=x, y=230, r=100, angleMin=-150, angleMax=150,
                      draw=dict(kind='vknob', style='skirt', colour='#d8d8d8')))
    for cid, name, y, lab_y, note in (('attack', 'ATTACK', 150, 72, 'Rozsah 20 µs – 800 µs (manuál WA76). Směr stupnice neověřen.'),
                                      ('release', 'RELEASE', 320, 405, 'Rozsah 50 ms – 1100 ms (manuál WA76).')):
        ticks(d, 1240, y, 50, 7, -135, 135, white, 3, 10)
        text_c(d, (1240, lab_y), name, font(30), white)
        c.append(dict(id=cid, name=name, group='', type='knob', minLabel='', maxLabel='', verified=True, note=note,
                      x=1240, y=y, r=50, angleMin=-135, angleMax=135, draw=dict(kind='vknob', style='small', colour='#1a1a1a')))
    text_c(d, (1480, 62), 'RATIO', font(30), white)
    ry = [118, 182, 246, 310]
    for lab, y in zip(['20', '12', '8', '4'], ry):
        text_c(d, (1415, y), lab, font(30), white)
        d.rounded_rectangle((1450, y - 22, 1510, y + 22), 6, fill=(12, 12, 12), outline=(90, 90, 90), width=2)
    c.append(dict(id='ratio', name='RATIO', group='', type='selector', options=['4', '8', '12', '20', 'ALL'],
                  note='Tlačítka 4, 8, 12, 20; „All-buttons in“ = všechna stisknutá (manuál). Kombinace dvou tlačítek se neukládá.',
                  verified=True, x=1480, y=214, r=34,
                  draw=dict(kind='vbuttons', points=[[1480, 310], [1480, 246], [1480, 182], [1480, 118]], w=60, h=44, colour='#f2f2ee',
                            map=[0, 1, 2, 3, 99])))
    vu(d, 1640, 90, 2030, 330, (238, 168, 64), (40, 25, 10), 'VU')
    text_c(d, (1835, 372), 'WA76 LIMITING AMPLIFIER', font(30), white)
    text_c(d, (1835, 410), 'WARM AUDIO', font(26), (200, 120, 60))
    text_c(d, (2190, 62), 'METER', font(30), white)
    for lab, y in zip(['GR', '+8', '+4', 'OFF'], ry):
        text_c(d, (2268, y), lab, font(28), white)
        d.rounded_rectangle((2160, y - 22, 2220, y + 22), 6, fill=(12, 12, 12), outline=(90, 90, 90), width=2)
    c.append(dict(id='meter', name='METER', group='', type='selector', options=['GR', '+8', '+4', 'OFF'],
                  note='Měřák: GR / +8 / +4; OFF vypíná celý přístroj (manuál).', verified=True, x=2190, y=214, r=34,
                  draw=dict(kind='vbuttons', points=[[2190, y] for y in ry], w=60, h=44, colour='#f2f2ee', map=[0, 1, 2, 3])))
    save(img, 'wa76.jpg')
    return dict(id='warmaudio.wa76', name='Warm Audio WA76', manufacturer='Warm Audio', formFactor='19" 2U', category='mixing', kind='compressor',
                w=W, h=H, image='wa76.jpg', profileVersion=1, layoutVerified=False, anchor='',
                imageNote='Vlastní kresba (ne fotografie výrobce). Popisky podle manuálu WA76, rozmístění podle fotek z obchodu.',
                sources=['Warm Audio WA76 User Manual (gear4music.com, download_148406.pdf)', 'Fotka produktu gear4music.cz (rozmístění)'],
                openQuestions=['Čísla stupnic INPUT/OUTPUT/ATTACK/RELEASE nejsou ověřená – ukládá se poloha.',
                               'Přesné rozmístění ovladačů ověřit na skutečném panelu.',
                               'Kombinace dvou tlačítek RATIO (mimo ALL) se neukládá.'],
                controls=c)

# ---------------------------------------------------------------------------------- WA-2A (2U, stribrny)
def wa2a():
    W, H = 2400, 442
    img = Image.new('RGB', (W, H), (196, 198, 200)); d = ImageDraw.Draw(img)
    for y in range(H):   # kartacovany hlinik
        v = 196 + int(6 * math.sin(y * 0.9) + 4 * math.sin(y * 0.13))
        d.line((0, y, W, y), fill=(v, v + 1, v + 3))
    d.rectangle((0, 0, W - 1, H - 1), outline=(140, 142, 146), width=3)
    rack_holes(d, W, H, (60, 60, 62), (120, 120, 124))
    ink = (25, 25, 28)
    text_c(d, (200, 70), 'WARM', font(56), (200, 60, 30)); text_c(d, (200, 112), 'AUDIO', font(26), (200, 60, 30))
    # vypinac (neni ukladany ovladac)
    d.rounded_rectangle((180, 250, 240, 330), 10, fill=(150, 150, 154), outline=(90, 90, 94), width=2)
    d.ellipse((196, 236, 224, 264), fill=(30, 30, 30)); text_c(d, (210, 370), 'POWER', font(28), ink)
    c = []
    for cid, name, x, note in (('gain', 'GAIN', 680, 'Výstupní (make-up) zisk, cca 40 dB (manuál).'),
                               ('peak', 'PEAK REDUCTION', 1720, 'Míra komprese (práh). Zcela vlevo = měkký bypass (manuál).')):
        ticks(d, x, 220, 105, 11, -150, 150, ink)
        text_c(d, (x, 392), name, font(38), ink)
        c.append(dict(id=cid, name=name, group='', type='knob', minLabel='', maxLabel='', verified=True,
                      note=note + ' Čísla stupnice neověřená – ukládá se poloha.',
                      x=x, y=220, r=105, angleMin=-150, angleMax=150, draw=dict(kind='vknob', style='skirt', colour='#2a2a2a')))
    vu(d, 1000, 80, 1400, 330, (246, 240, 224), (30, 30, 30), 'VU')
    text_c(d, (1200, 380), 'WA-2A  TUBE OPTO COMPRESSOR', font(30), ink)
    # LIMIT / COMPRESS
    text_c(d, (2050, 62), 'COMPRESS', font(26), ink); text_c(d, (2050, 205), 'LIMIT', font(26), ink)
    d.ellipse((2030, 114, 2070, 154), fill=(150, 150, 154), outline=(80, 80, 84), width=2)
    c.append(dict(id='mode', name='LIMIT / COMPRESS', group='', type='selector', options=['COMPRESS', 'LIMIT'], verified=False,
                  note='Dvoupolohový přepínač (manuál). Která poloha je nahoře, není ověřeno.',
                  x=2050, y=134, r=40, draw=dict(kind='vlever', points=[[2050, 94], [2050, 176]], colour='#e8e8e8')))
    # METER
    text_c(d, (1905, 362), 'METER', font(26), ink)
    for lab, a in (('GR', -60), ('+10', 0), ('+4', 60)):
        r = math.radians(a)
        text_c(d, (2050 + math.sin(r) * 80, 362 - math.cos(r) * 66), lab, font(24), ink)
    c.append(dict(id='meter', name='METER', group='', type='selector', options=['GAIN REDUCTION', '+10', '+4'], verified=True,
                  note='Měřák: gain reduction / výstup +10 / výstup +4 (manuál).',
                  x=2050, y=362, r=40, angleMin=-60, angleMax=60, draw=dict(kind='vknob', style='chicken', colour='#202020')))
    save(img, 'wa2a.jpg')
    return dict(id='warmaudio.wa2a', name='Warm Audio WA-2A', manufacturer='Warm Audio', formFactor='19" 2U', category='mixing', kind='compressor',
                w=W, h=H, image='wa2a.jpg', profileVersion=1, layoutVerified=False, anchor='',
                imageNote='Vlastní kresba (ne fotografie výrobce). Popisky podle manuálu WA-2A, rozmístění orientační.',
                sources=['Warm Audio WA-2A User Manual (bhphotovideo.com lit_files/755190.pdf)', 'Fotka produktu thomann.cz (rozmístění)'],
                openQuestions=['Rozmístění ovladačů je odhad podle malé fotky – ověřit na přístroji (pošli fotku zepředu).',
                               'Poloha přepínače LIMIT/COMPRESS (nahoře/dole) neověřena.',
                               'Pre-emphasis: manuál ji uvádí mezi předními ovladači, ale fotka ji neukazuje – zatím není v profilu.',
                               'Čísla stupnic GAIN a PEAK REDUCTION neověřená – ukládá se poloha.'],
                controls=c)

# ---------------------------------------------------------------------------------- patchbaye (1U)
def samson():
    W, H = 2400, 221
    img = Image.new('RGB', (W, H), (38, 78, 160)); d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W - 1, H - 1), outline=(25, 50, 110), width=3)
    rack_holes(d, W, H, (20, 30, 60), (90, 120, 190))
    white = (240, 244, 255)
    d.text((88, 50), 'S-patch', font=font(42), fill=white); d.text((88, 96), 'plus', font=font(26), fill=white)
    d.text((88, 132), '48 POINT', font=font(18), fill=white); d.text((88, 152), 'BALANCED', font=font(18), fill=white)
    d.text((88, 172), 'PATCHBAY', font=font(18), fill=white)
    text_c(d, (2262, 110), 'SAMSON', font(28), white)
    x0, x1, n = 270, 2190, 24
    dx = (x1 - x0) / (n - 1)
    topY, botY, modeY = 58, 166, 112
    for k in range(n):
        x = x0 + dx * k
        d.rounded_rectangle((x - 36, 16, x + 36, 205), 6, fill=(30, 66, 142))
        for y, num in ((topY, k + 1), (botY, k + 25)):
            d.ellipse((x - 25, y - 25, x + 25, y + 25), fill=(170, 176, 190)); d.ellipse((x - 15, y - 15, x + 15, y + 15), fill=(10, 10, 12))
            d.text((x + 24, y - 34), str(num), font=font(17), fill=white)
        d.rounded_rectangle((x - 14, modeY - 10, x + 14, modeY + 10), 3, fill=(235, 238, 245))
    save(img, 'spatchplus.jpg')
    return dict(id='samson.spatchplus', name='Samson S-patch Plus', manufacturer='Samson', formFactor='patchbay', category='', kind='patchbay',
                w=W, h=H, image='spatchplus.jpg', profileVersion=1, layoutVerified=False, anchor='',
                imageNote='Vlastní kresba (modro-bílý vzhled), ne fotografie výrobce.',
                sources=['Samson S-patch Plus User Manual (goknight.com/content/documentation)', 'Samson S-patch Plus Spec Sheet'],
                openQuestions=['Přesné rozteče jacků a poloha přepínačů jsou orientační.'],
                patchbay=dict(channels=24, topY=topY, bottomY=botY, x0=x0, dx=dx, jackR=25, bottomContinues=True,
                              modes=['N', 'HN', 'T'], modeNames=['Normalled', 'Half-normalled', 'Thru'],
                              modeHow='Přepínač každého kanálu na předním panelu (N / HN / T).',
                              topLabel='horní řada 1–24 (zezadu: zdroj – výstupy zařízení)',
                              bottomLabel='dolní řada 25–48 (zezadu: cíl – vstupy zařízení)',
                              note='Normalled/half-normalled: horní zadní jack napájí dolní zadní jack (manuál Samson).'),
                controls=[])

def neutrik():
    W, H = 2400, 221
    img = Image.new('RGB', (W, H), (22, 22, 24)); d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W - 1, H - 1), outline=(70, 70, 74), width=3)
    rack_holes(d, W, H, (6, 6, 6), (90, 90, 94))
    white = (225, 225, 225)
    d.text((86, 92), 'NEUTRIK', font=font(30), fill=white)
    d.text((86, 128), 'NYS-SPP-L1', font=font(20), fill=(170, 170, 170))
    x0, x1, n = 300, 2230, 24
    dx = (x1 - x0) / (n - 1)
    topY, botY = 74, 150
    for y0, y1 in ((10, 38), (183, 211)):
        d.rectangle((x0 - 40, y0, x1 + 40, y1), fill=(222, 222, 214))     # popisove prouzky
    for k in range(n):
        x = x0 + dx * k
        for y, grey in ((topY, False), (botY, True)):
            d.ellipse((x - 25, y - 25, x + 25, y + 25), fill=(140, 140, 146) if grey else (60, 60, 64))
            d.ellipse((x - 15, y - 15, x + 15, y + 15), fill=(8, 8, 8))
        text_c(d, (x, 112), str(k + 1), font(18), (180, 180, 180))
    save(img, 'nysspp.jpg')
    return dict(id='neutrik.nysspp.l1', name='Neutrik NYS-SPP-L1', manufacturer='Neutrik (Rean)', formFactor='patchbay', category='', kind='patchbay',
                w=W, h=H, image='nysspp.jpg', profileVersion=1, layoutVerified=False, anchor='',
                imageNote='Vlastní kresba (černý panel, popisové proužky), ne fotografie výrobce.',
                sources=['NEUTRIK NYS-SPP-L1 – popis produktu (markertek.com): 48 bodů, 2 řady, 24 desek, half-normal ve výrobě, otočením desky se mění'],
                openQuestions=['Režim po otočení desky není v dostupném popisu jednoznačně uveden – ověřit v návodu Neutrik.',
                               'Která řada je „šedá“ (normalizační) – kresba předpokládá dolní.'],
                patchbay=dict(channels=24, topY=topY, bottomY=botY, x0=x0, dx=dx, jackR=25, bottomContinues=False,
                              modes=['HN', 'FLIP'], modeNames=['Half-normal (výrobní nastavení)', 'Deska otočená (ověřit)'],
                              modeHow='Mění se otočením desky kanálu (žádný přepínač vpředu).',
                              topLabel='horní řada 1–24', bottomLabel='dolní řada 1–24 (šedé jacky)',
                              note='Výrobní nastavení: half-normal na dolní řadě. Zastrčení do šedého jacku odpojí horní řadu od dolní.'),
                controls=[])


# ---------------------------------------------------------------------------------- 500 moduly (1 slot)
W5, H5 = 450, 1576

def module_base(col, ink, title, brand):
    img = Image.new('RGB', (W5, H5), col); d = ImageDraw.Draw(img)
    d.rectangle((0, 0, W5 - 1, H5 - 1), outline=tuple(max(0, c - 40) for c in col), width=3)
    for y in (40, H5 - 40):
        d.ellipse((W5 / 2 - 18, y - 18, W5 / 2 + 18, y + 18), fill=tuple(max(0, c - 60) for c in col))
        d.ellipse((W5 / 2 - 8, y - 8, W5 / 2 + 8, y + 8), fill=(20, 20, 20))
    text_c(d, (W5 / 2, 110), title, font(54), ink)
    text_c(d, (W5 / 2, H5 - 110), brand, font(40), ink)
    return img, d

def button(d, x, y, label, ink, below=True):
    d.rounded_rectangle((x - 34, y - 26, x + 34, y + 26), 8, fill=(30, 30, 32), outline=(110, 110, 110), width=2)
    text_c(d, (x, y + (52 if below else -52)), label, font(30), ink)

def api512c():
    ink = (235, 235, 235)
    img, d = module_base((70, 74, 80), ink, '512c', 'API')
    c = []
    # LED VU (7 segmentu) - jen zobrazeni
    for i in range(7):
        d.rectangle((150, 190 + i * 22, 300, 204 + i * 22), fill=(40, 40, 40))
    text_c(d, (W5 / 2, 370), 'GAIN', font(40), ink)
    ticks(d, W5 / 2, 520, 110, 11, -150, 150, ink)
    text_c(d, (95, 680), 'MIN', font(26), ink); text_c(d, (355, 680), 'MAX', font(26), ink)
    c.append(dict(id='gain', name='GAIN', group='', type='knob', minLabel='', maxLabel='', verified=True,
                  note='Plynulý zisk: MIC 10–65 dB, HI-Z 14–50 dB (výrobce). Čísla stupnice na panelu neověřena – ukládá se poloha.',
                  x=W5 / 2, y=520, r=110, angleMin=-150, angleMax=150, draw=dict(kind='vknob', style='skirt', colour='#1c1c1c')))
    for (cid, lab, x, y, note) in (('polarity', 'Ø', 130, 820, 'Otočení polarity výstupu o 180°.'),
                                   ('phantom', '48V', 320, 820, 'Fantomové napájení 48 V.'),
                                   ('pad', 'PAD', 130, 980, 'Útlum 20 dB (MIC i HI-Z).'),
                                   ('hiz', 'MIC/HI-Z', 320, 980, 'Výběr vstupu: MIC (zad. XLR) / HI-Z (přední jack). ON = HI-Z.')):
        button(d, x, y, lab, ink)
        c.append(dict(id=cid, name=lab, group='Switches', type='button', note=note, verified=True, x=x, y=y, r=34,
                      draw=dict(kind='vbuttons', points=[[x, y]], w=68, h=52, colour='#ff3b2a', map=[-1, 0])))
    d.ellipse((W5 / 2 - 40, 1180, W5 / 2 + 40, 1260), fill=(160, 160, 165)); d.ellipse((W5 / 2 - 18, 1202, W5 / 2 + 18, 1238), fill=(10, 10, 10))
    text_c(d, (W5 / 2, 1300), 'HI-Z IN', font(26), ink)
    save(img, 'api512c.jpg')
    return dict(id='api.512c', name='API 512c', manufacturer='API', formFactor='500 series, 1 slot', slots500=1, kind='preamp', category='mixing',
                w=W5, h=H5, image='api512c.jpg', profileVersion=1, layoutVerified=False, anchor='',
                imageNote='Vlastní kresba podle popisu výrobce (rozmístění orientační).',
                sources=['API 512c – manuál / datasheet (barryrudolph.com/recall/manuals/api512c.pdf)', 'apiaudio.com/512c'],
                openQuestions=['Rozmístění ovladačů není z fotky ověřené.', 'Číselná stupnice zisku na panelu neověřena.'], controls=c)

def neve1073lb():
    ink = (240, 240, 240)
    img, d = module_base((96, 110, 128), ink, '1073LB', 'AMS NEVE')
    c = []
    gain_opts = ['LINE -20', 'LINE -15', 'LINE -10', 'LINE -5', 'LINE 0', 'LINE +5', 'LINE +10', 'MIC -20', 'MIC -25', 'MIC -30', 'MIC -35', 'MIC -40',
                 'MIC -45', 'MIC -50', 'OFF', 'MIC -55', 'MIC -60', 'MIC -65', 'MIC -70', 'MIC -75', 'MIC -80']
    text_c(d, (W5 / 2, 230), 'INPUT GAIN', font(34), ink)
    ticks(d, W5 / 2, 420, 110, 21, -160, 160, ink, 3, 12)
    c.append(dict(id='gain', name='INPUT GAIN', group='', type='stepped', steps=21, options=gain_opts, verified=False,
                  note='Poloha přepínače: LINE −20…+10 (7 poloh), MIC −20…−80 (14 poloh), OFF mezi −50 a −55 (manuál AMS Neve). Pořadí po otočení neověřeno na kusu.',
                  x=W5 / 2, y=420, r=95, angleMin=-160, angleMax=160, draw=dict(kind='vknob', style='skirt', colour='#b3261e')))
    text_c(d, (W5 / 2, 640), 'TRIM', font(32), ink)
    ticks(d, W5 / 2, 760, 62, 7, -150, 150, ink, 3, 10)
    text_c(d, (W5 / 2 - 100, 850), '-10', font(24), ink); text_c(d, (W5 / 2 + 100, 850), '+5', font(24), ink)
    c.append(dict(id='trim', name='TRIM', group='', type='knob', minLabel='-10', maxLabel='+5', verified=True,
                  note='−10 až +5 dB, středová aretace v 0 dB (manuál). Poloha 0 na stupnici neověřena – ukládá se poloha.',
                  x=W5 / 2, y=760, r=62, angleMin=-150, angleMax=150, draw=dict(kind='vknob', style='small', colour='#202020')))
    for (cid, lab, x, y, col, note) in (('phantom', '48V', 130, 960, '#ff3b2a', 'Fantom (manuál: zapíná se stiskem knobu TRIM, LED červená).'),
                                        ('phase', 'Ø', 320, 960, '#ff3b2a', 'Otočení fáze.'),
                                        ('loz', 'LO Z', 130, 1120, '#ffd21a', 'Impedance mikrofonního vstupu: ON = 300 Ω, OFF = 1200 Ω.'),
                                        ('frip', 'FR I/P', 320, 1120, '#3dff6e', 'Přední vstup (combo) místo zadního.')):
        button(d, x, y, lab, ink)
        c.append(dict(id=cid, name=lab, group='Switches', type='button', note=note, verified=True, x=x, y=y, r=34,
                      draw=dict(kind='vbuttons', points=[[x, y]], w=68, h=52, colour=col, map=[-1, 0])))
    d.ellipse((W5 / 2 - 52, 1240, W5 / 2 + 52, 1344), fill=(40, 40, 44)); text_c(d, (W5 / 2, 1370), 'COMBO', font(24), ink)
    save(img, 'neve1073lb.jpg')
    return dict(id='amsneve.1073lb', name='AMS Neve 1073LB', manufacturer='AMS Neve', formFactor='500 series, 1 slot', slots500=1, kind='preamp', category='mixing',
                w=W5, h=H5, image='neve1073lb.jpg', profileVersion=1, layoutVerified=False, anchor='',
                imageNote='Vlastní kresba podle manuálu AMS Neve (rozmístění orientační).',
                sources=['AMS Neve 1073LB User Manual (barryrudolph.com/recall/manuals/neve_1073_lb_mic_pre-manual.pdf)'],
                openQuestions=['Pořadí poloh přepínače zisku ve směru otáčení ověřit na kusu.', 'Rozmístění ovladačů orientační.'], controls=c)

def camden500():
    ink = (235, 235, 235)
    img, d = module_base((26, 28, 30), ink, 'CAMDEN 500', 'CRANBORNE AUDIO')
    c = []
    text_c(d, (W5 / 2, 220), 'MOJO', font(34), ink)
    ticks(d, W5 / 2, 330, 62, 7, -150, 150, ink, 3, 10)
    c.append(dict(id='mojo', name='MOJO', group='', type='knob', minLabel='OFF', maxLabel='', verified=True,
                  note='Saturace Mojo, zcela vlevo = bypass (manuál). Bez číselné stupnice.',
                  x=W5 / 2, y=330, r=62, angleMin=-150, angleMax=150, draw=dict(kind='vknob', style='small', colour='#202020')))
    text_c(d, (100, 450), 'THUMP', font(24), ink); text_c(d, (350, 450), 'CREAM', font(24), ink)
    c.append(dict(id='mojo_style', name='MOJO STYLE', group='', type='selector', options=['THUMP', 'CREAM'], verified=True,
                  note='Charakter Mojo: Thump / Cream.', x=W5 / 2, y=450, r=36, draw=dict(kind='vlever', points=[[W5 / 2 - 40, 450], [W5 / 2 + 40, 450]], colour='#e0e0e0')))
    for (cid, lab, x, y, note) in (('hpf', 'HPF', 130, 580, 'Horní propust 80 Hz, 12 dB/okt.'), ('polarity', 'Ø', 320, 580, 'Otočení polarity.')):
        button(d, x, y, lab, ink)
        c.append(dict(id=cid, name=lab, group='Switches', type='button', note=note, verified=True, x=x, y=y, r=34,
                      draw=dict(kind='vbuttons', points=[[x, y]], w=68, h=52, colour='#3d9bff', map=[-1, 0])))
    text_c(d, (W5 / 2, 720), 'GAIN', font(34), ink)
    labs = ['MIN', '2', '3', '4', '5', '6', '7', '8', '9', '10', '11', 'MAX']
    for i, t in enumerate(labs):
        a = math.radians(-150 + 300 * i / 11)
        text_c(d, (W5 / 2 + math.sin(a) * 150, 880 - math.cos(a) * 150), t, font(22), ink)
    c.append(dict(id='gain', name='GAIN', group='', type='stepped', steps=12, options=[], verified=True,
                  note='12 poloh po 5,5 dB: MIC 8–68,5 dB, LINE 0–60,5 dB, HI-Z 3–63,5 dB (manuál).',
                  scale=[[i / 11.0, t] for i, t in enumerate(labs)],
                  x=W5 / 2, y=880, r=100, angleMin=-150, angleMax=150, draw=dict(kind='vknob', style='skirt', colour='#bfbfbf')))
    button(d, 130, 1080, '48V', ink)
    c.append(dict(id='phantom', name='48V', group='Switches', type='button', note='Fantom pro zadní XLR.', verified=True, x=130, y=1080, r=34,
                  draw=dict(kind='vbuttons', points=[[130, 1080]], w=68, h=52, colour='#ff3b2a', map=[-1, 0])))
    for i, t in enumerate(['MIC', 'LINE', 'HI-Z']):
        text_c(d, (395, 1030 + i * 50), t, font(22), ink)
    c.append(dict(id='input', name='INPUT', group='', type='selector', options=['MIC', 'LINE', 'HI-Z'], verified=True,
                  note='Typ vstupu (v LINE se automaticky zapne pad 8 dB).', x=320, y=1080, r=36,
                  draw=dict(kind='vlever', points=[[320, 1030], [320, 1080], [320, 1130]], colour='#e0e0e0')))
    d.ellipse((W5 / 2 - 40, 1220, W5 / 2 + 40, 1300), fill=(160, 160, 165)); d.ellipse((W5 / 2 - 18, 1242, W5 / 2 + 18, 1278), fill=(10, 10, 10))
    text_c(d, (W5 / 2, 1330), 'HI-Z / LINE', font(24), ink)
    save(img, 'camden500.jpg')
    return dict(id='cranborne.camden500', name='Cranborne Audio Camden 500', manufacturer='Cranborne Audio', formFactor='500 series, 1 slot', slots500=1,
                kind='preamp', category='mixing', w=W5, h=H5, image='camden500.jpg', profileVersion=1, layoutVerified=False, anchor='',
                imageNote='Vlastní kresba podle manuálu Cranborne (rozmístění orientační).',
                sources=['Cranborne Audio Camden 500 User Manual (scan.co.uk/PDFs/Products/96391.pdf)'],
                openQuestions=['Rozmístění ovladačů orientační – ověřit podle fotky.'], controls=c)

def new_profiles():
    return [wa76(), wa2a(), samson(), neutrik(), api512c(), neve1073lb(), camden500()]

if __name__ == '__main__':
    for p in new_profiles(): print(p['id'], len(p['controls']))

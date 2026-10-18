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
    return dict(id='warmaudio.wa76', name='Warm Audio WA76', manufacturer='Warm Audio', formFactor='19" 2U', category='mixing',
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
    return dict(id='warmaudio.wa2a', name='Warm Audio WA-2A', manufacturer='Warm Audio', formFactor='19" 2U', category='mixing',
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
    return dict(id='samson.spatchplus', name='Samson S-patch Plus', manufacturer='Samson', formFactor='patchbay', category='',
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
    return dict(id='neutrik.nysspp.l1', name='Neutrik NYS-SPP-L1', manufacturer='Neutrik (Rean)', formFactor='patchbay', category='',
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

def new_profiles():
    return [wa76(), wa2a(), samson(), neutrik()]

if __name__ == '__main__':
    for p in new_profiles(): print(p['id'], len(p['controls']))

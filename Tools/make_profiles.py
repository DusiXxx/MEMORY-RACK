#!/usr/bin/env python3
"""Spoji geometrii (z build_*.py) s popisem ovladacu -> Assets/profiles.json.
ID ovladacu se nesmi menit (jsou klicem v ulozenych snapshotech)."""
import json

WORK = '/home/claude/panels/work'
OUT = __import__('os').environ.get('RM_OUT', '/home/claude/RackMemory/Assets')
geo = lambda n: json.load(open(f'{WORK}/geo_{n}.json'))


def rot(g, cid, name, group, mn='', mx='', centre='', steps=0, options=None, note='', verified=True, amin=-150.0, amax=150.0):
    k = g['knobs'][cid]
    c = {'id': cid, 'name': name, 'group': group, 'type': 'stepped' if steps else 'knob', 'steps': steps, 'options': options or [],
         'minLabel': mn, 'maxLabel': mx, 'centreLabel': centre, 'note': note, 'verified': verified,
         'x': k['x'], 'y': k['y'], 'r': k['r'], 'angleMin': amin, 'angleMax': amax}
    if 'sprite' in k:
        c['draw'] = {'kind': 'sprite', 'sprite': k['sprite'], 'spriteR': k['spriteR'], 'refAngle': k.get('refAngle', 0.0)}
    return c


def btn(g, cid, name, group, note=''):
    b = g['buttons'][cid]
    gl = b['glow']
    c = {'id': cid, 'name': name, 'group': group, 'type': 'button', 'note': note, 'verified': True, 'x': b['x'], 'y': b['y'], 'r': b['r']}
    if gl['shape'] == 'dot':
        c['draw'] = {'kind': 'leds', 'points': [[gl['x'], gl['y']]], 'ledR': gl['r'], 'colour': gl['colour'], 'map': [-1, 0]}
    else:
        c['draw'] = dict(gl, kind='glow')
    return c


profiles = []
E41 = 'Potenciometr s aretací, 41 kroků (elysia).'

# ============================================================================ xmax 500
g = geo('xmax500')
S = lambda cid, name, group, mn, mx, centre='', note=E41: rot(g, cid, name, group, mn, mx, centre, steps=41, note=note)
profiles.append({
    'id': 'elysia.xmax.500', 'name': 'elysia xmax 500', 'manufacturer': 'elysia', 'formFactor': '500 series, 2 sloty',
    'image': g['image'], 'w': g['w'], 'h': g['h'], 'layoutVerified': True,
    'imageNote': 'Panel je sestavený z oficiálního recall sheetu elysia (přesná geometrie a potisk); knoby a tlačítka jsou vykreslené.',
    'sources': ['elysia Recall Sheet xmax 500/qube (geometrie, potisk, stupnice)', 'elysia.com/xmax (41 kroků, x-freq 40–470 Hz, Tone ±8 dB, poměry 1:1.45 / 1:2.55)',
                'manuál xmax (Lowmo 150 Hz, Hit It, Punch, Link)'],
    'openQuestions': [],
    'controls': [
        S('link', 'link', 'Control', 'min', 'max', note='VariLink – propojení řídicích napětí pásem. ' + E41),
        S('thr_mid', 'mid thres', 'Threshold', 'min', 'max'),
        S('thr_side', 'side thres', 'Threshold', 'min', 'max'),
        S('thr_low', 'low thres', 'Threshold', 'min', 'max'),
        S('xfreq', 'x-freq', 'Control', '40', '470', note='Dělicí frekvence Low/Mid v Hz (potisk: 40, 60, 140, 210, 350, 470). ' + E41),
        S('release', 'time', 'Control', '10', '900', note='Společný release všech pásem (potisk: 10, 20, 180, 400, 900). ' + E41),
        S('tone', 'tone', 'Shape', '-8', '+8', '0', note='High shelf, dB. ' + E41),
        S('gain_mid', 'mid gain', 'Gain', '-12', '+12', '0'),
        S('gain_side', 'side gain', 'Gain', '-12', '+12', '0'),
        S('gain_low', 'low gain', 'Gain', '-12', '+12', '0'),
        S('sclip', 's-clip', 'Shape', 'min', 'max', note='Soft clipper. ' + E41),
        S('level', 'level', 'Shape', '-12', '+12', '0', note='Výstupní úroveň, dB. ' + E41),
        btn(g, 'lowmo', 'lowmo', 'Switches', 'High-pass 150 Hz (12 dB/okt) jen na Side.'),
        btn(g, 'punch', 'punch', 'Switches', 'Poměr 1:1.45 → 1:2.55.'),
        btn(g, 'hitit', 'hit it!', 'Switches', 'ZAP = aktivní, VYP = hardwire bypass.'),
    ]})

# ============================================================================ xfilter 500
g = geo('xfilter500')
S = lambda cid, name, group, mn, mx, centre='', note=E41: rot(g, cid, name, group, mn, mx, centre, steps=41, note=note)
profiles.append({
    'id': 'elysia.xfilter.500', 'name': 'elysia xfilter 500', 'manufacturer': 'elysia', 'formFactor': '500 series, 2 sloty (stereo link)',
    'image': g['image'], 'w': g['w'], 'h': g['h'], 'layoutVerified': True,
    'imageNote': 'Panel je sestavený z oficiálního recall sheetu elysia (přesná geometrie a potisk); knoby a tlačítka jsou vykreslené.',
    'sources': ['elysia Recall Sheet xfilter 500/qube (geometrie, potisk, stupnice)', 'elysia.com/xfilter (41 kroků, Q 0.5 / 1.0, Passive Massage 12 kHz)',
                'manuál xfilter 500 (funkce tlačítek)'],
    'openQuestions': ['Výrobce nabízí „Mastering Edition“ (±5 dB) jen jako 19" rack. Tento profil je standardní xfilter 500 (±16 / ±13 dB). '
                      'Pokud má tvůj kus jiný potisk, pošli fotku a panel upravím.'],
    'controls': [
        S('ls_gain', 'Gain', 'Low Shelf', '-16', '16', '0', 'Low shelf gain, dB. ' + E41),
        S('hm_gain', 'Gain', 'High Mid', '-13', '13', '0', 'High mid gain, dB. ' + E41),
        S('ls_freq', 'Freq', 'Low Shelf', '20', '900', '', 'Hz (potisk: 20 … 150 … 900). ' + E41),
        S('hm_freq', 'Freq', 'High Mid', '300', '16k', '', 'Hz (potisk: 300 … 2k0 … 16k). ' + E41),
        S('lm_gain', 'Gain', 'Low Mid', '-13', '13', '0', 'Low mid gain, dB. ' + E41),
        S('hs_gain', 'Gain', 'High Shelf', '-16', '16', '0', 'High shelf gain, dB. ' + E41),
        S('lm_freq', 'Freq', 'Low Mid', '45', '2k2', '', 'Hz (potisk: 45 … 300 … 2k2). ' + E41),
        S('hs_freq', 'Freq', 'High Shelf', '700', '28k', '', 'Hz (potisk: 700 … 5k0 … 28k). ' + E41),
        btn(g, 'passive', 'Passive Massage', 'Switches', 'Pevný pasivní LC filtr, rezonance 12 kHz, pokles od 17 kHz.'),
        btn(g, 'low_cut', 'Low Cut', 'Low Shelf', 'VYP = shelf, ZAP = low cut s rezonancí (12 dB/okt).'),
        btn(g, 'hm_q', 'High Mid Narrow Q', 'High Mid', 'VYP = široké Q (0.5), ZAP = úzké Q (1.0).'),
        btn(g, 'lm_q', 'Low Mid Narrow Q', 'Low Mid', 'VYP = široké Q (0.5), ZAP = úzké Q (1.0).'),
        btn(g, 'high_cut', 'High Cut', 'High Shelf', 'VYP = shelf, ZAP = high cut s rezonancí (12 dB/okt).'),
        btn(g, 'hitit', 'Hit It!', 'Switches', 'ZAP = aktivní, VYP = hardwire bypass.'),
    ]})


# ============================================================================ xfilter Mastering Edition (19", 1U)
g = geo('xfilterme')
ME_SLOTS = {'L': [0, 4, 8, 11, 18, 22, 25, 29, 32, 36, 40], 'R': [0, 4, 8, 11, 15, 18, 22, 29, 32, 36, 40]}


def me_knob(k, style):
    c = {'id': k['id'], 'name': k['name'], 'group': k['group'], 'type': 'stepped', 'steps': 41, 'options': [],
         'minLabel': k['min'], 'maxLabel': k['max'], 'centreLabel': k['centre'],
         'note': ('Gain, dB (Mastering Edition: ±5 dB). ' if k['name'] == 'Gain' else 'Frekvence, Hz. ') + E41,
         'verified': True, 'x': k['x'], 'y': k['y'], 'r': k['r'], 'angleMin': -150.0, 'angleMax': 150.0,
         'draw': {'kind': 'sprite', 'sprite': k['sprite'], 'spriteR': k['spriteR'], 'refAngle': 0.0},
         # potisk stupnice: krok, na kterem popisek lezi (odhad z polohy popisku na panelu)
         'scale': [[st / 40.0, lab] for st, lab in zip(ME_SLOTS[style], k['labels'])]}
    return c


def me_button(b):
    l = b['led']
    return {'id': b['id'], 'name': b['name'], 'group': b['group'], 'type': 'button', 'note': b['note'], 'verified': True,
            'x': b['x'], 'y': b['y'], 'r': b['r'],
            'draw': {'kind': 'leds', 'points': [[l['x'], l['y']]], 'ledR': l['r'], 'colour': l['colour'], 'map': [-1, 0]}}


profiles.append({
    'id': 'elysia.xfilter.me', 'name': 'elysia xfilter Mastering Edition', 'manufacturer': 'elysia', 'formFactor': '19" rack, 1U',
    'image': g['image'], 'w': g['w'], 'h': g['h'], 'layoutVerified': True,
    'imageNote': 'Panel z čelní fotografie elysia xfilter (stejné rozložení), potisk stupnic gainu a nápis podle Mastering Edition; knoby jsou vykreslené.',
    'sources': ['elysia.com – xfilter (čelní fotografie 1U)', 'elysia.com – xfilter Mastering Edition (fotografie: gain ±5 dB, nápis MASTERING EDITION)',
                'audiosteps / soundpure: 41 kroků, ±5 dB, Q 0.5 / 1.0, Passive Massage'],
    'openQuestions': ['Přesné kroky, na kterých leží čísla stupnice gainu, jsou odečtené z fotky (±1 krok). Ukládá se poloha knobu, ne dB.'],
    'controls': [me_knob(k, 'L' if i < 4 else 'R') for i, k in enumerate(g['knobs'])] + [me_button(b) for b in g['buttons']]})

# ============================================================================ nvelope 500
g = geo('nvelope500')
NV = 'Počet kroků potenciometru není v manuálu uveden – ukládá se plynulá poloha.'
K = lambda cid, name, group, mn, mx, centre='', note='': rot(g, cid, name, group, mn, mx, centre, note=(note + ' ' + NV).strip(), verified=False)
profiles.append({
    'id': 'elysia.nvelope.500', 'name': 'elysia nvelope 500', 'manufacturer': 'elysia', 'formFactor': '500 series, 2 sloty (2 kanály)',
    'image': g['image'], 'w': g['w'], 'h': g['h'], 'layoutVerified': True,
    'imageNote': 'Panel je sestavený z oficiálního recall sheetu elysia (přesná geometrie a potisk); knoby a tlačítka jsou vykreslené.',
    'sources': ['elysia Recall Sheet nvelope 500/qube (geometrie, potisk, stupnice)', 'manuál nvelope 500 (funkce ovladačů a režimů)'],
    'openQuestions': ['Nevím jistě, jestli mají knoby nvelope 500 aretaci (41 kroků jako xfilter). Zatím se ukládá plynulá poloha.'],
    'controls': [
        K('ch1_attack', 'Attack', 'Left', '-15', '15', '0', 'dB. V EQ Mode: gain high shelfu.'),
        K('ch2_attack', 'Attack', 'Right', '-15', '15', '0', 'dB. V EQ Mode: gain high shelfu.'),
        K('ch1_freq_a', 'Freq A', 'Left', '20', '8k0', '', 'Hz. Dual Band: počáteční frekvence attacku. EQ Mode: frekvence high shelfu.'),
        K('ch2_freq_a', 'Freq A', 'Right', '20', '8k0', '', 'Hz. Dual Band: počáteční frekvence attacku. EQ Mode: frekvence high shelfu.'),
        K('ch1_sustain', 'Sustain', 'Left', '-15', '15', '0', 'dB. V EQ Mode: gain low shelfu.'),
        K('ch2_sustain', 'Sustain', 'Right', '-15', '15', '0', 'dB. V EQ Mode: gain low shelfu.'),
        K('ch1_freq_s', 'Freq S', 'Left', '50', '15k', '', 'Hz. Dual Band: koncová frekvence sustainu. EQ Mode: frekvence low shelfu.'),
        K('ch2_freq_s', 'Freq S', 'Right', '50', '15k', '', 'Hz. Dual Band: koncová frekvence sustainu. EQ Mode: frekvence low shelfu.'),
        btn(g, 'autogain', 'Auto Gain', 'Switches', 'Jen ve Full Range.'),
        btn(g, 'ch2_fullrange', 'Full Range (Right)', 'Right', 'ZAP = Full Range, VYP = Dual Band.'),
        btn(g, 'ch2_eqmode', 'EQ Mode (Right)', 'Right', 'ZAP = dvoupásmový shelf EQ místo envelope.'),
        btn(g, 'ch2_on', 'Right On', 'Right', 'ZAP = kanál aktivní, VYP = hardwire bypass.'),
        btn(g, 'link', 'Stereo Link', 'Switches', 'Propojí kanály; master = levé knoby a spodní přepínače.'),
        btn(g, 'ch1_fullrange', 'Full Range (Left)', 'Left', 'ZAP = Full Range, VYP = Dual Band.'),
        btn(g, 'ch1_eqmode', 'EQ Mode (Left)', 'Left', 'ZAP = dvoupásmový shelf EQ místo envelope.'),
        btn(g, 'ch1_on', 'Left On', 'Left', 'ZAP = kanál aktivní, VYP = hardwire bypass.'),
    ]})

# ============================================================================ SPL Vitalizer Mk3-T
g = geo('vitalizer')
OFF9 = ['OFF'] + [str(i) for i in range(1, 10)] + ['MAX']
LOW9 = ['LOW'] + [str(i) for i in range(1, 10)] + ['HIGH']


def sc(c, labels, explicit=None):
    """potisk stupnice SPL: 11 popisku po 30 stupnich (-150 .. +150)"""
    if explicit is not None:
        c['scale'] = explicit
    else:
        c['scale'] = [[i / 10.0, l] + ([0] if l in ('OFF', 'LOW') else [10] if l in ('MAX', 'HIGH') else []) for i, l in enumerate(labels)]
    return c

K = lambda cid, name, group, mn, mx, centre='', note='': rot(g, cid, name, group, mn, mx, centre, note=note)
profiles.append({
    'id': 'spl.vitalizer.mk3t', 'name': 'SPL Vitalizer Mk3-T', 'manufacturer': 'SPL', 'formFactor': '19" rack, 1U',
    'image': g['image'], 'w': g['w'], 'h': g['h'], 'layoutVerified': True,
    'imageNote': 'Oficiální čelní fotografie SPL (Front-flat). Knoby se otáčejí jako výřezy z téže fotky.',
    'sources': ['spl.audio – VitalizerMk3-T_Front-flat (čelní fotografie)', 'SPL Manual Vitalizer Mk3-T (funkce a rozsahy)'],
    'openQuestions': [],
    'controls': [
        sc(K('drive', 'DRIVE', 'Input', '-20', '6', '0', 'dB. Úroveň do filtrové sítě, střed = vstupní úroveň.'), ['-20', '-13', '-8', '-5', '-2', '0', '1.5', '3', '4', '5', '6']),
        sc(K('bass', 'BASS SOUND', 'Bass', 'SOFT', 'TIGHT', '0', 'Vlevo měkký/teplý bas, vpravo suchý/perkusivní.'), None, [[0.0, 'SOFT'], [0.5, '0'], [1.0, 'TIGHT']]),
        sc(K('basscomp', 'BASS COMP', 'Bass', 'OFF', 'MAX', '', 'Jednoknoflíkový kompresor jen v basové cestě.'), OFF9),
        sc(K('midhi', 'MID-HI TUNE', 'Vitalizer', '22', '1.1', '', 'kHz. Pozor: vyšší frekvence je VLEVO.'), ['22', '14', '8.5', '6', '4', '2.8', '2.2', '1.8', '1.5', '1.3', '1.1']),
        sc(K('process', 'PROCESS', 'Vitalizer', 'OFF', 'MAX', '', 'Poměr efektu k originálu.'), OFF9),
        sc(K('lceq', 'LC-EQ', 'High', 'LOW', 'HIGH', '', 'Frekvence LC-EQ (cca 2–20 kHz).'), LOW9),
        sc(K('intensity', 'INTENSITY', 'High', 'OFF', 'MAX', '', 'Úroveň LC-EQ.'), OFF9),
        sc(K('expander', 'STEREO EXPANDER', 'Stereo', 'OFF', 'MAX', '', 'Šířka sterea.'), OFF9),
        btn(g, 'on', 'ON', 'Switches', 'Svítí = aktivní, nesvítí = bypass (bliká při nahřívání lamp).'),
    ]})

# ============================================================================ Manley Variable Mu Mastering
g = geo('manley')
REC = ['SLOW (8 s)', 'MS (4 s)', 'MED (0.6 s)', 'MF (0.4 s)', 'FAST (0.2 s)']
ATT_MS = [90, 80, 70, 60, 50, 40, 35, 30, 25, 20, 15]
ATT = [f'{l} ({ms} ms)' for l, ms in zip('KJIHGFEDCBA', ATT_MS)]
OUTS = [('%.1f dB' % (-11.5 + 0.5 * i)).replace('-0.0', '0.0') for i in range(24)]
INP = ['-4 dB', '-2 dB', '0 dB', '+2 dB', '+4 dB']


def tog(cid, name, group, options, note=''):
    t = g['toggles'][cid]
    return {'id': cid, 'name': name, 'group': group, 'type': 'selector', 'options': options, 'note': note, 'verified': True,
            'x': t['x'], 'y': t['y'], 'r': t['r'],
            'draw': {'kind': 'toggle', 'w': t['w'], 'h': t['h'], 'axis': t['axis'], 'photoState': t['photoState']}}


def man_channel(p, grp):
    return [
        rot(g, p + 'recovery', 'RECOVERY', grp, steps=5, options=REC, note='5 poloh. Časy podle manuálu.', amin=-60, amax=60),
        rot(g, p + 'output', 'OUTPUT', grp, steps=24, options=OUTS, note='24 poloh po 0.5 dB (−11.5 … 0 dB).', amin=-172.5, amax=172.5),
        rot(g, p + 'threshold', 'THRESHOLD', grp, 'MIN', 'MAX', steps=24, note='24 poloh. Limit: krok 0.5 dB, Compress: cca 0.25 dB.', amin=-172.5, amax=172.5),
        rot(g, p + 'attack', 'ATTACK', grp, steps=11, options=ATT, note='11 poloh K (SLOW) … A (FAST). Časy podle manuálu.', amin=-150, amax=150),
        tog(p + 'mode', 'LIMIT / COMPRESS', grp, ['COMPRESS', 'LIMIT'], 'Compress cca 1.5:1 (soft knee), Limit 4:1 až 20:1.'),
        tog(p + 'bypass', 'IN / BYPASS', grp, ['BYPASS', 'IN'], 'Dole = bypass.'),
        tog(p + 'hpsc', 'HP SC', grp, ['OFF', 'IN'], 'Sidechain high-pass. Nahoře = zapnuto (IN).'),
    ]


profiles.append({
    'id': 'manley.varimu.mastering', 'name': 'Manley Stereo Variable Mu (Mastering Version)', 'manufacturer': 'Manley Labs', 'formFactor': '19" rack, 2U',
    'image': g['image'], 'w': g['w'], 'h': g['h'], 'layoutVerified': True,
    'imageNote': 'Oficiální čelní fotografie Manley (Mastering Version). Knoby a páčky jsou výřezy z téže fotky.',
    'sources': ['manley.com – Variable Mu Mastering Version (čelní fotografie)', "Manley Stereo Variable Mu owner's manual, část Mastering Version"],
    'openQuestions': ['Fotka je verze s HP SC. Pokud má tvůj kus M/S nebo T-Bar mod, pošli fotku a doplním přepínače.'],
    'controls': man_channel('ch1_', 'Left') + [
        rot(g, 'input', 'DUAL INPUT', 'Global', steps=5, options=INP, note='Společný vstup pro oba kanály, 5 poloh po 2 dB.', amin=-60, amax=60),
        tog('link', 'SEP / LINK', 'Global', ['SEP', 'LINK'], 'Link = oba kanály se stahují o stejný počet dB.'),
    ] + man_channel('ch2_', 'Right')})

# ============================================================================ WesAudio _RHEA
g = geo('rhea')
RING = 'Enkodér s LED kroužkem (16 LED). Kroužek ukazuje hodnotu jen přibližně – přesné hodnoty drží recall plugin WesAudio.'
BLUE = '#2f8dff'


def ring(cid, name, mn, mx, steps, options=None, note=RING, verified=False):
    import math
    k = g['knobs'][cid]
    pts = g['rings'][cid]
    ang = lambda p: math.degrees(math.atan2(p[0] - k['x'], -(p[1] - k['y'])))
    return {'id': cid, 'name': name, 'group': 'Compressor', 'type': 'stepped', 'steps': steps, 'options': options or [], 'minLabel': mn, 'maxLabel': mx,
            'note': note, 'verified': verified, 'x': k['x'], 'y': k['y'], 'r': k['r'], 'angleMin': round(ang(pts[0]), 1), 'angleMax': round(ang(pts[-1]), 1),
            'draw': {'kind': 'ring', 'points': pts, 'ledR': g['ringLedR'] * 1.9, 'colour': BLUE}}


def ledsel(cid, name, options, lmap, note='', typ='selector'):
    b = g['buttons'][cid]
    return {'id': cid, 'name': name, 'group': 'Switches', 'type': typ, 'options': options, 'note': note, 'verified': True,
            'x': b['x'], 'y': b['y'], 'r': b['r'] * 1.2,
            'draw': {'kind': 'leds', 'points': g['leds'][cid], 'ledR': g['ledR'], 'colour': '#3dff6e', 'map': lmap}}


profiles.append({
    'id': 'wesaudio.rhea', 'name': 'WesAudio _RHEA', 'manufacturer': 'WesAudio', 'formFactor': '500 series, 2 sloty',
    'image': g['image'], 'w': g['w'], 'h': g['h'], 'layoutVerified': True,
    'imageNote': 'Čelní fotografie z manuálu WesAudio (nižší rozlišení; popisové čáry manuálu byly odretušovány).',
    'sources': ['WesAudio _RHEA User Manual V2 – Front Panel and Main Functions'],
    'openQuestions': ['_RHEA má vlastní total recall (USB + AU plugin), který ukládá přesné hodnoty. Tady je jen vizuální záznam podle LED.',
                      'Barva svítících LED na kroužcích je v pluginu zvolená kvůli čitelnosti (modrá), ne podle hardwaru.'],
    'controls': [
        ring('input', 'INPUT', '-15', '+15', 16),
        ring('threshold', 'THRESHOLD', 'MIN', 'MAX', 16),
        ring('output', 'OUTPUT', '-15', '+15', 16),
        ring('attack', 'ATTACK - mS', '', '', 6, ['0.5 ms', '1 ms', '3 ms', '10 ms', '20 ms', '50 ms'], 'Šest pevných časů.', True),
        ring('mix', 'MIX', 'DRY', 'WET', 16),
        ring('release', 'RELEASE - S', '', '', 6, ['0.1 s', '0.3 s', '0.6 s', '0.9 s', '1.8 s', '3.6 s'], 'Šest pevných časů (podle potisku panelu).', True),
        ledsel('thd', 'THD', ['OFF', 'MED', 'HIGH'], [-1, 0, 1], 'Harmonické zkreslení: MED 1 %, HIGH 2.5 %.'),
        ledsel('bypass', 'BYPASS', [], [-1, 0], 'Svítí = bypass.', 'button'),
        ledsel('scfilter', 'SC FILTER', ['OFF', '60 Hz', '90 Hz', '150 Hz'], [-1, 0, 1, 2], 'Sidechain high-pass.'),
    ]})

# Manley v racku uzivatele neni -> profil se negeneruje (podklady zustavaji v Tools pro pripad navratu)
profiles = [p for p in profiles if not p['id'].startswith('manley')]

# Nove pristroje s vlastni kresbou panelu (WA76, WA-2A, patchbaye)
import sys as _sys, os as _os
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
_os.environ.setdefault('RM_OUT', OUT)
from build_new_panels import new_profiles
profiles += new_profiles()

# Podklady pro cteni nastaveni z fotky: cim se pristroj na fotce zarovna (anchor) a jak se cte kazdy ovladac
for p in profiles:
    if p['id'].startswith('elysia'):
        p['anchor'] = 'disc'            # stribrne knoby = svetle kruhy na tmavem panelu
        for c in p['controls']:
            k = c.get('draw', {}).get('kind')
            if k == 'sprite': c['anchorR'] = c['r']; c['read'] = 'notch'; c['readR'] = c['r']
            elif k == 'leds': c['read'] = 'led'
            elif k == 'glow': c['read'] = 'ringglow'
    elif p['id'].startswith('spl'):
        p['anchor'] = 'ring'            # kruh bilych tecek stupnice kolem cerneho knobu
        for c in p['controls']:
            k = c.get('draw', {}).get('kind')
            if k == 'sprite': c['anchorR'] = 65.0; c['read'] = 'line'; c['readR'] = 38.0
            elif k == 'glow': c['read'] = 'rectglow'
    elif p['id'] == 'wesaudio.rhea':
        # _RHEA: zarovnani podle potisku panelu (SIFT), cte se LED krouzek enkoderu a LED prepinacu
        p['anchor'] = ''
        for c in p['controls']:
            k = c.get('draw', {}).get('kind')
            if k == 'ring': c['read'] = 'ledring'
            elif k == 'leds': c['read'] = 'ledsel'
    else:
        p['anchor'] = ''

for p in profiles:
    p.setdefault('profileVersion', 2)
    if p['formFactor'].startswith('500'):
        p['slots500'] = 2           # sirka v 500 slotech (lunchbox)
    ids = [c['id'] for c in p['controls']]
    assert len(ids) == len(set(ids)), p['id']
    for c in p['controls']:
        assert 0 < c['x'] < p['w'] and 0 < c['y'] < p['h'], (p['id'], c['id'])
        if c['type'] == 'stepped' and c.get('options'):
            assert len(c['options']) == c['steps'], (p['id'], c['id'])

json.dump({'profiles': profiles}, open(f'{OUT}/profiles.json', 'w'), ensure_ascii=False, indent=1)
print('profiles:', [(p['id'], len(p['controls'])) for p in profiles])

#!/usr/bin/env python3
"""Guía de construcción de la plantilla Solo «Hondonada del Rocío» (v2: aspecto natural).

El terreno se describe como en el editor: un relieve alto (el borde, que tapa
el vacío de fuera del mapa) en el que se excavan las zonas jugables con bordes
irregulares, rampas, el lago y el arroyo. Con eso se calcula un mapa de alturas,
se sombrea como un relieve y se reparte la decoración según el terreno.

Genera:
  guia.png   el mapa visto desde arriba con todo colocado y la leyenda
  spec.json  alturas (celda 50, como el editor), agua, materiales, caminos,
             decoración y todo lo jugable, para montar la plantilla

Convención: X crece a la derecha (este) y Z hacia arriba (norte).
"""
import json, math, os, random, collections
from PIL import Image, ImageDraw, ImageFont, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
random.seed(7)

# ── Terreno ──────────────────────────────────────────────────────────────────
BOUNDS = (-2400, -1900, 2400, 1900)  # minX, minZ, maxX, maxZ
CELL = 10                            # resolución del cálculo (el editor usa 50)
WATER_LEVEL = 40                     # línea de agua: todas las orillas están a Y 40 (el juego pone el agua a la de la más baja)

RIM, HUB, CREST, TERR, SHORE, MESA, TERRHI, RAMP, WATER, STUMP, RIDGE, SECRET = range(12)
TAG_NAMES = ["borde", "claro", "cresta", "terrazas", "orilla", "meseta", "terraza alta", "rampa", "agua", "tocón", "cresta de roca", "rincón"]

def smooth(e0, e1, x):
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0))); return t * t * (3 - 2 * t)

def waves(seed, n=7, f0=1 / 900, f1=1 / 70):
    r = random.Random(seed); out = []
    for k in range(n):
        t = k / (n - 1); f = f0 * (f1 / f0) ** t; a = 1 - t * 0.78; ang = r.uniform(0, 2 * math.pi)
        out.append((math.cos(ang) * f * 2 * math.pi, math.sin(ang) * f * 2 * math.pi, r.uniform(0, 2 * math.pi), a))
    s = sum(w[3] for w in out) * 0.45
    return [(a, b, c, d / s) for a, b, c, d in out]

def noise(w, x, z): return sum(d * math.sin(a * x + b * z + c) for a, b, c, d in w)

def sd_box(x0, z0, x1, z1, r):
    cx, cz, hx, hz = (x0 + x1) / 2, (z0 + z1) / 2, (x1 - x0) / 2 - r, (z1 - z0) / 2 - r
    def f(x, z):
        qx, qz = abs(x - cx) - hx, abs(z - cz) - hz
        return math.hypot(max(qx, 0), max(qz, 0)) + min(max(qx, qz), 0) - r
    return f

def sd_ell(cx, cz, rx, rz):
    """Distancia (aprox. de primer orden) al borde de una elipse: no la encoge
    a lo largo del eje largo, así los bordes suaves tienen el mismo ancho en
    todo el contorno."""
    def f(x, z):
        dx, dz = x - cx, z - cz
        k = math.hypot(dx / rx, dz / rz)
        if k < 1e-6: return -min(rx, rz)
        g = math.hypot(dx / (rx * rx), dz / (rz * rz)) / k
        return (k - 1) / g
    return f

def seg_d(x, z, ax, az, bx, bz):
    dx, dz = bx - ax, bz - az; L2 = dx * dx + dz * dz
    t = max(0.0, min(1.0, ((x - ax) * dx + (z - az) * dz) / L2)) if L2 else 0.0
    return math.hypot(x - ax - dx * t, z - az - dz * t), t

def sd_caps(pts, r):
    def f(x, z): return min(seg_d(x, z, *a, *b)[0] for a, b in zip(pts, pts[1:])) - r
    return f

def sd_arc(cx, cz, R, thick, gap_deg=0.0, gap_dir=-90.0):
    """Anillo de radio R; con hueco de `gap_deg` grados centrado en `gap_dir`."""
    g = math.radians(gap_deg / 2); gd = math.radians(gap_dir)
    ends = [(cx + R * math.cos(gd + s * g), cz + R * math.sin(gd + s * g)) for s in (-1, 1)]
    def f(x, z):
        a = math.atan2(z - cz, x - cx)
        diff = abs((a - gd + math.pi) % (2 * math.pi) - math.pi)
        if gap_deg and diff < g:
            return min(math.hypot(x - ex, z - ez) for ex, ez in ends) - thick
        return abs(math.hypot(x - cx, z - cz) - R) - thick
    return f

# Operaciones, en orden (como con las herramientas del editor). target: altura o
# ("ramp", ax, az, h0, bx, bz, h1). soft: ancho del acantilado. amp: irregularidad.
OPS = [
    ("Claro", HUB, sd_ell(0, -1000, 950, 520), 0, 70, 55),
    ("Claro (norte)", HUB, sd_box(-700, -700, 700, 420, 300), 0, 70, 55),
    ("Pasillo SE", HUB, sd_caps([(600, -1390), (1150, -1470)], 75), 0, 50, 8),
    ("Rincón SE", HUB, sd_box(1100, -1720, 1800, -1250, 200), 0, 70, 40),
    # Pasillo SO: más estrecho que el bloque (124) para que no se pueda rodear.
    ("Pasillo SO", HUB, sd_caps([(-650, -1350), (-1330, -1350)], 50), 0, 50, 4),
    ("Rincón secreto", SECRET, sd_box(-2120, -1650, -1300, -1150, 200), 0, 70, 45),
    ("Cresta (sur)", CREST, sd_box(950, -750, 2100, -300, 180), 40, 70, 45),
    ("Desfiladero", CREST, sd_caps([(1325, -340), (1325, -90)], 75), 40, 55, 8),
    ("Cresta (norte)", CREST, sd_box(950, -100, 2100, 760, 260), 40, 70, 50),
    ("Terrazas", TERR, sd_box(-2120, -760, -920, 660, 260), 40, 70, 50),
    ("Paso al lago", TERR, sd_caps([(-1750, 550), (-1700, 850)], 230), 40, 70, 40),
    ("Orilla del lago", SHORE, sd_ell(-700, 1120, 1350, 500), 40, 70, 55),
    ("Orilla sur", SHORE, sd_box(-460, 460, 760, 980, 200), 40, 70, 40),
    ("Meseta", MESA, sd_box(1150, 760, 2100, 1640, 260), 150, 45, 40),
    ("Rampa al este", RAMP, sd_caps([(760, -760), (1060, -680)], 120), ("ramp", 760, -760, 0, 1060, -680, 40), 40, 10),
    ("Rampa al oeste", RAMP, sd_caps([(-760, -760), (-1060, -680)], 120), ("ramp", -760, -760, 0, -1060, -680, 40), 40, 10),
    ("Rampa a la meseta", RAMP, sd_caps([(760, 900), (1240, 930)], 50), ("ramp", 760, 900, 40, 1240, 930, 150), 30, 5),
    ("Terraza alta", TERRHI, sd_box(-1550, -450, -1100, 0, 120), 80, 30, 22),
    # Donde apoya la rampa de subida (130 de largo, 40 de desnivel): borde recto y suelo llano delante.
    ("Borde de la rampa", TERRHI, sd_box(-1200, -300, -1100, -150, 0), 80, 8, 0),
    ("Pie de la rampa", TERR, sd_box(-1100, -300, -950, -150, 0), 40, 8, 0),
    ("Cresta de la cantera", RIDGE, sd_arc(-1550, 400, 170, 38, 48, -90), 130, 25, 9),
    ("Loma del tocón", HUB, sd_ell(0, -50, 300, 300), 15, 120, 18),
    # Ribera del arroyo: el claro sube en cuesta suave hasta Y 40, como las otras orillas, para
    # que el agua no se quede a ras del claro (el arroyo la corta después).
    ("Ribera del arroyo", HUB, sd_box(-700, 60, 760, 420, 150), ("zramp", 120, 15, 330, 40), 60, 20),
    # El agua se pinta sobre suelo llano a la altura de la orilla (Y 40): el juego
    # hunde él el fondo 25 y pone cada lago a la altura de su punto más bajo
    # (fondo incluido), así que excavar aquí lo dejaría en el fondo de una zanja.
    ("Lago", WATER, sd_ell(-700, 1120, 1100, 390), 40, 60, 45),
    ("Arroyo", WATER, sd_caps([(-500, 820), (-500, 430), (760, 430)], 60), 40, 20, 10),
    ("Isla", SHORE, sd_ell(-350, 1130, 200, 180), 40, 150, 22),  # con playa
]
WAVES = [waves(100 + i) for i in range(len(OPS))]
RIM_W, VAR_W = waves(1), waves(2, n=5, f0=1 / 500, f1=1 / 140)

def rim_height(x, z):
    edge = min(x - BOUNDS[0], BOUNDS[2] - x, z - BOUNDS[1], BOUNDS[3] - z)
    return 330 + 330 * smooth(0, 1, 1 - edge / 520) + 70 * noise(RIM_W, x, z)

def terrain(x, z):
    """Altura, material y cuánto alcanza el agua (lago o arroyo) en el punto.
    El agua se marca por altura (lo que queda bajo la línea de agua donde
    llega su excavación), no por material: así su orilla queda a ras del
    suelo y el juego, que pone el agua a la altura de la orilla más baja, no
    la hunde."""
    h, tag, wet = rim_height(x, z), RIM, 0.0
    for (name, t, sd, target, soft, amp), w in zip(OPS, WAVES):
        d = sd(x, z)
        if d > soft + amp * 1.3: continue
        d += amp * noise(w, x, z)
        m = 1 - smooth(0, soft, d)
        if m <= 0: continue
        if isinstance(target, tuple) and target[0] == "zramp":   # cuesta a lo largo de Z
            _, z0, h0, z1, h1 = target
            tgt = h0 + (h1 - h0) * max(0.0, min(1.0, (z - z0) / (z1 - z0)))
        elif isinstance(target, tuple):
            _, ax, az, h0, bx, bz, h1 = target
            tgt = h0 + (h1 - h0) * seg_d(x, z, ax, az, bx, bz)[1]
        else:
            tgt = target
        h = h * (1 - m) + tgt * m
        if m > 0.5: tag = t
        wet = max(wet, m) if t == WATER else wet * (1 - m)   # lo de después (la isla) quita agua
    return h, tag, wet

NX = (BOUNDS[2] - BOUNDS[0]) // CELL + 1
NZ = (BOUNDS[3] - BOUNDS[1]) // CELL + 1
def cx_(i): return BOUNDS[0] + i * CELL
def cz_(j): return BOUNDS[1] + j * CELL

H = [[0.0] * NX for _ in range(NZ)]
WI = [[0.0] * NX for _ in range(NZ)]
T = [[0] * NX for _ in range(NZ)]
for j in range(NZ):
    z = cz_(j)
    for i in range(NX):
        H[j][i], T[j][i], WI[j][i] = terrain(cx_(i), z)

def hget(i, j): return H[max(0, min(NZ - 1, j))][max(0, min(NX - 1, i))]
GRAD = [[0.0] * NX for _ in range(NZ)]
WET = [[False] * NX for _ in range(NZ)]
WALK = [[False] * NX for _ in range(NZ)]
for j in range(NZ):
    for i in range(NX):
        gx = (hget(i + 1, j) - hget(i - 1, j)) / (2 * CELL); gz = (hget(i, j + 1) - hget(i, j - 1)) / (2 * CELL)
        GRAD[j][i] = math.hypot(gx, gz)
        WET[j][i] = WI[j][i] > 0.5
        WALK[j][i] = not WET[j][i] and GRAD[j][i] < 0.6 and T[j][i] not in (RIM, RIDGE, STUMP)

def dist_field(src):
    """Distancia (en celdas, 8 vecinos) a la celda `src` más cercana."""
    D = [[10 ** 6] * NX for _ in range(NZ)]; q = collections.deque()
    for j in range(NZ):
        for i in range(NX):
            if src[j][i]: D[j][i] = 0; q.append((i, j))
    while q:
        i, j = q.popleft(); dn = D[j][i] + 1
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                a, b = i + di, j + dj
                if 0 <= a < NX and 0 <= b < NZ and D[b][a] > dn: D[b][a] = dn; q.append((a, b))
    return D
NOT_WALK = [[not WALK[j][i] for i in range(NX)] for j in range(NZ)]
D_EDGE = dist_field(NOT_WALK)   # en lo que se pisa: cuánto falta para el borde
D_WALK = dist_field(WALK)       # en lo que no: cuánto falta para lo jugable
D_WET = dist_field(WET)
D_DRY = dist_field([[not WET[j][i] for i in range(NX)] for j in range(NZ)])

def cell(x, z):
    return max(0, min(NX - 1, round((x - BOUNDS[0]) / CELL))), max(0, min(NZ - 1, round((z - BOUNDS[1]) / CELL)))

# ── Lo jugable (igual que el plan) ───────────────────────────────────────────
RULES = dict(objective="parts", target=9, days=1, startPikmin=dict(blue=0, red=20, yellow=0),
             medals=[40, 70, 100], timeLimit=0, music="forest", baseStage="stages/chal1.ini")
STORY = dict(captain=dict(x=-50, z=-1250), ship=dict(x=-300, z=-1200),
             onions=[dict(color="roja", x=300, z=-1100, buried=False),
                     dict(color="amarilla", x=1650, z=560, buried=True),
                     dict(color="azul", x=-1850, z=450, buried=True)])
PARTS = [
    ("P1", "uf06", -550, -1050, "15–25", "Junto a la nave"),
    ("P2", "uf08", 1450, -1500, "15–25", "Rincón SE, tras la puerta de papel"),
    ("P3", "un05", 1500, 50, "15–25", "Tras los géiseres (rojos)"),
    ("P4", "uf01", 1900, 400, "20–40", "Junto al Bulborb moteado dormido"),
    ("P5", "un08", -1350, -200, "20–40", "Terraza alta (lanzar amarillos o rampa)"),
    ("P6", "un02", -1550, 420, "25–45", "Cantera, tras la roca negra"),
    ("P7", "un13", -1250, 1100, "20–40", "Bajo el agua (azules)"),
    ("P8", "un09", -400, 1180, "25–45", "En la isla (azules o puente)"),
    ("P9", "un14", -1850, -1480, "10–25", "Secreta, tras el bloque de 10"),
    ("P10", "un11", 1800, 1350, "40–95", "Meseta final, con Patas Largas"),
]
ENEMIES = [
    ("chappy", "Bulborb enano", 1300, -1400, "Claro", "guarda P2"),
    ("chappy", "Bulborb enano", 1650, -1600, "Claro", "guarda P2"),
    ("mizigen", "Nido de Honeywisp", -350, -600, "Claro", "néctar"),
    ("qurione", "Honeywisp", -150, -800, "Claro", "néctar"),
    ("qurione", "Honeywisp", 250, -700, "Claro", "néctar"),
    ("tank", "Blowhog de fuego", 1150, -500, "Cresta", "a la entrada"),
    ("hibaA", "Géiser de fuego", 1325, -250, "Cresta", "desfiladero"),
    ("hibaA", "Géiser de fuego", 1325, -200, "Cresta", "desfiladero"),
    ("hibaA", "Géiser de fuego", 1325, -150, "Cresta", "desfiladero"),
    ("swallow", "Bulborb moteado", 1750, 250, "Cresta", "dormido, junto a P4"),
    ("beatle", "Escarabajo cañón", -1550, 100, "Terrazas", "mira al sur, ante la roca negra"),
    ("frog", "Wollywog amarillo", -1300, -600, "Terrazas", ""),
    ("napkid", "Snitchbug", -1800, -300, "Terrazas", ""),
    ("kabekuiA", "Sheargrub hembra", -1100, 250, "Terrazas", ""),
    ("kabekuiB", "Sheargrub macho", -1200, 450, "Terrazas", ""),
    ("kabekuiC", "Shearwig", -1080, 480, "Terrazas", ""),
    ("namazu", "Water Dumple", -1000, 1300, "Lago", "en el agua"),
    ("namazu", "Water Dumple", -750, 950, "Lago", "en el agua"),
    ("otama", "Wogpole", -1650, 1150, "Lago", "ambiente"),
    ("otama", "Wogpole", -1450, 1300, "Lago", "ambiente"),
    ("otama", "Wogpole", -1000, 1400, "Lago", "ambiente"),
    ("otama", "Wogpole", -650, 1400, "Lago", "ambiente"),
    ("frow", "Wollywog", -300, 1230, "Lago", "en la isla"),
    ("collec", "Breadbug", -1700, -1560, "Secreto", "puede robar P9"),
    ("hollec", "Nido de Breadbug", -1980, -1280, "Secreto", ""),
    ("kogane", "Escarabajo iridiscente", 1350, 1050, "Meseta", "néctar antes del jefe"),
    ("kumo", "Patas Largas (jefe)", 1650, 1250, "Meseta", "jefe"),
]
OBJECTS = [
    ("gate_soft", "Puerta de papel", 1000, -1448, "en medio del pasillo SE"),
    ("push_rock", "Bloque que se empuja", -1250, -1350, "en la boca del rincón; se empuja desde el pasillo, 300 al oeste, hasta dentro"),
    ("slope_up", "Rampa de subida", -1000, -225, "empieza abajo y sube al oeste; su parte alta apoya 15 sobre la terraza (130 de largo, 40 de desnivel)"),
    ("gate_bomb", "Puerta de roca negra", -1550, 235, "en el hueco de la cresta de la cantera; mira al sur"),
    ("bridge_long", "Puente largo", -350, 600, "de la orilla a la isla (360); se construye desde el sur"),
    ("key", "Llave", -300, 1080, "en la isla"),
    ("key_door", "Puerta con cerradura", 790, 900, "al pie de la rampa a la meseta"),
    ("bridge", "Puente (atajo)", 0, 520, "cruza el arroyo (180) entre orillas a Y 40; se construye desde el norte"),
    ("nectar", "Fuente de néctar", 150, -1280, "en el claro"),
]
PELLETS = [
    ("posy", "", -700, -1250), ("posy", "", 550, -800),
    ("1", "roja", -200, -480), ("1", "roja", 400, -1350), ("1", "roja", 200, -300),
    ("5", "roja", 1150, 250),
    ("1", "amarilla", -1850, -600), ("1", "amarilla", -1100, -620),
    ("5", "amarilla", -1200, -100),
    ("5", "azul", -1900, 600),
]
TRIGGERS = [
    ("T1", None, None, 0, "2 s tras empezar", "Cuadro: «La nave ha caído en la hondonada. Recupera 9 piezas antes de que anochezca.»"),
    ("T2", -1230, 300, 90, "1 Pikmin amarillo", "Aparecen 6 rocas bomba aquí + mensaje"),
    ("T3", 250, -450, 90, "5 piezas entregadas", "Salen 2 Bulbear enanos aquí + «Algo se mueve cerca de la nave...»"),
    ("T4", 1350, 1000, 250, "Capitán en la zona", "Cámara 3 s a Patas Largas + mensaje"),
    ("T5", None, None, 0, "Puente del arroyo hecho", "«¡Atajo abierto!»"),
    ("T6", 1450, 820, 130, "Capitán en el borde sur de la meseta", "«Desde aquí se puede saltar a la cresta.»"),
    ("T7", None, None, 0, "Bloque empujado", "«¡Has encontrado un rincón escondido!»"),
    ("T8", None, None, 0, "Las 16:00", "«Queda poco de día.»"),
]
# Caminos de tierra pintados (por donde se va), con su anchura.
PATHS = [
    ([(-300, -1120), (0, -1060), (300, -1060)], 70),
    ([(0, -1060), (60, -700), (-120, -380), (0, 250)], 60),
    ([(300, -1060), (760, -760), (1060, -680), (1250, -480), (1325, -330), (1325, -90), (1480, 60), (1700, 330), (1650, 520)], 65),
    ([(-300, -1120), (-760, -760), (-1060, -680), (-980, -380), (-1000, 120), (-1300, 200), (-1550, 230)], 65),
    ([(-1300, 200), (-1750, 330), (-1850, 470), (-1730, 800), (-1350, 700)], 55),
    ([(0, 520), (380, 780), (760, 900), (1240, 930), (1450, 1080), (1650, 1250)], 60),
    ([(300, -1100), (600, -1390), (1150, -1470), (1450, -1500)], 50),
    ([(-420, -1200), (-650, -1350), (-1330, -1350), (-1700, -1400)], 45),
]

# ── Decoración (objetos del juego, repartidos según el terreno) ─────────────
ITEMS = [(x, z) for _, _, x, z, _, _ in PARTS] + [(x, z) for _, _, x, z, _, _ in ENEMIES] + \
        [(x, z) for _, _, x, z, _ in OBJECTS] + [(x, z) for _, _, x, z in PELLETS] + \
        [(o["x"], o["z"]) for o in STORY["onions"]] + [(STORY["ship"]["x"], STORY["ship"]["z"]), (STORY["captain"]["x"], STORY["captain"]["z"])]

path_img = Image.new("L", (NX, NZ), 0); pd = ImageDraw.Draw(path_img)
for pts, wdt in PATHS:
    pd.line([((x - BOUNDS[0]) / CELL, (BOUNDS[3] - z) / CELL) for x, z in pts], fill=255, width=int(wdt / CELL), joint="curve")
    for x, z in pts:
        r = wdt / CELL / 2; px, py = (x - BOUNDS[0]) / CELL, (BOUNDS[3] - z) / CELL
        pd.ellipse([px - r, py - r, px + r, py + r], fill=255)
path_img = path_img.filter(ImageFilter.GaussianBlur(2.2))
PATHM = path_img.load()
def path_at(i, j): return PATHM[i, NZ - 1 - j] / 255.0

DECOR = []   # (kind, asset, x, z, scale)
_taken = collections.defaultdict(list)
def free(x, z, sp):
    k = (int(x // 200), int(z // 200))
    for a in (-1, 0, 1):
        for b in (-1, 0, 1):
            for ox, oz, osp in _taken[(k[0] + a, k[1] + b)]:
                if math.hypot(ox - x, oz - z) < (sp + osp) * 0.5 * 0.85: return False
    return True
def put(kind, asset, x, z, scale, sp):
    DECOR.append((kind, asset, round(x), round(z), round(scale, 2))); _taken[(int(x // 200), int(z // 200))].append((x, z, sp))
def near_item(x, z, r): return any(math.hypot(x - ix, z - iz) < r for ix, iz in ITEMS)

def pick(assets):
    e = random.choices(assets, weights=[x[3] if len(x) > 3 else 1 for x in assets])[0]
    return e[0], random.uniform(e[1], e[2])

def scatter(kind, assets, n, sp, ok, tries=40):
    placed = 0
    for _ in range(n * tries):
        if placed >= n: break
        x = random.uniform(BOUNDS[0] + 20, BOUNDS[2] - 20); z = random.uniform(BOUNDS[1] + 20, BOUNDS[3] - 20)
        i, j = cell(x, z)
        if not ok(i, j, x, z) or not free(x, z, sp): continue
        asset, scale = pick(assets); put(kind, asset, x, z, scale, sp); placed += 1

def clusters(kind, assets, centers_ok, ncl, per, rad, sp):
    for _ in range(ncl * 30):
        if ncl <= 0: break
        x = random.uniform(BOUNDS[0], BOUNDS[2]); z = random.uniform(BOUNDS[1], BOUNDS[3]); i, j = cell(x, z)
        if not centers_ok(i, j, x, z): continue
        ncl -= 1
        for _ in range(per * 6):
            a, r = random.uniform(0, 2 * math.pi), random.uniform(0, rad)
            px, pz = x + math.cos(a) * r, z + math.sin(a) * r; pi, pj = cell(px, pz)
            if centers_ok(pi, pj, px, pz) and free(px, pz, sp):
                asset, scale = pick(assets); put(kind, asset, px, pz, scale, sp)

def open_ground(i, j, x, z, edge=4, item=110, path=0.35):
    return WALK[j][i] and D_EDGE[j][i] >= edge and path_at(i, j) < path and not near_item(x, z, item) and T[j][i] != RAMP

# Ancho real de cada modelo (del catálogo del editor), para dibujarlo a su tamaño.
FOOT = {"objects/plants/hutaba.mod": 65, "objects/plants/ooinu_l.mod": 126, "objects/plants/wakame_l.mod": 150, "objects/plants/wakame_s.mod": 90,
        "objects/plants/ine.mod": 230, "objects/plants/mizukusa.mod": 113, "objects/plants/tanpopo.mod": 120, "objects/plants/ooinu_s.mod": 73,
        "objects/plants/clover.mod": 86, "objects/plants/kinokolt.mod": 60, "objects/plants/chidome.mod": 66, "objects/weeds/weed1.mod": 8,
        "objects/weeds/stone1.mod": 10, "objects/weeds/stone2.mod": 18, "objects/weeds/stone3.mod": 13,
        "piece:tocon_grande": 250, "piece:raiz_arco": 290, "piece:rama_curva": 630, "piece:tronco_y": 300, "piece:ramita_alta": 175,
        "piece:helecho": 250, "piece:helecho_2": 245}
COLLIDE = {"stump"}  # lo del borde no se pisa: si chocara, sus cajas cortarían los caminos de los pasos estrechos

# El hito: el tocón grande de los niveles del juego, en la loma del claro.
put("stump", "piece:tocon_grande", 0, -50, 1.25, 330)
CANOPY = [("objects/plants/hutaba.mod", 3.2, 5.0, 6), ("objects/plants/wakame_l.mod", 1.6, 2.4, 2),
          ("objects/plants/ine.mod", 1.2, 1.7, 2), ("objects/plants/ooinu_l.mod", 2.0, 2.8, 0.35)]
# Raíces, ramas y troncos de los niveles del juego asomando al filo.
scatter("piece_log", [("piece:raiz_arco", 0.9, 1.2), ("piece:rama_curva", 0.7, 1.0), ("piece:tronco_y", 0.9, 1.2), ("piece:ramita_alta", 0.8, 1.1)], 14, 450,
        lambda i, j, x, z: T[j][i] == RIM and 3 <= D_WALK[j][i] <= 10)
scatter("piece_fern", [("piece:helecho", 0.6, 0.9), ("piece:helecho_2", 0.6, 0.9)], 18, 320,
        lambda i, j, x, z: WALK[j][i] and 1 <= D_EDGE[j][i] <= 5 and not near_item(x, z, 150) and path_at(i, j) < 0.3 and T[j][i] != RAMP)
# Lo alto del borde: vegetación gigante que tapa lo de fuera y da escala.
scatter("canopy", CANOPY, 380, 115, lambda i, j, x, z: T[j][i] == RIM and D_WALK[j][i] >= 9 and H[j][i] > 260)
# Rocas grandes en el filo de los acantilados; piedras y helechos en sus pies.
scatter("boulder", [("objects/weeds/stone1.mod", 12, 19.5), ("objects/weeds/stone2.mod", 8, 14), ("objects/weeds/stone3.mod", 10, 18)], 100, 140,
        lambda i, j, x, z: T[j][i] == RIM and 2 <= D_WALK[j][i] <= 8)
scatter("stone", [("objects/weeds/stone1.mod", 5, 9), ("objects/weeds/stone2.mod", 3, 5), ("objects/weeds/stone3.mod", 4, 7)], 120, 85,
        lambda i, j, x, z: WALK[j][i] and 1 <= D_EDGE[j][i] <= 4 and not near_item(x, z, 90) and path_at(i, j) < 0.3)
# Prados.
GRASS = [("objects/weeds/weed1.mod", 2.5, 4.0)]
scatter("tuft", GRASS, 160, 70, lambda i, j, x, z: open_ground(i, j, x, z) and T[j][i] in (HUB, SHORE, SECRET))
scatter("tuft_dry", GRASS, 60, 90, lambda i, j, x, z: open_ground(i, j, x, z) and T[j][i] in (TERR, TERRHI, CREST, MESA))
clusters("flower_y", [("objects/plants/tanpopo.mod", 0.8, 1.1)], lambda i, j, x, z: open_ground(i, j, x, z, 3) and T[j][i] in (TERR, TERRHI, HUB), 12, 5, 120, 60)
clusters("flower_b", [("objects/plants/ooinu_s.mod", 0.9, 1.3)], lambda i, j, x, z: open_ground(i, j, x, z, 3) and T[j][i] in (SHORE, HUB), 8, 5, 110, 55)
clusters("clover", [("objects/plants/clover.mod", 0.8, 1.1)], lambda i, j, x, z: open_ground(i, j, x, z, 3) and T[j][i] in (HUB, SECRET, SHORE), 8, 4, 90, 60)
# Orillas y agua.
scatter("reed", [("objects/plants/ine.mod", 0.55, 0.85), ("objects/plants/mizukusa.mod", 0.8, 1.2)], 80, 70,
        lambda i, j, x, z: WALK[j][i] and D_WET[j][i] <= 4 and not near_item(x, z, 90) and path_at(i, j) < 0.3)
scatter("weed_water", [("objects/plants/wakame_s.mod", 0.9, 1.4), ("objects/plants/wakame_l.mod", 0.7, 1.1)], 40, 120,
        lambda i, j, x, z: WET[j][i] and D_DRY[j][i] >= 5 and not near_item(x, z, 100))
# Setas: corro alrededor de la arena del jefe y rincones en sombra.
for k in range(16):
    a = 2 * math.pi * k / 16; x, z = 1650 + math.cos(a) * 360, 1250 + math.sin(a) * 300; i, j = cell(x, z)
    if WALK[j][i] and not near_item(x, z, 80): put("mushroom", "objects/plants/kinokolt.mod", x, z, 1.2, 50)
clusters("mushroom", [("objects/plants/kinokolt.mod", 0.7, 1.1), ("objects/plants/chidome.mod", 0.8, 1.2)],
         lambda i, j, x, z: WALK[j][i] and D_EDGE[j][i] <= 5 and T[j][i] in (MESA, SECRET, SHORE) and not near_item(x, z, 90), 8, 4, 70, 45)
# Cresta de fuego: piedras oscuras.
scatter("stone_dark", [("objects/weeds/stone3.mod", 5, 9)], 40, 90, lambda i, j, x, z: open_ground(i, j, x, z, 2, 100) and T[j][i] == CREST)

# Comprobación: todo lo jugable en suelo llano y en su sitio.
PROBLEMS = []
def check(label, x, z, wet=False):
    i0, j0 = cell(x, z)
    for di in (-3, 0, 3):
        for dj in (-3, 0, 3):
            i, j = max(0, min(NX - 1, i0 + di)), max(0, min(NZ - 1, j0 + dj))
            if wet:
                if not WET[j][i]: PROBLEMS.append(f"{label} ({x}, {z}) no está en el agua"); return
            elif not WALK[j][i]:
                PROBLEMS.append(f"{label} ({x}, {z}) cae en {TAG_NAMES[T[j][i]]} (pendiente {GRAD[j][i]:.2f})"); return
WET_OK = {"P7", "namazu", "otama"}
for pid, _, x, z, _, _ in PARTS: check(pid, x, z, pid in WET_OK)
for k, n, x, z, _, _ in ENEMIES: check(n, x, z, k in WET_OK)
for k, n, x, z, _ in OBJECTS:
    if k not in ("bridge", "bridge_long", "slope_up", "gate_bomb"): check(n, x, z)
for k, c, x, z in PELLETS: check(f"pastilla {k} {c}", x, z)
for o in STORY["onions"]: check("cebolla " + o["color"], o["x"], o["z"])
check("nave", STORY["ship"]["x"], STORY["ship"]["z"]); check("capitán", STORY["captain"]["x"], STORY["captain"]["z"])

# Acceso: desde el capitán por suelo que se pisa, con las puertas, los bloques y
# el agua cerrados (solo rojos al empezar). Solo deben quedar a mano P1, P3 y P4.
def rect_cells(x, z, face, hw, z0, z1):
    a = math.radians(face); fx, fz = math.sin(a), math.cos(a); rx, rz = math.cos(a), -math.sin(a)
    out = set()
    for k in range(int(z0), int(z1) + 1, 5):
        for w in range(int(-hw), int(hw) + 1, 5):
            out.add(cell(x + fx * k + rx * w, z + fz * k + rz * w))
    return out
SHUT = {"gate_soft": (128, -30, 30), "gate_bomb": (128, -30, 30), "key_door": (80, -10, 10), "push_rock": (62, -61, 127)}
FACE = {"gate_soft": 98.3, "push_rock": None, "gate_bomb": 0, "key_door": 90}
OBJ_FACE = {(1000, -1448): 98.3, (-1250, -1350): 270, (-1550, 235): 0, (790, 900): 90}
closed = set()
for k, n, x, z, _ in OBJECTS:
    if k in SHUT: closed |= rect_cells(x, z, OBJ_FACE[(x, z)], *SHUT[k])
def reach(blocked):
    seen = set(); q = collections.deque([cell(STORY["captain"]["x"], STORY["captain"]["z"])])
    while q:
        i, j = q.popleft()
        if (i, j) in seen or not (0 <= i < NX and 0 <= j < NZ) or not WALK[j][i] or (i, j) in blocked: continue
        seen.add((i, j))
        q.extend(((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)))
    return seen
start = reach(closed)
ACCESS = [pid for pid, _, x, z, _, _ in PARTS if cell(x, z) in start]
if set(ACCESS) != {"P1", "P3", "P4"}: PROBLEMS.append("Al empezar se llega a " + ", ".join(ACCESS) + " (deberían ser solo P1, P3 y P4)")
print("al empezar se llega a:", ACCESS)

# ── Pintar el relieve ────────────────────────────────────────────────────────
FLAT = {RIM: (64, 98, 56), HUB: (124, 178, 84), CREST: (184, 130, 88), TERR: (190, 176, 98), SHORE: (106, 168, 90),
        MESA: (146, 152, 120), TERRHI: (208, 192, 114), RAMP: (168, 144, 102), WATER: (120, 130, 100), STUMP: (120, 84, 52),
        RIDGE: (140, 128, 110), SECRET: (88, 136, 68)}
ROCK = {CREST: (150, 98, 70), MESA: (122, 120, 112), STUMP: (96, 64, 40)}
DIRT = (164, 134, 96)
LX, LY, LZ = -0.55, 0.62, 0.56; ln = math.sqrt(LX * LX + LY * LY + LZ * LZ); LX, LY, LZ = LX / ln, LY / ln, LZ / ln
def lerpc(a, b, t): return tuple(a[k] + (b[k] - a[k]) * t for k in range(3))

small = Image.new("RGB", (NX, NZ)); px = small.load()
rise = CELL * math.sqrt(2) * 0.72
for j in range(NZ):
    for i in range(NX):
        h, t, g = H[j][i], T[j][i], GRAD[j][i]
        x, z = cx_(i), cz_(j)
        gx = (hget(i + 1, j) - hget(i - 1, j)) / (2 * CELL); gz = (hget(i, j + 1) - hget(i, j - 1)) / (2 * CELL)
        nx_, ny_, nz_ = -gx, 1.0, -gz; nn = math.sqrt(nx_ * nx_ + 1 + nz_ * nz_)
        shade = max(0.0, (nx_ * LX + ny_ * LY + nz_ * LZ) / nn)
        lit = 1.0
        for k in range(1, 26):
            a, b = i - k, j + k
            if a < 0 or b >= NZ: break
            if H[b][a] > h + rise * k: lit = 0.62; break
        if WET[j][i]:
            depth = D_DRY[j][i] * CELL / 5; tt = min(1.0, depth / 60)   # más oscura lejos de la orilla
            col = lerpc((104, 192, 210), (30, 88, 164), tt)
            if depth < 8: col = lerpc(col, (200, 230, 235), 0.45)
            col = tuple(c * (0.88 + 0.12 * lit) for c in col)
        else:
            base = FLAT[t]
            if t == RIM and h < 300: base = lerpc((92, 120, 70), base, smooth(150, 300, h))
            if WALK[j][i] and D_WET[j][i] <= 3: base = lerpc(base, (206, 192, 142), 0.65)   # arena de la orilla
            p = path_at(i, j)
            if p > 0 and WALK[j][i]: base = lerpc(base, DIRT, min(1.0, p * 1.1))
            steep = smooth(0.35, 1.3, g)
            base = lerpc(base, ROCK.get(t, (126, 110, 92)), steep)
            v = 1 + 0.09 * noise(VAR_W, x, z)
            k = (0.38 + 0.82 * shade) * lit * v * (1 + h / 2600)
            col = tuple(c * k for c in base)
        px[i, NZ - 1 - j] = tuple(max(0, min(255, int(c))) for c in col)

# ── Imagen final ─────────────────────────────────────────────────────────────
SS = 2
SCALE = 0.5 * SS
ML, MT = 80 * SS, 130 * SS
MAPW = int((BOUNDS[2] - BOUNDS[0]) * SCALE); MAPH = int((BOUNDS[3] - BOUNDS[1]) * SCALE)
PANEL_X = ML + MAPW + 50 * SS
W = PANEL_X + 1500 * SS; Hh = MT + MAPH + 80 * SS
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"; BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
_fc = {}
def font(size, bold=False):
    k = (size, bold)
    if k not in _fc: _fc[k] = ImageFont.truetype(BOLD if bold else FONT, int(size * SS))
    return _fc[k]
def P(x, z): return (ML + (x - BOUNDS[0]) * SCALE, MT + (BOUNDS[3] - z) * SCALE)
def L(v): return v * SCALE

img = Image.new("RGB", (W, Hh), (26, 28, 30))
relief = small.filter(ImageFilter.GaussianBlur(0.8)).resize((MAPW, MAPH), Image.BICUBIC)
grain = Image.effect_noise((MAPW // 3, MAPH // 3), 40).resize((MAPW, MAPH), Image.BICUBIC).convert("RGB")
relief = Image.blend(relief, Image.composite(relief, grain, Image.new("L", (MAPW, MAPH), 235)), 1.0)
img.paste(relief, (ML, MT))
d = ImageDraw.Draw(img, "RGBA")

def text(xy, s, size=15, col=(240, 240, 235), bold=False, anchor="la", outline=True):
    f = font(size, bold)
    if outline: d.text(xy, s, font=f, fill=col, anchor=anchor, stroke_width=int(3 * SS), stroke_fill=(20, 22, 24))
    else: d.text(xy, s, font=f, fill=col, anchor=anchor)
def wtext(x, z, s, **kw): text(P(x, z), s, **kw)
def circ(x, z, r, **kw):
    a, b = P(x, z); r = L(r); d.ellipse([a - r, b - r, a + r, b + r], **kw)

# Decoración.
def blob(x, z, r, cols, rnd):
    for k, c in enumerate(cols):
        off = (2 - k) * r * 0.12
        for _ in range(3):
            a = rnd.uniform(0, 2 * math.pi); rr = r * rnd.uniform(0.45, 0.7) * (1 - k * 0.18)
            circ(x - off * 0.7 + math.cos(a) * r * 0.35, z + off * 0.7 + math.sin(a) * r * 0.35, rr, fill=c)
rnd = random.Random(3)
def R(asset, s): return FOOT[asset] * s / 2  # radio real en planta
for kind, asset, x, z, s in DECOR:
    if kind == "weed_water":
        r = R(asset, s)
        for k in range(4):
            a = rnd.uniform(0, 2 * math.pi); circ(x + math.cos(a) * r * 0.4, z + math.sin(a) * r * 0.4, r * 0.45, fill=(30, 90, 70, 150))
for kind, asset, x, z, s in DECOR:
    r = R(asset, s)
    if kind == "canopy":
        circ(x + r * 0.25, z - r * 0.3, r * 0.95, fill=(20, 40, 18, 110))
        blob(x, z, r, [(40, 74, 36), (58, 100, 46), (82, 128, 58), (112, 156, 76)], rnd)
        if asset.endswith("ooinu_l.mod"):
            for k in range(5):
                a = rnd.uniform(0, 2 * math.pi); circ(x + math.cos(a) * r * 0.5, z + math.sin(a) * r * 0.5, r * 0.09, fill=(150, 170, 235))
    elif kind in ("boulder", "stone", "stone_dark"):
        base = (92, 80, 74) if kind == "stone_dark" else ((118, 120, 104) if kind == "boulder" else (140, 136, 124))
        circ(x + r * 0.3, z - r * 0.35, r, fill=(20, 20, 20, 90))
        circ(x, z, r, fill=base)
        circ(x - r * 0.25, z + r * 0.25, r * 0.55, fill=tuple(min(255, c + 30) for c in base))
        if kind == "boulder": circ(x - r * 0.1, z + r * 0.45, r * 0.5, fill=(78, 116, 58, 200))
    elif kind == "stump":
        circ(x + r * 0.2, z - r * 0.25, r, fill=(20, 20, 10, 120))
        for k, c in enumerate([(92, 62, 36), (128, 88, 52), (150, 110, 70), (128, 88, 52), (110, 76, 44)]):
            circ(x, z, r * (1 - k * 0.2), fill=c)
    elif kind == "piece_log":
        ang = rnd.uniform(0, math.pi); dx, dz = math.cos(ang) * r, math.sin(ang) * r
        d.line([P(x - dx, z - dz), P(x + dx, z + dz)], fill=(70, 48, 28), width=int(L(70)))
        d.line([P(x - dx, z - dz), P(x + dx, z + dz)], fill=(120, 84, 50), width=int(L(44)))
    elif kind == "piece_fern":
        for k in range(7):
            a = k * 2 * math.pi / 7 + rnd.uniform(-0.2, 0.2)
            d.line([P(x, z), P(x + math.cos(a) * r, z + math.sin(a) * r)], fill=(48, 110, 52), width=int(L(26)))
    elif kind in ("tuft", "tuft_dry"):
        c = (70, 120, 50) if kind == "tuft" else (150, 140, 70); l = 8 * s
        for k in range(5):
            a = -math.pi / 2 + (k - 2) * 0.35
            d.line([P(x, z), P(x + math.cos(a) * l * 0.5, z - math.sin(a) * l)], fill=c, width=2 * SS)
    elif kind in ("flower_y", "flower_b"):
        c = (250, 214, 60) if kind == "flower_y" else (150, 180, 250)
        circ(x, z, r * 0.45, fill=c); circ(x, z, r * 0.15, fill=(255, 250, 220))
    elif kind == "clover":
        for k in range(3):
            a = k * 2 * math.pi / 3; circ(x + math.cos(a) * r * 0.3, z + math.sin(a) * r * 0.3, r * 0.3, fill=(60, 130, 60))
    elif kind == "reed":
        for k in range(5):
            ox = (k - 2) * r * 0.25; d.line([P(x + ox, z), P(x + ox * 1.6, z + r * 0.9)], fill=(60, 110, 60), width=2 * SS)
    elif kind == "mushroom":
        circ(x, z, r * 0.6, fill=(200, 60, 50)); circ(x - r * 0.2, z + r * 0.2, r * 0.14, fill=(255, 240, 230)); circ(x + r * 0.25, z - r * 0.1, r * 0.12, fill=(255, 240, 230))

# Rejilla suave (cada 400) y coordenadas.
grid = Image.new("RGBA", img.size, (0, 0, 0, 0)); gd = ImageDraw.Draw(grid)
for gx in range(BOUNDS[0], BOUNDS[2] + 1, 200):
    gd.line([P(gx, BOUNDS[3]), P(gx, BOUNDS[1])], fill=(255, 255, 255, 110 if gx == 0 else (42 if gx % 400 == 0 else 18)), width=SS)
for gz in range(BOUNDS[1], BOUNDS[3] + 1, 200):
    gd.line([P(BOUNDS[0], gz), P(BOUNDS[2], gz)], fill=(255, 255, 255, 110 if gz == 0 else (42 if gz % 400 == 0 else 18)), width=SS)
img = Image.alpha_composite(img.convert("RGBA"), grid).convert("RGB"); d = ImageDraw.Draw(img, "RGBA")
for gx in range(BOUNDS[0], BOUNDS[2] + 1, 400):
    a, b = P(gx, BOUNDS[3]), P(gx, BOUNDS[1])
    text((a[0], a[1] - 8 * SS), str(gx), 13, (200, 200, 200), anchor="md", outline=False)
    text((b[0], b[1] + 8 * SS), str(gx), 13, (200, 200, 200), anchor="ma", outline=False)
for gz in range(BOUNDS[1], BOUNDS[3] + 1, 400):
    a = P(BOUNDS[0], gz); text((a[0] - 8 * SS, a[1]), str(gz), 13, (200, 200, 200), anchor="rm", outline=False)

# Nombres de zona.
for name, x, z, sub in [("CLARO DEL ATERRIZAJE", 380, -1480, "Y 0 · zona segura"), ("CRESTA DE FUEGO", 1560, -650, "Y 40 · rojos"),
                        ("TERRAZAS AMARILLAS", -1520, -700, "Y 40 · amarillos"), ("TERRAZA ALTA", -1325, -400, "Y 80"),
                        ("LAGO DEL ECO", -1150, 1520, "Y 40 · agua (Y 36): azules"), ("MESETA FINAL", 1620, 1560, "Y 150 · todos"),
                        ("ARROYO", 350, 430, "hondo"), ("RINCÓN SE", 1450, -1290, ""), ("RINCÓN SECRETO", -1720, -1210, ""),
                        ("CANTERA", -1550, 620, "cresta de roca"), ("TOCÓN", 0, -260, "")]:
    wtext(x, z, name, size=16, bold=True, anchor="mm", col=(255, 250, 225))
    if sub: wtext(x, z - 52, sub, size=12, anchor="mm", col=(235, 235, 220))

def dashed(pts, col, w, dash=26, gap=16):
    for (x0, z0), (x1, z1) in zip(pts, pts[1:]):
        a, b = P(x0, z0), P(x1, z1); n = math.hypot(b[0] - a[0], b[1] - a[1]); t = 0.0
        while t < n:
            t1 = min(n, t + dash * SS)
            d.line([(a[0] + (b[0] - a[0]) * t / n, a[1] + (b[1] - a[1]) * t / n), (a[0] + (b[0] - a[0]) * t1 / n, a[1] + (b[1] - a[1]) * t1 / n)], fill=col, width=int(w * SS))
            t = t1 + gap * SS
def arrow_head(a, b, col, size=16):
    (x0, y0), (x1, y1) = P(*a), P(*b); ang = math.atan2(y1 - y0, x1 - x0); s = size * SS
    d.polygon([(x1, y1), (x1 - s * math.cos(ang - 0.45), y1 - s * math.sin(ang - 0.45)), (x1 - s * math.cos(ang + 0.45), y1 - s * math.sin(ang + 0.45))], fill=col)

ROUTE = [[(0, -1150), (-550, -1050)],
         [(300, -1000), (820, -740), (1150, -560), (1325, -350), (1325, -50), (1500, 50), (1800, 330), (1650, 520)],
         [(-300, -950), (-820, -740), (-1000, -350), (-1000, 100), (-1450, 200), (-1700, 330), (-1850, 450)],
         [(-1800, 520), (-1730, 820), (-1250, 700), (-560, 660), (-350, 740), (-350, 1060)],
         [(-200, 700), (380, 780), (790, 900), (1240, 930), (1350, 1000), (1650, 1250)]]
ROUTE_LABELS = ["1 Claro", "2 Rojos: cresta", "3 Amarillos: terrazas", "4 Azules: lago", "5 Meseta final"]
SHORTCUTS = [[(0, 600), (0, 250), (0, -650)], [(1450, 860), (1450, 640), (1350, 200)]]
for r in ROUTE: dashed(r, (255, 255, 255, 230), 3); arrow_head(r[-2], r[-1], (255, 255, 255, 230), 14)
for r in SHORTCUTS: dashed(r, (120, 255, 200, 240), 4, 16, 12); arrow_head(r[-2], r[-1], (120, 255, 200), 18)
for i, (x, z) in enumerate([(-250, -1080), (1000, -560), (-900, -560), (-1960, 700), (1000, 1040)]):
    a, b = P(x, z); r = 17 * SS
    d.ellipse([a - r, b - r, a + r, b + r], fill=(255, 255, 255), outline=(30, 30, 30), width=SS * 2)
    text((a, b), str(i + 1), 17, (30, 30, 30), bold=True, anchor="mm", outline=False)

for tid, x, z, r, cond, act in TRIGGERS:
    if x is None: continue
    a, b = P(x, z); rr = L(r)
    for k in range(0, 360, 20): d.arc([a - rr, b - rr, a + rr, b + rr], k, k + 11, fill=(210, 140, 255), width=SS * 3)
    text((a, b - rr - 12 * SS), tid, 14, (220, 165, 255), bold=True, anchor="md")

PCOL = {"roja": (226, 70, 60), "amarilla": (240, 210, 60), "azul": (70, 120, 235), "": (255, 255, 255)}
for kind, color, x, z in PELLETS:
    a, b = P(x, z)
    if kind == "posy":
        for k in range(5):
            an = k * 2 * math.pi / 5; r = 9 * SS; cx, cy = a + math.cos(an) * r, b + math.sin(an) * r
            d.ellipse([cx - 7 * SS, cy - 7 * SS, cx + 7 * SS, cy + 7 * SS], fill=(250, 150, 200), outline=(120, 40, 80))
        d.ellipse([a - 6 * SS, b - 6 * SS, a + 6 * SS, b + 6 * SS], fill=(255, 230, 90))
    else:
        r = (11 if kind == "1" else 15) * SS
        d.ellipse([a - r, b - r, a + r, b + r], fill=PCOL[color], outline=(255, 255, 255), width=SS * 2)
        text((a, b), kind, 12, (20, 20, 20), bold=True, anchor="mm", outline=False)

for i, (kind, name, x, z, note) in enumerate(OBJECTS):
    a, b = P(x, z); s = 15 * SS
    col = {"key": (255, 215, 0), "nectar": (255, 170, 60), "gate_bomb": (40, 40, 40)}.get(kind, (80, 100, 135))
    d.rectangle([a - s, b - s, a + s, b + s], fill=col, outline=(255, 255, 255), width=SS * 2)
    text((a, b), f"O{i + 1}", 11, (255, 255, 255), bold=True, anchor="mm")

for i, (kind, name, x, z, zone, note) in enumerate(ENEMIES):
    a, b = P(x, z)
    boss = kind == "kumo"; friendly = kind in ("qurione", "mizigen", "kogane", "otama")
    if kind == "hibaA":
        s = 15 * SS; d.polygon([(a, b - s), (a + s, b + s * 0.8), (a - s, b + s * 0.8)], fill=(255, 120, 30), outline=(255, 255, 255))
    else:
        r = (30 if boss else 14) * SS
        col = (150, 20, 30) if boss else ((200, 140, 60) if friendly else (215, 50, 50))
        d.ellipse([a - r, b - r, a + r, b + r], fill=col, outline=(255, 255, 255), width=SS * 2)
    text((a, b), f"E{i + 1}", 13 if boss else 10, (255, 255, 255), bold=True, anchor="mm")

OCOL = {"roja": (226, 70, 60), "amarilla": (240, 210, 60), "azul": (70, 120, 235)}
for o in STORY["onions"]:
    a, b = P(o["x"], o["z"]); r = 32 * SS
    if o["buried"]:
        for k in range(0, 360, 30): d.arc([a - r, b - r, a + r, b + r], k, k + 18, fill=OCOL[o["color"]], width=SS * 6)
        text((a, b + r + 6 * SS), "enterrada", 12, OCOL[o["color"]], anchor="ma")
    else:
        d.ellipse([a - r, b - r, a + r, b + r], fill=OCOL[o["color"]], outline=(255, 255, 255), width=SS * 3)
    text((a, b), "C", 18, (255, 255, 255), bold=True, anchor="mm")
sx, sy = P(STORY["ship"]["x"], STORY["ship"]["z"]); s = 40 * SS
d.polygon([(sx, sy - s), (sx + s, sy + s * 0.7), (sx - s, sy + s * 0.7)], fill=(235, 235, 240), outline=(40, 40, 50))
text((sx, sy + 5 * SS), "NAVE", 12, (30, 30, 40), bold=True, anchor="mm", outline=False)
cxp, cyp = P(STORY["captain"]["x"], STORY["captain"]["z"]); r = 18 * SS
d.regular_polygon((cxp, cyp, r), 5, fill=(255, 255, 255), outline=(30, 30, 30))
text((cxp + r + 4 * SS, cyp), "Capitán", 12, anchor="lm")
for pid, model, x, z, pk, where in PARTS:
    a, b = P(x, z); r = (24 if pid == "P10" else 19) * SS
    d.regular_polygon((a, b, r), 6, fill=(255, 196, 40), outline=(90, 60, 0))
    text((a, b), pid, 13 if pid != "P10" else 14, (60, 35, 0), bold=True, anchor="mm", outline=False)
    text((a, b + r + 3 * SS), pk, 11, (255, 230, 150), anchor="ma")

nx, ny = P(BOUNDS[2] - 160, BOUNDS[3] - 190)
d.polygon([(nx, ny - 50 * SS), (nx + 16 * SS, ny), (nx - 16 * SS, ny)], fill=(255, 255, 255))
d.polygon([(nx, ny + 50 * SS), (nx + 16 * SS, ny), (nx - 16 * SS, ny)], fill=(120, 120, 120))
text((nx, ny - 56 * SS), "N  (+Z)", 14, bold=True, anchor="md")
bx, by = ML + MAPW - L(500), 84 * SS
d.rectangle([bx, by - 5 * SS, bx + L(500), by + 5 * SS], fill=(255, 255, 255))
text((bx + L(250), by - 10 * SS), "500 unidades", 13, anchor="md", outline=False)

text((ML, 30 * SS), "HONDONADA DEL ROCÍO — guía de construcción (plantilla Solo)", 30, (255, 240, 190), bold=True, outline=False)
text((ML, 78 * SS), f"Objetivo: {RULES['target']} de {len(PARTS)} piezas en {RULES['days']} día · 20 rojos · medallas "
     f"{RULES['medals'][0]}/{RULES['medals'][1]}/{RULES['medals'][2]} · mapa {BOUNDS[2]-BOUNDS[0]} × {BOUNDS[3]-BOUNDS[1]} · X → este, Z ↑ norte · rejilla 200",
     16, (210, 210, 200), outline=False)

# ── Leyenda ──────────────────────────────────────────────────────────────────
class Col:
    def __init__(self, x, y, w): self.x, self.y, self.w = x, y, w
    def h(self, s): self.y += 10 * SS; text((self.x, self.y), s, 18, (255, 225, 140), bold=True, outline=False); self.y += 28 * SS
    def l(self, s, col=(225, 225, 220), size=13.5):
        f = font(size); line = ""
        for w_ in s.split(" "):
            t = (line + " " + w_).strip()
            if d.textlength(t, font=f) > self.w and line:
                d.text((self.x, self.y), line, font=f, fill=col); self.y += int(size * 1.45 * SS); line = "    " + w_
            else: line = t
        d.text((self.x, self.y), line, font=f, fill=col); self.y += int(size * 1.45 * SS)

cA = Col(PANEL_X, MT, 700 * SS); cB = Col(PANEL_X + 750 * SS, MT, 700 * SS)
cA.h("Cómo se ve natural")
for s in ["Nada de muros rectos: todo el contorno es terreno. Cada zona se excava en un relieve alto con bordes irregulares (ruido de 15 a 55 unidades) y acantilados de 30 a 70 de ancho.",
          "El borde del mapa no se ve: laderas que suben de Y 330 a Y 660 hacia fuera, con vegetación gigante encima (hutaba ×3–5, wakame_l, ine, ooinu_l) y raíces y ramas de los niveles del juego asomando al filo.",
          "La cantera no tiene muros: es una cresta de roca curva (Y 130) con un hueco para la roca negra.",
          "Caminos de tierra pintados por donde se pasa; arena en las orillas; roca en los acantilados (rojiza en la cresta, gris en la meseta).",
          "Hito: el tocón grande de los niveles del juego (×1,25) en una loma del claro, visible desde casi todo el mapa.",
          f"Decoración: {len(DECOR)} objetos del juego (tope del editor: 1500; vigilar el tope de vértices)."]:
    cA.l("• " + s)
cA.h("Símbolos")
for s in ["Hexágono dorado P# = pieza (debajo, Pikmin mín–máx) · Cuadrado O# = objeto", "Círculo E# = enemigo (rojo malo, naranja inofensivo/útil, granate grande = jefe) · triángulo = géiser",
          "C = cebolla (discontinua = enterrada) · 1/5 = pastilla · flor rosa = flor de pastillas", "Lila discontinuo T# = disparador · blanco = recorrido 1→5 · verde = atajo",
          "Copas verdes = plantas gigantes del borde · marrón = raíces y ramas · estrella verde = helecho · gris = rocas · amarillo/azul = flores · rojo = setas · trazos = hierba y juncos"]:
    cA.l("• " + s)
cA.h("Alturas")
cA.l("• Claro Y 0 · cresta, terrazas, orilla e isla Y 40 · terraza alta Y 80 · meseta Y 150 · borde Y 330–660")
cA.l("• Agua a Y 36: todas sus orillas están a Y 40 (el claro sube en cuesta suave hasta la ribera del arroyo). El juego hunde el fondo 25 bajo el agua.")
cA.l("• Rampas de terreno: claro→cresta y claro→terrazas (Y 0→40), orilla→meseta (Y 40→150), ribera del arroyo (Y 15→40)")
cA.h("Misión")
cA.l(f"• Nave ({STORY['ship']['x']}, {STORY['ship']['z']}) · Capitán ({STORY['captain']['x']}, {STORY['captain']['z']})")
for o in STORY["onions"]: cA.l(f"• Cebolla {o['color']} ({o['x']}, {o['z']})" + (" — enterrada" if o["buried"] else " — activa"))
cA.l(f"• Objetivo {RULES['target']} piezas · {RULES['days']} día · 20 rojos · medallas {RULES['medals']} · música {RULES['music']} · {RULES['baseStage']}")
cA.h("Piezas")
for pid, model, x, z, pk, where in PARTS: cA.l(f"{pid} {model} ({x}, {z}) {pk} — {where}")
cA.h("Objetos")
for i, (kind, name, x, z, note) in enumerate(OBJECTS): cA.l(f"O{i + 1} {name} [{kind}] ({x}, {z}) — {note}")

cB.h(f"Enemigos ({len(ENEMIES)})")
zone = None
for i, (kind, name, x, z, zn, note) in enumerate(ENEMIES):
    if zn != zone: zone = zn; cB.l(zn.upper(), (170, 210, 255), 13)
    cB.l(f"E{i + 1} {name} [{kind}] ({x}, {z})" + (f" — {note}" if note else ""))
cB.h("Pastillas")
cB.l(" · ".join(("flor" if k == "posy" else f"{k} {c}") + f" ({x}, {z})" for k, c, x, z in PELLETS))
cB.h("Disparadores")
for tid, x, z, r, cond, act in TRIGGERS:
    cB.l(f"{tid}: {cond}" + (f" zona ({x}, {z}) r{r}" if x is not None else "") + f" → {act}")
cB.h("Recorrido")
for s in ROUTE_LABELS: cB.l("• " + s)
cB.l("• Atajos: puente del arroyo (orilla → claro) y salto del borde sur de la meseta a la cresta (solo de bajada: 110 de alto).")
cB.l("• El capitán cruza el agua, pero solo los azules le siguen: hacen el puente del arroyo desde el norte y entonces sube todo el ejército del claro.")
cB.l("• Medidas reales: puente 180 (tablero a Y+10), puente largo 360, rampa 130 de largo y 40 de desnivel, bloque 124 × 188; el terreno está hecho a esas medidas.")
if PROBLEMS:
    cB.h("A revisar"); [cB.l("• " + p, (255, 150, 140)) for p in PROBLEMS]

img = img.resize((W // SS, Hh // SS), Image.LANCZOS)
img.save(os.path.join(HERE, "guia.png"), optimize=True)

# ── spec.json: alturas a celda 50 como el editor ─────────────────────────────
STEP = 50 // CELL
spec = dict(name="Hondonada del Rocío", modes=["solo"], bounds=BOUNDS, cell=50, waterLevel=WATER_LEVEL, rules=RULES,
            heights=[[round(H[j][i]) for i in range(0, NX, STEP)] for j in range(0, NZ, STEP)],
            water=[[1 if WET[j][i] else 0 for i in range(0, NX, STEP)] for j in range(0, NZ, STEP)],
            material=[[T[j][i] for i in range(0, NX, STEP)] for j in range(0, NZ, STEP)], materialNames=TAG_NAMES,
            ops=[dict(name=n, tag=TAG_NAMES[t], target=tg, soft=s, amp=a) for n, t, _, tg, s, a in OPS],
            paths=[dict(points=p, width=w) for p, w in PATHS],
            decor=[dict(kind=k, asset=a, x=x, z=z, scale=s) for k, a, x, z, s in DECOR],
            story=STORY,
            parts=[dict(id=p, model=m, x=x, z=z, pikmin=k, where=w) for p, m, x, z, k, w in PARTS],
            enemies=[dict(type=t, name=n, x=x, z=z, zone=zn, note=no) for t, n, x, z, zn, no in ENEMIES],
            objects=[dict(kind=k, name=n, x=x, z=z, note=no) for k, n, x, z, no in OBJECTS],
            pellets=[dict(kind=k, color=c, x=x, z=z) for k, c, x, z in PELLETS],
            triggers=[dict(id=i, x=x, z=z, radius=r, condition=c, action=a) for i, x, z, r, c, a in TRIGGERS])
with open(os.path.join(HERE, "spec.json"), "w", encoding="utf-8") as f:
    json.dump(spec, f, ensure_ascii=False, separators=(",", ":"))
# ── Datos para la plantilla del editor (tools/mapmaker/src/dew_hollow_data.inc) ─
import base64, struct
EC = 25                                   # celda del terreno en el juego
ENX, ENZ = (BOUNDS[2] - BOUNDS[0]) // EC, (BOUNDS[3] - BOUNDS[1]) // EC
MATS = ["forest", "grass", "moss", "lush", "orange", "sand", "pebble", "stones", "dirt", "rock"]
TAG_MAT = {RIM: 0, HUB: 1, SECRET: 2, SHORE: 3, CREST: 4, TERR: 5, TERRHI: 6, MESA: 7, RAMP: 8, WATER: 5, RIDGE: 9, STUMP: 1}
EH = [[0.0] * (ENX + 1) for _ in range(ENZ + 1)]; ET = [[0] * (ENX + 1) for _ in range(ENZ + 1)]; EW = [[0.0] * (ENX + 1) for _ in range(ENZ + 1)]
for j in range(ENZ + 1):
    for i in range(ENX + 1):
        EH[j][i], ET[j][i], EW[j][i] = terrain(BOUNDS[0] + i * EC, BOUNDS[1] + j * EC)
def eh(i, j): return EH[max(0, min(ENZ, j))][max(0, min(ENX, i))]
heights, mats, water = bytearray(), [], []
for j in range(ENZ + 1):
    for i in range(ENX + 1):
        h, t = EH[j][i], ET[j][i]; x, z = BOUNDS[0] + i * EC, BOUNDS[1] + j * EC
        heights += struct.pack("<h", max(-2000, min(2000, round(h))))
        g = math.hypot((eh(i + 1, j) - eh(i - 1, j)) / (2 * EC), (eh(i, j + 1) - eh(i, j - 1)) / (2 * EC))
        ci, cj = cell(x, z); m = TAG_MAT[t]
        if t not in (WATER, RIM):
            if WALK[cj][ci] and D_WET[cj][ci] <= 3: m = 5                 # arena en la orilla
            if WALK[cj][ci] and path_at(ci, cj) > 0.5: m = 8              # caminos de tierra
        if g > 0.85 and t != WATER: m = 9                                  # acantilados de roca
        mats.append(m)
        water.append(int(round(255 * max(0.0, min(1.0, 0.5 + (EW[j][i] - 0.5) * 3)))))
# Nivel del agua que pondrá el juego: la orilla más baja (puntos secos junto al agua).
_shore = []
for j in range(ENZ + 1):
    for i in range(ENX + 1):
        if water[j * (ENX + 1) + i] < 128: continue
        for a_, b_ in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ii, jj = i + a_, j + b_
            if 0 <= ii <= ENX and 0 <= jj <= ENZ: _shore.append(EH[jj][ii])  # como el juego: sus puntos y los vecinos
print("agua: el juego la pondrá a %.0f (punto más bajo %.0f menos 4)" % (min(_shore) - 4, min(_shore)))
def rle(vals):
    out = bytearray(); k = 0
    while k < len(vals):
        n = 1
        while k + n < len(vals) and vals[k + n] == vals[k] and n < 255: n += 1
        out += bytes((vals[k], n)); k += n
    return out
def chunks(b):
    t = base64.b64encode(bytes(b)).decode(); return ",\n".join('\t"%s"' % t[k:k + 2000] for k in range(0, len(t), 2000))
drng = random.Random(11)
lines = []
for kind, asset, x, z, sc in DECOR:
    h, _, _ = terrain(x, z)
    if kind == "weed_water": h -= 25   # al fondo (el juego lo hunde 25 bajo el agua)
    lines.append('\t{ "%s", %d, %d, %d, %d, %.2ff, %s },' % (asset, x, round(h) - 2, z, drng.randrange(360), sc, "true" if kind in COLLIDE else "false"))
inc = f"""// Generado por games/pikmin1/docs/plantillas/hondonada_rocio/make_guide.py: no editar a mano.
// Plantilla «Hondonada del Rocío»: terreno (celda {EC}), materiales, agua y decoración.
#pragma once

constexpr double kDewMinX = {BOUNDS[0]}, kDewMinZ = {BOUNDS[1]}, kDewMaxX = {BOUNDS[2]}, kDewMaxZ = {BOUNDS[3]}, kDewCell = {EC};
constexpr int kDewNX = {ENX}, kDewNZ = {ENZ};
/// Materiales, en este orden: {", ".join(MATS)}.
constexpr int kDewMatCount = {len(MATS)};
/// Alturas por punto (int16, little endian), en base64.
const char* const kDewHeights[] = {{
{chunks(heights)}
}};
/// Material por punto, en tramos (valor, repeticiones), en base64.
const char* const kDewMats[] = {{
{chunks(rle(mats))}
}};
/// Agua por punto (0-255), en tramos (valor, repeticiones), en base64.
const char* const kDewWater[] = {{
{chunks(rle(water))}
}};
struct DewDecor {{
	const char* asset; ///< ruta del juego o "piece:<id>" (officialPieces)
	int x, y, z, ry;
	float scale;
	bool collide;
}};
const DewDecor kDewDecor[] = {{
{chr(10).join(lines)}
}};
"""
with open(os.path.join(HERE, "..", "..", "..", "..", "..", "tools", "mapmaker", "src", "dew_hollow_data.inc"), "w", encoding="utf-8") as f:
    f.write(inc)
print("ok", W // SS, Hh // SS, "decor", len(DECOR))
for p in PROBLEMS: print("REVISAR:", p)

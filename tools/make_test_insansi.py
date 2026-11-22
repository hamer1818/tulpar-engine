#!/usr/bin/env python3
"""A6 yeniden hedefleme test karakteri (2026-11-16): 21 eklemli insansi iskelet, gomulu .gltf.

  python3 tools/make_test_insansi.py <mixamo|unreal|bozuk> <cikti.gltf> [--eksen-kaydir] [--klip] [--a-poz]

Iskelet dunya DINLENME konumlari her secenekte AYNI (Y yukari, metre; kalca 1 m). Adlandirma:
  mixamo : mixamorig:Hips, mixamorig:LeftArm, mixamorig:LeftForeArm, mixamorig:LeftUpLeg ...
  unreal : pelvis, spine_01, upperarm_l, lowerarm_l, thigh_l, calf_l, ball_l ...
  bozuk  : bone_000 ... (esleme bulunamaz: yeniden hedefleme REDDEDILMELI — kapinin kontrolu)
--eksen-kaydir: her eklemin YEREL dinlenme donusu farkli (Rz(25*k) * Rx(15*k)); dunya konumlari ayni kalsin diye yerel oteleme
  ebeveynin dunya donusuyle cozulur. Yani kemik eksen gelenegi kaynaktan FARKLI — yerel donusu kopyalayan saf yontem burada bozulur.
--klip: "Selam" klibi (1 s, 3 ornek): sol ust kol dunya Z etrafinda 0 -> 90 derece (kol yukari), sag on kol dunya X etrafinda 0 -> -90,
  kalca z 0 -> 1 m. Klip yalniz dinlenme donusleri BIRIM olan iskelette (eksen kaydirmasiz) yazilir: yerel donus = dunya farki.
--a-poz (2026-11-22): kollar A duruşunda — ust kol / on kol / el dunya konumlari omuz eklemi (upper_arm) etrafinda dunya Z'de
  40 derece ASAGI dondurulur (sol -40, sag +40). Dinlenme pozu kaynaktan (T) FARKLI: T<->A dinlenme farki duzeltmesinin kapisi.
  tests/assets/insansi_unreal_a.gltf = `unreal ... --eksen-kaydir --a-poz`.
Her eklem bir ucgenle (agirlik 1) cizilir: mesh yalniz skin'i gecerli kilmak icin.
"""
import base64
import json
import math
import struct
import sys

# (kanonik, ebeveyn, dunya konumu)
ISKELET = [
    ("hips", None, (0.0, 1.0, 0.0)),
    ("spine", "hips", (0.0, 1.1, 0.0)),
    ("chest", "spine", (0.0, 1.3, 0.0)),
    ("neck", "chest", (0.0, 1.5, 0.0)),
    ("head", "neck", (0.0, 1.6, 0.0)),
]
for yan, sx in (("l", 1.0), ("r", -1.0)):
    ISKELET += [
        ("shoulder_" + yan, "chest", (0.05 * sx, 1.45, 0.0)),
        ("upper_arm_" + yan, "shoulder_" + yan, (0.2 * sx, 1.45, 0.0)),
        ("lower_arm_" + yan, "upper_arm_" + yan, (0.5 * sx, 1.45, 0.0)),
        ("hand_" + yan, "lower_arm_" + yan, (0.75 * sx, 1.45, 0.0)),
        ("upper_leg_" + yan, "hips", (0.1 * sx, 0.95, 0.0)),
        ("lower_leg_" + yan, "upper_leg_" + yan, (0.1 * sx, 0.5, 0.0)),
        ("foot_" + yan, "lower_leg_" + yan, (0.1 * sx, 0.08, 0.0)),
        ("toes_" + yan, "foot_" + yan, (0.1 * sx, 0.02, 0.12)),
    ]

MIXAMO = {"hips": "Hips", "spine": "Spine", "chest": "Spine2", "neck": "Neck", "head": "Head"}
UNREAL = {"hips": "pelvis", "spine": "spine_01", "chest": "spine_03", "neck": "neck_01", "head": "head"}
for yan, uz in (("l", "Left"), ("r", "Right")):
    MIXAMO.update({"shoulder_" + yan: uz + "Shoulder", "upper_arm_" + yan: uz + "Arm", "lower_arm_" + yan: uz + "ForeArm",
                   "hand_" + yan: uz + "Hand", "upper_leg_" + yan: uz + "UpLeg", "lower_leg_" + yan: uz + "Leg",
                   "foot_" + yan: uz + "Foot", "toes_" + yan: uz + "ToeBase"})
    UNREAL.update({"shoulder_" + yan: "clavicle_" + yan, "upper_arm_" + yan: "upperarm_" + yan, "lower_arm_" + yan: "lowerarm_" + yan,
                   "hand_" + yan: "hand_" + yan, "upper_leg_" + yan: "thigh_" + yan, "lower_leg_" + yan: "calf_" + yan,
                   "foot_" + yan: "foot_" + yan, "toes_" + yan: "ball_" + yan})


def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def q_conj(q):
    return (-q[0], -q[1], -q[2], q[3])


def q_rot(q, v):
    p = q_mul(q_mul(q, (v[0], v[1], v[2], 0.0)), q_conj(q))
    return (p[0], p[1], p[2])


def q_axis(ax, deg):
    h = math.radians(deg) / 2
    s = math.sin(h)
    return (ax[0] * s, ax[1] * s, ax[2] * s, math.cos(h))


def mat_inv_rt(q, t):
    # (R, t) dunya -> ters: R^T, -R^T t ; sutun-major 16 float
    c = q_conj(q)
    it = q_rot(c, (-t[0], -t[1], -t[2]))
    ex, ey, ez = q_rot(c, (1, 0, 0)), q_rot(c, (0, 1, 0)), q_rot(c, (0, 0, 1))
    return [ex[0], ex[1], ex[2], 0, ey[0], ey[1], ey[2], 0, ez[0], ez[1], ez[2], 0, it[0], it[1], it[2], 1]


def uret(adlar, kaydir, klip):
    idx = {k: i for i, (k, _, _) in enumerate(ISKELET)}
    n = len(ISKELET)
    yerel_r = []
    for k in range(n):
        yerel_r.append(q_mul(q_axis((0, 0, 1), 25.0 * (k % 7)), q_axis((1, 0, 0), 15.0 * (k % 5))) if kaydir else (0.0, 0.0, 0.0, 1.0))
    dunya_r = [None] * n
    for i, (k, ebeveyn, _) in enumerate(ISKELET):
        dunya_r[i] = yerel_r[i] if ebeveyn is None else q_mul(dunya_r[idx[ebeveyn]], yerel_r[i])
    dugumler = []
    for i, (k, ebeveyn, p) in enumerate(ISKELET):
        if ebeveyn is None:
            t = p
        else:
            pp = ISKELET[idx[ebeveyn]][2]
            t = q_rot(q_conj(dunya_r[idx[ebeveyn]]), (p[0] - pp[0], p[1] - pp[1], p[2] - pp[2]))
        if adlar == "mixamo":
            ad = "mixamorig:" + MIXAMO[k]
        elif adlar == "unreal":
            ad = UNREAL[k]
        else:
            ad = "bone_%03d" % i
        d = {"name": ad, "translation": [round(v, 6) for v in t], "rotation": [round(v, 7) for v in yerel_r[i]]}
        cocuk = [j for j, (_, e2, _) in enumerate(ISKELET) if e2 == k]
        if cocuk:
            d["children"] = cocuk
        dugumler.append(d)
    buf = bytearray()

    def ekle(b, hedef=None):
        while len(buf) % 4:
            buf.append(0)
        o = len(buf)
        buf.extend(b)
        return o, len(b)

    gorunumler, erisimciler = [], []

    def erisimci(veri, tur, bilesen, sayi, minmax=None):
        o, uz = ekle(veri)
        gorunumler.append({"buffer": 0, "byteOffset": o, "byteLength": uz})
        a = {"bufferView": len(gorunumler) - 1, "componentType": bilesen, "count": sayi, "type": tur}
        if minmax:
            a["min"], a["max"] = minmax
        erisimciler.append(a)
        return len(erisimciler) - 1

    # mesh: eklem basina kucuk ucgen (dunya konumunda), agirlik 1
    poz, eklem, agirlik = [], [], []
    for i, (_, _, p) in enumerate(ISKELET):
        for dx, dy in ((0.0, 0.0), (0.03, 0.0), (0.0, 0.03)):
            poz.append((p[0] + dx, p[1] + dy, p[2]))
            eklem.append((i, 0, 0, 0))
            agirlik.append((1.0, 0.0, 0.0, 0.0))
    mn = [min(v[c] for v in poz) for c in range(3)]
    mx = [max(v[c] for v in poz) for c in range(3)]
    a_poz = erisimci(b"".join(struct.pack("<3f", *v) for v in poz), "VEC3", 5126, len(poz), (mn, mx))
    a_ek = erisimci(b"".join(struct.pack("<4H", *v) for v in eklem), "VEC4", 5123, len(eklem))
    a_ag = erisimci(b"".join(struct.pack("<4f", *v) for v in agirlik), "VEC4", 5126, len(agirlik))
    ibm = b""
    for i, (_, _, p) in enumerate(ISKELET):
        ibm += struct.pack("<16f", *mat_inv_rt(dunya_r[i], p))
    a_ibm = erisimci(ibm, "MAT4", 5126, n)
    mesh_dugum = len(dugumler)
    dugumler.append({"name": "govde", "mesh": 0, "skin": 0})
    gltf = {
        "asset": {"version": "2.0", "generator": "tulpar make_test_insansi.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, mesh_dugum]}],
        "nodes": dugumler,
        "meshes": [{"primitives": [{"attributes": {"POSITION": a_poz, "JOINTS_0": a_ek, "WEIGHTS_0": a_ag}}]}],
        "skins": [{"joints": list(range(n)), "inverseBindMatrices": a_ibm, "skeleton": 0}],
    }
    if klip:
        assert not kaydir, "klip yalniz birim dinlenme donuslu iskelette (yerel = dunya farki)"
        zaman = [0.0, 0.5, 1.0]
        a_t = erisimci(struct.pack("<3f", *zaman), "SCALAR", 5126, 3, ([0.0], [1.0]))
        kanallar, ornekleyiciler = [], []

        def iz(dugum, yol, degerler, tur):
            a_v = erisimci(b"".join(struct.pack("<%df" % len(v), *v) for v in degerler), tur, 5126, len(degerler))
            ornekleyiciler.append({"input": a_t, "output": a_v, "interpolation": "LINEAR"})
            kanallar.append({"sampler": len(ornekleyiciler) - 1, "target": {"node": dugum, "path": yol}})

        iz(idx["upper_arm_l"], "rotation", [q_axis((0, 0, 1), a) for a in (0.0, 45.0, 90.0)], "VEC4")
        iz(idx["lower_arm_r"], "rotation", [q_axis((1, 0, 0), a) for a in (0.0, -45.0, -90.0)], "VEC4")
        iz(idx["hips"], "translation", [(0.0, 1.0, z) for z in (0.0, 0.5, 1.0)], "VEC3")
        gltf["animations"] = [{"name": "Selam", "channels": kanallar, "samplers": ornekleyiciler}]
    gltf["buffers"] = [{"byteLength": len(buf), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(buf)).decode()}]
    gltf["bufferViews"] = gorunumler
    gltf["accessors"] = erisimciler
    return gltf


if __name__ == "__main__":
    if len(sys.argv) < 3 or sys.argv[1] not in ("mixamo", "unreal", "bozuk"):
        print(__doc__)
        sys.exit(2)
    if "--a-poz" in sys.argv:
        for i, (k, e, p) in enumerate(ISKELET):
            if k.split("_")[0] in ("upper", "lower", "hand") and k[-2:] in ("_l", "_r") and not k.startswith("upper_leg") and not k.startswith("lower_leg"):
                yan = k[-1]
                o = [q for (a, _, q) in ISKELET if a == "upper_arm_" + yan][0]
                aci = math.radians(-40.0 if yan == "l" else 40.0)
                dx, dy = p[0] - o[0], p[1] - o[1]
                ISKELET[i] = (k, e, (o[0] + dx * math.cos(aci) - dy * math.sin(aci), o[1] + dx * math.sin(aci) + dy * math.cos(aci), p[2]))
    g = uret(sys.argv[1], "--eksen-kaydir" in sys.argv, "--klip" in sys.argv)
    with open(sys.argv[2], "w") as f:
        json.dump(g, f, separators=(",", ":"))
    print("yazildi:", sys.argv[2], len(ISKELET), "eklem")

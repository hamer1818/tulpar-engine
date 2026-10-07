#!/usr/bin/env python3
"""Kupler ile Kurelerin Savasi — govdesiz sus modelleri (glTF, tek dosya, gomulu tampon).

Neden: kopru varliklarindan `kutu`/`kure` HER ZAMAN fizik govdesi kurar ve
sabit govdeyi `isinla` her cagrida yeniden kurar (bridge/engine_api.cpp
teng_set_pos). Birligin kafasi, silahi, kalkani ve isiyan mermi/lazer ise
yalniz GORUNTU: `model_koy` (govdesiz) + `isinla` kare basina ucuz (konum
yazmak). Dokusuz, beyaz taban renk: `model_koy`'un renk tonu dogrudan rengi
verir. Isiyan modellerin emissive'i > 1 (dogrusal): parlama (bloom) esigini
asar.

Belirlenimli: ayni girdi -> ayni bayt. Yeniden uretmek:
  python3 tools/kup_kure_modelleri.py
"""
import base64, json, math, os, struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(os.path.dirname(HERE), "tulpar", "oyunlar", "kup_kure_savasi", "assets", "modeller")


def kutu(sx, sy, sz):
    faces = [((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0)),
             ((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1))]
    h = (sx * 0.5, sy * 0.5, sz * 0.5)
    pos, nrm, uv, idx = [], [], [], []
    for n, u, v in faces:
        base = len(pos)
        for (su, sv, tu, tv) in [(-1, -1, 0, 0), (1, -1, 1, 0), (1, 1, 1, 1), (-1, 1, 0, 1)]:
            p = [(n[i] + u[i] * su + v[i] * sv) * h[i] for i in range(3)]
            pos.append(p)
            nrm.append(list(n))
            uv.append([tu, tv])
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return pos, nrm, uv, idx


def kure(dilim=18, halka=10, r=0.5):
    pos, nrm, uv, idx = [], [], [], []
    for a in range(halka + 1):
        th = math.pi * a / halka
        for b in range(dilim + 1):
            ph = 2 * math.pi * b / dilim
            n = [math.sin(th) * math.cos(ph), math.cos(th), math.sin(th) * math.sin(ph)]
            n = [round(c, 6) for c in n]
            pos.append([round(c * r, 6) for c in n])
            nrm.append(n)
            uv.append([b / dilim, a / halka])
    for a in range(halka):
        for b in range(dilim):
            i0 = a * (dilim + 1) + b
            i1 = i0 + dilim + 1
            for tri in ([i0, i1, i0 + 1], [i0 + 1, i1, i1 + 1]):
                p0, p1, p2 = pos[tri[0]], pos[tri[1]], pos[tri[2]]
                if p0 == p1 or p1 == p2 or p0 == p2:
                    continue
                e1 = [p1[i] - p0[i] for i in range(3)]
                e2 = [p2[i] - p0[i] for i in range(3)]
                cr = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
                cen = [(p0[i] + p1[i] + p2[i]) / 3 for i in range(3)]
                if sum(cr[i] * cen[i] for i in range(3)) < 0:
                    tri = [tri[0], tri[2], tri[1]]
                idx += tri
    return pos, nrm, uv, idx


def yaz(ad, geo, emissive=(0, 0, 0), roughness=0.8):
    pos, nrm, uv, idx = geo
    f32 = lambda rows: b"".join(struct.pack("<%df" % len(r), *r) for r in rows)
    bpos, bnrm, buv = f32(pos), f32(nrm), f32(uv)
    bidx = struct.pack("<%dH" % len(idx), *idx)
    buf = bpos + bnrm + buv + bidx + (b"\x00" * ((4 - len(bidx) % 4) % 4))
    mn = [min(p[i] for p in pos) for i in range(3)]
    mx = [max(p[i] for p in pos) for i in range(3)]
    mat = {"name": ad, "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1], "metallicFactor": 0,
                                                 "roughnessFactor": roughness}}
    if any(emissive):
        mat["emissiveFactor"] = list(emissive)
    g = {
        "asset": {"version": "2.0", "generator": "tulpar kup_kure_modelleri.py"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": ad}],
        "meshes": [{"name": ad, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                                "indices": 3, "material": 0}]}],
        "materials": [mat],
        "buffers": [{"byteLength": len(buf), "uri": "data:application/octet-stream;base64," + base64.b64encode(buf).decode()}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": len(bpos), "target": 34962},
            {"buffer": 0, "byteOffset": len(bpos), "byteLength": len(bnrm), "target": 34962},
            {"buffer": 0, "byteOffset": len(bpos) + len(bnrm), "byteLength": len(buv), "target": 34962},
            {"buffer": 0, "byteOffset": len(bpos) + len(bnrm) + len(buv), "byteLength": len(bidx), "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(pos), "type": "VEC3", "min": mn, "max": mx},
            {"bufferView": 1, "componentType": 5126, "count": len(nrm), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(uv), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": len(idx), "type": "SCALAR"},
        ],
    }
    os.makedirs(OUT_DIR, exist_ok=True)
    yol = os.path.join(OUT_DIR, ad + ".gltf")
    with open(yol, "w") as f:
        json.dump(g, f, separators=(",", ":"), sort_keys=True)
        f.write("\n")
    print("%s (%d bayt): %d vertex, %d ucgen" % (os.path.relpath(yol), os.path.getsize(yol), len(pos), len(idx) // 3))


def main():
    yaz("kup", kutu(1, 1, 1))                                   # kafa, kale parcasi (1 m)
    yaz("kure", kure())                                         # kafa (cap 1 m)
    yaz("cubuk", kutu(1.0, 0.12, 0.12))                         # silah / yay (x boyunca 1 m)
    yaz("kalkan", kutu(0.14, 0.9, 0.7))                         # agir birligin kalkani
    yaz("isik_turuncu", kure(12, 8), emissive=(4.0, 1.5, 0.3))  # ates mermisi, patlama
    yaz("isik_mavi", kure(12, 8), emissive=(0.5, 2.2, 4.5))     # plazma (Gelecek cagi)
    yaz("lazer_mavi", kutu(0.06, 1.0, 0.06), emissive=(0.8, 3.0, 6.0))
    yaz("lazer_kirmizi", kutu(0.06, 1.0, 0.06), emissive=(6.0, 0.8, 0.6))


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Ölçüm varlığı üretici: N eklemli, ~T üçgenli İSKELETLİ "karakter" (zincir silindir) -> <çıktı>.gltf

Karakter bütçesi ölçümü (docs/MODEL-ANIMASYON-PLANI.md §6) içindir: gerçek bir karakterin eklem/üçgen sayısını taşıyan,
her eklemi 2 sn'lik döngüde sallanan bir klip içeren tek dosyalık glTF (tampon data URI). Depoya GİRMEZ (çıktı büyük);
yeniden üretilir, belirlenimlidir (aynı girdi, aynı bayt).

  python3 tools/make_test_karakter.py <eklem> <ucgen> <cikti.gltf>
"""
import base64, json, math, struct, sys


def uret(eklem, ucgen):
    h = 0.05                                   # eklem aralığı (m): 60 eklem ≈ 3 m boy (ölçek sahnede ayarlanır)
    halka = eklem
    seg = max(3, round(ucgen / (2 * (halka - 1))))
    pos, nrm, jnt, wgt = [], [], [], []
    for i in range(halka):
        for k in range(seg):
            a = 2 * math.pi * k / seg
            x, z = 0.12 * math.cos(a), 0.12 * math.sin(a)
            pos += [x, i * h, z]
            nrm += [math.cos(a), 0.0, math.sin(a)]
            j2 = min(i + 1, eklem - 1)
            jnt += [i, j2, 0, 0]
            wgt += [0.7, 0.3, 0.0, 0.0] if j2 != i else [1.0, 0.0, 0.0, 0.0]
    idx = []
    for i in range(halka - 1):
        for k in range(seg):
            a, b = i * seg + k, i * seg + (k + 1) % seg
            c, d = a + seg, b + seg
            idx += [a, c, b, b, c, d]
    nv = halka * seg
    # ters bağ matrisleri (sütun-major): eklem i dünyada (0, i*h, 0)
    ibm = []
    for i in range(eklem):
        ibm += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -i * h, 0, 1]
    anahtar = 61
    sure = 2.0
    t_in = [sure * k / (anahtar - 1) for k in range(anahtar)]
    rot = []
    for j in range(eklem):
        for k in range(anahtar):
            ang = 0.25 * math.sin(2 * math.pi * k / (anahtar - 1) + 0.3 * j)
            rot += [0.0, 0.0, math.sin(ang / 2), math.cos(ang / 2)]
    bolumler = []   # (bayt, hedef)
    def ekle(b, hedef=None):
        while sum(len(x[0]) for x in bolumler) % 4:
            bolumler.append((b"\x00", None))
        bolumler.append((b, hedef))
        return sum(len(x[0]) for x in bolumler[:-1]), len(b)
    f = lambda v: struct.pack("<%df" % len(v), *v)
    o_pos = ekle(f(pos), 34962); o_nrm = ekle(f(nrm), 34962)
    o_jnt = ekle(struct.pack("<%dB" % len(jnt), *jnt), 34962); o_wgt = ekle(f(wgt), 34962)
    o_idx = ekle(struct.pack("<%dI" % len(idx), *idx), 34963)
    o_ibm = ekle(f(ibm)); o_tin = ekle(f(t_in)); o_rot = ekle(f(rot))
    o_tin2 = o_rot2 = None   # 2. klip tamponlari asagida eklenir
    bv, ac = [], []
    def bview(o, hedef=None):
        d = {"buffer": 0, "byteOffset": o[0], "byteLength": o[1]}
        if hedef: d["target"] = hedef
        bv.append(d); return len(bv) - 1
    def acc(view, ct, count, tip, **kw):
        d = {"bufferView": view, "componentType": ct, "count": count, "type": tip}; d.update(kw); ac.append(d); return len(ac) - 1
    lo = [min(pos[0::3]), min(pos[1::3]), min(pos[2::3])]; hi = [max(pos[0::3]), max(pos[1::3]), max(pos[2::3])]
    a_pos = acc(bview(o_pos, 34962), 5126, nv, "VEC3", min=lo, max=hi)
    a_nrm = acc(bview(o_nrm, 34962), 5126, nv, "VEC3")
    a_jnt = acc(bview(o_jnt, 34962), 5121, nv, "VEC4")
    a_wgt = acc(bview(o_wgt, 34962), 5126, nv, "VEC4")
    a_idx = acc(bview(o_idx, 34963), 5125, len(idx), "SCALAR")
    a_ibm = acc(bview(o_ibm), 5126, eklem, "MAT4")
    a_tin = acc(bview(o_tin), 5126, anahtar, "SCALAR", min=[t_in[0]], max=[t_in[-1]])
    v_rot = bview(o_rot)
    # 2. klip "egil": 1,2 sn, X ekseni etrafinda daha genis salinim (A2 gecis denemesi: iki klip FARKLI gorunmeli)
    sure2 = 1.2
    t_in2 = [sure2 * k / (anahtar - 1) for k in range(anahtar)]
    rot2 = []
    for j in range(eklem):
        for k in range(anahtar):
            ang = 0.45 * math.sin(2 * math.pi * k / (anahtar - 1) + 0.15 * j)
            rot2 += [math.sin(ang / 2), 0.0, 0.0, math.cos(ang / 2)]
    o_tin2 = ekle(f(t_in2)); o_rot2 = ekle(f(rot2))
    a_tin2 = acc(bview(o_tin2), 5126, anahtar, "SCALAR", min=[t_in2[0]], max=[t_in2[-1]])
    v_rot2 = bview(o_rot2)
    veri = b"".join(x[0] for x in bolumler)
    nodes = [{"name": "karakter", "mesh": 0, "skin": 0}]
    for j in range(eklem):
        n = {"name": "eklem_%d" % j, "translation": [0, 0 if j == 0 else h, 0]}
        if j + 1 < eklem: n["children"] = [j + 2]
        nodes.append(n)
    kanallar, ornek = [], []
    # her eklem için ayrı rotasyon erişimcisi (aynı görünümde farklı ofset)
    for j in range(eklem):
        ac.append({"bufferView": v_rot, "byteOffset": j * anahtar * 16, "componentType": 5126, "count": anahtar, "type": "VEC4"})
        ornek.append({"input": a_tin, "output": len(ac) - 1, "interpolation": "LINEAR"})
        kanallar.append({"sampler": j, "target": {"node": j + 1, "path": "rotation"}})
    kanallar2, ornek2 = [], []
    for j in range(eklem):
        ac.append({"bufferView": v_rot2, "byteOffset": j * anahtar * 16, "componentType": 5126, "count": anahtar, "type": "VEC4"})
        ornek2.append({"input": a_tin2, "output": len(ac) - 1, "interpolation": "LINEAR"})
        kanallar2.append({"sampler": j, "target": {"node": j + 1, "path": "rotation"}})
    g = {
        "asset": {"version": "2.0", "generator": "tools/make_test_karakter.py"},
        "scene": 0, "scenes": [{"nodes": [0, 1]}],
        "nodes": nodes,
        "meshes": [{"primitives": [{"attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "JOINTS_0": a_jnt, "WEIGHTS_0": a_wgt}, "indices": a_idx}]}],
        "skins": [{"joints": list(range(1, eklem + 1)), "inverseBindMatrices": a_ibm, "skeleton": 1}],
        "animations": [{"name": "salla", "samplers": ornek, "channels": kanallar}, {"name": "egil", "samplers": ornek2, "channels": kanallar2}],
        "accessors": ac, "bufferViews": bv,
        "buffers": [{"byteLength": len(veri), "uri": "data:application/octet-stream;base64," + base64.b64encode(veri).decode()}],
    }
    return g, nv, len(idx) // 3


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    g, nv, nt = uret(int(sys.argv[1]), int(sys.argv[2]))
    with open(sys.argv[3], "w") as f:
        json.dump(g, f)
    print("karakter: %d eklem, %d kose, %d ucgen -> %s" % (int(sys.argv[1]), nv, nt, sys.argv[3]))

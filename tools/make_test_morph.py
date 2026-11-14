#!/usr/bin/env python3
"""tests/assets/morph_kup.gltf: A5 (morph target) test varligi. 8 koseli kup (kenar 1, merkez orijin), 2 hedef:
  hedef 0 'uzat'    : ust 4 kosenin y'si +1 (kup 2 m boyuna uzar)
  hedef 1 'genislet': tum kosenin x'i isaretine gore +0.5 (genislik 1 -> 2)
Iskeletsiz (A5 yalniz iskeletsiz mesh'te morph yapar). Tekrar uretilebilir (belirlenimli): python3 tools/make_test_morph.py
Ayrica tests/assets/morph_kup_anim.gltf: ayni kup + iskeletsiz 'nefes' klibi (glTF `weights` kanali, LINEAR, 2 s):
  t=0 [0, 0] -> t=1 [1, 0.5] -> t=2 [0, 0]. morph_kup.gltf bayt bayt ayni kalir (klipsiz kapilar ona bagli)."""
import base64, json, struct, sys, os

h = 0.5
V = [(-h, -h, -h), (h, -h, -h), (h, h, -h), (-h, h, -h), (-h, -h, h), (h, -h, h), (h, h, h), (-h, h, h)]
F = [(0, 2, 1), (0, 3, 2), (4, 5, 6), (4, 6, 7), (0, 1, 5), (0, 5, 4), (2, 3, 7), (2, 7, 6), (1, 2, 6), (1, 6, 5), (3, 0, 4), (3, 4, 7)]
N = [(0.0, 0.0, 0.0)] * 8                      # normaller: merkezden disa (yumusak kup)
def nrm(v):
    l = sum(c * c for c in v) ** 0.5
    return tuple(c / l for c in v)
N = [nrm(v) for v in V]
D0 = [(0.0, 1.0 if v[1] > 0 else 0.0, 0.0) for v in V]
D1 = [(0.5 if v[0] > 0 else -0.5, 0.0, 0.0) for v in V]

buf = bytearray()
def add(fmt, rows):
    off = len(buf)
    for r in rows:
        buf.extend(struct.pack(fmt, *r))
    return off
o_pos = add("<3f", V); o_nrm = add("<3f", N); o_d0 = add("<3f", D0); o_d1 = add("<3f", D1)
idx = [i for f in F for i in f]
o_idx = len(buf)
buf.extend(struct.pack("<%dH" % len(idx), *idx))
while len(buf) % 4: buf.append(0)

lo = [min(v[i] for v in V) for i in range(3)]; hi = [max(v[i] for v in V) for i in range(3)]
g = {
  "asset": {"version": "2.0", "generator": "tulpar-engine tools/make_test_morph.py"},
  "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0, "name": "morph_kup"}],
  "meshes": [{"name": "morph_kup", "weights": [0, 0], "extras": {"targetNames": ["uzat", "genislet"]},
              "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 4, "material": 0,
                              "targets": [{"POSITION": 2}, {"POSITION": 3}]}]}],
  "materials": [{"name": "kup", "pbrMetallicRoughness": {"baseColorFactor": [0.9, 0.5, 0.2, 1], "metallicFactor": 0, "roughnessFactor": 0.8}}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 8, "type": "VEC3", "min": lo, "max": hi},
    {"bufferView": 1, "componentType": 5126, "count": 8, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 8, "type": "VEC3", "min": [0, 0, 0], "max": [0, 1, 0]},
    {"bufferView": 3, "componentType": 5126, "count": 8, "type": "VEC3", "min": [-0.5, 0, 0], "max": [0.5, 0, 0]},
    {"bufferView": 4, "componentType": 5123, "count": len(idx), "type": "SCALAR"}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": o_pos, "byteLength": 96}, {"buffer": 0, "byteOffset": o_nrm, "byteLength": 96},
    {"buffer": 0, "byteOffset": o_d0, "byteLength": 96}, {"buffer": 0, "byteOffset": o_d1, "byteLength": 96},
    {"buffer": 0, "byteOffset": o_idx, "byteLength": len(idx) * 2}],
  "buffers": [{"byteLength": len(buf), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(buf)).decode()}],
}
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "assets", "morph_kup.gltf")
with open(out, "w") as f:
    json.dump(g, f, indent=1, sort_keys=True)
    f.write("\n")
print("yazildi:", os.path.normpath(out))

# --- A5: weights kanalli klip (ayri dosya) ---
import copy
buf2 = bytearray(buf)
o_t = len(buf2); buf2.extend(struct.pack("<3f", 0.0, 1.0, 2.0))
o_w = len(buf2); buf2.extend(struct.pack("<6f", 0.0, 0.0, 1.0, 0.5, 0.0, 0.0))
g2 = copy.deepcopy(g)
g2["asset"]["generator"] = "tulpar-engine tools/make_test_morph.py (anim)"
g2["bufferViews"] += [{"buffer": 0, "byteOffset": o_t, "byteLength": 12}, {"buffer": 0, "byteOffset": o_w, "byteLength": 24}]
nv = len(g2["bufferViews"])
g2["accessors"] += [{"bufferView": nv - 2, "componentType": 5126, "count": 3, "type": "SCALAR", "min": [0.0], "max": [2.0]},
                    {"bufferView": nv - 1, "componentType": 5126, "count": 6, "type": "SCALAR"}]
na = len(g2["accessors"])
g2["animations"] = [{"name": "nefes", "samplers": [{"input": na - 2, "output": na - 1, "interpolation": "LINEAR"}],
                     "channels": [{"sampler": 0, "target": {"node": 0, "path": "weights"}}]}]
g2["buffers"] = [{"byteLength": len(buf2), "uri": "data:application/octet-stream;base64," + base64.b64encode(bytes(buf2)).decode()}]
out2 = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "assets", "morph_kup_anim.gltf")
with open(out2, "w") as f:
    json.dump(g2, f, indent=1, sort_keys=True)
    f.write("\n")
print("yazildi:", os.path.normpath(out2))

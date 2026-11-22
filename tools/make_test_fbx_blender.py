"""FBX test varliklari (Blender icinden): morph (blend shape) + coklu UV.

Kullanim:  blender -b --factory-startup -P tools/make_test_fbx_blender.py -- tests/assets
Uretir:
  morph_kup.fbx  — 2 m'lik kup (Blender'da Z-yukari; motorda Y-yukari), iki sekil anahtari:
                   "Uzun": ust 4 kose +1 m yukari (Blender +Z -> motor +Y), "Yan": +X yuzundeki 4 kose +0,5 m +X.
                   Klip (30 kare, 30 Hz; Blender sekil anahtari yiginini "Scene" adlandirir): Uzun 0 -> 1 (kare 1 -> 31), Yan sabit 0,25.
  iki_uv.fbx     — 1x1 duzlem, iki UV katmani: "Bos" (butun koseler 0,0 — Unreal donusumundeki dejenere kanal, 8eg) ve
                   "Gercek" (0..1). Motor dejenere kanali atlayip "Gercek"i almali.
Depoya giren ikililer bu betikle yeniden uretilebilir (Blender 5.3, 2026-11-22).
"""
import sys

import bpy

out = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "."


def temizle():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for a in bpy.data.actions:
        bpy.data.actions.remove(a)


def disa(ad, nesne):
    bpy.ops.object.select_all(action="DESELECT")
    nesne.select_set(True)
    bpy.context.view_layer.objects.active = nesne
    bpy.ops.export_scene.fbx(filepath=f"{out}/{ad}", use_selection=True, apply_unit_scale=True, bake_anim=True,
                             bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False, add_leaf_bones=False)


scn = bpy.context.scene
scn.render.fps = 30
scn.frame_start, scn.frame_end = 1, 31

# --- morph_kup.fbx ------------------------------------------------------------------------------------------------------
temizle()
bpy.ops.mesh.primitive_cube_add(size=2.0)
kup = bpy.context.active_object
kup.name = "MorphKup"
kup.shape_key_add(name="Basis")
uzun = kup.shape_key_add(name="Uzun", from_mix=False)
yan = kup.shape_key_add(name="Yan", from_mix=False)
for i, v in enumerate(kup.data.vertices):
    if v.co.z > 0:
        uzun.data[i].co.z += 1.0
    if v.co.x > 0:
        yan.data[i].co.x += 0.5
uzun.value = 0.0
uzun.keyframe_insert("value", frame=1)
uzun.value = 1.0
uzun.keyframe_insert("value", frame=31)
yan.value = 0.25
yan.keyframe_insert("value", frame=1)
yan.keyframe_insert("value", frame=31)
for fc in kup.data.shape_keys.animation_data.action.fcurves if hasattr(kup.data.shape_keys.animation_data.action, "fcurves") else []:
    for k in fc.keyframe_points:
        k.interpolation = "LINEAR"
kup.data.shape_keys.animation_data.action.name = "Nefes"
disa("morph_kup.fbx", kup)

# --- iki_uv.fbx ---------------------------------------------------------------------------------------------------------
temizle()
bpy.ops.mesh.primitive_plane_add(size=1.0)
d = bpy.context.active_object
d.name = "IkiUV"
me = d.data
me.uv_layers[0].name = "Bos"
gercek = me.uv_layers.new(name="Gercek")
for lp in me.loops:
    me.uv_layers["Bos"].data[lp.index].uv = (0.0, 0.0)
    co = me.vertices[lp.vertex_index].co
    gercek.data[lp.index].uv = (co.x + 0.5, co.y + 0.5)
disa("iki_uv.fbx", d)
print("FBX TEST VARLIKLARI YAZILDI:", out)

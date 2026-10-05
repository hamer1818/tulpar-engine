#!/usr/bin/env python3
"""Vulkan loader sondasi — motordan BAGIMSIZ: loader ICD'yi ve dogrulama
katmanini gercekten goruyor mu?

NEDEN: CI'da "surucu paketi kurulu" yetmez; manifest loader'in aramadigi bir
yerdeyse loader cihaz LISTELEMEZ ve motorun GPU kapilari sessizce atlanir
(Linux/macOS isleri bunu `vulkaninfo` ile olcer). MSYS2'de vulkaninfo yok
(mingw-w64-x86_64-vulkan-tools paketi bulunamadi, olculdu 2026-10-05), bu
yuzden Windows isi ayni olcumu bu sondayla yapar: ctypes ile loader'i acar,
instance katmanlarini sayar, bir Vulkan 1.1 instance kurar ve fiziksel
cihazlarin adini basar. Motorun kodundan (rhi/) hicbir sey kullanmaz — ayni
hatayi paylasan bir olcu aleti olmasin diye.

  python3 tools/vk_sonda.py                         ozet basar
  python3 tools/vk_sonda.py --cihaz llvmpipe --katman VK_LAYER_KHRONOS_validation
                                                    ikisi de yoksa cikis 1

Cikis: 0 = istenenlerin hepsi goruldu; 1 = goruldu ama istenen eksik;
2 = loader acilamadi ya da instance kurulamadi.
"""
import argparse
import ctypes
import sys


def loader_ac():
    if sys.platform == "win32":
        adlar = ["vulkan-1.dll"]
        tip = ctypes.WinDLL
    elif sys.platform == "darwin":
        adlar = ["libvulkan.1.dylib", "/opt/homebrew/lib/libvulkan.1.dylib", "/usr/local/lib/libvulkan.1.dylib"]
        tip = ctypes.CDLL
    else:
        adlar = ["libvulkan.so.1", "libvulkan.so"]
        tip = ctypes.CDLL
    for ad in adlar:
        try:
            return tip(ad), ad
        except OSError:
            continue
    return None, None


class VkLayerProperties(ctypes.Structure):
    _fields_ = [("layerName", ctypes.c_char * 256), ("specVersion", ctypes.c_uint32),
                ("implementationVersion", ctypes.c_uint32), ("description", ctypes.c_char * 256)]


class VkApplicationInfo(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", ctypes.c_void_p), ("pApplicationName", ctypes.c_char_p),
                ("applicationVersion", ctypes.c_uint32), ("pEngineName", ctypes.c_char_p),
                ("engineVersion", ctypes.c_uint32), ("apiVersion", ctypes.c_uint32)]


class VkInstanceCreateInfo(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", ctypes.c_void_p), ("flags", ctypes.c_uint32),
                ("pApplicationInfo", ctypes.POINTER(VkApplicationInfo)), ("enabledLayerCount", ctypes.c_uint32),
                ("ppEnabledLayerNames", ctypes.c_void_p), ("enabledExtensionCount", ctypes.c_uint32),
                ("ppEnabledExtensionNames", ctypes.c_void_p)]


def main():
    ap = argparse.ArgumentParser(description="Vulkan loader sondasi (motordan bagimsiz)")
    ap.add_argument("--cihaz", help="fiziksel cihaz adinda aranacak alt dizgi (buyuk/kucuk harf duyarsiz)")
    ap.add_argument("--katman", help="instance katmanlarinda aranacak ad")
    a = ap.parse_args()

    vk, ad = loader_ac()
    if not vk:
        print("vk_sonda: Vulkan loader ACILAMADI")
        return 2
    print("vk_sonda: loader %s" % ad)
    u32 = ctypes.c_uint32

    n = u32(0)
    vk.vkEnumerateInstanceLayerProperties(ctypes.byref(n), None)
    katmanlar = (VkLayerProperties * max(n.value, 1))()
    vk.vkEnumerateInstanceLayerProperties(ctypes.byref(n), katmanlar)
    adlar = [katmanlar[i].layerName.decode(errors="replace") for i in range(n.value)]
    print("vk_sonda: %d instance katmani: %s" % (len(adlar), ", ".join(adlar) or "-"))

    app = VkApplicationInfo(0, None, b"vk_sonda", 1, b"vk_sonda", 1, (1 << 22) | (1 << 12))  # 1.1
    ci = VkInstanceCreateInfo(1, None, 0, ctypes.pointer(app), 0, None, 0, None)
    inst = ctypes.c_void_p()
    r = vk.vkCreateInstance(ctypes.byref(ci), None, ctypes.byref(inst))
    if r != 0:
        print("vk_sonda: vkCreateInstance DUSTU (VkResult %d; -9 = VK_ERROR_INCOMPATIBLE_DRIVER: ICD yok)" % r)
        return 2
    m = u32(0)
    vk.vkEnumeratePhysicalDevices(inst, ctypes.byref(m), None)
    cihazlar = (ctypes.c_void_p * max(m.value, 1))()
    vk.vkEnumeratePhysicalDevices(inst, ctypes.byref(m), cihazlar)
    cihaz_adlari = []
    for i in range(m.value):
        # VkPhysicalDeviceProperties: 5 x uint32 (api, surucu, vendor, device,
        # tur) sonra char deviceName[256]. Yapinin geri kalani (~800 B) icin
        # bol bir tampon.
        tampon = ctypes.create_string_buffer(4096)
        vk.vkGetPhysicalDeviceProperties(ctypes.c_void_p(cihazlar[i]), tampon)
        api = int.from_bytes(tampon.raw[0:4], "little")
        isim = tampon.raw[20:276].split(b"\0", 1)[0].decode(errors="replace")
        cihaz_adlari.append(isim)
        print("vk_sonda: cihaz %d: %s (Vulkan %d.%d.%d)" % (i, isim, api >> 22, (api >> 12) & 0x3FF, api & 0xFFF))
    vk.vkDestroyInstance(inst, None)
    if m.value == 0:
        print("vk_sonda: instance kuruldu ama fiziksel cihaz YOK")

    eksik = []
    if a.cihaz and not any(a.cihaz.lower() in c.lower() for c in cihaz_adlari):
        eksik.append("cihaz '%s'" % a.cihaz)
    if a.katman and a.katman not in adlar:
        eksik.append("katman '%s'" % a.katman)
    if eksik:
        print("vk_sonda: EKSIK: " + ", ".join(eksik))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

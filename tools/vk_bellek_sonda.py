#!/usr/bin/env python3
"""Vulkan bellek ayirma/birakma sondasi — motordan ve ImGui'den BAGIMSIZ.

NEDEN: Windows lavapipe'ta (MSYS2 mesa 26.2.4, CI windows-latest, 2026-10-05)
motor kosumlari 0xC0000374 (heap bozulmasi) ile oluyor; PageHeap + cdb yigini
ImGui'nin doku yukleme tamponunu birakan vkFreeMemory'sinin ICINDE
(msvcrt!free <- vulkan_lvp) gosterdi (Tuzaklar 8cr). Sorun surucude mi, bizim
kullanim kalibimizda mi? Bu sonda o kalibi (tampon kur, ayir, bagla, esle,
yaz, birak) asgari haliyle, motor kodu OLMADAN, ctypes ile kosar:

  python3 tools/vk_bellek_sonda.py            her boyutta ayir->esle->yaz->birak
  python3 tools/vk_bellek_sonda.py --tampon   + ImGui gibi VkBuffer kur/bagla/yok et

Cikis 0 = butun turlar bitti (surec saglam). Surec 0xC0000374 ile olurse
bozulma bu kalibin kendisindedir (motor yok). Windows'ta tam sayfa yigini
(PageHeap) acikken kosturmak bozulmayi YAZMA/BIRAKMA aninda yakalatir.

OLCULDU (CI windows-latest, 2026-10-05):
  MSYS2 mesa 26.2.4 lavapipe   cikis 127 (0xC0000374) PageHeap'siz de; PageHeap
                               + cdb: VERIFIER STOP 10 corrupted start stamp,
                               msvcrt!free <- vulkan_lvp+0x223921 <-
                               vulkan_lvp+0x2b6b50 <- _ctypes. Sonuc: surucu.
  SwiftShader (Chrome 154)     10 boyut x 3 tur, BITTI (surec saglam).
  RTX 5080 / Linux (yerel)     BITTI.
tools/windows_vulkan_icd.sh `kur` bu sondayi her CI kosumunda kosar.
"""
import argparse
import ctypes
import sys

u32, u64, vp = ctypes.c_uint32, ctypes.c_uint64, ctypes.c_void_p


class AppInfo(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", vp), ("pApplicationName", ctypes.c_char_p), ("applicationVersion", u32),
                ("pEngineName", ctypes.c_char_p), ("engineVersion", u32), ("apiVersion", u32)]


class InstInfo(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", vp), ("flags", u32), ("pApplicationInfo", ctypes.POINTER(AppInfo)),
                ("enabledLayerCount", u32), ("ppEnabledLayerNames", vp), ("enabledExtensionCount", u32),
                ("ppEnabledExtensionNames", vp)]


class QueueInfo(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", vp), ("flags", u32), ("queueFamilyIndex", u32), ("queueCount", u32),
                ("pQueuePriorities", ctypes.POINTER(ctypes.c_float))]


class DevInfo(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", vp), ("flags", u32), ("queueCreateInfoCount", u32),
                ("pQueueCreateInfos", ctypes.POINTER(QueueInfo)), ("enabledLayerCount", u32), ("ppEnabledLayerNames", vp),
                ("enabledExtensionCount", u32), ("ppEnabledExtensionNames", vp), ("pEnabledFeatures", vp)]


class MemType(ctypes.Structure):
    _fields_ = [("propertyFlags", u32), ("heapIndex", u32)]


class MemHeap(ctypes.Structure):
    _fields_ = [("size", u64), ("flags", u32)]


class MemProps(ctypes.Structure):
    _fields_ = [("memoryTypeCount", u32), ("memoryTypes", MemType * 32), ("memoryHeapCount", u32), ("memoryHeaps", MemHeap * 16)]


class AllocInfo(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", vp), ("allocationSize", u64), ("memoryTypeIndex", u32)]


class BufInfo(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", vp), ("flags", u32), ("size", u64), ("usage", u32), ("sharingMode", ctypes.c_int),
                ("queueFamilyIndexCount", u32), ("pQueueFamilyIndices", vp)]


class MemReq(ctypes.Structure):
    _fields_ = [("size", u64), ("alignment", u64), ("memoryTypeBits", u32)]


HOST_VISIBLE, HOST_COHERENT = 0x2, 0x4


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--tampon", action="store_true", help="ImGui gibi: VkBuffer kur, bellegi bagla, yok et")
    ap.add_argument("--tur", type=int, default=3, help="her boyut icin tur sayisi")
    a = ap.parse_args()
    vk = (ctypes.WinDLL if sys.platform == "win32" else ctypes.CDLL)("vulkan-1.dll" if sys.platform == "win32" else "libvulkan.so.1")
    for f in ("vkCreateInstance", "vkEnumeratePhysicalDevices", "vkCreateDevice", "vkAllocateMemory", "vkMapMemory",
              "vkCreateBuffer", "vkBindBufferMemory", "vkGetBufferMemoryRequirements"):
        getattr(vk, f).restype = ctypes.c_int

    app = AppInfo(0, None, b"vk_bellek_sonda", 1, b"-", 1, (1 << 22) | (1 << 12))
    inst = vp()
    r = vk.vkCreateInstance(ctypes.byref(InstInfo(1, None, 0, ctypes.pointer(app), 0, None, 0, None)), None, ctypes.byref(inst))
    if r:
        print("vk_bellek_sonda: vkCreateInstance %d" % r)
        return 2
    n = u32(1)
    pd = vp()
    vk.vkEnumeratePhysicalDevices(inst, ctypes.byref(n), ctypes.byref(pd))
    if not n.value:
        print("vk_bellek_sonda: cihaz yok")
        return 2
    ad = ctypes.create_string_buffer(4096)
    vk.vkGetPhysicalDeviceProperties(pd, ad)
    print("vk_bellek_sonda: cihaz %s" % ad.raw[20:276].split(b"\0", 1)[0].decode(errors="replace"))
    mp = MemProps()
    vk.vkGetPhysicalDeviceMemoryProperties(pd, ctypes.byref(mp))
    tip = next(i for i in range(mp.memoryTypeCount) if mp.memoryTypes[i].propertyFlags & HOST_VISIBLE)
    pr = (ctypes.c_float * 1)(1.0)
    qi = QueueInfo(2, None, 0, 0, 1, pr)
    dev = vp()
    r = vk.vkCreateDevice(pd, ctypes.byref(DevInfo(3, None, 0, 1, ctypes.pointer(qi), 0, None, 0, None, None)), None, ctypes.byref(dev))
    if r:
        print("vk_bellek_sonda: vkCreateDevice %d" % r)
        return 2
    boyutlar = [256, 1000, 4096, 12345, 65536, 262144, 1 << 20, (1 << 20) + 17, 4 << 20, 16 << 20]
    for b in boyutlar:
        for _ in range(a.tur):
            buf = u64(0)
            boy = b
            if a.tampon:
                vk.vkCreateBuffer(dev, ctypes.byref(BufInfo(12, None, 0, b, 0x1, 0, 0, None)), None, ctypes.byref(buf))  # TRANSFER_SRC
                req = MemReq()
                vk.vkGetBufferMemoryRequirements(dev, buf, ctypes.byref(req))
                boy = req.size
            mem = u64(0)
            r = vk.vkAllocateMemory(dev, ctypes.byref(AllocInfo(5, None, boy, tip)), None, ctypes.byref(mem))
            if r:
                print("vk_bellek_sonda: vkAllocateMemory(%d) %d" % (boy, r))
                return 1
            if a.tampon:
                vk.vkBindBufferMemory(dev, buf, mem, u64(0))
            ptr = vp()
            if vk.vkMapMemory(dev, mem, u64(0), u64(boy), 0, ctypes.byref(ptr)) == 0:
                ctypes.memset(ptr, 0x5A, boy)
                vk.vkUnmapMemory(dev, mem)
            if a.tampon:
                vk.vkDestroyBuffer(dev, buf, None)
            vk.vkFreeMemory(dev, mem, None)
        print("vk_bellek_sonda: %9d B x %d tur tamam" % (b, a.tur))
        sys.stdout.flush()
    vk.vkDestroyDevice(dev, None)
    vk.vkDestroyInstance(inst, None)
    print("vk_bellek_sonda: BITTI (surec saglam)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

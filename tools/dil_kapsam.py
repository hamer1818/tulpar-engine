#!/usr/bin/env python3
"""Editör İngilizce kapsam ölçümü (app/editor_dil.hpp).

Kullanım: dil_kapsam.py <eksik.txt> [tavan]
eksik.txt = TULPAR_EDITOR_DIL=en ile çalışan editörün TULPAR_DIL_EKSIK dosyası (çevrilemeyen, normalleşmiş
metinler). Burada yalnız TÜRKÇE'ye özgü harf (çğıöşü) içeren, İngilizce karşılığı olmayan ve başka bir
satırın kesik öneki olmayan metinler sayılır; bunlar ekranda Türkçe kalmış demektir. Sayı tavanı aşarsa
çıkış 1 (kapı: kapsam geriye gitmesin; tavan çeviri eklendikçe İNER, yükseltilmez).
"""
import re, sys

def ana():
    eksik = [l.rstrip('\n') for l in open(sys.argv[1], encoding='utf-8')]
    tavan = int(sys.argv[2]) if len(sys.argv) > 2 else None
    inc = open(__file__.replace('tools/dil_kapsam.py', 'app/editor_dil_en.inc'), encoding='utf-8').read()
    ens = set(re.findall(r'", "(.*?)"\},', inc))
    gorulen, tekil = set(), []
    for l in eksik:
        if l not in gorulen:
            gorulen.add(l); tekil.append(l)
    sayilan = []
    for l in tekil:
        b = l.rstrip('…')
        if not re.search(r'[çğıöşüÇĞİÖŞÜ]', l): continue
        if l in ens or b in ens: continue
        if any(o != l and o.startswith(b) and len(o) > len(b) for o in tekil): continue  # kesik ön ek
        if any(o != l and o.endswith(b) and len(o) > len(b) for o in tekil): continue    # kesik son ek (soldan kısaltma)
        if '·' in l and all(any(p.strip() in ens for p in [q]) for q in l.split('·')): continue
        sayilan.append(l)
    for l in sayilan: print('  TURKCE KALDI:', l)
    print('dil kapsam: %d cevrilemeyen Turkce metin (tur: %d tekil metin)%s' % (
        len(sayilan), len(tekil), '' if tavan is None else ', tavan %d' % tavan))
    if tavan is not None and len(sayilan) > tavan: sys.exit(1)

ana()

#!/usr/bin/env python3
"""Arayuz taramasi (docs/ARAYUZ-SURUCUSU.md): Sahne > "+" menusundeki HER varlik turunu ekler ve
Ozellikler panelinde kesilen ("…") metinleri listeler; sonunda Dunya paneli. Kesik yoksa sessiz.

  tools/arayuz_tara.py [WxH]        # varsayilan 1600x1000; ornek: 1280x720
Arama kutusu ipucu ("Ara…") bilerek ucnoktalidir, sayilmaz.
"""
import os, subprocess, re, sys
A=['python3', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'arayuz.py')]
def k(*a):
    r=subprocess.run(A+list(a),capture_output=True,text=True); return r.returncode, r.stdout
def ogeler(f=None, pencere=None):
    args=['ogeler']+([f] if f else [])+([f'pencere={pencere}'] if pencere else [])
    _,o=k(*args); return re.findall(r'^"(.*?)" @(.*?) \((\d+),(\d+) (\d+)x(\d+)\)(.*)$',o,re.M)
k('baslat','--size',sys.argv[1] if len(sys.argv)>1 else '1600x1000')
sonuc={}
k('tikla','+','pencere=Sahne')
ana=[o for o in ogeler(pencere='Popup')]
menu_adlari=[(o[0],'kapali' in o[6]) for o in ana]
k('tus','Escape')
for ad,alt in menu_adlari:
    if not alt: continue
    k('tikla','+','pencere=Sahne'); k('uzerine',ad,'pencere=Popup'); k('bekle','3')
    altlar=[o[0] for o in ogeler() if '###Menu_' in o[1] and '###Menu_' not in o[0]]
    k('tus','Escape'); k('tus','Escape')
    for a in altlar:
        k('tikla','+','pencere=Sahne'); k('uzerine',ad,'pencere=Popup'); k('bekle','3')
        rc,o=k('tikla',a,'pencere=Menu_')
        if rc: print('TIKLANAMADI',ad,'>',a,o.strip()[:120]); k('tus','Escape'); k('tus','Escape'); continue
        k('bekle','3')
        _,m=k('metinler','…','pencere=Ozellikler')
        kes=[l for l in m.splitlines() if l.startswith('"') and not l.lower().startswith('"ara…"')]
        if kes: print(f'{ad} > {a}: {len(kes)} kesik', *kes, sep='\n   ', flush=True)
k('tikla','Dünya','sira=1'); _,m=k('metinler','…','pencere=Dunya'); print('DUNYA', m, flush=True)
k('bitir')

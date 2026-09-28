#!/usr/bin/env python3
"""/proc/<pid>/smaps dokumunu gruplara ayirir: surucu kutuphaneleri, motor arenasi
(>=400 MB rezervli anonim eslem), heap, ikili, digerleri. Kullanim:
    python3 tools/taban_bellek.py smaps_k60.txt
Olcu programi: tulpar/examples/engine_taban_bellek.tpr (docs/PLAN.md §5)."""
import re
import sys
from collections import defaultdict

path = sys.argv[1]
groups = defaultdict(int)
anon_sizes = []
cur = None
cur_size = 0
for line in open(path, errors='replace'):
    m = re.match(r'^([0-9a-f]+)-([0-9a-f]+) \S+ \S+ \S+ \S+\s*(.*)$', line)
    if m:
        name = m.group(3).strip()
        cur_size = (int(m.group(2), 16) - int(m.group(1), 16)) // 1024
        if not name:
            key = 'anon'
        elif 'nvidia' in name.lower() or 'libGLX' in name or 'libEGL' in name:
            key = 'surucu (nvidia/GL kutuphaneleri)'
        elif name.startswith('/dev/'):
            key = 'aygit eslemesi (' + name.split('/')[2] + ')'
        elif name.startswith('['):
            key = name
        elif name.startswith('/tmp/') or 'tulpar_run' in name or name.endswith('taban'):
            key = 'ikili (motor + Tulpar runtime)'
        elif name.startswith('/usr/lib') or name.startswith('/lib'):
            key = 'diger paylasilan kutuphaneler'
        else:
            key = 'diger dosya: ' + name.split('/')[-1]
        cur = key
        continue
    m = re.match(r'^Rss:\s+(\d+) kB', line)
    if m and cur:
        kb = int(m.group(1))
        if cur == 'anon':
            anon_sizes.append((cur_size, kb))
            if cur_size >= 400 * 1024:
                groups['anon: motor arenasi (>=400 MB rezerv)'] += kb
            else:
                groups['anon: diger (malloc, surucu yiginlari, yigitlar)'] += kb
        else:
            groups[cur] += kb
total = sum(groups.values())
for k, v in sorted(groups.items(), key=lambda x: -x[1]):
    print('%9d kB  %s' % (v, k))
print('%9d kB  TOPLAM' % total)
big = sorted(anon_sizes, key=lambda x: -x[1])[:5]
print('en buyuk anon (boyut kB, rss kB):', big)

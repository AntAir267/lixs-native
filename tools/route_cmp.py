"""Compare a route run with a baseline (tools/route1.sh outputs): memory hashes and runtime call
counts at each hash point, and the frame dumps pixel by pixel.
    route_cmp.py <baseline dir> <run dir>"""
import sys, os
from PIL import Image, ImageChops
a, b = sys.argv[1], sys.argv[2]
ha = [l.split() for l in open(os.path.join(a, 'hash.txt'))]
hb = [l.split() for l in open(os.path.join(b, 'hash.txt'))]
same = [x[0] for x, y in zip(ha, hb) if x[2] == y[2]]
first = next((x[0] for x, y in zip(ha, hb) if x[2] != y[2]), None)
print('hash points: %d vs %d; same %d; first differing frame %s' % (len(ha), len(hb), len(same), first))
for x, y in list(zip(ha, hb))[:3] + list(zip(ha, hb))[-1:]:
    print('  frame %s calls %s vs %s (%+d)' % (x[0], x[1], y[1], int(y[1]) - int(x[1])))
for f in sorted(os.listdir(a)):
    if not f.endswith('.png') or not os.path.exists(os.path.join(b, f)):
        continue
    ia = Image.open(os.path.join(a, f)).convert('RGB')
    ib = Image.open(os.path.join(b, f)).convert('RGB')
    d = ImageChops.difference(ia, ib)
    n = sum(1 for p in d.getdata() if max(p) > 16)
    print('  %s: %s' % (f, 'identical' if not d.getbbox() else '%d pixels differ, box %s' % (n, d.getbbox())))

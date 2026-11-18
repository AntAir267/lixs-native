"""Cross-check discovery (gen/insns.txt, gen/rejected.txt) with Ghidra's functions
(work/ghidra/all/index.tsv): each Ghidra entry is one of ours, an instruction inside one of our
functions (an interior start: an EXTRA_ENTRIES candidate if anything calls it from outside), a
pointer discovery rejected, or code discovery never reached.
    ghidra_check.py [gen] [work/ghidra/all/index.tsv]"""
import sys, os
gen = sys.argv[1] if len(sys.argv) > 1 else 'gen'
idx = sys.argv[2] if len(sys.argv) > 2 else 'work/ghidra/all/index.tsv'
owner = {}
entries = set()
for l in open(os.path.join(gen, 'insns.txt')):
    f = l.split()
    e = int(f[0], 16)
    entries.add(e)
    for a in f[1:]:
        owner.setdefault(int(a, 16), e)
rejected = {}
for l in open(os.path.join(gen, 'rejected.txt')):
    a, why = l.rstrip('\n').split(' ', 1)
    rejected[int(a, 16)] = why
out = {'ours': [], 'interior': [], 'rejected': [], 'unreached': []}
for l in open(idx):
    a, size, name = l.rstrip('\n').split('\t')
    a = int(a, 16)
    if a in entries:
        out['ours'].append(a)
    elif a in owner:
        out['interior'].append('%08x in %08x %s size %s' % (a, owner[a], name, size))
    elif a in rejected:
        out['rejected'].append('%08x %s size %s: %s' % (a, name, size, rejected[a]))
    else:
        out['unreached'].append('%08x %s size %s' % (a, name, size))
print('ghidra functions: %d ours, %d interior, %d rejected pointers, %d unreached' %
      tuple(len(out[k]) for k in ('ours', 'interior', 'rejected', 'unreached')))
for k in ('interior', 'rejected', 'unreached'):
    for l in out[k]:
        print(k, l)

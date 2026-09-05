import os, re, json, itertools

def page_order(scen):
    out = []
    for line in open(scen):
        s = line.strip()
        if s.startswith('# PAGE '):
            out.append(s.split()[2])
    return out

ORD = {'upui': page_order('scen/probe-up.txt'), 'master': page_order('scen/probe.txt')}
ORD['273'] = ORD['master']

res = {}
for f in sorted(os.listdir('probe')):
    tree, dim, lay, size = f[:-4].split('-', 3)
    vals = re.findall(r'state ui\.(nav_layout|overflow) = (\S+)', open('probe/' + f).read())
    lay_vals = [v for k, v in vals if k == 'nav_layout']
    ovf = [v for k, v in vals if k == 'overflow']
    pages = ORD[tree]
    assert len(ovf) == len(pages), (f, len(ovf), len(pages))
    assert set(lay_vals) == {lay}, (f, set(lay_vals))
    res[(tree, dim, lay, size)] = dict(zip(pages, ovf))
json.dump({'|'.join(k): v for k, v in res.items()}, open('overflow.json', 'w'), indent=0)

print('OVERFLOW REGRESSIONS versus upstream (upstream fits, fork does not)')
print('reference text size = normal, the only size upstream renders')
for dim in ('135x240', '80x160', '320x240'):
    for lay in ('buttons', 'touch'):
        u = res[('upui', dim, lay, 'normal')]
        for tree in ('master', '273'):
            f = res[(tree, dim, lay, 'normal')]
            for p, v in u.items():
                if v == 'no' and f.get(p) == 'yes':
                    print('  REGRESSION', dim, lay, p, 'upstream=no', tree + '=yes')
                if v == 'yes' and f.get(p) == 'no':
                    print('  fixed     ', dim, lay, p, 'upstream=yes', tree + '=no')
print()
print('FORK-ONLY pages that overflow at normal (no upstream counterpart)')
for dim in ('135x240', '80x160', '320x240'):
    for lay in ('buttons', 'touch'):
        u = res[('upui', dim, lay, 'normal')]
        for tree in ('master', '273'):
            f = res[(tree, dim, lay, 'normal')]
            for p, v in f.items():
                if p not in u and v == 'yes':
                    print(' ', dim, lay, p, tree)

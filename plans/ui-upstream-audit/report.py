import json, os, collections

RAW = 'https://raw.githubusercontent.com/MaxRink/furble/docs/ui-upstream-audit/plans/ui-upstream-audit/captures'
rows = json.load(open('diffs.json'))
ovf = json.load(open('overflow.json'))
PANELS = [('135x240', 'M5StickS3'), ('80x160', 'M5StickC'), ('320x240', 'M5Stack Core')]
SIZES = ['small', 'normal', 'large']

def link(tree, panel, lay, size, page):
    l = ('buttons-lg0' if lay == 'buttons' else lay) if tree == '273' else lay
    return '%s/%s/%s/%s/%s/%s.png' % (RAW, tree, panel, l, size, page)

def cell(v, tree_a, tree_b, panel, lay, size, page):
    if isinstance(v, str):
        return 'absent upstream' if v == 'absent' else v
    if v.get('same'):
        return 'identical'
    if v.get('size_changed'):
        return 'panel size differs'
    box = v['box']
    return '[%d px](%s) vs [after](%s), box x%d-%d y%d-%d' % (
        v['pixels'], link(tree_a, panel, lay, size, page),
        link(tree_b, panel, lay, size, page), box[0], box[2], box[1], box[3])

out = []
w = out.append

def og(t, d, l, s, p):
    return ovf.get('|'.join((t, d, l, s)), {}).get(p, '-')

# ---- regression list ----
regressions = []
for d, _ in PANELS:
    for l in ('buttons', 'touch'):
        u = ovf['|'.join(('upui', d, l, 'normal'))]
        for p, v in u.items():
            m = og('master', d, l, 'normal', p)
            t = og('273', d, l, 'normal', p)
            if v == 'no' and (m == 'yes' or t == 'yes'):
                regressions.append((p, d, l, m, t))
json.dump(regressions, open('regressions.json', 'w'))

# ---- per-panel diff tables ----
for panel, board in PANELS:
    w('## %s (%s)\n' % (panel, board))
    for lay in ('buttons', 'touch'):
        for size in SIZES:
            sel = [r for r in rows if r['panel'] == panel and r['layout'] == lay and r['size'] == size]
            if not sel:
                continue
            ident = collections.Counter()
            shown = []
            for r in sel:
                same = []
                for tag in ('up_master', 'master_273', 'up_273'):
                    v = r[tag]
                    if isinstance(v, dict) and v.get('same'):
                        ident[tag] += 1
                        same.append(tag)
                if len(same) == 3:
                    continue
                shown.append(r)
            w('### %s layout, %s text\n' % (lay, size))
            if size != 'normal':
                w('Upstream has no text size setting, so its column repeats the')
                w('normal render. Only the master versus #273 column carries')
                w('information at this size.\n')
            w('Identical pairs not shown: upstream vs master %d, master vs #273 %d, upstream vs #273 %d.\n'
              % (ident['up_master'], ident['master_273'], ident['up_273']))
            if not shown:
                w('Every page identical across all three trees.\n')
                continue
            w('| page | overflow up/master/273 | upstream vs master | master vs #273 |')
            w('| --- | --- | --- | --- |')
            for r in shown:
                p = r['page']
                o = '%s / %s / %s' % (og('upui', panel, lay, size, p),
                                      og('master', panel, lay, size, p),
                                      og('273', panel, lay, size, p))
                w('| %s | %s | %s | %s |' % (
                    p, o,
                    cell(r['up_master'], 'upui', 'master', panel, lay, size, p),
                    cell(r['master_273'], 'master', '273', panel, lay, size, p)))
            w('')
open('tables.md', 'w').write('\n'.join(out))
print('tables written, regressions:', len(regressions))
for r in regressions:
    print(' ', r)

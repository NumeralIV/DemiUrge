"""Compare simrun --hash runs: per (seed, policy) the world hash and the chronicle entry count must match across reps."""
import re, sys, glob, os
here = os.path.dirname(os.path.abspath(__file__))
reps = sorted(glob.glob(os.path.join(here, 'determinism-rep*.txt')))
table = {}
for i, f in enumerate(reps):
    key = None
    for line in open(f, encoding='utf-8', errors='replace'):
        m = re.match(r'== seed (\d+)\s+policy (\S+)', line)
        if m: key = (int(m.group(1)), m.group(2)); continue
        m = re.search(r'chronicle (\d+)', line)
        if key and line.strip().startswith('events:') and m: table.setdefault(key, {}).setdefault(i, {})['chron'] = int(m.group(1))
        m = re.match(r'\s+hash ([0-9a-f]+)', line)
        if key and m: table.setdefault(key, {}).setdefault(i, {})['hash'] = m.group(1)
bad = 0
for key in sorted(table):
    runs = table[key]
    hashes = {runs[i].get('hash') for i in runs}; chrons = {runs[i].get('chron') for i in runs}
    ok = len(runs) == len(reps) and len(hashes) == 1 and len(chrons) == 1
    bad += 0 if ok else 1
    print(f"seed {key[0]} {key[1]:8s} {'same' if ok else 'DIFFERENT'}  hash {sorted(map(str,hashes))}  chronicle {sorted(map(str,chrons))}  ({len(runs)} reps)")
print(f"{len(reps)} reps, {len(table)} worlds, {bad} differ")
sys.exit(1 if bad else 0)

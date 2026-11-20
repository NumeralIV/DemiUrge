"""Provenance sidecars for Google Flow images (docs/ASSETS.md: "a provenance file each").

    python tools/flow_provenance.py backfill [--dry]
        Writes <image>.provenance.json next to every JPEG in art/textures-flow/ that has none, from what the
        file itself proves (its C2PA manifests: generation and download timestamps, generated vs edited,
        upscaled) and the project's records (the prompt template and the pilot's exact prompts in
        art/textures-pilot/README.md). Never overwrites an existing sidecar.

    python tools/flow_provenance.py ingest <downloaded file> <dest .jpg> --prompt="..." [--model="Nano Banana 2.1"]
        For a new Flow download: checks it is a 2048 x 2048 JPEG with a Google C2PA manifest, moves it to <dest>
        (inside the project) and writes <dest stem>.provenance.json with the exact prompt.

    python tools/flow_provenance.py show <image.jpg>      # print the C2PA summary

System Python + Pillow only. The C2PA store is read with a byte-level scan (no c2pa library): enough to get each
manifest's id, its RFC 3161 time-stamp, its actions and whether it has an ingredient (a parent image).
"""
import datetime as dt
import hashlib
import json
import os
import re
import shutil
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
LIB = os.path.join(ROOT, 'art', 'textures-flow')
PILOT_FLOW = os.path.join(ROOT, 'art', 'textures-pilot', 'flow')
ACCOUNT = "owner's Google account (signed in to Flow in the project browser; address not written out)"
TOOL = 'Google Flow (labs.google/flow)'
MODEL = 'Nano Banana 2.1'
LOCAL_TZ = dt.timezone(dt.timedelta(hours=-5), 'EST')   # the owner's PC clock (file times are -05:00)

TEMPLATE = ("Seamless tileable texture of {material, its age and condition}, {the shapes, sizes and how they are "
            "laid}, {two or three colours}, seen straight on{, from above for roofs and floors}, orthographic, "
            "flat even overcast lighting, no shadows, no perspective, fills the whole square edge to edge, tiles "
            "without visible seams, photorealistic material scan, high detail, no text")
PILOT_PROMPTS = {   # art/textures-pilot/README.md, "The prompt recipe that worked" (exact, recorded by the pilot)
    'stone': ("Seamless tileable texture of an ancient rubble stone wall, irregular rounded fieldstones in warm grey "
              "and ochre bedded in rough clay mortar, seen straight on, orthographic, flat even overcast lighting, "
              "no shadows, no perspective, fills the whole square edge to edge, tiles without visible seams, "
              "photorealistic material scan, high detail, no text"),
    'thatch': ("Seamless tileable texture of weathered reed thatch roofing, bundles of dry golden-brown reeds in "
               "overlapping courses, seen straight on from above, orthographic, flat even overcast lighting, no "
               "shadows, no perspective, fills the whole square edge to edge, tiles without visible seams, "
               "photorealistic material scan, high detail, no text"),
}


# ------------------------------------------------------------------------------------------------- C2PA scan
def c2pa_summary(path):
    """List of manifests in file order: {urn, timestamp_utc, actions, has_ingredient}. Only the first block of
    each URN carries its claim; later repeats are references inside other manifests and are skipped."""
    b = open(path, 'rb').read(600000)
    starts = [(m.start(), m.group().decode()) for m in re.finditer(rb'urn:c2pa:[0-9a-f-]{36}', b)]
    seen, out = set(), []
    for i, (st, urn) in enumerate(starts):
        en = starts[i + 1][0] if i + 1 < len(starts) else len(b)
        seg = b[st:en]
        if urn in seen:
            continue
        t = re.search(rb'itstTokens.{0,200}?(20\d{12})Z', seg, re.S)
        if not t:
            continue
        seen.add(urn)
        ts = dt.datetime.strptime(t.group(1).decode(), '%Y%m%d%H%M%S').replace(tzinfo=dt.timezone.utc)
        acts = [a.decode() for a in re.findall(rb'c2pa\.(created|edited|opened|placed|converted|cropped|resized)', seg)]
        out.append(dict(urn=urn, timestamp_utc=ts.isoformat().replace('+00:00', 'Z'), actions=acts,
                        has_ingredient=b'c2pa.ingredient' in seg,
                        description=('Created by Google Generative AI' if b'Created by Google Generative AI' in seg
                                     else None)))
    return out


def classify(mans):
    """generated: [created]; then optional [created + ingredient] = an edit of an input image; last
    [opened, resized] = Flow's upscale on download."""
    gen = [m for m in mans if 'created' in m['actions'] and not m['has_ingredient']]
    edit = [m for m in mans if 'created' in m['actions'] and m['has_ingredient']]
    up = [m for m in mans if 'resized' in m['actions']]
    return gen, edit, up


def image_facts(path):
    from PIL import Image
    with Image.open(path) as im:
        fmt, size, mode = im.format, im.size, im.mode
    h = hashlib.sha256(open(path, 'rb').read()).hexdigest()
    return dict(format=fmt, width=size[0], height=size[1], mode=mode, bytes=os.path.getsize(path), sha256=h)


def local(ts_utc):
    t = dt.datetime.fromisoformat(ts_utc.replace('Z', '+00:00'))
    return t.astimezone(LOCAL_TZ).isoformat()


def rel(p):
    return os.path.relpath(p, ROOT).replace('\\', '/')


def sidecar(img):
    return os.path.splitext(img)[0] + '.provenance.json'


# ------------------------------------------------------------------------------------------------- records
def base_record(img, mans, facts):
    gen, edit, up = classify(mans)
    first = gen[0] if gen else None
    rec = dict(
        file=rel(img), tool=TOOL, model=MODEL,
        date_generated_utc=(edit[-1] if edit else first or {}).get('timestamp_utc'),
        date_generated_local=local((edit[-1] if edit else first)['timestamp_utc']) if (edit or first) else None,
        date_downloaded_utc=up[-1]['timestamp_utc'] if up else None,
        aspect='1:1', download='2K upscaled' if up and facts['width'] == 2048 else 'unknown',
        download_size='%d x %d %s, %d bytes' % (facts['width'], facts['height'], facts['format'], facts['bytes']),
        sha256=facts['sha256'], credits='0 (owner\'s subscription, docs/ASSETS.md; not shown per image)',
        account=ACCOUNT,
        c2pa=dict(manifests=mans, reads=(
            'Google-signed C2PA: "Created by Google Generative AI" (digitalSourceType trainedAlgorithmicMedia), '
            'SynthID watermark applied' + ('; then an edit of an input image' if edit else '') +
            ('; then opened and resized (the 2K upscale on download)' if up else '') + '.')),
    )
    return rec, gen, edit


def backfill(dry=False):
    imgs = []
    for d, _, fs in os.walk(LIB):
        imgs += [os.path.join(d, f) for f in fs if f.lower().endswith('.jpg')]
    imgs.sort()
    gen_urn = {}                                  # generation manifest urn -> library file
    summ = {}
    for p in imgs:
        summ[p] = c2pa_summary(p)
        g, _, _ = classify(summ[p])
        if g and not classify(summ[p])[1]:
            gen_urn[g[0]['urn']] = p
    pilot = {}
    if os.path.isdir(PILOT_FLOW):
        for f in os.listdir(PILOT_FLOW):
            if f.endswith('.jpg'):
                q = os.path.join(PILOT_FLOW, f)
                pilot[hashlib.sha256(open(q, 'rb').read()).hexdigest()] = q
    wrote = skipped = 0
    for p in imgs:
        sc = sidecar(p)
        if os.path.exists(sc):
            skipped += 1
            continue
        facts = image_facts(p)
        rec, gen, edit = base_record(p, summ[p], facts)
        same = pilot.get(facts['sha256'])
        if same:
            fam = 'stone' if 'stone' in os.path.basename(same) else 'thatch'
            rec['prompt'] = PILOT_PROMPTS[fam]
            rec['prompt_status'] = ('exact, recorded in art/textures-pilot/README.md ("The prompt recipe that '
                                    'worked"); one prompt per material, two images')
            rec['same_bytes_as'] = rel(same)
            rec['model_status'] = 'recorded by the pilot README'
        elif edit:
            parent = gen_urn.get(gen[0]['urn']) if gen else None
            rec['prompt'] = None
            rec['prompt_status'] = ('edit in Flow of an input image (C2PA: a second "created" manifest with an '
                                    'ingredient); edit instruction not recorded')
            rec['parent'] = rel(parent) if parent else None
            rec['parent_status'] = ('the input image\'s generation manifest (%s) is the same as this library '
                                    'file\'s' % gen[0]['urn']) if parent else 'parent not found in the library'
            rec['model_status'] = 'assumed from the project recipe; C2PA does not name the model'
        else:
            rec['prompt'] = TEMPLATE
            rec['prompt_status'] = ('template, exact wording not recorded (art/textures-pilot/README.md "Prompt '
                                    'template"; the generating agent\'s transcript is not available)')
            rec['material_hint'] = os.path.basename(os.path.dirname(p))
            rec['model_status'] = 'assumed from the project recipe; C2PA does not name the model'
        rec['file_mtime_local'] = dt.datetime.fromtimestamp(os.path.getmtime(p), LOCAL_TZ).isoformat()
        rec['recorded'] = '%s, backfilled by tools/flow_provenance.py' % dt.date.today().isoformat()
        if not dry:
            with open(sc, 'w', encoding='utf-8') as f:
                json.dump(rec, f, indent=1, ensure_ascii=False)
        wrote += 1
        print('PROV', rel(sc), rec['date_generated_local'], rec['prompt_status'].split(' (')[0][:40])
    print('PROV_DONE wrote=%d skipped(existing)=%d total=%d' % (wrote, skipped, len(imgs)))


def ingest(src, dest, prompt, model=MODEL):
    from PIL import Image
    dest = os.path.abspath(dest)
    if not dest.startswith(ROOT + os.sep):
        sys.exit('dest must be inside the project: %s' % dest)
    with Image.open(src) as im:
        if im.format != 'JPEG' or im.size != (2048, 2048):
            sys.exit('not a 2048 x 2048 JPEG: %s %s %s' % (src, im.format, im.size))
    mans = c2pa_summary(src)
    gen, _, _ = classify(mans)
    if not gen:
        sys.exit('no Google C2PA generation manifest in %s' % src)
    if os.path.exists(dest):
        sys.exit('exists, not overwriting: %s' % dest)
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    shutil.move(src, dest)
    facts = image_facts(dest)
    rec, _, _ = base_record(dest, mans, facts)
    rec.update(model=model, prompt=prompt, prompt_status='exact', model_status='as set in Flow',
               recorded='%s, at download, by tools/flow_provenance.py' % dt.date.today().isoformat())
    with open(sidecar(dest), 'w', encoding='utf-8') as f:
        json.dump(rec, f, indent=1, ensure_ascii=False)
    print('INGEST', rel(dest), rec['date_generated_local'])


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    fl = dict(x[2:].split('=', 1) for x in a if x.startswith('--') and '=' in x)
    pos = [x for x in a if not x.startswith('--')]
    if pos[0] == 'backfill':
        backfill('--dry' in a)
    elif pos[0] == 'ingest':
        ingest(pos[1], pos[2], fl['prompt'], fl.get('model', MODEL))
    elif pos[0] == 'show':
        print(json.dumps(c2pa_summary(pos[1]), indent=1))
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()

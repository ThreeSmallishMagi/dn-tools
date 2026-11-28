#!/usr/bin/env bash
# Rebuild the dither tour page from doc/tour/dither-tour.html: every figure from
# the current build, then the self-contained HTML.  Run after `build`.
#   doc/tour/build.sh [work-dir]
# Installs the shipped films into a private config and calibrates Kodak-4X and
# Kodak-2484 first (a few minutes), so the masks are what a user gets after
# install_dither.  Writes <work-dir>/dither-tour.built.html.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd); cd "$ROOT"
TOUR=$(mkdir -p "${1:-/tmp/dither-tour}" && cd "${1:-/tmp/dither-tour}" && pwd)
I=$TOUR/img; W=$TOUR/web; mkdir -p "$I" "$W"
export PATH="$ROOT/dither_env/bin:$PATH" XDG_CONFIG_HOME=$TOUR/cfg
cp doc/tour/dither-tour.html "$TOUR/dither-tour.html"

if [ ! -d "$XDG_CONFIG_HOME/dither/crops" ]; then
    bash -c 'source ./aliases >/dev/null 2>&1; install_samples --no-calibrate && calibrate_samples Kodak-4X Kodak-2484'
fi
CROP=$XDG_CONFIG_HOME/dither/crops/Kodak-4X.png

python3 - "$TOUR" "$CROP" <<'PY'
import cv2, numpy as np, sys
T, crop = sys.argv[1], sys.argv[2]
i = cv2.imread('examples/m039crop.png', cv2.IMREAD_GRAYSCALE)
small = cv2.resize(i, (256, 256), interpolation=cv2.INTER_AREA)
cv2.imwrite(f'{T}/dog.png', small); cv2.imwrite(f'{T}/img/00-source.png', small)
cv2.imwrite(f'{T}/hero_src.png', i)
f = cv2.imread(crop, cv2.IMREAD_GRAYSCALE); y, x = (f.shape[0] - 256) // 2, (f.shape[1] - 256) // 2
cv2.imwrite(f'{T}/img/20-film.png', f[y:y + 256, x:x + 256])
cv2.imwrite(f'{T}/strip.png', np.tile(np.linspace(8, 247, 640, dtype=np.uint8), (28, 1)))
PY

D="build/dither $TOUR/dog.png"; S="--mask-sample Kodak-4X"; T=$TOUR/t.png
q() { "$@" >/dev/null 2>&1; }
q $D --threshold round  --spatial 0 --out "$I/01-round.png"
q $D --threshold random --spatial 0 --out "$I/02-random.png"
q $D --threshold mask   --spatial 0 --out "$I/03-mask.png"
q $D --spatial 0   --out "$I/04-sp0.png"
q $D --spatial 0.5 --out "$I/05-sp05.png"
q $D --spatial 1   --out "$I/06-sp1.png"
q $D --levels 2  --out "$I/07-lev2.png"
q $D --levels 4  --out "$I/08-lev4.png"
q $D --levels 16 --out "$I/09-lev16.png"
q $D --threshold mask --mask-method blue --mask-size 256x256 --mask-out "$I/10-m-blue.png" --out "$T"
for m in dft markov inpaint direct; do
    q $D --mask-method $m $S --mask-size 256x256 --mask-out "$I/1x-m-$m.png" --out "$T"
done
q $D --threshold mask --mask-method blue --supersample 4 --out "$I/2x-dog-blue.png"
q $D --mask-method inpaint $S --supersample 4 --out "$I/2x-dog-inpaint.png"
for sc in 1 2 4; do q $D --mask-method dft $S --mask-scale $sc --mask-size 256x256 --mask-out "$I/3x-scale$sc.png" --out "$T"; done
for ss in 1 4 8; do q $D --mask-method inpaint $S --supersample $ss --out "$I/4x-ss$ss.png"; done
for fr in 1 4 16; do q $D --threshold mask --frames $fr --spatial 0 --out "$I/5x-fr$fr.png"; done
q $D --threshold mask --supersample 4 --out-size 1024 --out "$TOUR/grid.png"
q $D --threshold mask --supersample 4 --out-size 128 --out "$I/6x-small.png"
q build/maketexture "$CROP" --sample 256x256+128+128 --size 256x256 --paste "$TOUR/paste.png" --out "$I/7x-texture.png"
q build/dither "$TOUR/hero_src.png" --mask-method inpaint $S --spatial 1 --out "$TOUR/hero.png"
q build/dither "$TOUR/strip.png" --threshold mask --spatial 0 --out "$TOUR/strip_d.png"

python3 - "$TOUR" <<'PY'
import base64, cv2, glob, os, re, sys
T = sys.argv[1]; I, W = f'{T}/img', f'{T}/web'
cv2.imwrite(f'{I}/6x-grid.png', cv2.imread(f'{T}/grid.png', 0)[256:512, 256:512])
cv2.imwrite(f'{I}/7x-paste.png', cv2.imread(f'{T}/paste.png', 0)[96:480, 96:480])
TONE = ('00-source', '20-film', '10-m-', '1x-m-', '3x-scale', '7x-', '4x-ss8', '6x-small')
for f in sorted(glob.glob(f'{I}/*.png')):
    b = os.path.basename(f)[:-4]; img = cv2.imread(f, 0)
    if b.startswith(TONE): cv2.imwrite(f'{W}/{b}.jpg', img, [cv2.IMWRITE_JPEG_QUALITY, 92])
    else: cv2.imwrite(f'{W}/{b}.png', img, [cv2.IMWRITE_PNG_COMPRESSION, 9])
cv2.imwrite(f'{W}/hero-src.jpg', cv2.imread(f'{T}/hero_src.png', 0), [cv2.IMWRITE_JPEG_QUALITY, 90])
cv2.imwrite(f'{W}/hero-dither.png', cv2.imread(f'{T}/hero.png', 0), [cv2.IMWRITE_PNG_COMPRESSION, 9])
cv2.imwrite(f'{W}/strip.png', cv2.imread(f'{T}/strip_d.png', 0), [cv2.IMWRITE_PNG_COMPRESSION, 9])
mime = {'.png': 'image/png', '.jpg': 'image/jpeg'}
uri = lambda n: f'data:{mime[os.path.splitext(n)[1]]};base64,' + base64.b64encode(open(f'{W}/{n}', 'rb').read()).decode()
src = open(f'{T}/dither-tour.html').read()
missing = [n for n in re.findall(r'\{\{([^}]+)\}\}', src) if not os.path.exists(f'{W}/{n}')]
if missing: sys.exit(f'missing images: {missing}')
out = re.sub(r'\{\{([^}]+)\}\}', lambda m: uri(m.group(1)), src)
out = out.replace('--col: 68ch;', f'--col: 68ch;\n    --strip: url({uri("strip.png")});', 1)
open(f'{T}/dither-tour.built.html', 'w').write(out)
print(f'{T}/dither-tour.built.html, {len(out) // 1024} KB')
PY

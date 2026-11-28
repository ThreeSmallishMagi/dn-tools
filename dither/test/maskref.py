"""How a mask's reference must be prepared, asked of the program that built it.

The scoring tools compare a mask with its sample prepared the way the
generator prepared it. The high-pass that preparation uses is a default the
generator table decides -- 32 for dft and direct, 0 for markov and inpaint --
so a tool that kept its own copy of the number scored a mask from one
generator against a reference made for another. They used to default to 32
for every mask, which went wrong silently the day markov and inpaint stopped
high-passing. So the tools take the generator by name and ask `dither
--explain` what it resolves to, and the number lives in one place.
"""
import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DITHER = os.path.join(ROOT, 'build', 'dither')
# Any readable sample will do: the high-pass depends on the generator, not on
# the pixels, but the program will not resolve a sample generator without one.
PROBE = os.path.join(ROOT, 'examples', 'Kodak-4X-1-crop.png')
METHODS = ('dft', 'markov', 'inpaint', 'direct')


def resolved(method, key):
    """The value `key` resolves to for `method`, as --explain prints it."""
    r = subprocess.run([DITHER, '--explain', '--no-config', '--mask-method', method,
                        '--mask-sample', PROBE], capture_output=True, text=True)
    for line in (r.stdout + r.stderr).splitlines():
        m = re.match(r'^\s+' + re.escape(key) + r'\s+([0-9.]+)\s', line)
        if m:
            return float(m.group(1))
    raise SystemExit(f"{DITHER} --explain did not report {key} for {method}")


def highpass_for(method):
    """The --mask-highpass `method` uses when none is given."""
    return resolved(method, 'mask.highpass')


def add_arguments(ap, what):
    """--method and --highpass, the second overriding the first."""
    ap.add_argument('--method', choices=METHODS,
                    help=f"the generator that built the mask(s); {what} uses the "
                         "high-pass it uses, as dither --explain reports it")
    ap.add_argument('--highpass', type=float, default=None, metavar='PX',
                    help=f"the high-pass {what} uses, in px, instead of --method's "
                         "(0 for none)")


def resolve(args, ap):
    """Fill args.highpass, refusing to guess when neither option was given.

    direct is the exception that cannot be resolved this way. By default it does
    not high-pass the sample it ranks: it equalises it, ranking each pixel within
    a neighbourhood of direct.equalise px, and its high-pass is only a check that
    the sample has texture. These tools model a subtractive high-pass and
    nothing else, so a reference built from direct's mask.highpass is a
    different preparation from the mask's -- on a shaded sample levelfit scored
    a direct mask 1.43 against its own preparation's 0.00. Rather than report
    that as a fit, --method direct is refused while equalising is on; grainfit,
    whose blocks take their own quantiles, judges direct fairly, and --highpass
    still asks for a subtractive reference on purpose."""
    if args.highpass is None:
        if args.method is None:
            ap.error("say how the mask was built: --method dft|markov|inpaint|direct, "
                     "or --highpass PX")
        if args.method == 'direct':
            eq = resolved('direct', 'direct.equalise')
            if eq > 0:
                ap.error(f"direct equalises its sample (direct.equalise {eq:g} px) rather "
                         "than high-passing it, which this tool does not model. Score "
                         "it with test/grainfit, or pass --highpass PX to compare "
                         "against a subtractive high-pass deliberately")
        args.highpass = highpass_for(args.method)
    return args.highpass

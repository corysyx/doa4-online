"""Writes tests/host/suitconv_pins.h: bit-exact pins for the suit converter's numeric kit (src/doa4_suitconv_num.h),
and tests/host/suitconv_sort_pins.h: numpy's default argsort and argmin (src/doa4_suitconv_sort.h),
and tests/host/suitconv_tpr_pins.h: the recipe tpr.py's dec_hend3n and world_mats (src/doa4_suitconv_tpr.h).

  <CPython 3.12.1 with numpy 1.26.3> -I -B tests/host/make_suitconv_pins.py [out.h [sort_out.h [tpr_out.h]]]

The converter being ported runs on CPython 3.12.1 and numpy 1.26.3, so the pins come from exactly that interpreter (it
refuses to run on any other). Every input is synthetic: seeded random values plus searched and hand-picked edge cases.
The script reads no file and opens none except the header it writes; the same run gives the same bytes.

For each primitive the header holds inputs and outputs as hex bits, and where a plausible wrong implementation exists
(a naive sum, floor(x / y), the floor rounding formula, rounding to half through float, another association of a numpy
reduction, the other branch of numpy's _lerp) it also holds that wrong value, and this script asserts that at least
one pin tells the two apart. Otherwise a test could pass on the wrong code.
"""
import sys

if sys.version_info[:3] != (3, 12, 1):
    sys.exit(f'make_suitconv_pins.py needs CPython 3.12.1, this is {sys.version.split()[0]}')
import numpy as np  # noqa: E402

if np.__version__ != '1.26.3':
    sys.exit(f'make_suitconv_pins.py needs numpy 1.26.3, this is {np.__version__}')

import math  # noqa: E402
import os  # noqa: E402
import random  # noqa: E402
import struct  # noqa: E402
import warnings  # noqa: E402
from fractions import Fraction  # noqa: E402

SEED = 20261107
rng = random.Random(SEED)
NAN = float('nan')
INF = float('inf')


def db(x):
    """Bits of a double."""
    return struct.unpack('<Q', struct.pack('<d', float(x)))[0]


def bd(u):
    """Double from bits."""
    return struct.unpack('<d', struct.pack('<Q', u))[0]


def wide(lo_exp, hi_exp):
    """A random finite double with a random sign, mantissa and an exponent in [lo_exp, hi_exp] (unbiased)."""
    e = rng.randint(lo_exp, hi_exp) + 1023
    return bd((rng.getrandbits(1) << 63) | (e << 52) | rng.getrandbits(52))


def mixed():
    """A random value with a wide spread of magnitudes, so additions round and associations differ."""
    return rng.gauss(0.0, 1.0) * 10.0 ** rng.randint(-4, 4)


out = []


def emit(s):
    out.append(s)


def u64s(name, values, comment):
    """A static array of uint64 hex literals, 6 per line."""
    vals = list(values)
    emit(f'// {comment}\nstatic const uint64_t {name}[] = {{\n')
    for i in range(0, len(vals), 6):
        emit(','.join(f'0x{v:016x}' for v in vals[i:i + 6]) + ',\n')
    if not vals:
        emit('0,\n')
    emit('};\n')
    emit(f'static constexpr size_t {name}Count = {len(vals)};\n\n')


def u32s(name, values, comment):
    vals = list(values)
    emit(f'// {comment}\nstatic const uint32_t {name}[] = {{\n')
    for i in range(0, len(vals), 8):
        emit(','.join(f'0x{v:08x}' for v in vals[i:i + 8]) + ',\n')
    if not vals:
        emit('0,\n')
    emit('};\n')
    emit(f'static constexpr size_t {name}Count = {len(vals)};\n\n')


def cstr(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def message_of(fn):
    try:
        fn()
    except Exception as e:  # the message the converter's ConvertError must carry
        return type(e).__name__, str(e)
    raise AssertionError('expected an exception')


# ---------------------------------------------------------------------------------------------------------------
# PySum: builtin sum() of floats, starting from int 0.
def naive_sum(xs):
    s = 0.0
    for v in xs:
        s += v
    return s


sum_cases = [[1e16, 1.0, -1e16], [], [-0.0], [-0.0, -0.0], [0.1] * 10, [INF], [INF, 1.0], [INF, -INF],
             [1e308, 1e308], [1e308, 1e308, -1e308], [NAN, 1.0], [1.0, NAN], [5e-324, -5e-324, 5e-324],
             [1e100, 1.0, -1e100, 1e-100], [2.0 ** 53, 1.0, 1.0, -(2.0 ** 53)]]
for _ in range(400):
    n = rng.randint(1, 60)
    sum_cases.append([mixed() for _ in range(n)])
for _ in range(100):  # heavy cancellation
    n = rng.randint(2, 30)
    xs = [wide(-20, 60) for _ in range(n)]
    xs.append(-sum(xs))
    rng.shuffle(xs)
    sum_cases.append(xs)
sum_in, sum_tab = [], []
for xs in sum_cases:
    got = float(sum(xs))
    sum_tab += [len(sum_in), len(xs), db(got), db(naive_sum(xs))]
    sum_in += [db(v) for v in xs]
assert sum([1e16, 1.0, -1e16]) == 1.0 and naive_sum([1e16, 1.0, -1e16]) == 0.0
assert any(sum_tab[i + 2] != sum_tab[i + 3] for i in range(0, len(sum_tab), 4)), 'no PySum pin separates naive'

# ---------------------------------------------------------------------------------------------------------------
# PyFloorDiv: float // float (CPython float_floor_div).
def np_floor_div(x, y):
    with np.errstate(all='ignore'):
        return float(np.floor(np.float64(x) / np.float64(y)))


fd_pairs = [(1.0, 0.1), (-1.0, 0.1), (7.0, 2.0), (-7.0, 2.0), (7.0, -2.0), (-7.0, -2.0), (0.0, 3.0), (-0.0, 3.0),
            (0.0, -3.0), (-0.0, -3.0), (1e-300, 1e300), (-1e-300, 1e300), (1.0, INF), (-1.0, INF), (1.0, -INF),
            (-1.0, -INF), (INF, 1.0), (-INF, 1.0), (NAN, 1.0), (1.0, NAN), (5e-324, 1.0), (-5e-324, 1.0),
            (1e308, 1e-308), (0.3, 0.1), (0.7, 0.1), (2.0 ** 60 + 2.0 ** 8, 3.0)]
found = 0
while found < 60:  # x = k*y rounded, where x / y rounds to k but the true quotient is below k
    y = rng.uniform(0.001, 10.0)
    k = rng.randint(1, 100000)
    x = k * y
    if rng.random() < 0.5:
        x, y = -x, y
    if x // y != np_floor_div(x, y):
        fd_pairs.append((x, y))
        found += 1
for _ in range(300):
    fd_pairs.append((wide(-30, 30), wide(-30, 30)))
for _ in range(100):
    y = mixed()
    if y != 0.0:
        fd_pairs.append((mixed(), y))
fd_tab = []
for x, y in fd_pairs:
    fd_tab += [db(x), db(y), db(x // y), db(np_floor_div(x, y))]
assert 1.0 // 0.1 == 9.0 and np_floor_div(1.0, 0.1) == 10.0
assert any(fd_tab[i + 2] != fd_tab[i + 3] for i in range(0, len(fd_tab), 4)), 'no PyFloorDiv pin separates floor(x/y)'
FD_ZERO = message_of(lambda: 1.0 // 0.0)

# ---------------------------------------------------------------------------------------------------------------
# PyRoundDigits: round(x, n), correctly rounded through decimal (CPython double_round with dtoa).
def floor_round(x, n):
    if not math.isfinite(x):
        return x
    p = 10.0 ** n
    v = x * p + 0.5
    if not math.isfinite(v):
        return x
    return float(math.floor(v)) / p


rd_cases = [(0.125, 2), (0.375, 2), (2.675, 2), (0.5, 0), (1.5, 0), (2.5, 0), (-0.5, 0), (-2.5, 0), (-0.0, 2),
            (0.0, 0), (-0.001, 2), (0.001, 2), (0.03125, 4), (0.09375, 4), (0.015625, 6), (NAN, 4), (INF, 4), (-INF, 6),
            (1e300, 17), (-1e300, 0), (1e-300, 17), (-1e-300, 17), (5e-324, 17), (1.7976931348623157e308, 0),
            (123456.789012345, 4), (0.1 + 0.2, 17), (1.0 / 3.0, 17), (2.0 / 3.0, 16)]
diff4 = diff6 = 0
while diff4 < 40 or diff6 < 40:  # x near (k + 0.5) / 10^n, where x * 10^n rounds onto the half
    n = rng.choice((4, 6))
    k = rng.randint(0, 10 ** 7)
    x = (k + 0.5) / 10.0 ** n
    x = bd(db(x) + rng.choice((-1, 0, 1)))
    if rng.random() < 0.3:
        x = -x
    if round(x, n) != floor_round(x, n):
        rd_cases.append((x, n))
        if n == 4:
            diff4 += 1
        else:
            diff6 += 1
for _ in range(600):
    rd_cases.append((mixed() * rng.choice((1.0, 1e3, 1e-3)), rng.randint(0, 17)))
for _ in range(200):
    rd_cases.append((wide(-60, 60), rng.randint(0, 17)))
rd_tab = []
for x, n in rd_cases:
    rd_tab += [db(x), n, db(round(x, n)), db(floor_round(x, n))]
for nd in (4, 6):
    assert any(rd_tab[i + 1] == nd and rd_tab[i + 2] != rd_tab[i + 3] for i in range(0, len(rd_tab), 4)), \
        f'no round(x, {nd}) pin separates the floor formula'

# ---------------------------------------------------------------------------------------------------------------
# RoundHalfEven: round(x) (an int in Python, so no signed zero), against C round() (half away from zero).
def c_round(x):
    a = math.floor(abs(Fraction(x)) + Fraction(1, 2))
    return math.copysign(float(a), x)


rh_cases = [k + 0.5 for k in range(-10, 10)] + [-0.0, 0.0, 0.4, -0.4, 0.6, -0.6, 0.49999999999999994,
                                                -0.49999999999999994, 4503599627370495.5, -4503599627370495.5,
                                                4503599627370497.0, 2.0 ** 60, -(2.0 ** 60), 1e300, -1e300, 5e-324,
                                                -5e-324, 1.7976931348623157e308]
for _ in range(400):
    rh_cases.append(mixed() * 1000.0)
for _ in range(100):
    rh_cases.append(rng.randint(-100000, 100000) + 0.5)
rh_tab = []
for x in rh_cases:
    rh_tab += [db(x), db(float(round(x))), db(c_round(x))]
assert any(rh_tab[i + 1] != rh_tab[i + 2] for i in range(0, len(rh_tab), 3)), 'no round(x) pin separates C round()'
RH_INF = message_of(lambda: round(INF))
RH_NAN = message_of(lambda: round(NAN))

# ---------------------------------------------------------------------------------------------------------------
# PackF32: struct.pack('>f', x).
FLT_MAX = struct.unpack('>f', bytes.fromhex('7f7fffff'))[0]
FLT_TIE = float(Fraction(FLT_MAX) + Fraction(2) ** 103)  # FLT_MAX + half an ulp: rounds to 2^128, so overflows
f32_cases = [0.0, -0.0, 1.0, -1.0, FLT_MAX, -FLT_MAX, bd(db(FLT_TIE) - 1), -bd(db(FLT_TIE) - 1), INF, -INF, NAN,
             -NAN, bd(0x7ff0000000000001), bd(0x7ff4000000000000), 1e-50, -1e-50, 1.401298464324817e-45,
             7.006492321624085e-46, 7.006492321624087e-46, 1.1754943508222875e-38, 0.1, 1.0 / 3.0,
             1.0 + 2.0 ** -24, 1.0 + 3 * 2.0 ** -24, 1.0 + 2.0 ** -24 + 2.0 ** -60]
for _ in range(500):
    f32_cases.append(wide(-150, 126))
for _ in range(200):  # exact ties between two floats (f + half its ulp), both parities
    f = struct.unpack('>f', struct.pack('>f', wide(-120, 120)))[0]
    f32_cases.append(f + math.copysign(2.0 ** (math.frexp(f)[1] - 25), f))
f32_tab = []
for x in f32_cases:
    f32_tab += [db(x), struct.unpack('>I', struct.pack('>f', x))[0]]
f32_over = [FLT_TIE, -FLT_TIE, 1e39, -1e39, 1.7976931348623157e308]
F32_OVER = message_of(lambda: struct.pack('>f', 1e39))
for x in f32_over:
    assert message_of(lambda: struct.pack('>f', x)) == F32_OVER

# ---------------------------------------------------------------------------------------------------------------
# PackF16: struct.pack('>e', x) (PyFloat_Pack2 rounds the double directly), against rounding to float first.
def pack16(x):
    return struct.unpack('>H', struct.pack('>e', x))[0]


def pack16_via_float(x):
    """0x10000 when the float rounding lands on the overflow (65519.99999999999 rounds to 65520.0f)."""
    try:
        return pack16(struct.unpack('>f', struct.pack('>f', x))[0])
    except OverflowError:
        return 0x10000


f16_cases = [0.0, -0.0, 1.0, -1.0, 65504.0, -65504.0, 65519.99999999999, -65519.99999999999, INF, -INF, NAN, -NAN,
             2.0 ** -24, 2.0 ** -25, bd(db(2.0 ** -25) + 1), 2.0 ** -26, 3 * 2.0 ** -25, 2.0 ** -14, bd(db(2.0 ** -14) - 1),
             1e-10, 5e-324, 0.1, 1.0 / 3.0, 1.0 + 2.0 ** -11, 1.0 + 3 * 2.0 ** -11, 1.0 + 2.0 ** -11 + 2.0 ** -40,
             1.0 + 2.0 ** -11 - 2.0 ** -40]
dr = 0
while dr < 40:  # just above a half-ulp tie of the half format, by less than float keeps: float rounds onto the tie
    h = rng.randint(0x0001, 0x7bfe)
    v = struct.unpack('>e', struct.pack('>H', h))[0]
    e = math.frexp(v)[1]
    half_ulp = 2.0 ** (max(e - 1, -14) - 11)
    x = v + half_ulp + half_ulp * 2.0 ** -rng.randint(20, 40)
    x = -x if rng.random() < 0.5 else x
    if abs(x) < 65504.0 and pack16(x) != pack16_via_float(x):
        f16_cases.append(x)
        dr += 1
for _ in range(500):
    f16_cases.append(wide(-26, 15))
f16_tab = []
for x in f16_cases:
    f16_tab += [db(x), pack16(x), pack16_via_float(x)]
assert any(f16_tab[i + 1] != f16_tab[i + 2] for i in range(0, len(f16_tab), 3)), 'no PackF16 pin separates via-float'
f16_over = [65520.0, -65520.0, 1e10, 1.7976931348623157e308]
F16_OVER = message_of(lambda: struct.pack('>e', 65520.0))
for x in f16_over:
    assert message_of(lambda: struct.pack('>e', x)) == F16_OVER

# HalfToDouble: every half, as one FNV-1a 64 digest over the 8 little-endian bytes of each result, h = 0..65535;
# plus explicit pins for the special values and a random sample (readable failures).
def fnv1a64(data):
    h = 0xcbf29ce484222325
    for byte in data:
        h ^= byte
        h = (h * 0x100000001b3) & 0xffffffffffffffff
    return h


h2d_all = b''.join(struct.pack('<d', struct.unpack('>e', struct.pack('>H', h))[0]) for h in range(65536))
H2D_DIGEST = fnv1a64(h2d_all)
h2d_pick = [0x0000, 0x8000, 0x0001, 0x8001, 0x03ff, 0x0400, 0x3c00, 0xbc00, 0x7bff, 0xfbff, 0x7c00, 0xfc00, 0x7c01,
            0xfc01, 0x7e00, 0xfe00, 0x7fff, 0xffff, 0x3555, 0x5640]
h2d_pick += [rng.randint(0, 0xffff) for _ in range(300)]
h2d_tab = []
for h in h2d_pick:
    h2d_tab += [h, db(struct.unpack('>e', struct.pack('>H', h))[0])]

# ---------------------------------------------------------------------------------------------------------------
# Exp and Atan2 against numpy's array loops (np.exp, np.arctan2 on float64 arrays, the recipe's call shape).
exp_in = [0.0, -0.0, 1.0, -1.0, INF, -INF, NAN, 709.782712893384, 709.7827128933841, 710.0, -745.1332191019411,
          -745.1332191019412, -746.0, -708.3964185322641, -708.3964185322642, 2.0 ** -54, -(2.0 ** -54), 2.0 ** -55,
          bd(db(2.0 ** -54) - 1), 512.0, -512.0, bd(db(512.0) - 1), 1024.0, -1024.0, 1e-300, 5e-324, -5e-324, -50.0]
for _ in range(10000):  # Gaussian weights: exp(-d^2 / s)
    d = rng.uniform(0.0, 3.0)
    s = rng.uniform(0.05, 2.0)
    exp_in.append(-(d * d) / s)
for _ in range(5000):
    exp_in.append(rng.uniform(-1.0, 1.0))
for _ in range(8000):
    exp_in.append(rng.uniform(-745.2, 709.8))
for _ in range(4000):
    exp_in.append(wide(-1074 + 52, 1023) if rng.random() < 0.9 else bd(rng.getrandbits(52) | (rng.getrandbits(1) << 63)))
for _ in range(1500):  # subnormal results
    exp_in.append(rng.uniform(-745.2, -708.3))
for _ in range(1500):  # around the special-case thresholds of the reduction
    exp_in.append(rng.choice((1.0, -1.0)) * rng.choice((2.0 ** -54, 512.0, 1024.0)) * (1.0 + rng.uniform(-1e-3, 1e-3)))
while len(exp_in) < 50000:
    exp_in.append(-50.0 * rng.random())
assert len(exp_in) == 50000
with np.errstate(all='ignore'):
    exp_out = np.exp(np.array(exp_in, dtype=np.float64))
exp_tab = []
for x, y in zip(exp_in, exp_out):
    exp_tab += [db(x), db(y)]
assert db(np.exp(np.array([0.0]))[0]) == db(1.0)

at_in = [(0.0, -1.0), (-0.0, -1.0), (0.0, 1.0), (-0.0, 1.0), (1.0, 0.0), (-1.0, 0.0), (1.0, -0.0), (-1.0, -0.0),
         (0.0, 0.0), (-0.0, 0.0), (0.0, -0.0), (-0.0, -0.0), (INF, INF), (-INF, INF), (INF, -INF), (-INF, -INF),
         (1.0, INF), (-1.0, INF), (1.0, -INF), (-1.0, -INF), (INF, 1.0), (-INF, -1.0), (NAN, 1.0), (1.0, NAN),
         (1.0, 1.0), (-1.0, 1.0), (1.0, -1.0), (-1.0, -1.0), (1e300, 1e-300), (-1e300, -1e-300), (1e-300, -1e300),
         (-1e-300, -1e300), (5e-324, 1.0), (5e-324, -1.0), (1.0, 5e-324)]
for _ in range(20000):  # points around a unit circle
    th = rng.uniform(-math.pi, math.pi)
    r = 1.0 + rng.uniform(-1e-3, 1e-3)
    at_in.append((r * math.sin(th), r * math.cos(th)))
for _ in range(10000):  # small offsets near the origin
    k = rng.randint(0, 12)
    at_in.append((rng.gauss(0.0, 1.0) * 10.0 ** -k, rng.gauss(0.0, 1.0) * 10.0 ** -rng.randint(0, 12)))
for _ in range(5000):  # one coordinate tiny against the other
    big = rng.choice((1.0, -1.0)) * rng.uniform(0.5, 2.0)
    tiny = rng.gauss(0.0, 1.0) * 10.0 ** -rng.randint(6, 20)
    at_in.append((big, tiny) if rng.random() < 0.5 else (tiny, big))
for _ in range(10000):
    at_in.append((wide(-1022, 1023), wide(-1022, 1023)))
for _ in range(1000):
    at_in.append((mixed(), 1.0))
for _ in range(500):
    at_in.append((mixed(), -1.0))
for _ in range(500):
    at_in.append((rng.choice((0.0, -0.0)), mixed()))
for _ in range(500):
    at_in.append((mixed(), rng.choice((0.0, -0.0))))
for _ in range(500):  # |y/x| above 2^64, and below 2^-64 with x < 0
    at_in.append((wide(30, 60), wide(-60, -40)) if rng.random() < 0.5 else (wide(-60, -40), -abs(wide(30, 60))))
while len(at_in) < 50000:
    th = rng.uniform(0.0, math.pi)
    at_in.append((math.sin(th), math.cos(th)))
assert len(at_in) == 50000
with np.errstate(all='ignore'):
    at_out = np.arctan2(np.array([p[0] for p in at_in], dtype=np.float64), np.array([p[1] for p in at_in], dtype=np.float64))
at_tab = []
for (y, x), z in zip(at_in, at_out):
    at_tab += [db(y), db(x), db(z)]

# ---------------------------------------------------------------------------------------------------------------
# numpy reductions. Candidates (each from the reduction's initial 0.0, numpy's identity for add):
#   S sequential, P pairwise over the whole length (numpy_loops_utils.h.src DOUBLE_pairwise_sum), B pairwise per
#   8192-element buffer chunk, summed sequentially. Each kind of pin must contain an output where numpy's value differs
#   from each candidate other than the one it follows, or the pins could not tell the associations apart.
def pw(a):
    n = len(a)
    if n < 8:
        r = -0.0
        for v in a:
            r += v
        return r
    if n <= 128:
        r = list(a[:8])
        i = 8
        while i < n - (n % 8):
            for k in range(8):
                r[k] += a[i + k]
            i += 8
        res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]))
        while i < n:
            res += a[i]
            i += 1
        return res
    n2 = n // 2
    n2 -= n2 % 8
    return pw(a[:n2]) + pw(a[n2:])


def cand(xs):
    s = 0.0
    for v in xs:
        s += v
    p = 0.0 + pw(xs)
    b = 0.0
    for i in range(0, len(xs), 8192):
        b += pw(xs[i:i + 8192])
    return s, p, b


pool = []  # input doubles shared by the reduction, median and percentile pins


def put(vals):
    off = len(pool)
    pool.extend(float(v) for v in vals)
    return off


BIG_N = 20000
big_off = put(mixed() for _ in range(BIG_N))
KIND = {'sum_last_axis': 0, 'sum_axis0': 1, 'sum_1d': 2, 'mean_1d': 3, 'mean_axis0': 4, 'median_axis0': 5}
red_cases = []  # kind, off, rows, cols, out_off
red_out = []  # per output: numpy, S, P, B
follows = {}  # kind label -> set of candidates numpy matched on every output
separated = {}  # kind label -> set of candidates numpy differed from on some output


def record(label, kind, off, rows, cols, got, cands):
    red_cases.append((KIND[kind], off, rows, cols, len(red_out) // 4))
    eq = {'S', 'P', 'B'}
    for g, c in zip(got, cands):
        red_out.extend([db(g), db(c[0]), db(c[1]), db(c[2])])
        for name, v in zip('SPB', c):
            if db(v) != db(g):
                eq.discard(name)
                separated.setdefault(label, set()).add(name)
    follows[label] = follows.get(label, {'S', 'P', 'B'}) & eq


def matrix(off, rows, cols):
    return np.array(pool[off:off + rows * cols], dtype=np.float64).reshape(rows, cols)


def red_sum_last(off, rows, cols):
    m = matrix(off, rows, cols)
    got, got2 = m.sum(1), m.sum(-1)
    assert [db(v) for v in got] == [db(v) for v in got2]
    record('sum_last_axis', 'sum_last_axis', off, rows, cols, got, [cand(list(map(float, m[r]))) for r in range(rows)])


def red_sum0(off, rows, cols):
    m = matrix(off, rows, cols)
    label = 'sum_axis0 cols=1' if cols == 1 else 'sum_axis0 cols>=2'
    record(label, 'sum_axis0', off, rows, cols, m.sum(0), [cand(list(map(float, m[:, j]))) for j in range(cols)])


def red_sum1d(off, n):
    a = np.array(pool[off:off + n], dtype=np.float64)
    record('sum_1d', 'sum_1d', off, n, 1, [a.sum()], [cand(pool[off:off + n])])


def red_mean1d(off, n):
    a = np.array(pool[off:off + n], dtype=np.float64)
    with warnings.catch_warnings():
        warnings.simplefilter('ignore')
        with np.errstate(all='ignore'):
            got = a.mean()
            cs = [tuple(np.float64(v) / n for v in cand(pool[off:off + n]))]
    record('mean_1d', 'mean_1d', off, n, 1, [got], cs)


def red_mean0(off, rows, cols):
    m = matrix(off, rows, cols)
    label = 'mean_axis0 cols=1' if cols == 1 else 'mean_axis0 cols>=2'
    cs = [tuple(v / rows for v in cand(list(map(float, m[:, j])))) for j in range(cols)]
    record(label, 'mean_axis0', off, rows, cols, m.mean(0), cs)


def red_median0(off, rows, cols):
    m = matrix(off, rows, cols)
    with warnings.catch_warnings():
        warnings.simplefilter('ignore')
        with np.errstate(all='ignore'):
            got = np.median(m, axis=0)
    red_cases.append((KIND['median_axis0'], off, rows, cols, len(red_out) // 4))
    for g in got:  # no association candidates: all four fields are numpy's value
        red_out.extend([db(g)] * 4)


for n in (1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 100, 127, 128, 129, 200, 255, 256, 257, 1000):
    off = put(mixed() for _ in range(n))
    red_sum1d(off, n)
    red_mean1d(off, n)
for n in (1000, 4097, 8191, 8192, 8193, 10000, BIG_N):
    red_sum1d(big_off, n)
    red_mean1d(big_off, n)
nz8 = put([-0.0] * 9)
red_sum1d(nz8, 1)
red_sum1d(nz8, 9)
red_mean1d(nz8, 9)
red_sum1d(nz8, 0)
red_mean1d(nz8, 0)
for rows, cols in ((1, 3), (5, 3), (7, 4), (3, 9), (4, 20), (2, 129), (3, 200), (6, 17)):
    red_sum_last(put(mixed() for _ in range(rows * cols)), rows, cols)
for rows, cols in ((1, BIG_N), (2, BIG_N // 2), (4, BIG_N // 4)):
    red_sum_last(big_off, rows, cols)
red_sum_last(nz8, 1, 9)
red_sum_last(nz8, 3, 3)
for rows, cols in ((5, 3), (9, 2), (200, 2), (129, 5), (300, 1), (17, 1), (1, 4)):
    off = put(mixed() for _ in range(rows * cols))
    red_sum0(off, rows, cols)
    red_mean0(off, rows, cols)
for rows, cols in ((BIG_N, 1), (BIG_N // 2, 2), (BIG_N // 4, 4), (2000, 10), (9000, 1)):
    red_sum0(big_off, rows, cols)
    red_mean0(big_off, rows, cols)
red_sum0(nz8, 3, 3)
red_sum0(nz8, 9, 1)
red_mean0(nz8, 3, 3)
for rows, cols in ((1, 3), (2, 3), (3, 3), (4, 2), (5, 1), (6, 1), (25, 3), (9, 4), (2, 1), (40, 2)):
    red_median0(put(mixed() for _ in range(rows * cols)), rows, cols)
red_median0(nz8, 2, 1)
red_median0(nz8, 2, 3)
red_median0(nz8, 3, 3)
red_median0(put([1.0, NAN, 2.0]), 3, 1)
red_median0(put([3.0, 1.0, NAN, 2.0, 4.0, 0.5]), 3, 2)
red_median0(nz8, 0, 3)

ASSOC = {'sum_last_axis': 'B', 'sum_axis0 cols=1': 'B', 'sum_axis0 cols>=2': 'S', 'sum_1d': 'B', 'mean_1d': 'B',
         'mean_axis0 cols=1': 'B', 'mean_axis0 cols>=2': 'S'}
for label, assoc in ASSOC.items():
    assert follows.get(label) == {assoc}, f'{label}: numpy follows {sorted(follows.get(label, ()))}, expected {assoc}'
    others = {'S', 'P', 'B'} - {assoc}
    assert others <= separated.get(label, set()), f'{label}: no pin separates numpy from {sorted(others)}'

# Percentile, method 'linear': np.percentile(v, q). The test checks numpy's value and, NOT, the other _lerp branch.
def lerp_branches(v, q):
    a = np.sort(np.array(v, dtype=np.float64))
    n = len(a)
    qq = np.true_divide(q, 100)
    virt = (n - 1) * qq
    prev = math.floor(virt)
    if virt >= n - 1:
        pi = ni = -1
    elif virt < 0:
        pi = ni = 0
    else:
        pi, ni = prev, prev + 1
    lo, hi = a[pi], a[ni]
    t = virt - np.float64(pi)
    with np.errstate(all='ignore'):
        d = hi - lo
        below = lo + d * t
        above = hi - d * (1 - t)
    return t, float(below), float(above)


pc_cases = []
for _ in range(300):
    n = rng.randint(2, 40)
    pc_cases.append(([mixed() for _ in range(n)], rng.choice((10, 90, 97, 50, 25, 75, 33.3, 0, 100, rng.uniform(0, 100)))))
pc_cases += [([5.0], 10), ([5.0], 90), ([1.0, 2.0], 50), ([1.0, NAN, 3.0], 50), ([1.0, INF, 3.0], 90),
             ([-1.0, 2.0, 7.0, 7.5], 97)]
pc_tab = []
side = {False: 0, True: 0}
for v, q in pc_cases:
    with np.errstate(all='ignore'):  # [1, 3, inf] at 90: inf - inf in _lerp's second branch
        got = float(np.percentile(np.array(v, dtype=np.float64), q))
    t, below, above = lerp_branches(v, q)
    hi_side = bool(t >= 0.5)
    other = below if hi_side else above
    if not math.isnan(got) and db(below) != db(above):
        side[hi_side] += 1
    pc_tab += [put(v), len(v), db(float(q)), db(got), db(other), int(hi_side)]
assert side[False] > 0 and side[True] > 0, f'percentile pins on each side of t = 0.5 where the branches differ: {side}'

# Linspace and radians.
ls_cases = [(0.0, math.pi, 9), (0.0, 1.0, 25), (-1.0, 1.0, 7), (5.0, 5.0, 4), (0.0, 5e-324, 3), (1.0, 2.0, 1),
            (1.0, 2.0, 0), (2.0, 1.0, 5), (-0.0, 1.0, 3), (0.0, 1.5e308, 3), (-1e308, 1e308, 5), (0.0, math.pi, 2)]
for _ in range(60):
    ls_cases.append((mixed(), mixed(), rng.randint(2, 60)))
ls_tab, ls_out = [], []
with np.errstate(all='ignore'):
    for a, b, num in ls_cases:
        y = np.linspace(a, b, num)
        ls_tab += [db(a), db(b), num, len(ls_out)]
        ls_out += [db(v) for v in y]
rad_in = [22.0, 0.0, -0.0, 180.0, 360.0, 90.0, -45.0, INF, -INF, NAN, 1e308, 5e-324]
rad_in += [mixed() * 100.0 for _ in range(300)]
rad_out = np.radians(np.array(rad_in, dtype=np.float64))
assert db(np.radians(22.0)) == db(rad_out[0])

# Floating-point environment: three NpSum1D({a, b}) pins that flush-to-zero, denormals-are-zero and round-up each
# change, and the contraction probe's two values.
TINY = 2.0 ** -1022
fpenv = [(1.5 * TINY, -TINY), (3 * 2.0 ** -1074, 5 * 2.0 ** -1074), (1.0, 2.0 ** -60)]
fpenv_tab = []
for a, b in fpenv:
    fpenv_tab += [db(a), db(b), db(np.array([a, b], dtype=np.float64).sum())]
assert 0.0 < bd(fpenv_tab[2]) < TINY  # FTZ would flush this result to zero
assert 0.0 < abs(fpenv[1][0]) < TINY and bd(fpenv_tab[5]) != 0.0  # DAZ would read both inputs as zero
assert bd(fpenv_tab[8]) == 1.0 and Fraction(1) + Fraction(2) ** -60 > 1  # round-up would give the next double
cp_a, cp_b, cp_c = 1.0 + 2.0 ** -30, 1.0 - 2.0 ** -30, -1.0
CP_UNFUSED = cp_a * cp_b + cp_c
CP_FUSED = float(Fraction(cp_a) * Fraction(cp_b) + Fraction(cp_c))
assert db(CP_UNFUSED) == db(0.0) and CP_FUSED == -(2.0 ** -60)

# ---------------------------------------------------------------------------------------------------------------
emit('// Generated by tests/host/make_suitconv_pins.py under CPython 3.12.1 and numpy 1.26.3. Do not edit.\n'
     '// Synthetic inputs only (seeded random values and searched edge cases); every value is the bits of a double\n'
     '// (uint64), a float (uint32 in a uint64 field) or a half. Read by suitconv_num_host_test.cpp.\n'
     f'// Seed {SEED}. Reduction associations numpy follows (S sequential, P pairwise, B pairwise per 8192 chunk):\n')
for label, assoc in ASSOC.items():
    emit(f'//   {label}: {assoc}\n')
emit('#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\nnamespace suitconv_pins {\n\n')
u64s('kSumIn', sum_in, 'PySum inputs (bits), referenced by kSum.')
u64s('kSum', sum_tab, 'PySum: offset into kSumIn, count, sum() result bits, naive left-to-right loop bits.')
u64s('kFloorDiv', fd_tab, 'PyFloorDiv: x, y, x // y, NOT np.floor(x / y) (bits).')
emit(f'static constexpr const char* kFloorDivZeroMessage = {cstr(FD_ZERO[1])};  // Python: {FD_ZERO[0]}\n\n')
u64s('kRound', rd_tab, 'PyRoundDigits: x, ndigits, round(x, ndigits), NOT floor(x * 10.0**n + 0.5) / 10.0**n (bits).')
u64s('kRoundHalfEven', rh_tab, 'RoundHalfEven: x, float(round(x)), NOT C round() (half away from zero) (bits).')
emit(f'static constexpr const char* kRoundInfMessage = {cstr(RH_INF[1])};  // Python: {RH_INF[0]}\n')
emit(f'static constexpr const char* kRoundNanMessage = {cstr(RH_NAN[1])};  // Python: {RH_NAN[0]}\n\n')
u64s('kPackF32', f32_tab, "PackF32: x (double bits), struct.pack('>f', x) as uint32.")
u64s('kPackF32Overflow', [db(x) for x in f32_over], "PackF32 inputs for which struct.pack('>f') raises.")
emit(f'static constexpr const char* kPackF32Message = {cstr(F32_OVER[1])};  // Python: {F32_OVER[0]}\n\n')
u64s('kPackF16', f16_tab, "PackF16: x (double bits), struct.pack('>e', x), NOT pack('>e') of float(x) "
                          "(uint16; 0x10000: that raises).")
u64s('kPackF16Overflow', [db(x) for x in f16_over], "PackF16 inputs for which struct.pack('>e') raises.")
emit(f'static constexpr const char* kPackF16Message = {cstr(F16_OVER[1])};  // Python: {F16_OVER[0]}\n\n')
u64s('kHalfToDouble', h2d_tab, "HalfToDouble: h, struct.unpack('>e', h) (bits).")
emit('// FNV-1a 64 over the 8 little-endian bytes of struct.unpack(\'>e\', h) for h = 0..65535 in order.\n'
     f'static constexpr uint64_t kHalfToDoubleDigest = 0x{H2D_DIGEST:016x}ull;\n\n')
u64s('kExp', exp_tab, 'Exp: x, np.exp(x) on a float64 array (bits).')
u64s('kAtan2', at_tab, 'Atan2: y, x, np.arctan2(y, x) on float64 arrays (bits).')
u64s('kPool', [db(v) for v in pool], 'Input doubles for the reduction, median and percentile pins.')
emit('// Reduction pins: kind (0 NpSumLastAxis per row, 1 NpSumAxis0, 2 NpSum1D, 3 NpMean1D, 4 NpMeanAxis0,\n'
     '// 5 NpMedianAxis0), offset into kPool, rows, cols (1-D kinds: rows = n, cols = 1), first output in kRedOut.\n')
u32s('kRed', [v for c in red_cases for v in c], 'Five uint32 per case.')
u64s('kRedOut', red_out, 'Per output: numpy, then candidates S, P, B (bits); median: numpy four times.')
u64s('kPercentile', pc_tab, 'NpPercentileLinear: offset into kPool, n, q, np.percentile(v, q), NOT the other _lerp branch, '
                            '1 if t >= 0.5.')
u64s('kLinspace', ls_tab, 'NpLinspace: start, stop, num, first output in kLinspaceOut.')
u64s('kLinspaceOut', ls_out, 'np.linspace outputs (bits).')
u64s('kRadians', [v for x, y in zip(rad_in, rad_out) for v in (db(x), db(y))], 'NpRadians: x, np.radians(x) (bits).')
u64s('kFpEnv', fpenv_tab, 'NpSum1D({a, b}) pins: a, b, np.sum (bits): FTZ, DAZ and round-up each change one.')
emit(f'static constexpr uint64_t kContractionUnfused = 0x{db(CP_UNFUSED):016x}ull;  // (1+2^-30)*(1-2^-30) + -1, two roundings\n')
emit(f'static constexpr uint64_t kContractionFused = 0x{db(CP_FUSED):016x}ull;  // the same, one rounding: -2^-60\n\n')
emit('}  // namespace suitconv_pins\n')

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'suitconv_pins.h')
text = ''.join(out)
with open(path, 'w', newline='\n', encoding='ascii') as f:
    f.write(text)
print(f'wrote {path} ({len(text)} bytes)')
print('pins: sum', len(sum_tab) // 4, 'floordiv', len(fd_tab) // 4, 'round', len(rd_tab) // 4, 'roundhalfeven',
      len(rh_tab) // 3, 'f32', len(f32_tab) // 2, 'f16', len(f16_tab) // 3, 'h2d', len(h2d_tab) // 2, 'exp',
      len(exp_tab) // 2, 'atan2', len(at_tab) // 3, 'red cases', len(red_cases), 'red outputs', len(red_out) // 4,
      'percentile', len(pc_tab) // 6, 'linspace', len(ls_tab) // 4, 'radians', len(rad_in))
print('percentile sides (t < 0.5, t >= 0.5) where the branches differ:', side[False], side[True])

# ===============================================================================================================
# suitconv_sort_pins.h: np.argsort(v) (default kind) and np.argmin(v) on float64 arrays, for NpArgsort / NpArgmin.
# Nothing above reads or changes anything below, so suitconv_pins.h keeps its bytes.
#
# The arrays are not stored: each is rebuilt from (seed, n, pool) by the integer-only splitmix64 below, implemented
# identically in suitconv_sort_host_test.cpp:
#   state = seed; next(): state += 0x9E3779B97F4A7C15 (mod 2^64); z = state; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9;
#   z = (z ^ (z >> 27)) * 0x94D049BB133111EB; return z ^ (z >> 31)   (all mod 2^64)
#   v[i] = pool[next() % len(pool)] for i = 0..n-1, in order.
# Per array the header stores seed, n, pool id, the FNV-1a 64 digest of numpy's int64 index array (8 little-endian
# bytes per index, in order), np.argmin, and a plausible wrong argmin; full index arrays only for n <= 20 and for the
# two killer arrays. Small pools give dense ties; pools 6 and 7 hold NaN; pool 2 holds -0.0 and +0.0 (equal).
M64 = (1 << 64) - 1


class SplitMix64:
    def __init__(self, seed):
        self.s = seed & M64

    def next(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & M64
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
        return z ^ (z >> 31)


assert SplitMix64(0).next() == 0xE220A8397B1DCDAF  # the published splitmix64.c (Vigna) output for seed 0

SORT_POOLS = [
    [0.0, 1.0, 2.0],
    [1.0, 2.0],
    [-0.0, 0.0, 1.0],
    [-1.5, 0.25, 0.5, 3.0, 7.75],
    [5.0],
    [-2.0, -1.0, -0.5, 0.0, 0.125, 1.0, 1.5, 2.0, 1e-300, 4.0],
    [NAN, 1.0, 2.0, -1.0],
    [NAN, -0.0, 0.0, INF, -INF],
]
SORT_REGULAR_POOLS = 6  # pools 0..5 have no NaN


def sort_values(seed, n, pool):
    g = SplitMix64(seed)
    p = SORT_POOLS[pool]
    return [p[g.next() % len(p)] for _ in range(n)]


def np_argsort(vals):
    a = np.array(vals, dtype=np.float64)
    idx = np.argsort(a)
    assert idx.dtype == np.int64, idx.dtype
    return a, idx


def idx_digest(idx):
    return fnv1a64(idx.astype('<i8').tobytes())


def wrong_argmin(vals):
    """The likeliest wrong argmin: the LAST index of the minimum when there is no NaN; with a NaN, the first minimum
    of the values that are not NaN (as if NaN were skipped)."""
    if any(x != x for x in vals):
        fin = [x for x in vals if x == x]
        if not fin:
            return 0
        m = min(fin)
        return next(i for i, x in enumerate(vals) if x == m)
    m = min(vals)
    return max(i for i, x in enumerate(vals) if x == m)


sort_master = SplitMix64(SEED)
SORT_LARGE_N = [100, 1000, 1914, 3608, 5000]
sort_cases = []  # (seed, n, pool, digest, argmin, wrong argmin, differs from stable)
sort_small_idx = []
# n <= 16 is all insertion sort in numpy (pr - pl > SMALL_QUICKSORT needs n >= 17), which is stable, so those arrays
# can never differ from a stable order; half the small arrays sweep n = 1..20 evenly and half take n = 17..20, where
# one partition runs.
sort_shapes = [1 + k % 20 for k in range(1000)] + [17 + k % 4 for k in range(1000)]
sort_shapes += [SORT_LARGE_N[k % 5] for k in range(1000)]
for k, n in enumerate(sort_shapes):
    if k % 16 == 15:
        pool = 6 + (k // 16) % 2
    else:
        pool = sort_master.next() % SORT_REGULAR_POOLS
    seed = sort_master.next()
    vals = sort_values(seed, n, pool)
    a, idx = np_argsort(vals)
    stable = np.argsort(a, kind='stable')
    am = int(np.argmin(a))
    sort_cases.append((seed, n, pool, idx_digest(idx), am, wrong_argmin(vals), not np.array_equal(idx, stable)))
    if n <= 20:
        sort_small_idx += [int(i) for i in idx]
    assert sorted(int(i) for i in idx) == list(range(n))


def musser_killer(n):
    """Musser's median-of-3 killer (Introspective Sorting and Selection Algorithms, 1997), values 1..n, n even."""
    k = n // 2
    a = [0] * n
    for i in range(1, k + 1):
        a[i - 1] = i if i % 2 == 1 else k + i - 1
        a[k + i - 1] = 2 * i
    return a


KILLER_N = 4096
killer_perm = musser_killer(KILLER_N)
assert sorted(killer_perm) == list(range(1, KILLER_N + 1))
# Killer 0: the permutation itself (distinct values, so its sorted order is unique). Killer 1: the same with each
# value halved and floored, so pairs tie and the heapsort fallback's tie order shows in the index array.
killers = [[float(x) for x in killer_perm], [float(x // 2) for x in killer_perm]]
killer_idx = []
for kv in killers:
    a, idx = np_argsort(kv)
    killer_idx.append([int(i) for i in idx])
assert killer_idx[1] != [int(i) for i in np.argsort(np.array(killers[1]), kind='stable')]

ARGMIN_EMPTY = message_of(lambda: np.argmin(np.array([], dtype=np.float64)))
assert len(np.argsort(np.array([], dtype=np.float64))) == 0

sort_differs = sum(c[6] for c in sort_cases)
SORT_DIFF_FRACTION = sort_differs / len(sort_cases)
assert SORT_DIFF_FRACTION >= 0.5, f'only {sort_differs} of {len(sort_cases)} pins differ from a stable order'
argmin_last_differs = sum(1 for c in sort_cases if c[2] < SORT_REGULAR_POOLS and c[4] != c[5])
argmin_nan_differs = sum(1 for c in sort_cases if c[2] >= SORT_REGULAR_POOLS and c[4] != c[5])
assert argmin_last_differs > 0 and argmin_nan_differs > 0, 'no argmin pin separates last-of-ties or NaN-skipping'
nan_cases = sum(1 for c in sort_cases if c[2] >= SORT_REGULAR_POOLS)
prng_check = []
for s in (0, SEED, sort_cases[0][0]):
    g = SplitMix64(s)
    prng_check += [s] + [g.next() for _ in range(4)]

sort_out = []


def semit(s):
    sort_out.append(s)


def su64s(name, values, comment):
    vals = list(values)
    semit(f'// {comment}\nstatic const uint64_t {name}[] = {{\n')
    for i in range(0, len(vals), 6):
        semit(','.join(f'0x{v:016x}' for v in vals[i:i + 6]) + ',\n')
    semit('};\n')
    semit(f'static constexpr size_t {name}Count = {len(vals)};\n\n')


def sints(name, ctype, values, comment):
    vals = list(values)
    semit(f'// {comment}\nstatic const {ctype} {name}[] = {{\n')
    for i in range(0, len(vals), 32):
        semit(','.join(str(v) for v in vals[i:i + 32]) + ',\n')
    semit('};\n')
    semit(f'static constexpr size_t {name}Count = {len(vals)};\n\n')


semit('// Generated by tests/host/make_suitconv_pins.py under CPython 3.12.1 and numpy 1.26.3. Do not edit.\n'
      '// np.argsort(v) (default kind: aquicksort_<npy::double_tag>, no SIMD dispatch on Windows) and np.argmin(v) on\n'
      '// synthetic float64 arrays. Read by suitconv_sort_host_test.cpp. Each array is rebuilt from (seed, n, pool):\n'
      '//   splitmix64: state = seed; next(): state += 0x9E3779B97F4A7C15; z = state;\n'
      '//   z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9; z = (z ^ (z >> 27)) * 0x94D049BB133111EB; return z ^ (z >> 31);\n'
      '//   (uint64 arithmetic); v[i] = pool[next() % pool size] for i = 0..n-1.\n'
      f'// Master seed {SEED}. {len(sort_cases)} arrays, {sort_differs} ({SORT_DIFF_FRACTION:.4f}) differ from '
      'np.argsort(kind="stable");\n'
      f'// {nan_cases} use a NaN pool. Argmin pins where the wrong argmin differs: last-of-ties {argmin_last_differs}, '
      f'NaN-skipping {argmin_nan_differs}.\n')
semit('#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\nnamespace suitconv_sort_pins {\n\n')
su64s('kPrngCheck', prng_check, 'splitmix64 self-check: per seed, the seed then its first 4 outputs.')
pool_bits, pool_start = [], []
for p in SORT_POOLS:
    pool_start.append(len(pool_bits))
    pool_bits += [db(x) for x in p]
su64s('kPoolValues', pool_bits, 'The value pools, concatenated (double bits): ' +
      '; '.join(f'pool {i}: {len(p)}' for i, p in enumerate(SORT_POOLS)) + '.')
sints('kPoolStart', 'uint32_t', pool_start, 'Offset of each pool in kPoolValues.')
sints('kPoolSize', 'uint32_t', [len(p) for p in SORT_POOLS], 'Size of each pool.')
su64s('kCases', [v for c in sort_cases for v in (c[0], c[1], c[2], c[3], c[4], c[5])],
      'Per array: seed, n, pool, FNV-1a 64 of np.argsort(v) as int64 LE bytes, np.argmin(v), NOT that: the last index '
      'of the minimum (no NaN) or the first minimum ignoring NaN (NaN pools).')
sints('kSmallIndices', 'uint8_t', sort_small_idx, 'np.argsort(v) in full for every array with n <= 20, in case order.')
for i, kv in enumerate(killers):
    su64s(f'kKiller{i}Values', [db(x) for x in kv],
          f'Killer {i} (n {KILLER_N}): ' + ("Musser's median-of-3 killer, values 1..4096" if i == 0 else
                                             'the same values halved and floored (pairs tie)') + ' (double bits).')
    sints(f'kKiller{i}Indices', 'uint16_t', killer_idx[i], f'np.argsort of killer {i}.')
semit(f'static constexpr const char* kArgminEmptyMessage = {cstr(ARGMIN_EMPTY[1])};  // Python: {ARGMIN_EMPTY[0]}\n\n')
semit('}  // namespace suitconv_sort_pins\n')

sort_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                               'suitconv_sort_pins.h')
sort_text = ''.join(sort_out)
with open(sort_path, 'w', newline='\n', encoding='ascii') as f:
    f.write(sort_text)
print(f'wrote {sort_path} ({len(sort_text)} bytes)')
by_n = {}
for c in sort_cases:
    key = c[1] if c[1] > 20 else ('1..16' if c[1] <= 16 else '17..20')
    d = by_n.setdefault(key, [0, 0])
    d[0] += 1
    d[1] += c[6]
print('sort pins:', len(sort_cases), 'arrays,', sort_differs, f'({SORT_DIFF_FRACTION:.4f}) differ from stable;',
      'by n (arrays, differ):', by_n, '; NaN-pool arrays', nan_cases, '; argmin NOT pins separating: last-of-ties',
      argmin_last_differs, 'NaN-skipping', argmin_nan_differs)

# ===============================================================================================================
# suitconv_tpr_pins.h: dec_hend3n and world_mats of the recipe's tpr.py (src/doa4_suitconv_tpr.h DecHend3n, WorldMats).
# This script opens no file, so the three functions below are a VERBATIM copy of the Python recipe's tpr.py (SHA-1
# 9d11ca4827cbc781c8833bd55045e43fb30d14c7): dec_hend3n (lines 429-434), mat_floats (191-192), world_mats (204-219).
# They are Python's own code run on the interpreter the converter follows; the pins are their outputs.
def dec_hend3n(v):
    def s(x, bits):
        if x & (1 << (bits - 1)):
            x -= 1 << bits
        return x / ((1 << (bits - 1)) - 1)
    return (s(v & 0x7FF, 11), s((v >> 11) & 0x7FF, 11), s((v >> 22) & 0x3FF, 10))


def mat_floats(m):
    return list(struct.unpack('>16f', m)) if isinstance(m, (bytes, bytearray)) else list(m)


def world_mats(nodes):
    """row-vector convention: world = local * world(parent)."""
    out = [None] * len(nodes)

    def mul(a, b):
        return [sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4)) for r in range(4) for c in range(4)]

    def w(i):
        if out[i] is None:
            m = mat_floats(nodes[i].mat)
            p = nodes[i].parent
            out[i] = m if p < 0 else mul(m, w(p))
        return out[i]
    for i in range(len(nodes)):
        w(i)
    return out


class _Nd:
    """Only .mat and .parent are read by world_mats."""
    def __init__(self, mat, parent):
        self.mat = mat
        self.parent = parent


def world_mats_naive_sum(nodes):
    """NOT tpr.py: world_mats with the naive left-to-right float sum from 0.0 in place of builtin sum (the wrong
    implementation a pin must be able to tell from the right one)."""
    out = [None] * len(nodes)

    def mul(a, b):
        res = []
        for r in range(4):
            for c in range(4):
                s = 0.0
                for k in range(4):
                    s += a[r * 4 + k] * b[k * 4 + c]
                res.append(s)
        return res

    def w(i):
        if out[i] is None:
            m = mat_floats(nodes[i].mat)
            p = nodes[i].parent
            out[i] = m if p < 0 else mul(m, w(p))
        return out[i]
    for i in range(len(nodes)):
        w(i)
    return out


def dec_hend3n_wrong(v):
    """NOT tpr.py: the divisor 1 << (bits - 1) in place of (1 << (bits - 1)) - 1."""
    def s(x, bits):
        if x & (1 << (bits - 1)):
            x -= 1 << bits
        return x / (1 << (bits - 1))
    return (s(v & 0x7FF, 11), s((v >> 11) & 0x7FF, 11), s((v >> 22) & 0x3FF, 10))


def f32(x):
    return struct.unpack('>f', struct.pack('>f', x))[0]


def raw_mat(vals):
    return struct.pack('>16f', *vals)


def rot(axis, deg):
    """A rotation matrix in the row-vector convention, float32 entries."""
    a = math.radians(deg)
    c, s = f32(math.cos(a)), f32(math.sin(a))
    m = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]
    if axis == 'x':
        m[5], m[6], m[9], m[10] = c, s, -s, c
    elif axis == 'y':
        m[0], m[2], m[8], m[10] = c, -s, s, c
    else:
        m[0], m[1], m[4], m[5] = c, s, -s, c
    return m


def with_translation(m, t):
    m = list(m)
    m[12], m[13], m[14] = f32(t[0]), f32(t[1]), f32(t[2])
    return m


def with_scale(m, sc):
    m = list(m)
    for r in range(3):
        for c in range(3):
            m[r * 4 + c] = f32(m[r * 4 + c] * sc)
    return m


def mat_mul_f32(a, b):
    return [f32(sum(a[r * 4 + k] * b[k * 4 + c] for k in range(4))) for r in range(4) for c in range(4)]


trng = random.Random(SEED + 4)
IDENT = [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]
wm_cases = []  # each: list of (raw 64 bytes, parent)


def chain(mats):
    return [(raw_mat(m), i - 1) for i, m in enumerate(mats)]


wm_cases.append([(raw_mat(IDENT), -1)])
wm_cases.append([(raw_mat(with_translation(IDENT, (1.5, -2.25, 3.0))), -1)])
for ax in 'xyz':  # a rotation about each axis under a translated root
    wm_cases.append(chain([with_translation(IDENT, (1.0, 2.0, 3.0)), rot(ax, 30.0),
                           with_translation(rot(ax, -45.0), (0.5, 0.25, -1.0))]))
for deg in (90.0, 180.0, 270.0, 17.0):  # exact and inexact angles
    wm_cases.append(chain([rot('x', deg), rot('y', deg), rot('z', deg), with_translation(rot('x', deg), (1, 1, 1))]))
neg0 = list(IDENT)
neg0[1] = -0.0
neg0[2] = -0.0
neg0[4] = -0.0
neg0[8] = -0.0
neg0[12], neg0[13], neg0[14] = -0.0, -0.0, -0.0
wm_cases.append(chain([neg0, neg0, rot('z', 90.0), neg0]))  # -0.0 entries
for _ in range(100):
    n = trng.randint(3, 6)
    mats = []
    for _k in range(n):
        m = IDENT
        for ax in trng.sample('xyz', trng.randint(1, 3)):
            m = mat_mul_f32(m, rot(ax, trng.uniform(-180.0, 180.0)))
        if trng.random() < 0.5:
            m = with_scale(m, trng.choice([0.5, 1.0, 1.25, 2.0, 0.1, 3.7]))
        m = with_translation(m, (trng.uniform(-50, 50), trng.uniform(-50, 50), trng.uniform(-50, 50)))
        mats.append(m)
    wm_cases.append(chain(mats))
for _ in range(30):  # branching trees, parents after children, several roots
    n = trng.randint(4, 8)
    mats = []
    par = []
    for k in range(n):
        m = rot(trng.choice('xyz'), trng.uniform(-180.0, 180.0))
        m = with_translation(m, (trng.uniform(-10, 10), trng.uniform(-10, 10), trng.uniform(-10, 10)))
        mats.append(m)
        par.append(-1 if k == 0 or trng.random() < 0.1 else trng.randrange(0, k))
    order = list(range(n))
    trng.shuffle(order)  # old node order[new] sits at index new
    pos = {old: new for new, old in enumerate(order)}
    nodes = [None] * n
    for old in range(n):
        nodes[pos[old]] = (raw_mat(mats[old]), -1 if par[old] < 0 else pos[par[old]])
    wm_cases.append(nodes)

wm_count, wm_mat, wm_par, wm_out, wm_naive = [], bytearray(), [], [], []
wm_differs = 0
wm_forward = 0
for case in wm_cases:
    nodes = [_Nd(m, p) for m, p in case]
    got = world_mats(nodes)
    naive = world_mats_naive_sum(nodes)
    wm_count.append(len(case))
    for (m, p), g, nv in zip(case, got, naive):
        wm_mat += m
        wm_par.append(p)
        wm_out += [db(x) for x in g]
        wm_naive += [db(x) for x in nv]
        wm_differs += sum(1 for x, y in zip(g, nv) if db(x) != db(y))
    wm_forward += sum(1 for i, (m, p) in enumerate(case) if p > i)
assert wm_differs > 20, f'only {wm_differs} world-matrix entries tell builtin sum from the naive sum'
assert wm_forward > 10, 'no pin has a parent after its child'

E11 = [0x000, 0x001, 0x3FF, 0x400, 0x401, 0x7FF]
E10 = [0x000, 0x001, 0x1FF, 0x200, 0x201, 0x3FF]
hend_words = [(c << 22) | (b << 11) | a for a in E11 for b in E11 for c in E10]
hend_words += [0x00000000, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, 0x12345678, 0xDEADBEEF]
hend_out = []
hend_wrong = []
for v in hend_words:
    hend_out += [db(x) for x in dec_hend3n(v)]
    hend_wrong += [db(x) for x in dec_hend3n_wrong(v)]
assert sum(1 for a, b in zip(hend_out, hend_wrong) if a != b) > 100, 'HEND3N pins do not separate the divisor'
assert dec_hend3n(0x400)[0] == -1024 / 1023 and dec_hend3n(0x200 << 22)[2] == -512 / 511

tout = []


def temit(s):
    tout.append(s)


def tints(name, ctype, values, comment, per_line=16, hexw=0):
    vals = list(values)
    temit(f'// {comment}\nstatic const {ctype} {name}[] = {{\n')
    for i in range(0, len(vals), per_line):
        temit(','.join((f'0x{v:0{hexw}x}' if hexw else str(v)) for v in vals[i:i + per_line]) + ',\n')
    temit('};\n')
    temit(f'static constexpr size_t {name}Count = {len(vals)};\n\n')


temit('// Generated by tests/host/make_suitconv_pins.py under CPython 3.12.1 (numpy 1.26.3 loaded). Do not edit.\n'
      '// Synthetic inputs only. Pins for the converter port of the recipe tpr.py (src/doa4_suitconv_tpr.h): the outputs of\n'
      '// the recipe\'s own dec_hend3n and world_mats (a verbatim copy in the generator; tpr.py SHA-1\n'
      '// 9d11ca4827cbc781c8833bd55045e43fb30d14c7). Read by suitconv_tpr_host_test.cpp.\n'
      f'// Master seed {SEED + 4}. World matrices: {len(wm_cases)} cases, {sum(wm_count)} nodes; {wm_differs} output entries differ from\n'
      '// the naive float sum, so the pins tell builtin sum() (num::PySum) from it; every matrix entry is a float32 value.\n')
temit('#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\nnamespace suitconv_tpr_pins {\n\n')
tints('kWmCount', 'uint32_t', wm_count, 'World matrices: node count of each case.', 16)
tints('kWmMat', 'uint8_t', list(wm_mat), 'World matrices: every node raw 64 matrix bytes (16 big-endian float32), cases back to back.', 32, 2)
tints('kWmParent', 'int16_t', wm_par, 'World matrices: every node parent index within its case (-1 = root).', 32)
tints('kWmOut', 'uint64_t', wm_out, 'world_mats() result, 16 doubles (bits) per node, cases back to back.', 6, 16)
tints('kWmNaive', 'uint64_t', wm_naive, 'The same with the naive float sum (the wrong implementation), for the separation check.', 6, 16)
tints('kHendWord', 'uint32_t', hend_words, 'HEND3N: every combination of {0, 1, max positive, sign bit, sign bit + 1, all ones} in the two 11-bit lanes and the 10-bit lane, then six more words.', 8, 8)
tints('kHendOut', 'uint64_t', hend_out, 'dec_hend3n() of each word: three doubles (bits) per word.', 6, 16)
tints('kHendWrong', 'uint64_t', hend_wrong, 'The same with the divisor 1 << (bits - 1) (the wrong implementation).', 6, 16)
temit('}  // namespace suitconv_tpr_pins\n')

tpr_path = sys.argv[3] if len(sys.argv) > 3 else os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                            'suitconv_tpr_pins.h')
tpr_text = ''.join(tout)
with open(tpr_path, 'w', newline='\n', encoding='ascii') as f:
    f.write(tpr_text)
print(f'wrote {tpr_path} ({len(tpr_text)} bytes)')
print('tpr pins: world-mat cases', len(wm_cases), 'nodes', sum(wm_count), 'entries differing from naive sum', wm_differs,
      'forward-parent nodes', wm_forward, 'hend words', len(hend_words))

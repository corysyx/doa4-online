// Suit converter (Plan 2): the floating-point environment and the Python / numpy numeric primitives the converter's
// C++ port calls, each giving the same bits as the Python original (CPython 3.12.1, numpy 1.26.3) on every PC.
//
// The rules every converter source follows (src/doa4_suitconv_fp.h, CMakeLists.txt DOA4_SUITCONV_SOURCES):
// no FMA, no contraction, no fast-math, no transcendental function from the C runtime (Exp and Atan2 are ports of
// musl v1.2.4), and every conversion runs through RunUnderConvertEnv (MXCSR 0x1F80: round to nearest, no FTZ /
// DAZ; on arm64 FPCR 0).
// Each function cites the source it follows by file and line at the version tag; the copies read are in
// notes/plans/2026-11-07-public-importer-plan2-converter/ref_src/. Pinned against the real interpreter by
// tests/host/suitconv_num_host_test.cpp (pins from tests/host/make_suitconv_pins.py). Pure C++ (no SDK).

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace doa4::suitconv {

// A conversion failure: an overflow where Python raises (struct.pack, round), a bad input. Later tasks reuse it.
class ConvertError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

}  // namespace doa4::suitconv

namespace doa4::suitconv::num {

// The floating-point control word conversion runs under. x86-64: MXCSR 0x1F80 (all exceptions masked, round to
// nearest, FTZ and DAZ off, no flags). arm64: FPCR 0 (round to nearest, no flush-to-zero, no default NaN, no traps).
#if defined(__aarch64__) || defined(_M_ARM64)
inline constexpr uint32_t kConvertMxcsr = 0;
#else
inline constexpr uint32_t kConvertMxcsr = 0x1F80;
#endif

// THE way converter work runs: sets the control word to kConvertMxcsr on the calling thread, calls fn(ctx), and
// restores the caller's value on every exit, an exception from fn included (it is rethrown). Every converter entry
// point (ConvertSuit and the like) runs its stages through this, in a translation unit other than this one.
// It is defined out of line and never inlined, and fn is called through a pointer, so the compiler cannot move any
// floating-point operation of fn across the control-word writes. (An FpEnvGuard in the same function as the
// arithmetic cannot promise that: the compiler assumes the default environment and may move a visible multiply
// across the guard's calls.)
void RunUnderConvertEnv(void (*fn)(void*), void* ctx);

// Sets the control word to kConvertMxcsr for its lifetime and restores the value it found on exit. For tests and for
// code whose floating-point work is all behind calls into other translation units; converter entry points use
// RunUnderConvertEnv instead.
class FpEnvGuard {
 public:
  FpEnvGuard();
  ~FpEnvGuard();
  FpEnvGuard(const FpEnvGuard&) = delete;
  FpEnvGuard& operator=(const FpEnvGuard&) = delete;

 private:
  uint32_t saved_;
};

// The calling thread's control word: MXCSR on x86-64, the low 32 bits of FPCR on arm64.
uint32_t Mxcsr();

// a*b + c computed in a converter translation unit: +0.0 for (1+2^-30, 1-2^-30, -1) when the build does not fuse
// (two roundings), -2^-60 when it does. It discriminates only on a CPU with FMA, where a fusing build could fuse.
double ContractionProbe(double a, double b, double c);

// builtin sum() over floats, started from the int 0 (CPython v3.12.1 Python/bltinmodule.c builtin_sum_impl, lines
// 2492-2685): the first float meets the int 0 (0 + x, so -0.0 becomes 0.0), later ones are added with Neumaier
// compensation, and the compensation is added at the end when it is non-zero and finite. No Add: 0.0 (Python: 0).
class PySum {
 public:
  void Add(double x);
  double Result() const;

 private:
  bool started_ = false;
  double f_ = 0.0;
  double c_ = 0.0;
};

// x // y for floats (CPython v3.12.1 Objects/floatobject.c float_floor_div and _float_div_mod, lines 668-735),
// which is not floor(x / y). Throws ConvertError("float floor division by zero") when y == 0, as Python raises.
double PyFloorDiv(double x, double y);

// round(x, ndigits) for ndigits 0..17 (CPython v3.12.1 Objects/floatobject.c float___round___impl and the dtoa
// double_round, lines 957-1117; double_round itself is lines 957-1007): x correctly rounded to ndigits decimals (ties to even on the exact value), then read
// back correctly rounded. NaN and infinities return themselves. Throws ConvertError for ndigits outside 0..17.
double PyRoundDigits(double x, int ndigits);

// round(x) (CPython v3.12.1 Objects/floatobject.c float___round___impl, lines 1082-1090): nearest, ties to even,
// returned as a double. Python returns an int, so a zero result is +0.0 (round(-0.4) is 0). Throws ConvertError
// ("cannot convert float infinity to integer" / "cannot convert float NaN to integer") as Python raises.
double RoundHalfEven(double x);

// struct.pack('>f', x)'s value (CPython v3.12.1 Objects/floatobject.c PyFloat_Pack4, IEEE branch, lines 2244-2265):
// x rounded to float; throws ConvertError("float too large to pack with f format") when a finite x rounds to inf.
float PackF32(double x);

// struct.pack('>e', x)'s value as bits (CPython v3.12.1 Objects/floatobject.c PyFloat_Pack2, lines 2060-2162): the
// double rounded straight to half, ties to even (not through float); throws ConvertError("float too large to pack
// with e format") when it overflows.
uint16_t PackF16(double x);

// struct.unpack('>e', bits) (CPython v3.12.1 Objects/floatobject.c PyFloat_Unpack2, lines 2403-2452).
double HalfToDouble(uint16_t bits);

// e^x and atan2(y, x), ported from musl v1.2.4 (src/math/exp.c, src/math/atan2.c, src/math/atan.c): no C runtime
// call. Within 1 ulp of numpy's np.exp / np.arctan2 on float64 arrays (pinned).
double Exp(double x);
double Atan2(double y, double x);

// numpy 1.26.3 add reductions over float64, each named for its call shape. The reduction starts from add's identity
// 0.0 (numpy/core/src/umath/reduction.c PyUFunc_ReduceWrapper, lines 295-361), so a sum of -0.0 is +0.0.
// An inner-axis reduction adds DOUBLE_pairwise_sum (numpy/core/src/umath/loops_utils.h.src lines 77-142) of each
// 8192-element buffer chunk in turn (numpy/core/src/umath/loops_arithm_fp.dispatch.c.src lines 58-75; NPY_BUFSIZE).
// `.sum(1)` / `.sum(-1)` of one row of a C-contiguous 2-D array.
double NpSumLastAxis(std::span<const double> row);
// `.sum(0)` of a C-contiguous (rows.size() / cols, cols) array into out[cols]: with cols >= 2 each column is added
// row after row (an elementwise add per row); with cols == 1 it is the 1-D sum.
void NpSumAxis0(std::span<const double> rows, size_t cols, std::span<double> out);
// `.sum()` / `np.sum()` of a 1-D array.
double NpSum1D(std::span<const double> v);
// `.mean()` of a 1-D array: NpSum1D / n (numpy/core/_methods.py _mean). n == 0 gives NaN, as numpy does.
double NpMean1D(std::span<const double> v);
// `.mean(0)` of a C-contiguous (rows.size() / cols, cols) array: NpSumAxis0 / rows.
void NpMeanAxis0(std::span<const double> rows, size_t cols, std::span<double> out);

// np.percentile(v, q) with the default method 'linear' over a 1-D array (numpy/lib/function_base.py percentile,
// _quantile, _get_indexes, _lerp; lines 4271-4284, 4594-4661, 4729-4850). NaN in v gives NaN. Throws ConvertError for
// an empty v or q outside [0, 100].
double NpPercentileLinear(std::vector<double> v, double q);
// np.median(a, axis=0) of a C-contiguous (rows.size() / cols, cols) array (numpy/lib/function_base.py _median, lines
// 3931-3990): the mean of the middle one or two sorted values of each column; a column with NaN gives NaN.
void NpMedianAxis0(std::span<const double> rows, size_t cols, std::span<double> out);
// np.linspace(start, stop, num) (numpy/core/function_base.py linspace, lines 122-182). Throws ConvertError when
// num < 0.
std::vector<double> NpLinspace(double start, double stop, int num);
// np.radians(x) (numpy/core/src/npymath/npy_math_internal.h.src npy_deg2rad, lines 445-455): x * (pi / 180).
double NpRadians(double x);

}  // namespace doa4::suitconv::num

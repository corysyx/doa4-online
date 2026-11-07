// Suit converter (Plan 2) numeric kit; see doa4_suitconv_num.h. Every function follows the cited source line for line
// (operation order included: under the rules of doa4_suitconv_fp.h the compiler keeps it), so the results are the
// Python original's bits. Sources, at the version tags: CPython v3.12.1, numpy v1.26.3, musl v1.2.4 (copies in
// notes/plans/2026-11-07-public-importer-plan2-converter/ref_src/).
//
// Exp and Atan2 are ports of musl v1.2.4. musl's licence (COPYRIGHT):
//
//   musl as a whole is licensed under the following standard MIT license:
//
//   Copyright (c) 2005-2020 Rich Felker, et al.
//
//   Permission is hereby granted, free of charge, to any person obtaining
//   a copy of this software and associated documentation files (the
//   "Software"), to deal in the Software without restriction, including
//   without limitation the rights to use, copy, modify, merge, publish,
//   distribute, sublicense, and/or sell copies of the Software, and to
//   permit persons to whom the Software is furnished to do so, subject to
//   the following conditions:
//
//   The above copyright notice and this permission notice shall be
//   included in all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//   EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// src/math/exp.c and src/math/exp_data.c (the table and constants below):
//   Copyright (c) 2018, Arm Limited.
//   SPDX-License-Identifier: MIT
// src/math/atan2.c and src/math/atan.c (origin: FreeBSD /usr/src/lib/msun/src/e_atan2.c, s_atan.c):
//   Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//   Developed at SunSoft, a Sun Microsystems, Inc. business.
//   Permission to use, copy, modify, and distribute this
//   software is freely granted, provided that this notice
//   is preserved.

#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_num.h"

#if defined(__aarch64__) || defined(_M_ARM64)
#define DOA4_SUITCONV_ARM64 1
#else
#include <immintrin.h>
#endif

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <system_error>

namespace doa4::suitconv::num {

namespace {

double D(uint64_t bits) { return std::bit_cast<double>(bits); }
uint64_t B(double x) { return std::bit_cast<uint64_t>(x); }

constexpr uint64_t kQuietNan = 0x7ff8000000000000ull;  // (double)NAN, CPython's Py_NAN

// 2^n for -1022 <= n <= 1023 (a normal double, so a multiply by it is exact while the product stays normal).
double Pow2(int n) { return D(uint64_t(n + 1023) << 52); }

// frexp for a positive finite non-zero x: x = f * 2^e with f in [0.5, 1). Exact.
double FrexpPositive(double x, int* e) {
  uint64_t u = B(x);
  int ex = int((u >> 52) & 0x7ff);
  int adjust = 0;
  if (ex == 0) {  // subnormal: scale into the normal range first
    u = B(x * 0x1p64);
    ex = int((u >> 52) & 0x7ff);
    adjust = -64;
  }
  *e = ex - 1022 + adjust;
  return D((u & ~(0x7ffull << 52)) | (1022ull << 52));
}

// C round(): nearest, ties away from zero, for a finite x.
double CRound(double x) {
  if (!(std::fabs(x) < 0x1p52)) return x;  // already integral
  double t = std::trunc(x);
  if (std::fabs(x - t) >= 0.5) t += std::copysign(1.0, x);  // x - t is exact below 2^52
  return t;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Floating-point environment.

namespace {

#if defined(DOA4_SUITCONV_ARM64)
// FPCR (arm64). Compile-checked for aarch64; the host tests run on x86-64 only.
uint32_t ReadControlWord() { return static_cast<uint32_t>(__builtin_arm_rsr64("fpcr")); }
void WriteControlWord(uint32_t v) { __builtin_arm_wsr64("fpcr", v); }
#else
uint32_t ReadControlWord() { return _mm_getcsr(); }
void WriteControlWord(uint32_t v) { _mm_setcsr(v); }
#endif

}  // namespace

[[clang::noinline]] void RunUnderConvertEnv(void (*fn)(void*), void* ctx) {
  const uint32_t saved = ReadControlWord();
  WriteControlWord(kConvertMxcsr);
  try {
    fn(ctx);
  } catch (...) {
    WriteControlWord(saved);
    throw;
  }
  WriteControlWord(saved);
}

FpEnvGuard::FpEnvGuard() : saved_(ReadControlWord()) { WriteControlWord(kConvertMxcsr); }
FpEnvGuard::~FpEnvGuard() { WriteControlWord(saved_); }

uint32_t Mxcsr() { return ReadControlWord(); }

double ContractionProbe(double a, double b, double c) { return a * b + c; }

// ---------------------------------------------------------------------------------------------------------------
// CPython v3.12.1.

// Python/bltinmodule.c builtin_sum_impl (lines 2492-2685): the int fast path from line 2536 (the first float leaves
// it through PyNumber_Add(int 0, item), line 2577, i.e. 0.0 + x) and the float path from line 2588 (Neumaier, then
// "if (c && Py_IS_FINITE(c)) f_result += c", lines 2601-2603).
void PySum::Add(double x) {
  if (!started_) {
    f_ = 0.0 + x;
    started_ = true;
    return;
  }
  const double t = f_ + x;
  if (std::fabs(f_) >= std::fabs(x)) {
    c_ += (f_ - t) + x;
  } else {
    c_ += (x - t) + f_;
  }
  f_ = t;
}

double PySum::Result() const {
  double r = f_;
  if (c_ != 0.0 && std::isfinite(c_)) r += c_;
  return r;
}

// Objects/floatobject.c float_floor_div (lines 721-733) and _float_div_mod (lines 668-703).
double PyFloorDiv(double x, double y) {
  if (y == 0.0) throw ConvertError("float floor division by zero");
  double mod = std::fmod(x, y);
  double div = (x - mod) / y;
  if (mod != 0.0) {  // C's "if (*mod)": NaN counts as non-zero
    if ((y < 0) != (mod < 0)) {
      mod += y;
      div -= 1.0;
    }
  } else {
    mod = std::copysign(0.0, y);
  }
  double floordiv;
  if (div != 0.0) {
    floordiv = std::floor(div);
    if (div - floordiv > 0.5) floordiv += 1.0;
  } else {
    floordiv = std::copysign(0.0, x / y);
  }
  return floordiv;
}

// Objects/floatobject.c float___round___impl (lines 1092-1114) and double_round (lines 957-1007):
// _Py_dg_dtoa(x, 3, ndigits) rounds the exact value of x to ndigits decimals, ties to even; the digits are read back
// by _Py_dg_strtod, correctly rounded. std::to_chars with a precision and std::from_chars are both exact, so they
// give the same double. The sign survives a zero result (round(-0.001, 2) is -0.0), as in Python.
double PyRoundDigits(double x, int ndigits) {
  if (!std::isfinite(x)) return x;
  if (ndigits < 0 || ndigits > 17) throw ConvertError("PyRoundDigits: ndigits must be 0..17");
  char buf[400];  // sign, up to 309 integer digits, the point, 17 decimals
  const std::to_chars_result w = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::fixed, ndigits);
  if (w.ec != std::errc()) throw ConvertError("PyRoundDigits: to_chars failed");
  double rounded = 0.0;
  const std::from_chars_result r = std::from_chars(buf, w.ptr, rounded);
  if (r.ec != std::errc() || r.ptr != w.ptr) throw ConvertError("PyRoundDigits: from_chars failed");
  return rounded;
}

// Objects/floatobject.c float___round___impl, lines 1082-1090 (ndigits None):
// "rounded = round(x); if (fabs(x-rounded) == 0.5) rounded = 2.0*round(x/2.0); return PyLong_FromDouble(rounded);"
// PyLong_FromDouble raises for infinities and NaN (Objects/longobject.c) and an int has no -0.
double RoundHalfEven(double x) {
  if (std::isinf(x)) throw ConvertError("cannot convert float infinity to integer");
  if (std::isnan(x)) throw ConvertError("cannot convert float NaN to integer");
  double rounded = CRound(x);
  if (std::fabs(x - rounded) == 0.5) rounded = 2.0 * CRound(x / 2.0);
  return rounded == 0.0 ? 0.0 : rounded;
}

// Objects/floatobject.c PyFloat_Pack4, IEEE branch (lines 2244-2249): "float y = (float)x;
// if (Py_IS_INFINITY(y) && !Py_IS_INFINITY(x)) goto Overflow;".
float PackF32(double x) {
  const float y = static_cast<float>(x);
  if (std::isinf(y) && !std::isinf(x)) throw ConvertError("float too large to pack with f format");
  return y;
}

// Objects/floatobject.c PyFloat_Pack2, lines 2060-2162.
uint16_t PackF16(double x) {
  unsigned sign;
  int e;
  double f;
  unsigned bits;
  if (x == 0.0) {
    sign = std::signbit(x) ? 1 : 0;  // copysign(1.0, x) == -1.0
    e = 0;
    bits = 0;
  } else if (std::isinf(x)) {
    sign = x < 0.0;
    e = 0x1f;
    bits = 0;
  } else if (std::isnan(x)) {
    sign = std::signbit(x) ? 1 : 0;
    e = 0x1f;
    bits = 512;
  } else {
    sign = x < 0.0;
    if (sign) x = -x;
    f = FrexpPositive(x, &e);
    f *= 2.0;  // normalize f to [1.0, 2.0)
    e--;
    if (e >= 16) {
      throw ConvertError("float too large to pack with e format");
    } else if (e < -25) {  // |x| < 2**-25: underflow to zero
      f = 0.0;
      e = 0;
    } else if (e < -14) {  // |x| < 2**-14: gradual underflow (ldexp(f, 14 + e), exact)
      f = f * Pow2(14 + e);
      e = 0;
    } else {
      e += 15;
      f -= 1.0;  // get rid of the leading 1
    }
    f *= 1024.0;  // 2**10
    bits = static_cast<unsigned short>(f);  // truncation
    if ((f - bits > 0.5) || ((f - bits == 0.5) && (bits % 2 == 1))) {  // round to even
      ++bits;
      if (bits == 1024) {  // the carry propagated out of a string of 10 1 bits
        bits = 0;
        ++e;
        if (e == 31) throw ConvertError("float too large to pack with e format");
      }
    }
  }
  return static_cast<uint16_t>(bits | (unsigned(e) << 10) | (sign << 15));
}

// Objects/floatobject.c PyFloat_Unpack2, lines 2403-2452 (ldexp(x, e) with e in [-14, 15] is exact).
double HalfToDouble(uint16_t h) {
  const unsigned sign = (h >> 15) & 1;
  int e = (h >> 10) & 0x1f;
  const unsigned f = h & 0x3ff;
  if (e == 0x1f) {
    if (f == 0) return sign ? -INFINITY : INFINITY;
    return D(sign ? (kQuietNan | (1ull << 63)) : kQuietNan);  // sign ? -fabs(Py_NAN) : fabs(Py_NAN)
  }
  double x = double(f) / 1024.0;
  if (e == 0) {
    e = -14;
  } else {
    x += 1.0;
    e -= 15;
  }
  x = x * Pow2(e);
  if (sign) x = -x;
  return x;
}

// ---------------------------------------------------------------------------------------------------------------
// musl v1.2.4 src/math/exp.c (lines 31-134) with the data of src/math/exp_data.c (lines 12-182) and exp_data.h
// (EXP_TABLE_BITS 7, EXP_POLY_ORDER 5, EXP_USE_TOINT_NARROW 0); TOINT_INTRINSICS is 0 and WANT_ROUNDING 1
// (src/internal/libm.h). The underflow-flag side effect in specialcase (fp_force_eval) is left out: it changes
// no result bits.

namespace {

constexpr int kExpTableBits = 7;
constexpr uint64_t kN = 1u << kExpTableBits;
constexpr double kInvLn2N = 0x1.71547652b82fep0 * kN;
constexpr double kNegLn2hiN = -0x1.62e42fefa0000p-8;
constexpr double kNegLn2loN = -0x1.cf79abc9e3b3ap-47;
constexpr double kShift = 0x1.8p52;
constexpr double kC2 = 0x1.ffffffffffdbdp-2;  // poly[5 - EXP_POLY_ORDER]
constexpr double kC3 = 0x1.555555555543cp-3;
constexpr double kC4 = 0x1.55555cf172b91p-5;
constexpr double kC5 = 0x1.1111167a4d017p-7;

// 2^(k/N) ~= H[k]*(1 + T[k]) for int k in [0,N): tab[2*k] = asuint64(T[k]), tab[2*k+1] = asuint64(H[k]) - (k << 52)/N.
const uint64_t kExpTab[2 * kN] = {
    0x0000000000000000ull, 0x3ff0000000000000ull,
    0x3c9b3b4f1a88bf6eull, 0x3feff63da9fb3335ull,
    0xbc7160139cd8dc5dull, 0x3fefec9a3e778061ull,
    0xbc905e7a108766d1ull, 0x3fefe315e86e7f85ull,
    0x3c8cd2523567f613ull, 0x3fefd9b0d3158574ull,
    0xbc8bce8023f98efaull, 0x3fefd06b29ddf6deull,
    0x3c60f74e61e6c861ull, 0x3fefc74518759bc8ull,
    0x3c90a3e45b33d399ull, 0x3fefbe3ecac6f383ull,
    0x3c979aa65d837b6dull, 0x3fefb5586cf9890full,
    0x3c8eb51a92fdeffcull, 0x3fefac922b7247f7ull,
    0x3c3ebe3d702f9cd1ull, 0x3fefa3ec32d3d1a2ull,
    0xbc6a033489906e0bull, 0x3fef9b66affed31bull,
    0xbc9556522a2fbd0eull, 0x3fef9301d0125b51ull,
    0xbc5080ef8c4eea55ull, 0x3fef8abdc06c31ccull,
    0xbc91c923b9d5f416ull, 0x3fef829aaea92de0ull,
    0x3c80d3e3e95c55afull, 0x3fef7a98c8a58e51ull,
    0xbc801b15eaa59348ull, 0x3fef72b83c7d517bull,
    0xbc8f1ff055de323dull, 0x3fef6af9388c8deaull,
    0x3c8b898c3f1353bfull, 0x3fef635beb6fcb75ull,
    0xbc96d99c7611eb26ull, 0x3fef5be084045cd4ull,
    0x3c9aecf73e3a2f60ull, 0x3fef54873168b9aaull,
    0xbc8fe782cb86389dull, 0x3fef4d5022fcd91dull,
    0x3c8a6f4144a6c38dull, 0x3fef463b88628cd6ull,
    0x3c807a05b0e4047dull, 0x3fef3f49917ddc96ull,
    0x3c968efde3a8a894ull, 0x3fef387a6e756238ull,
    0x3c875e18f274487dull, 0x3fef31ce4fb2a63full,
    0x3c80472b981fe7f2ull, 0x3fef2b4565e27cddull,
    0xbc96b87b3f71085eull, 0x3fef24dfe1f56381ull,
    0x3c82f7e16d09ab31ull, 0x3fef1e9df51fdee1ull,
    0xbc3d219b1a6fbffaull, 0x3fef187fd0dad990ull,
    0x3c8b3782720c0ab4ull, 0x3fef1285a6e4030bull,
    0x3c6e149289cecb8full, 0x3fef0cafa93e2f56ull,
    0x3c834d754db0abb6ull, 0x3fef06fe0a31b715ull,
    0x3c864201e2ac744cull, 0x3fef0170fc4cd831ull,
    0x3c8fdd395dd3f84aull, 0x3feefc08b26416ffull,
    0xbc86a3803b8e5b04ull, 0x3feef6c55f929ff1ull,
    0xbc924aedcc4b5068ull, 0x3feef1a7373aa9cbull,
    0xbc9907f81b512d8eull, 0x3feeecae6d05d866ull,
    0xbc71d1e83e9436d2ull, 0x3feee7db34e59ff7ull,
    0xbc991919b3ce1b15ull, 0x3feee32dc313a8e5ull,
    0x3c859f48a72a4c6dull, 0x3feedea64c123422ull,
    0xbc9312607a28698aull, 0x3feeda4504ac801cull,
    0xbc58a78f4817895bull, 0x3feed60a21f72e2aull,
    0xbc7c2c9b67499a1bull, 0x3feed1f5d950a897ull,
    0x3c4363ed60c2ac11ull, 0x3feece086061892dull,
    0x3c9666093b0664efull, 0x3feeca41ed1d0057ull,
    0x3c6ecce1daa10379ull, 0x3feec6a2b5c13cd0ull,
    0x3c93ff8e3f0f1230ull, 0x3feec32af0d7d3deull,
    0x3c7690cebb7aafb0ull, 0x3feebfdad5362a27ull,
    0x3c931dbdeb54e077ull, 0x3feebcb299fddd0dull,
    0xbc8f94340071a38eull, 0x3feeb9b2769d2ca7ull,
    0xbc87deccdc93a349ull, 0x3feeb6daa2cf6642ull,
    0xbc78dec6bd0f385full, 0x3feeb42b569d4f82ull,
    0xbc861246ec7b5cf6ull, 0x3feeb1a4ca5d920full,
    0x3c93350518fdd78eull, 0x3feeaf4736b527daull,
    0x3c7b98b72f8a9b05ull, 0x3feead12d497c7fdull,
    0x3c9063e1e21c5409ull, 0x3feeab07dd485429ull,
    0x3c34c7855019c6eaull, 0x3feea9268a5946b7ull,
    0x3c9432e62b64c035ull, 0x3feea76f15ad2148ull,
    0xbc8ce44a6199769full, 0x3feea5e1b976dc09ull,
    0xbc8c33c53bef4da8ull, 0x3feea47eb03a5585ull,
    0xbc845378892be9aeull, 0x3feea34634ccc320ull,
    0xbc93cedd78565858ull, 0x3feea23882552225ull,
    0x3c5710aa807e1964ull, 0x3feea155d44ca973ull,
    0xbc93b3efbf5e2228ull, 0x3feea09e667f3bcdull,
    0xbc6a12ad8734b982ull, 0x3feea012750bdabfull,
    0xbc6367efb86da9eeull, 0x3fee9fb23c651a2full,
    0xbc80dc3d54e08851ull, 0x3fee9f7df9519484ull,
    0xbc781f647e5a3ecfull, 0x3fee9f75e8ec5f74ull,
    0xbc86ee4ac08b7db0ull, 0x3fee9f9a48a58174ull,
    0xbc8619321e55e68aull, 0x3fee9feb564267c9ull,
    0x3c909ccb5e09d4d3ull, 0x3feea0694fde5d3full,
    0xbc7b32dcb94da51dull, 0x3feea11473eb0187ull,
    0x3c94ecfd5467c06bull, 0x3feea1ed0130c132ull,
    0x3c65ebe1abd66c55ull, 0x3feea2f336cf4e62ull,
    0xbc88a1c52fb3cf42ull, 0x3feea427543e1a12ull,
    0xbc9369b6f13b3734ull, 0x3feea589994cce13ull,
    0xbc805e843a19ff1eull, 0x3feea71a4623c7adull,
    0xbc94d450d872576eull, 0x3feea8d99b4492edull,
    0x3c90ad675b0e8a00ull, 0x3feeaac7d98a6699ull,
    0x3c8db72fc1f0eab4ull, 0x3feeace5422aa0dbull,
    0xbc65b6609cc5e7ffull, 0x3feeaf3216b5448cull,
    0x3c7bf68359f35f44ull, 0x3feeb1ae99157736ull,
    0xbc93091fa71e3d83ull, 0x3feeb45b0b91ffc6ull,
    0xbc5da9b88b6c1e29ull, 0x3feeb737b0cdc5e5ull,
    0xbc6c23f97c90b959ull, 0x3feeba44cbc8520full,
    0xbc92434322f4f9aaull, 0x3feebd829fde4e50ull,
    0xbc85ca6cd7668e4bull, 0x3feec0f170ca07baull,
    0x3c71affc2b91ce27ull, 0x3feec49182a3f090ull,
    0x3c6dd235e10a73bbull, 0x3feec86319e32323ull,
    0xbc87c50422622263ull, 0x3feecc667b5de565ull,
    0x3c8b1c86e3e231d5ull, 0x3feed09bec4a2d33ull,
    0xbc91bbd1d3bcbb15ull, 0x3feed503b23e255dull,
    0x3c90cc319cee31d2ull, 0x3feed99e1330b358ull,
    0x3c8469846e735ab3ull, 0x3feede6b5579fdbfull,
    0xbc82dfcd978e9db4ull, 0x3feee36bbfd3f37aull,
    0x3c8c1a7792cb3387ull, 0x3feee89f995ad3adull,
    0xbc907b8f4ad1d9faull, 0x3feeee07298db666ull,
    0xbc55c3d956dcaebaull, 0x3feef3a2b84f15fbull,
    0xbc90a40e3da6f640ull, 0x3feef9728de5593aull,
    0xbc68d6f438ad9334ull, 0x3feeff76f2fb5e47ull,
    0xbc91eee26b588a35ull, 0x3fef05b030a1064aull,
    0x3c74ffd70a5fddcdull, 0x3fef0c1e904bc1d2ull,
    0xbc91bdfbfa9298acull, 0x3fef12c25bd71e09ull,
    0x3c736eae30af0cb3ull, 0x3fef199bdd85529cull,
    0x3c8ee3325c9ffd94ull, 0x3fef20ab5fffd07aull,
    0x3c84e08fd10959acull, 0x3fef27f12e57d14bull,
    0x3c63cdaf384e1a67ull, 0x3fef2f6d9406e7b5ull,
    0x3c676b2c6c921968ull, 0x3fef3720dcef9069ull,
    0xbc808a1883ccb5d2ull, 0x3fef3f0b555dc3faull,
    0xbc8fad5d3ffffa6full, 0x3fef472d4a07897cull,
    0xbc900dae3875a949ull, 0x3fef4f87080d89f2ull,
    0x3c74a385a63d07a7ull, 0x3fef5818dcfba487ull,
    0xbc82919e2040220full, 0x3fef60e316c98398ull,
    0x3c8e5a50d5c192acull, 0x3fef69e603db3285ull,
    0x3c843a59ac016b4bull, 0x3fef7321f301b460ull,
    0xbc82d52107b43e1full, 0x3fef7c97337b9b5full,
    0xbc892ab93b470dc9ull, 0x3fef864614f5a129ull,
    0x3c74b604603a88d3ull, 0x3fef902ee78b3ff6ull,
    0x3c83c5ec519d7271ull, 0x3fef9a51fbc74c83ull,
    0xbc8ff7128fd391f0ull, 0x3fefa4afa2a490daull,
    0xbc8dae98e223747dull, 0x3fefaf482d8e67f1ull,
    0x3c8ec3bc41aa2008ull, 0x3fefba1bee615a27ull,
    0x3c842b94c3a9eb32ull, 0x3fefc52b376bba97ull,
    0x3c8a64a931d185eeull, 0x3fefd0765b6e4540ull,
    0xbc8e37bae43be3edull, 0x3fefdbfdad9cbe14ull,
    0x3c77893b4d91cd9dull, 0x3fefe7c1819e90d8ull,
    0x3c5305c14160cc89ull, 0x3feff3c22b8f71f1ull,
};

uint32_t Top12(double x) { return uint32_t(B(x) >> 52); }

double ExpSpecialCase(double tmp, uint64_t sbits, uint64_t ki) {
  double scale, y;
  if ((ki & 0x80000000) == 0) {
    // k > 0, the exponent of scale might have overflowed by <= 460.
    sbits -= 1009ull << 52;
    scale = D(sbits);
    y = 0x1p1009 * (scale + scale * tmp);
    return y;
  }
  // k < 0, need special care in the subnormal range.
  sbits += 1022ull << 52;
  scale = D(sbits);
  y = scale + scale * tmp;
  if (y < 1.0) {
    // Round y to the right precision before scaling it into the subnormal range to avoid double rounding.
    double hi, lo;
    lo = scale - y + scale * tmp;
    hi = 1.0 + y;
    lo = 1.0 - hi + y + lo;
    y = (hi + lo) - 1.0;
    if (y == 0.0) y = 0.0;  // avoid -0.0 with downward rounding (WANT_ROUNDING)
  }
  y = 0x1p-1022 * y;
  return y;
}

}  // namespace

double Exp(double x) {
  uint32_t abstop = Top12(x) & 0x7ff;
  if (abstop - Top12(0x1p-54) >= Top12(512.0) - Top12(0x1p-54)) {
    if (abstop - Top12(0x1p-54) >= 0x80000000)
      return 1.0 + x;  // tiny x (0 included); avoids a spurious underflow
    if (abstop >= Top12(1024.0)) {
      if (B(x) == B(-INFINITY)) return 0.0;
      if (abstop >= Top12(INFINITY)) return 1.0 + x;
      if (B(x) >> 63) return 0x1p-767 * 0x1p-767;  // __math_uflow(0)
      return 0x1p769 * 0x1p769;                     // __math_oflow(0)
    }
    abstop = 0;  // large x is special-cased below
  }
  // exp(x) = 2^(k/N) * exp(r), with exp(r) in [2^(-1/2N),2^(1/2N)]; x = ln2/N*k + r, int k, r in [-ln2/2N, ln2/2N].
  const double z = kInvLn2N * x;
  double kd = z + kShift;  // z - kd is in [-1, 1] in non-nearest rounding modes
  const uint64_t ki = B(kd);
  kd -= kShift;
  const double r = x + kd * kNegLn2hiN + kd * kNegLn2loN;
  // 2^(k/N) ~= scale * (1 + tail).
  const uint64_t idx = 2 * (ki % kN);
  const uint64_t top = ki << (52 - kExpTableBits);
  const double tail = D(kExpTab[idx]);
  // This is only a valid scale when -1023*N < k < 1024*N.
  const uint64_t sbits = kExpTab[idx + 1] + top;
  // exp(x) = 2^(k/N) * exp(r) ~= scale + scale * (tail + exp(r) - 1).
  const double r2 = r * r;
  const double tmp = tail + r + r2 * (kC2 + r * kC3) + r2 * r2 * (kC4 + r * kC5);
  if (abstop == 0) return ExpSpecialCase(tmp, sbits, ki);
  const double scale = D(sbits);
  return scale + scale * tmp;
}

// musl v1.2.4 src/math/atan.c (lines 35-116) and src/math/atan2.c (lines 40-107).

namespace {

constexpr double kAtanHi[] = {
    4.63647609000806093515e-01,  // atan(0.5)hi 0x3FDDAC67, 0x0561BB4F
    7.85398163397448278999e-01,  // atan(1.0)hi 0x3FE921FB, 0x54442D18
    9.82793723247329054082e-01,  // atan(1.5)hi 0x3FEF730B, 0xD281F69B
    1.57079632679489655800e+00,  // atan(inf)hi 0x3FF921FB, 0x54442D18
};
constexpr double kAtanLo[] = {
    2.26987774529616870924e-17,  // atan(0.5)lo 0x3C7A2B7F, 0x222F65E2
    3.06161699786838301793e-17,  // atan(1.0)lo 0x3C81A626, 0x33145C07
    1.39033110312309984516e-17,  // atan(1.5)lo 0x3C700788, 0x7AF0CBBD
    6.12323399573676603587e-17,  // atan(inf)lo 0x3C91A626, 0x33145C07
};
constexpr double kAT[] = {
    3.33333333333329318027e-01,   // 0x3FD55555, 0x5555550D
    -1.99999999998764832476e-01,  // 0xBFC99999, 0x9998EBC4
    1.42857142725034663711e-01,   // 0x3FC24924, 0x920083FF
    -1.11111104054623557880e-01,  // 0xBFBC71C6, 0xFE231671
    9.09088713343650656196e-02,   // 0x3FB745CD, 0xC54C206E
    -7.69187620504482999495e-02,  // 0xBFB3B0F2, 0xAF749A6D
    6.66107313738753120669e-02,   // 0x3FB10D66, 0xA0D03D51
    -5.83357013379057348645e-02,  // 0xBFADDE2D, 0x52DEFD9A
    4.97687799461593236017e-02,   // 0x3FA97B4B, 0x24760DEB
    -3.65315727442169155270e-02,  // 0xBFA2B444, 0x2C6A6C2F
    1.62858201153657823623e-02,   // 0x3F90AD3A, 0xE322DA11
};
// The decimal constants must be the intended bits (the hex words in musl's comments).
static_assert(std::bit_cast<uint64_t>(kAtanHi[0]) == 0x3FDDAC670561BB4Full);
static_assert(std::bit_cast<uint64_t>(kAtanHi[1]) == 0x3FE921FB54442D18ull);
static_assert(std::bit_cast<uint64_t>(kAtanHi[2]) == 0x3FEF730BD281F69Bull);
static_assert(std::bit_cast<uint64_t>(kAtanHi[3]) == 0x3FF921FB54442D18ull);
static_assert(std::bit_cast<uint64_t>(kAtanLo[0]) == 0x3C7A2B7F222F65E2ull);
static_assert(std::bit_cast<uint64_t>(kAtanLo[1]) == 0x3C81A62633145C07ull);
static_assert(std::bit_cast<uint64_t>(kAtanLo[2]) == 0x3C7007887AF0CBBDull);
static_assert(std::bit_cast<uint64_t>(kAtanLo[3]) == 0x3C91A62633145C07ull);
static_assert(std::bit_cast<uint64_t>(kAT[0]) == 0x3FD555555555550Dull);
static_assert(std::bit_cast<uint64_t>(kAT[1]) == 0xBFC999999998EBC4ull);
static_assert(std::bit_cast<uint64_t>(kAT[2]) == 0x3FC24924920083FFull);
static_assert(std::bit_cast<uint64_t>(kAT[3]) == 0xBFBC71C6FE231671ull);
static_assert(std::bit_cast<uint64_t>(kAT[4]) == 0x3FB745CDC54C206Eull);
static_assert(std::bit_cast<uint64_t>(kAT[5]) == 0xBFB3B0F2AF749A6Dull);
static_assert(std::bit_cast<uint64_t>(kAT[6]) == 0x3FB10D66A0D03D51ull);
static_assert(std::bit_cast<uint64_t>(kAT[7]) == 0xBFADDE2D52DEFD9Aull);
static_assert(std::bit_cast<uint64_t>(kAT[8]) == 0x3FA97B4B24760DEBull);
static_assert(std::bit_cast<uint64_t>(kAT[9]) == 0xBFA2B4442C6A6C2Full);
static_assert(std::bit_cast<uint64_t>(kAT[10]) == 0x3F90AD3AE322DA11ull);

constexpr double kPi = 3.1415926535897931160E+00;    // 0x400921FB, 0x54442D18
constexpr double kPiLo = 1.2246467991473531772E-16;  // 0x3CA1A626, 0x33145C07
static_assert(std::bit_cast<uint64_t>(kPi) == 0x400921FB54442D18ull);
static_assert(std::bit_cast<uint64_t>(kPiLo) == 0x3CA1A62633145C07ull);

double Atan(double x) {
  uint32_t ix = uint32_t(B(x) >> 32);
  const uint32_t sign = ix >> 31;
  ix &= 0x7fffffff;
  int id;
  if (ix >= 0x44100000) {  // |x| >= 2^66
    if (std::isnan(x)) return x;
    const double z = kAtanHi[3] + 0x1p-120f;
    return sign ? -z : z;
  }
  if (ix < 0x3fdc0000) {  // |x| < 0.4375
    if (ix < 0x3e400000) return x;  // |x| < 2^-27 (musl raises underflow for a subnormal x here; no result change)
    id = -1;
  } else {
    x = std::fabs(x);
    if (ix < 0x3ff30000) {    // |x| < 1.1875
      if (ix < 0x3fe60000) {  // 7/16 <= |x| < 11/16
        id = 0;
        x = (2.0 * x - 1.0) / (2.0 + x);
      } else {  // 11/16 <= |x| < 19/16
        id = 1;
        x = (x - 1.0) / (x + 1.0);
      }
    } else {
      if (ix < 0x40038000) {  // |x| < 2.4375
        id = 2;
        x = (x - 1.5) / (1.0 + 1.5 * x);
      } else {  // 2.4375 <= |x| < 2^66
        id = 3;
        x = -1.0 / x;
      }
    }
  }
  // end of argument reduction
  double z = x * x;
  const double w = z * z;
  // break sum from i=0 to 10 aT[i]z**(i+1) into odd and even poly
  const double s1 = z * (kAT[0] + w * (kAT[2] + w * (kAT[4] + w * (kAT[6] + w * (kAT[8] + w * kAT[10])))));
  const double s2 = w * (kAT[1] + w * (kAT[3] + w * (kAT[5] + w * (kAT[7] + w * kAT[9]))));
  if (id < 0) return x - x * (s1 + s2);
  z = kAtanHi[id] - (x * (s1 + s2) - kAtanLo[id] - x);
  return sign ? -z : z;
}

}  // namespace

double Atan2(double y, double x) {
  if (std::isnan(x) || std::isnan(y)) return x + y;
  const uint64_t ux = B(x), uy = B(y);
  uint32_t ix = uint32_t(ux >> 32), lx = uint32_t(ux);
  uint32_t iy = uint32_t(uy >> 32), ly = uint32_t(uy);
  if (((ix - 0x3ff00000) | lx) == 0) return Atan(y);  // x = 1.0
  const uint32_t m = ((iy >> 31) & 1) | ((ix >> 30) & 2);  // 2*sign(x)+sign(y)
  ix = ix & 0x7fffffff;
  iy = iy & 0x7fffffff;
  // when y = 0
  if ((iy | ly) == 0) {
    switch (m) {
      case 0:
      case 1:
        return y;  // atan(+-0,+anything)=+-0
      case 2:
        return kPi;  // atan(+0,-anything) = pi
      case 3:
        return -kPi;  // atan(-0,-anything) =-pi
    }
  }
  // when x = 0
  if ((ix | lx) == 0) return m & 1 ? -kPi / 2 : kPi / 2;
  // when x is INF
  if (ix == 0x7ff00000) {
    if (iy == 0x7ff00000) {
      switch (m) {
        case 0:
          return kPi / 4;  // atan(+INF,+INF)
        case 1:
          return -kPi / 4;  // atan(-INF,+INF)
        case 2:
          return 3 * kPi / 4;  // atan(+INF,-INF)
        case 3:
          return -3 * kPi / 4;  // atan(-INF,-INF)
      }
    } else {
      switch (m) {
        case 0:
          return 0.0;  // atan(+...,+INF)
        case 1:
          return -0.0;  // atan(-...,+INF)
        case 2:
          return kPi;  // atan(+...,-INF)
        case 3:
          return -kPi;  // atan(-...,-INF)
      }
    }
  }
  // |y/x| > 0x1p64
  if (ix + (64 << 20) < iy || iy == 0x7ff00000) return m & 1 ? -kPi / 2 : kPi / 2;
  // z = atan(|y/x|) without spurious underflow
  double z;
  if ((m & 2) && iy + (64 << 20) < ix)  // |y/x| < 0x1p-64, x<0
    z = 0;
  else
    z = Atan(std::fabs(y / x));
  switch (m) {
    case 0:
      return z;  // atan(+,+)
    case 1:
      return -z;  // atan(-,+)
    case 2:
      return kPi - (z - kPiLo);  // atan(+,-)
    default:  // case 3
      return (z - kPiLo) - kPi;  // atan(-,-)
  }
}

// ---------------------------------------------------------------------------------------------------------------
// numpy v1.26.3.

namespace {

// numpy/core/src/umath/loops_utils.h.src DOUBLE_pairwise_sum, lines 77-142 (contiguous input).
double PairwiseSum(const double* a, size_t n) {
  if (n < 8) {
    // Start with -0 to preserve -0 values (0 + -0 == 0 while -0 + -0 == -0).
    double res = -0.0;
    for (size_t i = 0; i < n; i++) res += a[i];
    return res;
  } else if (n <= 128) {  // PW_BLOCKSIZE
    double r[8];
    for (int k = 0; k < 8; ++k) r[k] = a[k];
    size_t i;
    for (i = 8; i < n - (n % 8); i += 8) {
      r[0] += a[i + 0];
      r[1] += a[i + 1];
      r[2] += a[i + 2];
      r[3] += a[i + 3];
      r[4] += a[i + 4];
      r[5] += a[i + 5];
      r[6] += a[i + 6];
      r[7] += a[i + 7];
    }
    double res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
    for (; i < n; i++) res += a[i];  // the non-multiple-of-8 rest
    return res;
  } else {
    // divide by two but avoid non-multiples of unroll factor
    size_t n2 = n / 2;
    n2 -= n2 % 8;
    return PairwiseSum(a, n2) + PairwiseSum(a + n2, n - n2);
  }
}

// An add reduction over a contiguous axis: the output starts at add's identity 0.0 (numpy/core/src/umath/reduction.c
// PyUFunc_ReduceWrapper, lines 295-361) and the reduce loop adds the pairwise sum of each inner-loop chunk to it
// ("*((@type@*)src0) @OP@= @TYPE@_pairwise_sum(src1, len, ssrc1)", loops_arithm_fp.dispatch.c.src lines 58-75).
// The buffered reduction iterator hands the loop at most NPY_BUFSIZE = 8192 elements at a time
// (numpy/core/include/numpy/ndarraytypes.h; not in ref_src): measured with the reference interpreter, and pinned at
// lengths 8191, 8192, 8193, 10000 and 20000.
constexpr size_t kBufSize = 8192;

double ReduceContiguous(const double* a, size_t n) {
  double acc = 0.0;
  for (size_t i = 0; i < n; i += kBufSize) acc += PairwiseSum(a + i, std::min(kBufSize, n - i));
  return acc;
}

size_t RowCount(std::span<const double> values, size_t cols, std::span<double> out) {
  if (cols == 0 || values.size() % cols != 0 || out.size() != cols)
    throw ConvertError("numpy axis-0 reduction: the shape does not match");
  return values.size() / cols;
}

}  // namespace

double NpSumLastAxis(std::span<const double> row) { return ReduceContiguous(row.data(), row.size()); }

// Axis 0 of a C-contiguous 2-D array: the iterator's inner loop runs along the rows (the smaller stride), so for
// cols >= 2 each output element gets one elementwise add per row, in row order (DOUBLE_add's non-reduce path,
// loops_arithm_fp.dispatch.c.src from line 76); with cols == 1 the axis is the only one and it is the 1-D reduction.
void NpSumAxis0(std::span<const double> rows, size_t cols, std::span<double> out) {
  const size_t n = RowCount(rows, cols, out);
  if (cols == 1) {
    out[0] = ReduceContiguous(rows.data(), n);
    return;
  }
  for (size_t j = 0; j < cols; ++j) out[j] = 0.0;
  for (size_t r = 0; r < n; ++r)
    for (size_t j = 0; j < cols; ++j) out[j] = out[j] + rows[r * cols + j];
}

double NpSum1D(std::span<const double> v) { return ReduceContiguous(v.data(), v.size()); }

// numpy/core/_methods.py _mean (not in ref_src): umr_sum(arr, axis, ...) then true_divide by the count.
double NpMean1D(std::span<const double> v) { return NpSum1D(v) / static_cast<double>(v.size()); }

void NpMeanAxis0(std::span<const double> rows, size_t cols, std::span<double> out) {
  const size_t n = RowCount(rows, cols, out);
  NpSumAxis0(rows, cols, out);
  for (size_t j = 0; j < cols; ++j) out[j] = out[j] / static_cast<double>(n);
}

// numpy/lib/function_base.py percentile (lines 4271-4284: q = np.true_divide(q, 100)), _quantile (lines 4764-4850),
// the 'linear' method (lines 109-112: virtual index (n - 1) * q, gamma unchanged), _get_indexes (lines 4729-4761),
// _get_gamma (lines 4619-4638: virtual - previous, the previous index already an integer) and _lerp (lines 4641-4661).
double NpPercentileLinear(std::vector<double> v, double q) {
  const size_t n = v.size();
  if (n == 0) throw ConvertError("percentile of an empty array");
  const double qq = q / 100.0;
  if (!(0.0 <= qq && qq <= 1.0)) throw ConvertError("Percentiles must be in the range [0, 100]");
  for (double x : v)
    if (std::isnan(x)) return x;  // a NaN sorts last, and the result is arr[-1]
  const double virt = static_cast<double>(n - 1) * qq;
  int64_t prev, next;
  if (virt >= static_cast<double>(n - 1)) {  // above the last index: take the last value
    prev = next = -1;
  } else if (virt < 0.0) {
    prev = next = 0;
  } else {
    prev = static_cast<int64_t>(std::floor(virt));
    next = prev + 1;
  }
  std::sort(v.begin(), v.end());
  const double a = v[prev < 0 ? n - 1 : size_t(prev)];
  const double b = v[next < 0 ? n - 1 : size_t(next)];
  const double t = virt - static_cast<double>(prev);
  // _lerp: diff_b_a = b - a; a + diff_b_a * t, replaced by b - diff_b_a * (1 - t) where t >= 0.5.
  const double diff_b_a = b - a;
  double lerp = a + diff_b_a * t;
  if (t >= 0.5) lerp = b - diff_b_a * (1 - t);
  return lerp;
}

// numpy/lib/function_base.py _median (lines 3931-3990): partition each column, then mean() over the middle one or two
// rows (an axis-0 sum from 0.0 divided by the count; for one column the 1-D sum, which gives the same value for one
// or two terms), and a column whose last partitioned value is NaN gives that NaN (_median_nancheck).
void NpMedianAxis0(std::span<const double> rows, size_t cols, std::span<double> out) {
  const size_t n = RowCount(rows, cols, out);
  std::vector<double> col(n);
  for (size_t j = 0; j < cols; ++j) {
    for (size_t r = 0; r < n; ++r) col[r] = rows[r * cols + j];
    if (n == 0) {
      // Mean of an empty slice: numpy divides 0.0 by 0 at run time, which on x86 gives the default NaN
      // 0xfff8000000000000 (pinned). Written as the bits: here the compiler knows n == 0 and would fold 0.0 / 0 to
      // +NaN (0x7ff8000000000000).
      out[j] = D(0xfff8000000000000ull);
      continue;
    }
    const double* nan = nullptr;
    for (const double& x : col)
      if (std::isnan(x)) {
        nan = &x;
        break;
      }
    if (nan) {
      out[j] = *nan;
      continue;
    }
    std::sort(col.begin(), col.end());
    const size_t mid = n / 2;
    double acc = 0.0;
    if (n % 2 == 1) {
      acc = acc + col[mid];
      out[j] = acc / 1.0;
    } else {
      acc = acc + col[mid - 1];
      acc = acc + col[mid];
      out[j] = acc / 2.0;
    }
  }
}

// numpy/core/function_base.py linspace, lines 122-182 (endpoint=True, scalar start and stop, float64): y = arange(num)
// (exact integers), times step = delta / div (or divided by div then times delta when step is 0), plus start, and the
// last value set to stop.
std::vector<double> NpLinspace(double start, double stop, int num) {
  if (num < 0) throw ConvertError("Number of samples must be non-negative.");
  const int div = num - 1;
  const double delta = stop - start;
  std::vector<double> y(static_cast<size_t>(num));
  for (int i = 0; i < num; ++i) y[i] = static_cast<double>(i);
  if (div > 0) {
    const double step = delta / static_cast<double>(div);
    if (step == 0) {  // special handling for denormal numbers, gh-5437
      for (double& v : y) v = v / static_cast<double>(div);
      for (double& v : y) v = v * delta;
    } else {
      for (double& v : y) v = v * step;
    }
  } else {
    for (double& v : y) v = v * delta;
  }
  for (double& v : y) v = v + start;
  if (num > 1) y[num - 1] = stop;
  return y;
}

// numpy/core/src/npymath/npy_math_internal.h.src lines 445 and 452-455: DEG2RAD = NPY_PI / 180.0, x * DEG2RAD.
double NpRadians(double x) {
  constexpr double kNpyPi = 3.141592653589793238462643383279502884;  // NPY_PI
  constexpr double kDeg2Rad = kNpyPi / 180.0;
  return x * kDeg2Rad;
}

}  // namespace doa4::suitconv::num

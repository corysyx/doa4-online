// Suit converter (Plan 2): the floating-point rules every converter source is built under. Include this header FIRST
// in every src/doa4_suitconv_*.cpp.
//
// The converter must write the same bytes on every PC, so its arithmetic must be the plain IEEE double arithmetic the
// Python original does: no fused multiply-add (one rounding where Python rounds twice), no contraction, no fast-math
// reassociation, no errno-driven library paths. CMakeLists.txt gives these files
//   -mno-fma -ffp-contract=off -fno-fast-math -fno-math-errno
// after the target's -march=x86-64-v3, and the host runners pass the same flags through /clang:. If a build drops
// them, the checks below stop it at compile time instead of letting it produce different bytes.

#pragma once

#pragma clang fp contract(off)

#if defined(__FMA__)
#error "doa4_suitconv: FMA is enabled for a converter source; compile it with -mno-fma (CMakeLists.txt DOA4_SUITCONV_SOURCES)"
#endif
#if defined(__FMA4__)
#error "doa4_suitconv: FMA4 is enabled for a converter source; compile it with -mno-fma4 / -mno-fma"
#endif
#if defined(__FAST_MATH__)
#error "doa4_suitconv: fast-math is enabled for a converter source; compile it with -fno-fast-math"
#endif

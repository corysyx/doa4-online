#include "doa4_suitconv_fp.h"

// Suit converter (Plan 2): numpy 1.26.3's default argsort and its argmin for float64; see doa4_suitconv_sort.h.
// A line-by-line port of numpy v1.26.3, citing file:line in the copies in
// notes/plans/2026-11-07-public-importer-plan2-converter/ref_src/ (numpy_quicksort_1263.cpp is numpy/core/src/npysort/
// quicksort.cpp, numpy_npysort_heapsort.h is npysort/npysort_heapsort.h, numpy_npysort_common.h is
// npysort/npysort_common.h, numpy_npy_sort.h.src is common/npy_sort.h.src). Pointers stay pointers, as in numpy, so
// every comparison and swap happens in numpy's order. numpy is BSD-3-Clause, Copyright (c) 2005-2023, NumPy Developers.

#include "doa4_suitconv_num.h"
#include "doa4_suitconv_sort.h"

#include <atomic>
#include <cstdint>
#include <utility>

namespace doa4::suitconv::num {
namespace {

using npy_intp = int64_t;  // 64-bit Windows: npy_intp is a 64-bit signed integer, as np.argsort's int64 result

// numpy_quicksort_1263.cpp:67-68. NPY_BITSOF_INTP is 64 here.
constexpr int PYA_QS_STACK = 64 * 2;
constexpr npy_intp SMALL_QUICKSORT = 15;

std::atomic<uint64_t> g_heapsort_count{0};
ArgsortFn g_argsort_for_test = nullptr;

// numpy_npysort_common.h:131-135 (double_tag::less in numpy_tag.h calls it).
inline bool DOUBLE_LT(double a, double b) { return a < b || (b != b && a == a); }

// numpy_npy_sort.h.src:12-19.
inline int npy_get_msb(uint64_t unum) {
  int depth_limit = 0;
  while (unum >>= 1) {
    depth_limit++;
  }
  return depth_limit;
}

// numpy_npysort_heapsort.h:69-117, aheapsort_<npy::double_tag>. `a = tosort - 1` (line 76) points one slot before
// tosort; NpArgsort gives every buffer a spare leading slot, so that pointer is valid in C++ too.
int aheapsort_(const double* vv, npy_intp* tosort, npy_intp n) {
  g_heapsort_count.fetch_add(1, std::memory_order_relaxed);
  const double* v = vv;
  npy_intp *a, i, j, l, tmp;
  /* The arrays need to be offset by one for heapsort indexing */
  a = tosort - 1;

  for (l = n >> 1; l > 0; --l) {  // :78
    tmp = a[l];
    for (i = l, j = l << 1; j <= n;) {
      if (j < n && DOUBLE_LT(v[a[j]], v[a[j + 1]])) {  // :81
        j += 1;
      }
      if (DOUBLE_LT(v[tmp], v[a[j]])) {  // :84
        a[i] = a[j];
        i = j;
        j += j;
      } else {
        break;
      }
    }
    a[i] = tmp;  // :93
  }

  for (; n > 1;) {  // :96
    tmp = a[n];
    a[n] = a[1];
    n -= 1;
    for (i = 1, j = 2; j <= n;) {
      if (j < n && DOUBLE_LT(v[a[j]], v[a[j + 1]])) {  // :101
        j++;
      }
      if (DOUBLE_LT(v[tmp], v[a[j]])) {  // :104
        a[i] = a[j];
        i = j;
        j += j;
      } else {
        break;
      }
    }
    a[i] = tmp;  // :113
  }

  return 0;
}

// numpy_quicksort_1263.cpp:222-307, aquicksort_<npy::double_tag, npy_double>.
int aquicksort_(const double* vv, npy_intp* tosort, npy_intp num) {
  const double* v = vv;
  double vp;
  npy_intp* pl = tosort;
  npy_intp* pr = tosort + num - 1;
  npy_intp* stack[PYA_QS_STACK];
  npy_intp** sptr = stack;
  npy_intp *pm, *pi, *pj, *pk, vi;
  int depth[PYA_QS_STACK];
  int* psdepth = depth;
  int cdepth = npy_get_msb(static_cast<uint64_t>(num)) * 2;  // :235

  for (;;) {
    if (cdepth < 0) {  // :238
      aheapsort_(vv, pl, pr - pl + 1);
      goto stack_pop;
    }
    while ((pr - pl) > SMALL_QUICKSORT) {  // :242
      /* quicksort partition */
      pm = pl + ((pr - pl) >> 1);
      if (DOUBLE_LT(v[*pm], v[*pl])) {  // :245
        std::swap(*pm, *pl);
      }
      if (DOUBLE_LT(v[*pr], v[*pm])) {  // :248
        std::swap(*pr, *pm);
      }
      if (DOUBLE_LT(v[*pm], v[*pl])) {  // :251
        std::swap(*pm, *pl);
      }
      vp = v[*pm];  // :254
      pi = pl;
      pj = pr - 1;
      std::swap(*pm, *pj);
      for (;;) {  // :258
        do {
          ++pi;
        } while (DOUBLE_LT(v[*pi], vp));  // :261
        do {
          --pj;
        } while (DOUBLE_LT(vp, v[*pj]));  // :264
        if (pi >= pj) {
          break;
        }
        std::swap(*pi, *pj);
      }
      pk = pr - 1;  // :270
      std::swap(*pi, *pk);
      /* push largest partition on stack */
      if (pi - pl < pr - pi) {  // :273
        *sptr++ = pi + 1;
        *sptr++ = pr;
        pr = pi - 1;
      } else {
        *sptr++ = pl;
        *sptr++ = pi - 1;
        pl = pi + 1;
      }
      *psdepth++ = --cdepth;  // :283
    }

    /* insertion sort */
    for (pi = pl + 1; pi <= pr; ++pi) {  // :287
      vi = *pi;
      vp = v[vi];
      pj = pi;
      pk = pi - 1;
      while (pj > pl && DOUBLE_LT(vp, v[*pk])) {  // :292
        *pj-- = *pk--;
      }
      *pj = vi;
    }
  stack_pop:  // :297
    if (sptr == stack) {
      break;
    }
    pr = *(--sptr);
    pl = *(--sptr);
    cdepth = *(--psdepth);
  }

  return 0;
}

}  // namespace

std::vector<int64_t> NpArgsort(std::span<const double> v) {
  if (g_argsort_for_test) return g_argsort_for_test(v);
  const npy_intp n = static_cast<npy_intp>(v.size());
  if (n == 0) return {};
  // numpy (item_selection.c _new_argsortlike) fills the index buffer with 0..n-1 and calls aquicksort_double
  // (numpy_quicksort_1263.cpp:942-950), which skips aquicksort_dispatch on Windows (:72-84) and runs aquicksort_.
  // Slot 0 is the spare leading slot aheapsort_'s `tosort - 1` needs.
  std::vector<int64_t> buf(v.size() + 1, 0);
  npy_intp* tosort = buf.data() + 1;
  for (npy_intp i = 0; i < n; ++i) tosort[i] = i;
  aquicksort_(v.data(), tosort, n);
  return std::vector<int64_t>(buf.begin() + 1, buf.end());
}

// numpy 1.26.3's float argmin (numpy/core/src/multiarray/arraytypes.c.src @fname@_argmin, whose result the SIMD
// version in argfunc.dispatch.c.src keeps; neither file is in ref_src, the pins hold the behaviour): the first NaN
// wins, otherwise the first minimum (`!(*ip >= mp)`).
int64_t NpArgmin(std::span<const double> v) {
  if (v.empty()) throw ConvertError("attempt to get argmin of an empty sequence");
  double mp = v[0];
  int64_t min_ind = 0;
  if (mp != mp) return 0; /* nan encountered; it's minimal */
  for (size_t i = 1; i < v.size(); i++) {
    /* Propagate nans, similarly as max() and min() */
    if (!(v[i] >= mp)) { /* negated, for correct nan handling */
      mp = v[i];
      min_ind = static_cast<int64_t>(i);
      if (mp != mp) break; /* nan encountered, it's minimal */
    }
  }
  return min_ind;
}

void SetArgsortForTest(ArgsortFn fn) { g_argsort_for_test = fn; }

uint64_t ArgsortHeapsortCount() { return g_heapsort_count.load(std::memory_order_relaxed); }

}  // namespace doa4::suitconv::num

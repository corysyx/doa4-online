// Suit converter (Plan 2): numpy's default np.argsort and np.argmin on float64 arrays, with numpy 1.26.3's exact
// order for ties (two of the converted suits' bytes depend on which of several tied vertices argsort lists first, and
// a stable sort changes them).
//
// NpArgsort is a line-by-line port of aquicksort_<npy::double_tag> (numpy v1.26.3 numpy/core/src/npysort/
// quicksort.cpp lines 222-307, called by aquicksort_double at lines 942-950; the AVX512 dispatch at lines 72-84 is off
// on Windows) with its heapsort fallback aheapsort_ (npysort_heapsort.h lines 69-117). Copies of the sources are in
// notes/plans/2026-11-07-public-importer-plan2-converter/ref_src/. Pinned against the real interpreter by
// tests/host/suitconv_sort_host_test.cpp (pins from tests/host/make_suitconv_pins.py). Pure C++ (no SDK). The sort
// only compares (no arithmetic), so it needs no floating-point environment of its own.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace doa4::suitconv::num {

// np.argsort(v) with the default kind on numpy 1.26.3 (Windows): the int64 indices that sort v ascending, with NaN
// last (DOUBLE_LT), ties in numpy's introsort order. An empty v gives an empty result.
std::vector<int64_t> NpArgsort(std::span<const double> v);

// np.argmin(v) on numpy 1.26.3: the index of the first minimum; if v holds a NaN, the index of the first NaN.
// Throws ConvertError("attempt to get argmin of an empty sequence") for an empty v (numpy's ValueError).
int64_t NpArgmin(std::span<const double> v);

// TEST ONLY. Replaces what NpArgsort runs with fn (Task 10 swaps in a stable sort to show which outputs depend on the
// tie order); nullptr (the default) restores the port. A plain function pointer, not synchronised: it is not
// thread-safe, and only tests set it, before any converter work starts and after it ends.
using ArgsortFn = std::vector<int64_t> (*)(std::span<const double> v);
void SetArgsortForTest(ArgsortFn fn);

// How many times the port has taken the heapsort fallback (aquicksort_'s cdepth < 0 branch) since the process
// started; for the tests, which read it before and after a call.
uint64_t ArgsortHeapsortCount();

}  // namespace doa4::suitconv::num

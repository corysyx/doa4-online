"""Rollback milestone 1 (corysyx/doa4-online): make every store of the generated code mark its 4 KB guest page in
doa4_wmap (src/net/rb_wmap.h).

  py -3 -I tools/np_wmap_patch.py [--check]

generated/ is checked in and a normal build does not regenerate it; run this again after a codegen run (BUILDING.md
section 8), which writes the files without the marks. It is idempotent: a file already patched is left as it is.
--check changes nothing and exits 1 if any file still needs the patch (the host-test step can run it).

What it changes:
  generated/default/dead_or_alive_4_init.h  REX_STORE_U8/16/32/64 mark the page of the first and the last byte
                                            (an unaligned store can cross a page); REX_RAW_ADDR_W(x) = mark, then
                                            REX_RAW_ADDR(x)
  generated/default/*.cpp                   the guest stores written with REX_RAW_ADDR use REX_RAW_ADDR_W:
                                            vector stores (stvx..., 16-byte aligned), dcbz (128 bytes, aligned) and the
                                            atomic stores (stwcx./stdcx.: __sync_bool_compare_and_swap)
Loads keep REX_RAW_ADDR. The MMIO stores (REX_MM_STORE_*) are not game memory and are left alone.
"""
import argparse
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
GEN = os.path.join(REPO, "generated", "default")
INIT_H = os.path.join(GEN, "dead_or_alive_4_init.h")

MARKER = "doa4_wmap"

INIT_OLD = {
    "#define REX_STORE_U8(x, y)  (*(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = (y))":
    "#define REX_STORE_U8(x, y)  (REX_WMARK1(x), *(volatile u8*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = (y))",
    "#define REX_STORE_U16(x, y) (*(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap16(y))":
    "#define REX_STORE_U16(x, y) (REX_WMARK(x, 2), *(volatile u16*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap16(y))",
    "#define REX_STORE_U32(x, y) (*(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap32(y))":
    "#define REX_STORE_U32(x, y) (REX_WMARK(x, 4), *(volatile u32*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap32(y))",
    "#define REX_STORE_U64(x, y) (*(volatile u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap64(y))":
    "#define REX_STORE_U64(x, y) (REX_WMARK(x, 8), *(volatile u64*)(base + (u32)(x) + REX_PHYS_HOST_OFFSET(x)) = __builtin_bswap64(y))",
}
RAW_ADDR_DEF = "#define REX_RAW_ADDR(x) (base + (u32)(x) + REX_PHYS_HOST_OFFSET(x))"
WMARK_DEFS = """
// DOA4 rollback (corysyx/doa4-online, tools/np_wmap_patch.py): every guest store marks its 4 KB page in doa4_wmap
// (src/net/rb_wmap.h). REX_WMARK marks the pages of a store's first and last byte.
extern "C" unsigned char doa4_wmap[1u << 20];
#define REX_WMARK1(x) (doa4_wmap[(u32)(x) >> 12] = 1)
#define REX_WMARK(x, n) (doa4_wmap[(u32)(x) >> 12] = 1, doa4_wmap[((u32)(x) + ((n) - 1)) >> 12] = 1)
#define REX_RAW_ADDR_W(x) (REX_WMARK1(x), REX_RAW_ADDR(x))"""

# Guest stores written with REX_RAW_ADDR in the generated .cpp files (every other REX_RAW_ADDR is a load).
CPP_PATTERNS = [
    ("simde_mm_store_si128((simde__m128i*)REX_RAW_ADDR(", "simde_mm_store_si128((simde__m128i*)REX_RAW_ADDR_W("),
    ("memset((void*)REX_RAW_ADDR(", "memset((void*)REX_RAW_ADDR_W("),
    ("__sync_bool_compare_and_swap(reinterpret_cast<uint32_t*>(REX_RAW_ADDR(",
     "__sync_bool_compare_and_swap(reinterpret_cast<uint32_t*>(REX_RAW_ADDR_W("),
    ("__sync_bool_compare_and_swap(reinterpret_cast<uint64_t*>(REX_RAW_ADDR(",
     "__sync_bool_compare_and_swap(reinterpret_cast<uint64_t*>(REX_RAW_ADDR_W("),
]


def patch_init(text):
    if MARKER in text:
        return text, 0
    if RAW_ADDR_DEF not in text:
        raise SystemExit("np_wmap_patch: REX_RAW_ADDR is not where it was expected in " + INIT_H)
    text = text.replace(RAW_ADDR_DEF, RAW_ADDR_DEF + WMARK_DEFS, 1)
    n = 1
    for old, new in INIT_OLD.items():
        if text.count(old) != 1:
            raise SystemExit("np_wmap_patch: store macro not found exactly once: " + old)
        text = text.replace(old, new)
        n += 1
    return text, n


def patch_cpp(text):
    n = 0
    for old, new in CPP_PATTERNS:
        k = text.count(old)
        if k:
            text = text.replace(old, new)
            n += k
    return text, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    pending = 0
    changed_files = 0
    total = 0
    targets = [INIT_H] + sorted(glob.glob(os.path.join(GEN, "*.cpp")))
    for path in targets:
        with open(path, "r", encoding="utf-8", newline="") as f:
            text = f.read()
        new, n = patch_init(text) if path == INIT_H else patch_cpp(text)
        if not n:
            continue
        pending += 1
        total += n
        if not args.check:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(new)
            changed_files += 1
    # what is left: a raw-address store form this script does not know (a new codegen pattern) would go unmarked
    unknown = 0
    store_like = re.compile(r"(store|memset|memcpy|compare_and_swap|exchange)[^;]*REX_RAW_ADDR\(")
    for path in targets[1:]:
        with open(path, "r", encoding="utf-8", newline="") as f:
            for line in f:
                if "REX_RAW_ADDR(" in line and store_like.search(line) and "simde_mm_load_si128((simde__m128i*)REX_RAW_ADDR(" not in line:
                    unknown += 1
                    if unknown <= 5:
                        print("np_wmap_patch: unmarked store form: " + line.strip()[:160])
    if args.check:
        print("np_wmap_patch: %d file(s) need the patch, %d unmarked store form(s)" % (pending, unknown))
        return 1 if pending or unknown else 0
    print("np_wmap_patch: %d file(s) changed, %d store site(s) marked, %d unmarked store form(s)" % (changed_files, total, unknown))
    return 1 if unknown else 0


if __name__ == "__main__":
    sys.exit(main())

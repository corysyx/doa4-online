"""Scans suit converter objects for fused multiply-adds and C runtime transcendental calls.

  py -3 tests/host/suitconv_fp_scan.py FOLDER_OR_OBJECT [...]

A folder is searched, with its subfolders, for doa4_suitconv_*.obj and doa4_suitconv_*.o (the converter objects:
the host runners write them to %TEMP%\\doa4_suitconv_test\\obj, a CMake build under
CMakeFiles/dead_or_alive_4.dir/src as doa4_suitconv_*.cpp.obj); a file is scanned as given (the positive control).
For each object it prints
  <path>: fma N, libm M [names]
where N counts the FMA instructions in `llvm-objdump -d` (x86: vfmadd, vfmsub, vfnmadd, vfnmsub, vfmaddsub,
vfmsubadd, FMA4 included; arm64: fmadd, fmsub, fnmadd, fnmsub, fmla, fmls) and M the undefined symbols from
`llvm-nm -u` that name a forbidden math function (pow exp exp2 log log2 log10 sin cos tan asin acos atan atan2 hypot
cbrt fma sinh cosh tanh expm1 log1p and their f forms, each also with one leading underscore: the C runtime's
aliases, e.g. std::hypot(float) calls _hypotf; an __imp_ prefix is stripped first). sqrt floor ceil trunc fmod,
memory and C++ runtime symbols are allowed.
Exit 0 when every object is clean, 1 when any has a finding, 2 when nothing could be scanned (no object, a tool
failed, or a disassembly with no instructions: an empty result from a broken instrument is not a clean result).
Standard library only; the LLVM tools come from DOA4_LLVM_BIN or C:\\Program Files\\LLVM\\bin.
"""
import glob
import os
import re
import subprocess
import sys

LLVM_BIN = os.environ.get('DOA4_LLVM_BIN', r'C:\Program Files\LLVM\bin')
FORBIDDEN = {'pow', 'exp', 'exp2', 'log', 'log2', 'log10', 'sin', 'cos', 'tan', 'asin', 'acos', 'atan', 'atan2', 'hypot',
             'cbrt', 'fma', 'sinh', 'cosh', 'tanh', 'expm1', 'log1p'}
FORBIDDEN |= {name + 'f' for name in FORBIDDEN}
FMA_RE = re.compile(r'\bvfn?m(?:add|sub)\w*\b|\bfn?m(?:add|sub)\b|\bfml[as]\b')
INSN_RE = re.compile(r'^\s*[0-9a-f]+:\s+\S')


def tool(name):
    path = os.path.join(LLVM_BIN, name + '.exe')
    return path if os.path.exists(path) else name


def run(args):
    r = subprocess.run(args, capture_output=True, text=True, errors='replace')
    if r.returncode != 0:
        raise RuntimeError(f'{os.path.basename(args[0])} exit {r.returncode}: {r.stderr.strip()[:300]}')
    return r.stdout


def scan(path):
    """(fma count, sorted forbidden names, instruction count)."""
    dis = run([tool('llvm-objdump'), '-d', '--no-show-raw-insn', path])
    insns = sum(1 for line in dis.splitlines() if INSN_RE.match(line))
    fma = sum(len(FMA_RE.findall(line)) for line in dis.splitlines() if INSN_RE.match(line))
    names = set()
    for line in run([tool('llvm-nm'), '-u', path]).splitlines():
        parts = line.split()
        if not parts:
            continue
        sym = parts[-1]
        if sym.startswith('__imp_'):
            sym = sym[len('__imp_'):]
        if sym in FORBIDDEN or (sym.startswith('_') and sym[1:] in FORBIDDEN):
            names.add(sym)
    return fma, sorted(names), insns


def main(argv):
    if not argv:
        print(__doc__.strip())
        return 2
    objects = []
    for arg in argv:
        if os.path.isdir(arg):
            found = sorted(f for pattern in ('doa4_suitconv_*.obj', 'doa4_suitconv_*.o')
                           for f in glob.glob(os.path.join(arg, '**', pattern), recursive=True))
            if not found:
                print(f'{arg}: no doa4_suitconv_*.obj or doa4_suitconv_*.o in it or its subfolders')
                return 2
            objects += found
        elif os.path.isfile(arg):
            objects.append(arg)
        else:
            print(f'{arg}: not found')
            return 2
    dirty = 0
    for path in objects:
        try:
            fma, names, insns = scan(path)
        except (OSError, RuntimeError) as e:
            print(f'{os.path.basename(path)}: scan failed: {e}')
            return 2
        if insns == 0:
            print(f'{os.path.basename(path)}: no instructions disassembled')
            return 2
        libm = f'libm {len(names)}' + (' ' + ' '.join(names) if names else '')
        print(f'{path}: fma {fma}, {libm} ({insns} instructions)')
        dirty += fma > 0 or bool(names)
    print(f'{len(objects)} object(s) scanned, {dirty} with findings')
    return 1 if dirty else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

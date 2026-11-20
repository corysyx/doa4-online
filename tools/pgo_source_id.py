"""The source id of a PGO profile (docs/PGO.md): which game tree, SDK commit and LLVM version pgo/doa4.profdata was made from.

  py -3 -I tools/pgo_source_id.py compute [--sdk COMMIT] [--repo DIR]
      prints the four lines of pgo/source.txt: game=<sha256>, sdk=<commit>, llvm=23.1.4, names=portable
      (--sdk defaults to the commit named in netplay_sdk.txt)
  py -3 -I tools/pgo_source_id.py check --sdk-from {pin|worktree:PATH} [--repo DIR]
      exit 0, 'pgo: use <profile>'  both pgo/ files exist and name this tree, this SDK, this LLVM and NAMES_MODE
      exit 0, 'pgo: none'           neither file exists (a plain build)
      exit 1, 'pgo: refused: <why> (to build without the profile: ...)'
                                    anything else: half a profile, a mismatch, a working tree that differs from HEAD

game= is the SHA-256 of the sorted 'git ls-tree -r HEAD' lines of ID_PATHS (one LF after each line): blob ids, not
the commit id, because committing pgo/ itself makes a new commit, and not the working tree's bytes, because a CRLF and
an LF checkout must agree. So the working tree under ID_PATHS must equal HEAD (git status is empty there), or the id
would name code that is not the code being built. --sdk-from pin reads commit= from netplay_sdk.txt (the release
workflow); worktree:PATH reads 'git -C PATH rev-parse HEAD' (tools/np_build.bat, with the local SDK checkout).
"""
import argparse
import hashlib
import os
import re
import subprocess
import sys

# The release workflow's paths: list minus 'pgo/**' and 'netplay_sdk.txt' (the SDK is the sdk= line). The test
# paths_list_agrees reads the workflow file and compares, so the two cannot drift.
ID_PATHS = ("src", "generated", "third_party", "CMakeLists.txt", "CMakePresets.json", "hints.toml",
            "dead_or_alive_4_manifest.toml")
LLVM = "23.1.4"  # the release workflow's pinned LLVM (its 'LLVM 23.1.4' step); the local install is the same version
# How file-local functions are named in the profile. tools/np_build.bat (both profile modes) and the workflow pass
# NAMES_FLAG, so a name is <file name>;<function>, the same at any build path; a profile made in the other mode gives a
# build nothing for those functions, with no warning, so source.txt records the mode and check compares it.
NAMES_FLAG = "-mllvm -static-func-full-module-prefix=false"
# Both modes also pass -fmacro-prefix-map=<game root>=/doa4 and <SDK root>=/rexglue-sdk: an anonymous namespace's
# ?A0x hash is taken from the source file's name, so without the maps those names move with the build path too.
MACRO_ROOTS = ("/doa4", "/rexglue-sdk")
# The mode is looked up from the flags, never set beside them: a new flag set without an entry here fails at import.
# (basename was the mode of commit 4601af3: the names flag without the maps.)
NAMES_MODES = {("-mllvm -static-func-full-module-prefix=false", ("/doa4", "/rexglue-sdk")): "portable"}  # literals
NAMES_MODE = NAMES_MODES[(NAMES_FLAG, MACRO_ROOTS)]
KEYS = ("game", "sdk", "llvm", "names")
PROFILE = os.path.join("pgo", "doa4.profdata")
SOURCE = os.path.join("pgo", "source.txt")
SDK_PIN = "netplay_sdk.txt"
TOOL_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMMIT_RE = re.compile(r"[0-9a-f]{40}")
# The end of every refusal check prints: remaking the profile takes hours, building without it takes nothing.
WAY_OUT = "(to build without the profile: DOA4_PGO=off locally; in CI delete pgo/doa4.profdata and pgo/source.txt)"


class Refused(Exception):
    pass


def git(repo, *args):
    try:
        p = subprocess.run(["git", "-C", repo, "-c", "core.quotepath=false", *args], capture_output=True)
    except OSError as e:
        raise Refused(f"git could not be started: {e}")
    if p.returncode != 0:
        msg = p.stderr.decode("utf-8", "replace").strip().splitlines()
        raise Refused(f"git {args[0]} in {repo} failed (exit {p.returncode}): {msg[0] if msg else ''}")
    return p.stdout.decode("utf-8")


def dirty_path(repo):
    """The first path under ID_PATHS whose working tree differs from HEAD (edited, staged, deleted or untracked), or None."""
    out = git(repo, "status", "--porcelain", "--untracked-files=all", "--", *ID_PATHS)
    for line in out.splitlines():
        if line.strip():
            path = line[3:]
            return path.split(" -> ")[-1].strip('"')
    return None


def game_id(repo):
    out = git(repo, "ls-tree", "-r", "--full-tree", "HEAD", "--", *ID_PATHS)
    lines = sorted(l for l in out.split("\n") if l)
    if not lines:
        raise Refused(f"no tracked file under {', '.join(ID_PATHS)} in {repo}")
    return hashlib.sha256("".join(l + "\n" for l in lines).encode("utf-8")).hexdigest()


def pin_commit(repo):
    path = os.path.join(repo, SDK_PIN)
    try:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        raise Refused(f"{SDK_PIN} could not be read: {e}")
    for line in text.splitlines():
        key, _, value = line.partition("=")
        if key.strip() == "commit":
            return value.strip()
    raise Refused(f"{SDK_PIN} has no commit= line")


def sdk_commit(repo, sdk_from):
    if sdk_from == "pin":
        return pin_commit(repo)
    if sdk_from.startswith("worktree:") and len(sdk_from) > len("worktree:"):
        return git(sdk_from[len("worktree:"):], "rev-parse", "HEAD").strip()
    raise Refused(f"--sdk-from must be pin or worktree:PATH, not {sdk_from!r}")


def compute(repo, sdk):
    if not COMMIT_RE.fullmatch(sdk or ""):
        raise Refused(f"the SDK commit must be a full 40-digit lower-case commit id, not {sdk!r}")
    dirty = dirty_path(repo)
    if dirty:
        raise Refused(f"working tree differs in {dirty}")
    return f"game={game_id(repo)}\nsdk={sdk}\nllvm={LLVM}\nnames={NAMES_MODE}\n"


def parse_source(text):
    values = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        key, sep, value = line.partition("=")
        if not sep or key not in KEYS or key in values:
            raise Refused(f"source.txt malformed at {line!r}")
        values[key] = value.strip()
    missing = [k for k in KEYS if k not in values]
    if missing:
        raise Refused(f"source.txt malformed: no {', '.join(missing)} line")
    return values


def check(repo, sdk_from):
    profile = os.path.join(repo, PROFILE)
    source = os.path.join(repo, SOURCE)
    has_profile, has_source = os.path.isfile(profile), os.path.isfile(source)
    if not has_profile and not has_source:
        return "pgo: none"
    if not has_source:
        raise Refused("profile without source.txt")
    if not has_profile:
        raise Refused("source.txt without profile")
    dirty = dirty_path(repo)
    if dirty:
        raise Refused(f"working tree differs in {dirty}")
    try:
        with open(source, "r", encoding="ascii", errors="replace") as f:
            text = f.read()
    except OSError as e:
        raise Refused(f"source.txt could not be read: {e}")
    want = parse_source(text)
    game = game_id(repo)
    # corysyx/doa4-online: this fork's game tree is dwreckgg's plus its rollback work, so it never matches the profile
    # it takes from upstream. With DOA4_PGO_STALE_OK=1 (the fork's release workflow) a tree mismatch alone is allowed:
    # clang applies the profile to every function whose hash still matches (almost all of generated/) and builds the
    # rest without it. The SDK, LLVM and naming checks still refuse.
    if want["game"] != game and os.environ.get("DOA4_PGO_STALE_OK") == "1":
        print(f"pgo: the game tree differs from the profile's (source.txt game={want['game'][:12]}, this tree "
              f"{game[:12]}): used anyway (DOA4_PGO_STALE_OK)", file=sys.stderr)
    elif want["game"] != game:
        raise Refused(f"game tree differs (source.txt game={want['game'][:12]}, this tree {game[:12]}): remake the profile")
    sdk = sdk_commit(repo, sdk_from)
    if want["sdk"] != sdk:
        raise Refused(f"sdk differs (source.txt sdk={want['sdk']}, {sdk_from.split(':')[0]} {sdk}): remake the profile")
    if want["llvm"] != LLVM:
        raise Refused(f"llvm differs (source.txt llvm={want['llvm']}, the build uses {LLVM})")
    if want["names"] != NAMES_MODE:
        raise Refused(f"names differ (source.txt names={want['names']}, the build uses {NAMES_MODE}): remake the profile")
    return "pgo: use " + os.path.abspath(profile).replace("\\", "/")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("compute")
    c.add_argument("--sdk", help="the SDK commit the profile is built against (default: netplay_sdk.txt's commit)")
    c.add_argument("--repo", default=TOOL_REPO)
    k = sub.add_parser("check")
    k.add_argument("--sdk-from", required=True, help="pin (netplay_sdk.txt) or worktree:PATH (that checkout's HEAD)")
    k.add_argument("--repo", default=TOOL_REPO)
    args = ap.parse_args(argv)
    repo = os.path.abspath(args.repo)
    try:
        if args.cmd == "compute":
            sys.stdout.write(compute(repo, args.sdk if args.sdk else pin_commit(repo)))
        else:
            print(check(repo, args.sdk_from))
    except Refused as e:
        print(f"pgo: refused: {e}" + (" " + WAY_OUT if args.cmd == "check" else ""))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

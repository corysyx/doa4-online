"""Tests for the netplay release tools: tools/release_stage.py (the release's assets) and tools/run_host_tests.py (the host
tests as CI runs them). Synthetic folders only (no build, no game).

  py -3 -I tests/tools/test_release_tools.py
"""
import hashlib
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import traceback
import zipfile

sys.dont_write_bytecode = True  # load() must not leave tools/__pycache__ in the tree

if os.name == "nt":
    # The fake runners are batch files: give every process started here a console without a window.
    _Popen = subprocess.Popen

    class _QuietPopen(_Popen):
        def __init__(self, *args, **kwargs):
            kwargs["creationflags"] = kwargs.get("creationflags", 0) | subprocess.CREATE_NO_WINDOW
            super().__init__(*args, **kwargs)

    subprocess.Popen = _QuietPopen

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
TOOLS = os.path.join(REPO, "tools")
HOST = os.path.join(REPO, "tests", "host")
STAGE = os.path.join(TOOLS, "release_stage.py")
SECTION = os.path.join(TOOLS, "changelog_section.py")
RUN_HOST = os.path.join(TOOLS, "run_host_tests.py")
GAME_FILES = ["dead_or_alive_4.exe", "rexruntime.dll", "TracyClient.dll"]
PORT = "0123456789abcdef0123456789abcdef01234567"
SDK = "b643ea14b38f8475382840a5eb360b0d4090ae5e"
TAG = "alpha-v0.3.0"
ZIP = "DOA4-alpha-v0.3.0-win64.zip"
NOTICES = "THIRD_PARTY_NOTICES.md"
checks = 0
failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print("FAIL", what)


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(TOOLS, name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run(script, *args, env=None):
    p = subprocess.run([sys.executable, "-I", script, *args], capture_output=True, text=True, encoding="utf-8",
                       errors="replace", env=env)
    return p.returncode, p.stdout + p.stderr


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def read(path):
    with open(path, "rb") as f:
        return f.read()


# Three different contents and sizes (one past 64 KiB, one empty), so a SHA256SUMS line that hashes the wrong file, or
# hashes one file for every name, gives a different line.
def contents():
    return {
        "dead_or_alive_4.exe": bytes((i * 7 + (i >> 8) * 13) % 256 for i in range(300_001)),
        "rexruntime.dll": b"rexruntime stand-in\n" * 50,
        "TracyClient.dll": b"",
    }


def make_build(root, skip=None, decoys=True):
    build = os.path.join(root, "build")
    for name, data in contents().items():
        if name != skip:
            write(os.path.join(build, name), data)
    if decoys:  # game data and other build outputs beside the three: the selection is by name, so none is staged
        for name in ("default.xex", "stg.afs", "intro.sfd", "x.dat", "dead_or_alive_4.pdb", "dead_or_alive_4.lib",
                     os.path.join("movie", "intro.sfd")):
            write(os.path.join(build, name), b"decoy " + name.encode())
    return build


# A synthetic CHANGELOG.md, newest first: the staged tag's version (0.3.0) sits between a newer and an older section,
# and each body has its own '###' headings, so a cut at the wrong line, or the wrong section, gives a different text.
BODY_030 = ("### New\n- Thirty feature one.\n- Thirty feature two.\n\n### Fixed\n- Thirty fix.\n\n"
            "### Notes\n- Thirty note.\n")
CHANGELOG = ("# Changelog\n\n"
             "## 0.3.1 (2026-11-11)\n\n### New\n- Thirty-one feature.\n\n"
             "## 0.3.0 (2026-11-08)\n\n" + BODY_030 + "\n"
             "## 0.2.9 (2026-11-01)\n\n### Fixed\n- Twenty-nine fix.\n")


def stage(root, build, *extra, launcher=True, out_name="staging", tag=TAG, changelog=CHANGELOG):
    """changelog: the text of a CHANGELOG.md passed with --changelog, or None to leave the option out (the repo's file)."""
    out = os.path.join(root, out_name)
    args = ["--build", build, "--out", out, "--tag", tag, "--port-commit", PORT, "--sdk-commit", SDK]
    if changelog is not None:
        cp = os.path.join(root, "CHANGELOG_" + out_name + ".md")
        write(cp, changelog.encode("utf-8"))
        args += ["--changelog", cp]
    if launcher:
        lz = os.path.join(root, "launcher_pkg.zip")
        write(lz, b"PK launcher stand-in")
        args += ["--launcher-zip", lz]
    code, text = run(STAGE, *args, *extra)
    return code, text, out


def staged_names(out):
    return sorted(os.listdir(out)) if os.path.isdir(out) else []


# ---- release_stage.py ---------------------------------------------------------------------------------------------

def test_stage_exact_set(root):
    build = make_build(root)
    code, text, out = stage(root, build)
    check(code == 0, f"stage: exit {code}\n{text}")
    want = sorted(GAME_FILES + ["SHA256SUMS.txt", NOTICES, ZIP, "DOA4-launcher.zip"])
    check(staged_names(out) == want, f"stage: the staging set is exactly {want}: {staged_names(out)}")
    for name, data in contents().items():
        p = os.path.join(out, name)
        check(os.path.isfile(p) and read(p) == data, f"stage: {name} is a byte copy of the build's")
    # SHA256SUMS: one line per game file, '<lower hex>  <name>', in the order exe, rexruntime, Tracy; the oracle is hashlib
    want_sums = "".join(f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in contents().items())
    sums = read(os.path.join(out, "SHA256SUMS.txt")) if os.path.isfile(os.path.join(out, "SHA256SUMS.txt")) else b""
    check(sums == want_sums.encode("ascii"), f"stage: SHA256SUMS.txt is hashlib's three lines\n{sums!r}\nwant\n{want_sums!r}")
    check(sums.count(b"\n") == 3 and b"\r" not in sums, f"stage: SHA256SUMS.txt has exactly three LF lines (no line for the zips): {sums!r}")
    # the zip: the three, SHA256SUMS.txt and README-netplay.txt, nothing else; each member equal to the staged file
    zp = os.path.join(out, ZIP)
    members = {}
    if os.path.isfile(zp):
        with zipfile.ZipFile(zp) as z:
            members = {i.filename: z.read(i) for i in z.infolist()}
    want_members = sorted(GAME_FILES + ["SHA256SUMS.txt", NOTICES, "README-netplay.txt"])
    check(sorted(members) == want_members, f"stage: zip members exactly {want_members}: {sorted(members)}")
    for name in GAME_FILES + ["SHA256SUMS.txt"]:
        p = os.path.join(out, name)
        check(name in members and os.path.isfile(p) and members[name] == read(p), f"stage: zip member {name} equals the staged file")
    # R4-2: the BSD notice duty: THIRD_PARTY_NOTICES.md (the repository's own file) is staged and is in the zip, byte for byte
    notices = read(os.path.join(REPO, NOTICES))
    check(len(notices) > 100, f"stage: the repository's {NOTICES} is a real file ({len(notices)} bytes)")
    check(os.path.isfile(os.path.join(out, NOTICES)) and read(os.path.join(out, NOTICES)) == notices, f"stage: staged {NOTICES} is the repository's file")
    check(members.get(NOTICES) == notices, f"stage: zip member {NOTICES} is the repository's file")
    # the notices of the ported code (P3D-12): numpy (BSD-3), CPython (PSF-2.0) and the libm of Plan 2 (musl), each by name
    for needle in ("numpy", "Python Software Foundation", "musl"):
        check(needle in notices.decode("utf-8", "replace"), f"stage: {NOTICES} names {needle!r}")
    readme = members.get("README-netplay.txt", b"").decode("utf-8", "replace")
    # R4-1: the firewall advice (both players, Private AND Public, Alt+Tab, UPnP or forward UDP 7200) and the R1-2 line
    for needle in ("first time you host or join", "both players", "Private", "Public", "Alt+Tab", "UPnP", "UDP 7200"):
        check(needle in readme, f"stage: README-netplay.txt says {needle!r}\n{readme}")
    for needle in (TAG, PORT, SDK, "SHA256SUMS.txt", "no game data"):
        check(needle.lower() in readme.lower(), f"stage: README-netplay.txt names {needle!r}\n{readme}")
    # P3D-6 / Task 7 step 5: the build's public name first, and the optional swimsuit line (no DOAX2 data in the release)
    check(readme.startswith(f"Dead or Alive 4 Online {TAG}"), f"stage: README-netplay.txt starts with the build's name and tag: {readme[:60]!r}")
    check("USA copy of Dead or Alive Xtreme 2" in readme and "No DOAX2 data is in this release." in readme,
          f"stage: README-netplay.txt has the optional swimsuit line: {readme}")
    # Task 13: the tag's CHANGELOG section (0.3.0's whole body, CRLF like the rest of the file), under its own line;
    # NOT the newer (0.3.1) or the older (0.2.9) section, and not a '## ' heading line
    want_changes = ("What's new in 0.3.0:\r\n\r\n" + BODY_030.replace("\n", "\r\n")).encode("ascii")
    raw = members.get("README-netplay.txt", b"")
    check(want_changes in raw, f"stage: README-netplay.txt carries 0.3.0's CHANGELOG section\n{raw!r}")
    for needle in ("Thirty-one", "Twenty-nine", "## 0."):
        check(needle not in readme, f"stage: README-netplay.txt has no {needle!r} (another version's section)\n{readme}")
    check(b"\r\n" in raw and b"\n" not in raw.replace(b"\r\n", b""), f"stage: README-netplay.txt has CRLF line ends only\n{raw!r}")
    lp =os.path.join(out, "DOA4-launcher.zip")
    check(os.path.isfile(lp) and read(lp) == b"PK launcher stand-in", "stage: DOA4-launcher.zip is a copy of --launcher-zip")


def test_stage_without_launcher(root):
    build = make_build(root, decoys=False)
    code, text, out = stage(root, build, launcher=False)
    want = sorted(GAME_FILES + ["SHA256SUMS.txt", NOTICES, ZIP])
    check(code == 0 and staged_names(out) == want, f"stage without --launcher-zip: exit {code}, {staged_names(out)}\n{text}")


def test_stage_missing_file(root):
    for name in GAME_FILES:
        sub = os.path.join(root, "m_" + name)
        build = make_build(sub, skip=name)
        code, text, out = stage(sub, build)
        check(code == 1, f"stage, {name} missing: exit {code}\n{text}")
        check(name in text and "missing" in text, f"stage, {name} missing: the message names it\n{text}")
        check(staged_names(out) == [], f"stage, {name} missing: nothing staged: {staged_names(out)}")


def test_stage_game_data_refused_by_pattern(root):
    # the pattern rule is checked before the list rule, so its own message shows (a mutant without it still exits 1,
    # through the list rule, and is caught by the message check)
    build = make_build(root)
    for i, extra in enumerate(("default.xex", "stg.afs", "STG.AFS", "intro.sfd", "x.dat")):
        if not os.path.exists(os.path.join(build, extra)):
            write(os.path.join(build, extra), b"decoy")
        code, text, out = stage(root, build, "--extra", extra, out_name=f"out_p{i}")
        check(code == 1 and extra in text and "game data" in text, f"stage --extra {extra}: refused as game data: exit {code}\n{text}")
        check(staged_names(out) == [], f"stage --extra {extra}: nothing staged: {staged_names(out)}")
    for i, extra in enumerate(("movie/intro.sfd", "movie\\notes.txt")):
        write(os.path.join(build, *re.split(r"[\\/]", extra)), b"decoy")
        code, text, out = stage(root, build, "--extra", extra, out_name=f"out_s{i}")
        check(code == 1 and extra in text and "under a folder" in text, f"stage --extra {extra}: refused (under a folder): exit {code}\n{text}")
        check(staged_names(out) == [], f"stage --extra {extra}: nothing staged: {staged_names(out)}")


def test_stage_extra_file_refused(root):
    build = make_build(root)
    write(os.path.join(build, "notes.txt"), b"not a release file")
    code, text, out = stage(root, build, "--extra", "notes.txt")
    check(code == 1 and "notes.txt" in text and "not one of the release files" in text,
          f"stage --extra notes.txt: refused by the list rule: exit {code}\n{text}")
    check(staged_names(out) == [], f"stage --extra notes.txt: nothing staged: {staged_names(out)}")
    code, text, out = stage(root, build, "--extra", "rexruntime.dll", out_name="out_dup")
    check(code == 1 and "rexruntime.dll" in text and "twice" in text, f"stage --extra rexruntime.dll: refused (named twice): exit {code}\n{text}")
    check(staged_names(out) == [], f"stage --extra rexruntime.dll: nothing staged: {staged_names(out)}")


def test_stage_refuses_a_used_folder_and_a_bad_tag(root):
    build = make_build(root, decoys=False)
    out = os.path.join(root, "staging")
    write(os.path.join(out, "old.txt"), b"left from an earlier run")
    code, text, out = stage(root, build)
    check(code == 1 and "not empty" in text, f"stage into a non-empty folder: refused: exit {code}\n{text}")
    check(staged_names(out) == ["old.txt"], f"stage into a non-empty folder: nothing added: {staged_names(out)}")
    for i, tag in enumerate(("netplay-v1.0", "netplay-v1.0.7/../x", "")):
        o = os.path.join(root, f"out_t{i}")
        code, text = run(STAGE, "--build", build, "--out", o, "--tag", tag, "--port-commit", PORT, "--sdk-commit", SDK)
        check(code == 1 and "--tag" in text, f"stage --tag {tag!r}: refused by the tag rule: exit {code}\n{text}")
        check(staged_names(o) == [], f"stage --tag {tag!r}: nothing staged: {staged_names(o)}")


def test_stage_changelog_refusals(root):
    # Task 13: no section for the tag's version, or no CHANGELOG file: refused with the clean message, nothing staged
    build = make_build(root, decoys=False)
    no_030 = CHANGELOG.replace("## 0.3.0 (2026-11-08)", "## 0.3.00 (2026-11-08)")  # a near miss, not 0.3.0's heading
    code, text, out = stage(root, build, changelog=no_030, out_name="out_nosec")
    check(code == 1 and "0.3.0" in text and "no section" in text and "Traceback" not in text,
          f"stage, no 0.3.0 section: refused with the message: exit {code}\n{text}")
    check(staged_names(out) == [], f"stage, no 0.3.0 section: nothing staged: {staged_names(out)}")
    missing = os.path.join(root, "no such CHANGELOG.md")
    code, text, out = stage(root, build, "--changelog", missing, changelog=None, out_name="out_nofile")
    check(code == 1 and "not found" in text and "Traceback" not in text,
          f"stage, no CHANGELOG file: refused with the message: exit {code}\n{text}")
    check(staged_names(out) == [], f"stage, no CHANGELOG file: nothing staged: {staged_names(out)}")


def test_stage_default_changelog(root):
    # called as the workflow calls it (no --changelog): the repository's CHANGELOG.md, here its 0.3.1 section
    build = make_build(root, decoys=False)
    code, text, out = stage(root, build, launcher=False, tag="alpha-v0.3.1", changelog=None)
    check(code == 0, f"stage alpha-v0.3.1 with the repository's CHANGELOG.md: exit {code}\n{text}")
    zp = os.path.join(out, "DOA4-alpha-v0.3.1-win64.zip")
    raw = b""
    if os.path.isfile(zp):
        with zipfile.ZipFile(zp) as z:
            raw = z.read("README-netplay.txt") if "README-netplay.txt" in z.namelist() else b""
    for needle in (b"What's new in 0.3.1:\r\n\r\n### New\r\n- Language and subtitles are saved",
                   b"### Notes\r\n- The first start after updating rebuilds the shader cache, so it is slower once.\r\n"):
        check(needle in raw, f"stage alpha-v0.3.1: README-netplay.txt carries {needle!r}\n{raw!r}")


def refusal(fn, *args):
    """The message of the NoSection that fn(*args) raises, or None when it returns."""
    try:
        fn(*args)
    except Exception as e:  # any other class is reported by the caller's check on the class name
        return f"{type(e).__name__}: {e}"
    return None


def section_or_refusal(cs, text, version):
    """cs.section(text, version), or the refusal's text: a wrong refusal fails its own check, not the whole test."""
    try:
        return cs.section(text, version)
    except Exception as e:
        return f"{type(e).__name__}: {e}"


def test_changelog_section(root):
    cs = load("changelog_section")
    sec = lambda text, version: section_or_refusal(cs, text, version)  # noqa: E731
    # the body under '## <version> (...)' up to the next '## ' heading: the middle, the first (newest) and the last
    check(sec(CHANGELOG, "0.3.0") == BODY_030, f"section 0.3.0: exactly its body: {sec(CHANGELOG, '0.3.0')!r}")
    check(sec(CHANGELOG, "0.3.1") == "### New\n- Thirty-one feature.\n", f"section 0.3.1 (the first): its body, not 0.3.0's: {sec(CHANGELOG, '0.3.1')!r}")
    check(sec(CHANGELOG, "0.2.9") == "### Fixed\n- Twenty-nine fix.\n", f"section 0.2.9 (the last): its body to the end: {sec(CHANGELOG, '0.2.9')!r}")
    check(sec(CHANGELOG.rstrip("\n"), "0.2.9") == "### Fixed\n- Twenty-nine fix.\n",
          "section 0.2.9, no newline at the end of the file: the same body")
    # a CRLF checkout (git's autocrlf on a Windows runner) gives the same LF body
    check(sec(CHANGELOG.replace("\n", "\r\n"), "0.3.0") == BODY_030, "section 0.3.0 of a CRLF file: the same LF body")
    # refused: no section, a heading that only starts with the version, an empty section, the version twice
    for text, version, token in ((CHANGELOG, "0.4.0", "no section"),
                                 ("## 0.3.10 (2026-12-01)\n\n- Ten.\n", "0.3.1", "no section"),
                                 ("## 0.3.0 (2026-11-08)\n\n\n## 0.2.9 (2026-11-01)\n- Fix.\n", "0.3.0", "empty"),
                                 (CHANGELOG + "\n## 0.3.0 (2026-10-01)\n- Again.\n", "0.3.0", "twice")):
        msg = refusal(cs.section, text, version)
        check(msg is not None and msg.startswith("NoSection") and token in msg and version in msg,
              f"section {version} of {text[:40]!r}...: NoSection naming it ({token!r}): {msg!r}")
    # the command line, as the workflow calls it: --out gets the body as LF bytes, no BOM; without --out, stdout
    cp = os.path.join(root, "CHANGELOG.md")
    write(cp, CHANGELOG.replace("\n", "\r\n").encode("ascii"))
    o = os.path.join(root, "changes.md")
    code, text = run(SECTION, "0.3.0", "--changelog", cp, "--out", o)
    check(code == 0 and os.path.isfile(o) and read(o) == BODY_030.encode("ascii"),
          f"changelog_section 0.3.0 --out: exit {code}, the body's bytes: {read(o) if os.path.isfile(o) else None!r}\n{text}")
    code, text = run(SECTION, "0.3.0", "--changelog", cp)
    check(code == 0 and text == BODY_030, f"changelog_section 0.3.0 to stdout: exit {code}: {text!r}")
    for i, (args, token) in enumerate(((["0.4.0", "--changelog", cp], "no section"),
                                       (["0.4", "--changelog", cp], "x.y.z"),
                                       (["0.4.0", "--changelog", os.path.join(root, "absent.md")], "not found"))):
        o = os.path.join(root, f"refused_{i}.md")
        code, text = run(SECTION, *args, "--out", o)
        check(code == 1 and token in text and args[0] in text and "Traceback" not in text,
              f"changelog_section {args[0]} ({token}): exit 1 with the message: exit {code}\n{text}")
        check(not os.path.exists(o), f"changelog_section {args[0]} ({token}): no --out file written")


def test_repo_changelog(root):
    # the repository's CHANGELOG.md: ASCII, newest first, and the 0.3.1 section has the three headings. A checkout with
    # git's autocrlf (a Windows runner) has CRLF, so the line ends are read as the checkout has them: all CRLF or all LF.
    cs = load("changelog_section")
    raw = read(os.path.join(REPO, "CHANGELOG.md"))
    if raw.count(b"\r\n") == raw.count(b"\n"):
        raw = raw.replace(b"\r\n", b"\n")
    check(raw.isascii() and b"\r" not in raw and raw.startswith(b"# Changelog\n\n## "),
          f"CHANGELOG.md: ASCII, one kind of line end, '# Changelog' then a '## ' section: {raw[:40]!r}")
    versions = [tuple(int(n) for n in m) for m in re.findall(rb"^## (\d+)\.(\d+)\.(\d+) \(", raw, re.M)]
    check(len(versions) >= 1 and versions == sorted(versions, reverse=True), f"CHANGELOG.md: sections newest first: {versions}")
    body = section_or_refusal(cs, raw.decode("ascii", "replace"), "0.3.1")
    check(body.startswith("### New\n- ") and "\n### Fixed\n- " in body and "\n### Notes\n- " in body,
          f"CHANGELOG.md 0.3.1: New, Fixed, Notes: {body!r}")
    # the version the release workflow will publish (DOA4_NETPLAY_VERSION, read as its Version step reads it) has a
    # section, so a raised version without notes fails here, not at the end of the release job
    cmake = read(os.path.join(REPO, "CMakeLists.txt")).decode("utf-8", "replace")
    m = re.search(r'set\(DOA4_NETPLAY_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"', cmake)
    version = m.group(1) if m else "?"
    got = section_or_refusal(cs, raw.decode("ascii", "replace"), version)
    check(m is not None and not got.startswith("NoSection"), f"CHANGELOG.md has a section for DOA4_NETPLAY_VERSION {version}: {got!r}")


# ---- run_host_tests.py --------------------------------------------------------------------------------------------

def write_bat(folder, name, lines):
    write(os.path.join(folder, f"run_{name}_test.bat"), ("\r\n".join(["@echo off"] + lines) + "\r\n").encode("ascii"))


def host_run(*args):
    return run(RUN_HOST, *args)


def line_for(text, name):
    for line in text.splitlines():
        if line.startswith(name + ": "):
            return line
    return None


def test_runner_verdicts(root):
    d = os.path.join(root, "host tests")  # a space in the path, as a user's checkout may have
    write_bat(d, "a", ["echo PASSED: 3 checks, 0 failures", "exit /b 0"])
    write_bat(d, "b", ["echo FAILED: 3 checks, 1 failures", "exit /b 1"])
    write_bat(d, "c", ["exit /b 0"])
    code, text = host_run("--dir", d)
    check(code == 1, f"run_host_tests a/b/c: exit {code}\n{text}")
    check(line_for(text, "a") == "a: passed 3", f"run_host_tests: 'a: passed 3': {line_for(text, 'a')!r}\n{text}")
    check((line_for(text, "b") or "").startswith("b: FAILED"), f"run_host_tests: 'b: FAILED': {line_for(text, 'b')!r}\n{text}")
    check((line_for(text, "c") or "").startswith("c: did not run (no summary line"), f"run_host_tests: c did not run: {line_for(text, 'c')!r}\n{text}")
    check("FAILED: 3 checks, 1 failures" in text.split(line_for(text, "b") or "\0", 1)[-1],
          f"run_host_tests: the failed runner's output follows its line\n{text}")
    # --only: just the named runner
    code, text = host_run("--dir", d, "--only", "a")
    check(code == 0 and line_for(text, "a") == "a: passed 3" and line_for(text, "b") is None and line_for(text, "c") is None,
          f"run_host_tests --only a: exit {code}\n{text}")
    # --only with a name no runner has: refused, never a vacuous pass
    code, text = host_run("--dir", d, "--only", "zz")
    check(code != 0 and "zz" in text, f"run_host_tests --only zz: refused: exit {code}\n{text}")
    # an empty folder: no runner found is not a pass
    e = os.path.join(root, "empty")
    os.makedirs(e)
    code, text = host_run("--dir", e)
    check(code != 0 and "no runner" in text, f"run_host_tests on an empty folder: refused: exit {code}\n{text}")


def test_runner_exit_code_and_vacuous_pass(root):
    d = os.path.join(root, "host")
    write_bat(d, "x7", ["echo PASSED: 3 checks, 0 failures", "exit /b 7"])  # the exit code reaches the judge
    write_bat(d, "f0", ["echo FAILED: 3 checks, 1 failures", "exit /b 0"])  # a FAILED summary is never a pass
    write_bat(d, "z", ["echo PASSED: 0 checks, 0 failures", "exit /b 0"])   # no check ran: not a pass
    write_bat(d, "ok", ["echo FAILED: 1 checks, 1 failures", "echo PASSED: 12 checks, 0 failures", "exit /b 0"])  # the last summary counts, not the first
    code, text = host_run("--dir", d)
    check(code == 1, f"run_host_tests exit codes: exit {code}\n{text}")
    lx = line_for(text, "x7") or ""
    check(lx.startswith("x7: FAILED") and "exit 7" in lx, f"run_host_tests: PASSED line but exit 7 -> FAILED with the code: {lx!r}\n{text}")
    check((line_for(text, "f0") or "").startswith("f0: FAILED"), f"run_host_tests: FAILED line with exit 0 -> FAILED\n{text}")
    check((line_for(text, "z") or "").startswith("z: FAILED"), f"run_host_tests: 0 checks -> FAILED\n{text}")
    check(line_for(text, "ok") == "ok: passed 12", f"run_host_tests: the last summary line counts: {line_for(text, 'ok')!r}\n{text}")


def test_runner_older_shapes(root):
    # runners whose tests print an older summary line (the table in run_host_tests.py), under their real names
    d = os.path.join(root, "host")
    write_bat(d, "toml_upsert", ["echo toml_upsert: all checks passed", "exit /b 0"])
    write_bat(d, "backup", ["echo 2 check(s) failed", "exit /b 1"])
    write_bat(d, "speakers", ["echo all speakers checks passed", "exit /b 0"])
    write_bat(d, "dpps", ["echo canonical NaN results all 0xFFC00000 and the redirect agrees with the SDK function: yes", "exit /b 0"])
    code, text = host_run("--dir", d)
    check(code == 1, f"run_host_tests older shapes: exit {code} (backup failed)\n{text}")
    check(line_for(text, "toml_upsert") == "toml_upsert: passed", f"older shape: toml_upsert passed\n{text}")
    check((line_for(text, "backup") or "").startswith("backup: FAILED"), f"older shape: backup FAILED\n{text}")
    check(line_for(text, "speakers") == "speakers: passed", f"older shape: speakers passed\n{text}")
    check(line_for(text, "dpps") == "dpps: passed", f"older shape: dpps passed\n{text}")
    write_bat(d, "dpps", ["echo canonical NaN results all 0xFFC00000 and the redirect agrees with the SDK function: NO", "exit /b 1"])
    write_bat(d, "speakers", ["echo 1 FAILED", "exit /b 1"])
    write_bat(d, "toml_upsert", ["echo PASSED: 4 checks, 0 failures", "exit /b 0"])  # not toml_upsert's own line
    code, text = host_run("--dir", d, "--only", "dpps", "speakers", "toml_upsert")
    check(code == 1, f"run_host_tests older shapes, failing: exit {code}\n{text}")
    check((line_for(text, "dpps") or "").startswith("dpps: FAILED"), f"older shape: dpps NO -> FAILED\n{text}")
    check((line_for(text, "speakers") or "").startswith("speakers: FAILED"), f"older shape: speakers FAILED\n{text}")
    check((line_for(text, "toml_upsert") or "").startswith("toml_upsert: did not run"),
          f"older shape: a line that is not the runner's own is no summary\n{text}")


def test_runner_timeout(root):
    # R4-5: a runner that hangs is "did not run (timed out ...)", fails the run, and does not stop the others
    d = os.path.join(root, "host")
    write_bat(d, "hang", ["echo PASSED: 3 checks, 0 failures", "ping -n 60 127.0.0.1 >nul", "exit /b 0"])  # a summary line, then a sleep
    write_bat(d, "zok", ["echo PASSED: 3 checks, 0 failures", "exit /b 0"])
    t0 = time.time()
    code, text = host_run("--dir", d, "--timeout", "3")
    took = time.time() - t0
    check(code == 1, f"run_host_tests timeout: exit {code}\n{text}")
    check((line_for(text, "hang") or "").startswith("hang: did not run (timed out"), f"run_host_tests timeout: 'hang: did not run (timed out': {line_for(text, 'hang')!r}\n{text}")
    check(line_for(text, "zok") == "zok: passed 3", f"run_host_tests timeout: the next runner still runs: {line_for(text, 'zok')!r}\n{text}")
    check("1 passed, 0 failed, 1 did not run" in text, f"run_host_tests timeout: the count line\n{text}")
    check(took < 40, f"run_host_tests timeout: the hung runner was stopped after the limit, not after its sleep ({took:.0f} s)")
    # the default is 600 s, and a runner within its limit is judged as before
    check(load("run_host_tests").DEFAULT_TIMEOUT == 600, "run_host_tests: the default per-runner timeout is 600 s")
    code, text = host_run("--dir", d, "--only", "zok", "--timeout", "30")
    check(code == 0 and line_for(text, "zok") == "zok: passed 3", f"run_host_tests, within the limit: exit {code}\n{text}")


def shape_found(source, alternatives):
    """The first alternative (a list of literals) of which every literal is in `source`, or None."""
    for alt in alternatives:
        if all(w in source for w in alt):
            return alt
    return None


def test_shape_found(root):
    # the "default" shape is printed in one of two source forms; each form needs BOTH of its literals
    two = [['"%s: %d checks, %d failures', '"FAILED" : "PASSED"'], ['"FAILED: %d checks, %d failures', '"PASSED: %d checks, 0 failures']]
    ternary = 'printf("%s: %d checks, %d failures\n", f ? "FAILED" : "PASSED", a, b);'
    split = 'printf("FAILED: %d checks, %d failures\n", a, b); printf("PASSED: %d checks, 0 failures\n", a);'
    check(shape_found(ternary, two) == two[0] and shape_found(split, two) == two[1], "shape_found: both real source forms are accepted")
    check(shape_found('printf("PASSED: %d checks, 0 failures\n", a);', two) is None, "shape_found: a source with only the PASSED line is refused")
    check(shape_found('printf("FAILED: %d checks, %d failures\n", a, b);', two) is None, "shape_found: a source with only the FAILED line is refused")
    check(shape_found('printf("%s: %d checks, %d failures\n", a, b, c);', two) is None, "shape_found: a source that never says FAILED / PASSED is refused")
    check(shape_found("", two) is None, "shape_found: an empty source is refused")


def test_table_matches_sources(root):
    # every real runner: the summary shape run_host_tests.py expects is the one its test source prints
    rht = load("run_host_tests")
    literals = {
        "default": [['"%s: %d checks, %d failures', '"FAILED" : "PASSED"'],
                    ['"FAILED: %d checks, %d failures', '"PASSED: %d checks, 0 failures']],
        "speakers": [['"all speakers checks passed', '"%d FAILED']],
        "dpps": [['the redirect agrees with the SDK function: %s', '"yes" : "NO"']],
    }
    bats = sorted(f for f in os.listdir(HOST) if re.fullmatch(r"run_\w+_test\.bat", f))
    check(len(bats) >= 30, f"table: the real runners are found: {len(bats)}")
    for bat in bats:
        name = bat[len("run_"):-len("_test.bat")]
        text = read(os.path.join(HOST, bat)).decode("utf-8", "replace")
        exe = re.findall(r'/Fe"%OUT%\\(\w+)\.exe"', text)
        src = os.path.join(HOST, exe[0] + ".cpp") if exe else ""
        source = read(src).decode("utf-8", "replace") if src and os.path.isfile(src) else ""
        kind = rht.shape_kind(name)
        want = literals.get(kind) or [[f'"{name}: all checks passed', '"%d check(s) failed']]
        check(bool(source) and shape_found(source, want) is not None,
              f"table: {bat} ({kind}) prints one of {want} in {os.path.basename(src) or '?'}")


def main():
    tests = [(n, f) for n, f in sorted(globals().items()) if n.startswith("test_") and callable(f)]
    for name, fn in tests:
        root = tempfile.mkdtemp(prefix="release_tools_")
        try:
            fn(root)
        except Exception:
            global failures
            failures += 1
            print("FAIL", name, "raised:")
            traceback.print_exc()
        finally:
            shutil.rmtree(root, ignore_errors=True)
    print(f"{checks} checks, {failures} failures")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()

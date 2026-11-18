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


# ---- PGO: pgo_source_id.py, pgo_recipe.py, the workflow and np_build.bat wiring ------------------------------------

PGO_ID = os.path.join(TOOLS, "pgo_source_id.py")
PGO_RECIPE = os.path.join(TOOLS, "pgo_recipe.py")
WORKFLOW = os.path.join(REPO, ".github", "workflows", "netplay-release.yml")
NP_BUILD = os.path.join(TOOLS, "np_build.bat")
SDK_A = "4fe33826f8387d0e98e64bbcb5923355c45d9898"
SDK_B = "29495ab15083d8aa77bd7b560bdd39689ebea077"
# G3's list, written out here (not read from the tool), so a tool that drops or adds a path fails source_id_moves
ID_FILES = {"src/x.cpp": b"int x = 1;\n", "src/net/y.cpp": b"int y = 2;\n", "generated/g.cpp": b"int g = 3;\n",
            "third_party/t/t.h": b"#define T 4\n", "CMakeLists.txt": b"project(doa4)\n",
            "CMakePresets.json": b"{\"version\": 6}\n", "hints.toml": b"[hints]\na = 1\n",
            "dead_or_alive_4_manifest.toml": b"[manifest]\nb = 2\n"}
OTHER_FILES = {"tools/y.py": b"print(1)\n", "README.md": b"# readme\n",
               "netplay_sdk.txt": f"repo=https://example.invalid/sdk\ncommit={SDK_A}\n".encode("ascii")}
PROFILE_BYTES = b"\xff\x00synthetic profile, not a real one\x00\r\n\x01"  # never a real profile


def git(repo, *args):
    p = subprocess.run(["git", "-C", repo, "-c", "user.name=t", "-c", "user.email=t@t", *args], capture_output=True,
                       text=True, encoding="utf-8", errors="replace", timeout=120)
    return p.returncode, p.stdout + p.stderr


def make_repo(root, name="game", files=None):
    repo = os.path.join(root, name)
    for rel, data in (files or {**ID_FILES, **OTHER_FILES}).items():
        write(os.path.join(repo, *rel.split("/")), data)
    git(repo, "init", "-q")
    commit(repo, "init")
    return repo


def commit(repo, msg="change"):
    git(repo, "add", "-A")
    code, text = git(repo, "commit", "-q", "-m", msg)
    check(code == 0, f"fixture: git commit '{msg}' in {repo}: exit {code}\n{text}")


def head(repo):
    code, text = git(repo, "rev-parse", "HEAD")
    return text.strip() if code == 0 else "?"


def pgo(*args):
    return run(PGO_ID, *args)


def compute(repo, sdk=SDK_A):
    return pgo("compute", "--repo", repo, *(["--sdk", sdk] if sdk else []))


def line_of(text, prefix):
    for line in text.splitlines():
        if line.startswith(prefix):
            return line
    return None


def put_pgo(repo, source=None, profile=True):
    """pgo/doa4.profdata (synthetic bytes) and pgo/source.txt (the given text), each only when asked."""
    if profile:
        write(os.path.join(repo, "pgo", "doa4.profdata"), PROFILE_BYTES)
    if source is not None:
        write(os.path.join(repo, "pgo", "source.txt"), source.encode("ascii"))


def use_line(repo):
    return "pgo: use " + os.path.abspath(os.path.join(repo, "pgo", "doa4.profdata")).replace("\\", "/")


def oracle_game(repo):
    """G3 recomputed here: sha256 of the sorted 'git ls-tree -r HEAD' lines of the seven paths, one LF after each."""
    paths = ["src", "generated", "third_party", "CMakeLists.txt", "CMakePresets.json", "hints.toml",
             "dead_or_alive_4_manifest.toml"]
    p = subprocess.run(["git", "ls-tree", "-r", "HEAD", "--", *paths], cwd=repo, capture_output=True)
    lines = sorted(p.stdout.decode("utf-8").splitlines())
    return hashlib.sha256("".join(l + "\n" for l in lines).encode("utf-8")).hexdigest(), len(lines)


def test_pgo_source_id_stable(root):
    repo = make_repo(root)
    c1, t1 = compute(repo)
    c2, t2 = compute(repo)
    check(c1 == 0 and c2 == 0 and t1 == t2, f"source id: same tree twice, same output: exit {c1}/{c2}\n{t1}\n{t2}")
    want, n = oracle_game(repo)
    check(n == len(ID_FILES), f"source id oracle: the fixture has {len(ID_FILES)} tracked files under the seven paths: {n}")
    check(t1 == f"game={want}\nsdk={SDK_A}\nllvm=23.1.4\nnames=portable\n", f"source id: exactly the four lines (G3 + names):\n{t1!r}\nwant game={want}")
    # without --sdk, the sdk line is the pin's commit
    c3, t3 = compute(repo, sdk=None)
    check(c3 == 0 and line_of(t3, "sdk=") == f"sdk={SDK_A}", f"source id without --sdk: the pin's commit: exit {c3}\n{t3}")
    # a change under pgo/, in tools/, the README and the SDK pin, committed: the game line is the same
    put_pgo(repo, source=t1)
    write(os.path.join(repo, "tools", "y.py"), b"print(2)\n")
    write(os.path.join(repo, "README.md"), b"# readme, changed\n")
    write(os.path.join(repo, "netplay_sdk.txt"), f"repo=https://example.invalid/sdk\ncommit={SDK_B}\n".encode("ascii"))
    commit(repo, "outside the id")
    c4, t4 = compute(repo)
    check(c4 == 0 and line_of(t4, "game=") == line_of(t1, "game=") and line_of(t1, "game=") is not None,
          f"source id: pgo/, tools/, README and netplay_sdk.txt do not move the game line: exit {c4}\n{t1}\n{t4}")
    # a short or non-hex --sdk is refused (the check compares full commit ids as text)
    for bad in ("4fe33826", "z" * 40):
        c5, t5 = compute(repo, sdk=bad)
        check(c5 == 1 and "pgo: refused:" in t5 and "Traceback" not in t5, f"compute --sdk {bad}: refused: exit {c5}\n{t5}")


def test_pgo_source_id_moves(root):
    repo = make_repo(root)
    code, text = compute(repo)
    before = line_of(text, "game=")
    check(code == 0 and before is not None, f"source id moves: first compute: exit {code}\n{text}")
    # one byte in a file under each of the seven paths, committed: each moves the game line (a new value each time)
    seen = {before}
    for rel, data in ID_FILES.items():
        if rel == "src/net/y.cpp":
            continue
        write(os.path.join(repo, *rel.split("/")), data.replace(b"\n", b" \n", 1))
        commit(repo, "byte in " + rel)
        code, text = compute(repo)
        now = line_of(text, "game=")
        check(code == 0 and now is not None and now not in seen, f"source id: a byte changed in {rel} moves the game line: exit {code}\n{text}")
        want, _ = oracle_game(repo)
        check(now == f"game={want}", f"source id after {rel}: equals the oracle: {now} vs {want}")
        seen.add(now)
    # a new file under src/ moves it too
    write(os.path.join(repo, "src", "new.cpp"), b"int n;\n")
    commit(repo, "new file")
    code, text = compute(repo)
    check(code == 0 and line_of(text, "game=") not in seen, f"source id: a new file under src/ moves the game line\n{text}")


def test_pgo_none_when_absent(root):
    repo = make_repo(root)
    sdk = make_repo(root, "sdk", {"a.txt": b"sdk\n"})
    for how in ("pin", "worktree:" + sdk):
        code, text = pgo("check", "--repo", repo, "--sdk-from", how)
        check(code == 0 and text.strip() == "pgo: none", f"check --sdk-from {how}, no pgo/ files: exit 0, 'pgo: none': exit {code}\n{text!r}")


def test_pgo_refuses_half(root):
    repo = make_repo(root)
    code, text = compute(repo)
    put_pgo(repo, source=None, profile=True)
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 1 and "pgo: refused: profile without source.txt" in text and "pgo: use" not in text,
          f"check, profile alone: refused: exit {code}\n{text}")
    os.remove(os.path.join(repo, "pgo", "doa4.profdata"))
    c, src = compute(repo)
    put_pgo(repo, source=src, profile=False)
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 1 and "pgo: refused: source.txt without profile" in text and "pgo: use" not in text,
          f"check, source.txt alone (a matching one): refused: exit {code}\n{text}")


def test_pgo_mismatch_refuses(root):
    repo = make_repo(root)
    code, src = compute(repo)
    put_pgo(repo, source=src)
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 0 and text.strip() == use_line(repo), f"check, matching files: exit 0, '{use_line(repo)}': exit {code}\n{text!r}")
    # source.txt with CRLF (a committed file in a CRLF checkout) reads the same
    put_pgo(repo, source=src.replace("\n", "\r\n"))
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 0 and text.strip() == use_line(repo), f"check, CRLF source.txt: still used: exit {code}\n{text!r}")
    # a fix lands after the profile was made (Review Focus 2): committed, so the tree is clean and the game differs
    write(os.path.join(repo, "src", "x.cpp"), b"int x = 10;\n")
    commit(repo, "hot fix")
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 1 and "pgo: refused: game tree differs" in text and "working tree" not in text and "pgo: use" not in text,
          f"check after a committed fix: refused, game tree differs: exit {code}\n{text}")
    # llvm differs, with the game and the sdk right
    code, src2 = compute(repo)
    put_pgo(repo, source=src2.replace("llvm=23.1.4", "llvm=23.1.3"))
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 1 and "pgo: refused: llvm differs" in text and "pgo: use" not in text, f"check, llvm=23.1.3: refused: exit {code}\n{text}")
    # a source.txt without its sdk line is refused, never read as a match
    put_pgo(repo, source="".join(l + "\n" for l in src2.splitlines() if not l.startswith("sdk=")))
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 1 and "pgo: refused:" in text and "pgo: use" not in text and "Traceback" not in text,
          f"check, source.txt without sdk=: refused: exit {code}\n{text}")
    # the naming mode (names review F1): a profile from before the names line (game, sdk and llvm all matching) and one
    # made in the other mode are refused; a profile made in one mode gives a build in the other mode nothing, silently
    put_pgo(repo, source="".join(l + "\n" for l in src2.splitlines() if not l.startswith("names=")))
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 1 and "pgo: refused: source.txt malformed: no names line" in text and "pgo: use" not in text,
          f"check, a three-line source.txt (made before the names line) with game, sdk and llvm right: refused: exit {code}\n{text}")
    for other in ("path", "basename"):  # basename: a profile made with the names flag but without the macro prefix maps
        put_pgo(repo, source=src2.replace("names=portable", "names=" + other))
        code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
        check(code == 1 and "pgo: refused: names differ" in text and "pgo: use" not in text, f"check, names={other}: refused: exit {code}\n{text}")
    put_pgo(repo, source=src2)
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 0 and text.strip() == use_line(repo), f"check, the same four lines again: used: exit {code}\n{text!r}")


def test_pgo_sdk_differs(root):
    repo = make_repo(root)
    # pin: netplay_sdk.txt names SDK_A
    code, src = compute(repo, sdk=SDK_B)
    put_pgo(repo, source=src)
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 1 and "pgo: refused: sdk differs" in text and "pgo: use" not in text, f"check, sdk={SDK_B[:8]} vs pin {SDK_A[:8]}: refused: exit {code}\n{text}")
    write(os.path.join(repo, "netplay_sdk.txt"), f"repo=https://example.invalid/sdk\ncommit={SDK_B}\n".encode("ascii"))
    commit(repo, "pin moves")
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 0 and text.strip() == use_line(repo), f"check, pin moved to the profile's sdk: used: exit {code}\n{text}")
    # worktree:PATH reads that repository's HEAD
    sdk = make_repo(root, "sdk", {"a.txt": b"sdk\n"})
    h = head(sdk)
    code, src = compute(repo, sdk=h)
    put_pgo(repo, source=src)
    code, text = pgo("check", "--repo", repo, "--sdk-from", "worktree:" + sdk)
    check(code == 0 and text.strip() == use_line(repo), f"check --sdk-from worktree, HEAD {h[:8]}: used: exit {code}\n{text}")
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 1 and "pgo: refused: sdk differs" in text, f"check --sdk-from pin, profile made against the worktree: refused: exit {code}\n{text}")
    write(os.path.join(sdk, "a.txt"), b"sdk moved\n")
    commit(sdk, "sdk moves")
    code, text = pgo("check", "--repo", repo, "--sdk-from", "worktree:" + sdk)
    check(code == 1 and "pgo: refused: sdk differs" in text and "pgo: use" not in text, f"check --sdk-from worktree after an SDK commit: refused: exit {code}\n{text}")


def test_pgo_dirty_tree_refused(root):
    repo = make_repo(root)
    code, src = compute(repo)
    put_pgo(repo, source=src)
    write(os.path.join(repo, "src", "x.cpp"), b"int x = 11;\n")  # edited, not committed: HEAD's id would match
    code, text = compute(repo)
    check(code == 1 and "pgo: refused: working tree differs in src/x.cpp" in text and "game=" not in text,
          f"compute, src/x.cpp edited: refused: exit {code}\n{text}")
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 1 and "pgo: refused: working tree differs in src/x.cpp" in text and "pgo: use" not in text,
          f"check, src/x.cpp edited, matching files: refused: exit {code}\n{text}")
    git(repo, "checkout", "--", "src/x.cpp")
    write(os.path.join(repo, "hints.toml"), b"[hints]\na = 9\n")
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 1 and "working tree differs in hints.toml" in text, f"check, hints.toml edited: refused: exit {code}\n{text}")
    git(repo, "checkout", "--", "hints.toml")
    write(os.path.join(repo, "src", "new.cpp"), b"int n;\n")  # untracked under src/
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 1 and "working tree differs in src/new.cpp" in text, f"check, untracked src/new.cpp: refused: exit {code}\n{text}")
    os.remove(os.path.join(repo, "src", "new.cpp"))
    # edits outside the seven paths do not count
    write(os.path.join(repo, "tools", "y.py"), b"print(3)\n")
    write(os.path.join(repo, "notes.txt"), b"untracked\n")
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 0 and text.strip() == use_line(repo), f"check, edits outside the paths only: used: exit {code}\n{text}")
    # no profile at all: a working tree under development configures plainly (nothing is compiled from the id)
    shutil.rmtree(os.path.join(repo, "pgo"))
    write(os.path.join(repo, "src", "x.cpp"), b"int x = 12;\n")
    code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
    check(code == 0 and text.strip() == "pgo: none", f"check, no pgo/ files and src edited: 'pgo: none': exit {code}\n{text}")


def workflow_text():
    return read(WORKFLOW).decode("utf-8").replace("\r\n", "\n")


def workflow_paths(text):
    m = re.search(r"^\s+paths:\s*\[(.*?)\]", text, re.M | re.S)
    return re.findall(r"'([^']+)'", m.group(1)) if m else []


def configure_step(text):
    m = re.search(r"- name: Configure, build, host tests\n(.*?)(?=\n      - name: |\Z)", text, re.S)
    return [l.strip() for l in m.group(1).split("\n")] if m else []


def test_pgo_workflow_wiring(root):
    text = workflow_text()
    check("pgo/**" in workflow_paths(text), f"workflow: paths: has 'pgo/**': {workflow_paths(text)}")
    check("docs/PGO.md" in text, "workflow: a comment names docs/PGO.md")
    rule = text.split("# PUBLISH RULE", 1)[-1].split("\non:", 1)[0] if "# PUBLISH RULE" in text else ""
    check("pgo/" in rule, f"workflow: the PUBLISH RULE comment lists pgo/ among the game's inputs: {rule!r}")
    lines = configure_step(text)
    i_check = next((i for i, l in enumerate(lines) if "tools/pgo_source_id.py check --sdk-from pin" in l), None)
    i_cmake = next((i for i, l in enumerate(lines) if l.startswith("cmake --preset")), None)
    check(i_check is not None and i_cmake is not None and i_check < i_cmake,
          f"workflow Configure: the check runs before cmake: check line {i_check}, cmake line {i_cmake}")
    if i_check is None or i_cmake is None or i_check >= i_cmake:
        return
    # the check's exit code is caught on the next line and stops the job before cmake
    m = re.fullmatch(r'set "(\w+)=%errorlevel%"', lines[i_check + 1])
    rc = m.group(1) if m else "?"
    check(m is not None, f"workflow Configure: the line after the check keeps its exit code: {lines[i_check + 1]!r}")
    check(f'if not "%{rc}%"=="0" exit /b 1' in lines[i_check + 1:i_cmake], f"workflow Configure: a non-zero check stops the job before cmake")
    # the profile flag: one line in the whole workflow, gated on 'pgo: use', setting a variable reset before it
    flag_lines = [l.strip() for l in text.split("\n") if "-fprofile-use" in l]
    check(len(flag_lines) == 1 and '"pgo: use"' in flag_lines[0], f"workflow: -fprofile-use only in one line, gated on 'pgo: use': {flag_lines}")
    m = re.search(r'set "(\w+)= -fprofile-use=%%c' + re.escape(" " + NAMES + MAPS_WF) + '"', flag_lines[0]) if flag_lines else None
    var = m.group(1) if m else "?"
    i_flag = lines.index(flag_lines[0]) if flag_lines and flag_lines[0] in lines else None
    check(m is not None and i_flag is not None and i_check < i_flag < i_cmake, f"workflow Configure: the flag line sits between the check and cmake: {i_flag}")
    check(f'set "{var}="' in lines[i_check:i_flag or 0], f"workflow Configure: {var} is cleared before the flag line")
    cm = lines[i_cmake]
    check(f'"-DCMAKE_C_FLAGS=-march=x86-64-v3%{var}%"' in cm and f'"-DCMAKE_CXX_FLAGS=-march=x86-64-v3%{var}%"' in cm,
          f"workflow Configure: cmake passes %{var}% in both C and C++ flags: {cm}")
    check("profile" not in cm, f"workflow Configure: the cmake line names no profile itself: {cm}")


def test_pgo_paths_list_agrees(root):
    ids = load("pgo_source_id")
    listed = workflow_paths(workflow_text())
    want = sorted(p[:-3] if p.endswith("/**") else p for p in listed if p not in ("pgo/**", "netplay_sdk.txt"))
    check(len(listed) >= 9 and "netplay_sdk.txt" in listed, f"paths agree: the workflow's paths: list was read: {listed}")
    check(sorted(ids.ID_PATHS) == want, f"paths agree: pgo_source_id.ID_PATHS {sorted(ids.ID_PATHS)} == the workflow's {want}")
    for p in ids.ID_PATHS:
        check(os.path.exists(os.path.join(REPO, p)), f"paths agree: {p} exists in this repository")


def test_pgo_gitattributes_binary(root):
    path = os.path.join(REPO, ".gitattributes")
    lines = read(path).decode("ascii").replace("\r\n", "\n").split("\n") if os.path.isfile(path) else []
    check("pgo/*.profdata binary" in lines, f".gitattributes: the line 'pgo/*.profdata binary': {lines}")
    code, text = git(REPO, "check-attr", "binary", "diff", "text", "--", "pgo/doa4.profdata")
    check(code == 0 and "binary: set" in text and "text: unset" in text and "diff: unset" in text,
          f"git check-attr on pgo/doa4.profdata: binary (no text conversion, no diff): exit {code}\n{text}")


def test_pgo_np_build_wiring(root):
    text = read(NP_BUILD).decode("ascii").replace("\r\n", "\n")
    lines = text.split("\n")
    cm = [l for l in lines if l.startswith("cmake --preset")]
    check(len(cm) == 1, f"np_build.bat: one configure line: {cm}")
    cm = cm[0] if cm else ""
    # every configure passes all four flag variables, so none keeps a cached value from an earlier configure
    for name in ("CMAKE_C_FLAGS", "CMAKE_CXX_FLAGS", "CMAKE_EXE_LINKER_FLAGS", "CMAKE_SHARED_LINKER_FLAGS"):
        check(f'"-D{name}=' in cm, f"np_build.bat: the configure line passes {name}: {cm}")
    i_cm = lines.index(cm) if cm in lines else -1
    i_check = next((i for i, l in enumerate(lines) if 'pgo_source_id.py" check --sdk-from "worktree:%DOA4_SDK%"' in l), None)
    check(i_check is not None and i_check < i_cm, f"np_build.bat: the check runs before cmake (lines {i_check}, {i_cm})")
    flag_lines = [l for l in lines if "-fprofile-use" in l]
    check(len(flag_lines) == 1 and '"pgo: use"' in flag_lines[0], f"np_build.bat: -fprofile-use only in one line gated on 'pgo: use': {flag_lines}")
    for needle in ("-fprofile-generate=", "-DDOA4_PGO_DUMP", "-Wl,/force:unresolved", "-Wl,/export:__llvm_profile_dump"):
        check(needle in text, f"np_build.bat: the instrumented mode passes {needle}")
    # the profile names of file-local functions carry no source path in either mode (docs/PGO.md), so a build at
    # another path (CI) finds them: the flag sits in the gen line and in the use line, and nowhere else
    ids = load("pgo_source_id")
    check(getattr(ids, "NAMES_FLAG", None) == NAMES and getattr(ids, "MACRO_ROOTS", None) == ("/doa4", "/rexglue-sdk")
          and getattr(ids, "NAMES_MODE", None) == "portable",
          f"pgo_source_id.py: NAMES_FLAG is the scripts' flag, MACRO_ROOTS their map targets, NAMES_MODE portable: "
          f"{getattr(ids, 'NAMES_FLAG', None)!r}, {getattr(ids, 'MACRO_ROOTS', None)!r}, {getattr(ids, 'NAMES_MODE', None)!r}")
    # the anonymous-namespace hash follows the source file's name: both profile lines map the game and SDK roots
    root_lines = [l for l in lines if l.startswith('set "PGO_ROOT=')]
    check(root_lines == ['set "PGO_ROOT=%CD:\\=/%"'] and lines.index(root_lines[0]) > lines.index('cd /d "%~dp0.."'),
          f"np_build.bat: PGO_ROOT is the worktree's folder with forward slashes, set after the cd: {root_lines}")
    gen_lines = [l for l in lines if "-fprofile-generate=" in l]
    names_lines = [l for l in lines if NAMES in l and not l.lower().startswith("rem ")]  # commands, not the comment
    check(len(gen_lines) == 1 and len(flag_lines) == 1 and sorted(names_lines) == sorted(gen_lines + flag_lines),
          f"np_build.bat: {NAMES!r} in exactly the gen and the use line: gen {gen_lines}, use {flag_lines}, with it {names_lines}")
    maps_lines = [l for l in lines if MAPS_BAT.strip() in l and not l.lower().startswith("rem ")]
    check(len(gen_lines) == 1 and len(flag_lines) == 1 and sorted(maps_lines) == sorted(gen_lines + flag_lines)
          and all(l.rstrip().endswith(NAMES + MAPS_BAT + '"') for l in maps_lines),
          f"np_build.bat: the gen and the use line end with {NAMES + MAPS_BAT!r}: {maps_lines}")
    check(not any(l.rstrip().endswith("(") for l in lines), "np_build.bat: no parenthesised block (no errorlevel read inside one)")


# ---- the wiring under real cmd: the workflow step's own lines and np_build.bat configure, cmake replaced by a recorder

WF_STEP = "Configure, build, host tests"
WF_VSDEVCMD = r'set "DOA4_VSDEVCMD=%VS%\Common7\Tools\VsDevCmd.bat"'
BAT_VSDEVCMD = r'"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"'
PLAIN = "-march=x86-64-v3"
SHLINK = "-Wl,/force:unresolved -Wl,/export:__llvm_profile_dump"
GUARD = "pgo: use line read but no flag set"
NAMES = "-mllvm -static-func-full-module-prefix=false"  # path-free profile names for file-local functions
# path-free anonymous-namespace names: the game and SDK roots mapped to fixed tokens (the script text and the workflow text)
MAPS_BAT = " -fmacro-prefix-map=%PGO_ROOT%=/doa4 -fmacro-prefix-map=%DOA4_SDK%=/rexglue-sdk"
MAPS_WF = (" -fmacro-prefix-map=%GITHUB_WORKSPACE:\\=/%/doa4-recomp=/doa4"
           " -fmacro-prefix-map=%GITHUB_WORKSPACE:\\=/%/rexglue-sdk=/rexglue-sdk")


def maps_want(game_root, sdk_root):
    """The two map flags as cmake receives them, for a game and an SDK root with forward slashes."""
    return f" -fmacro-prefix-map={game_root}=/doa4 -fmacro-prefix-map={sdk_root}=/rexglue-sdk"


def step_run_lines(text, step):
    """The 'run: |' block of the named workflow step, dedented: the lines the runner writes into its .cmd file."""
    lines = text.split("\n")
    i = next((k for k, l in enumerate(lines) if l.strip() == f"- name: {step}"), None)
    j = next((k for k in range(i + 1, len(lines)) if lines[k].strip() == "run: |"), None) if i is not None else None
    if j is None:
        return []
    key = len(lines[j]) - len(lines[j].lstrip())
    body = []
    for l in lines[j + 1:]:
        if l.strip() and len(l) - len(l.lstrip()) <= key:
            break
        body.append(l)
    while body and not body[-1].strip():
        body.pop()
    ind = min((len(l) - len(l.lstrip()) for l in body if l.strip()), default=0)
    return [l[ind:] for l in body]


def make_stubs(root):
    """cmake.cmd (first on PATH) records its arguments one per line through Python's own argv parsing (what cmake.exe
    would get); VsDevCmd.bat does nothing. A .cmd called without CALL ends its caller, so nothing after cmake runs."""
    d = os.path.join(root, "stubs")
    os.makedirs(d)
    rec_py = os.path.join(d, "cmake_rec.py")
    with open(rec_py, "w", encoding="ascii", newline="\n") as f:
        f.write("import os, sys\n"
                "with open(os.environ['DOA4_TEST_CMAKE_REC'], 'a', encoding='utf-8', newline=chr(10)) as f:\n"
                "    f.write(''.join(a + chr(10) for a in sys.argv[1:]) + '--end of call--' + chr(10))\n")
    with open(os.path.join(d, "cmake.cmd"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(f'@"{sys.executable}" -I -B "{rec_py}" %*\n')
    vsdev = os.path.join(d, "VsDevCmd.bat")
    with open(vsdev, "w", encoding="ascii", newline="\r\n") as f:
        f.write("@exit /b 0\n")
    return d, vsdev


def recorded_calls(rec):
    if not os.path.isfile(rec):
        return None
    return [c.split("\n")[:-1] for c in read(rec).decode("utf-8").split("--end of call--\n") if c]


def dflags(args):
    out = {}
    for a in args:
        if a.startswith("-D") and "=" in a:
            k, _, v = a[2:].partition("=")
            out[k] = v
    return out


def test_pgo_wiring_cmd(root):
    check(os.name == "nt", "wiring under cmd: needs Windows cmd (CI is Windows); never skipped")
    if os.name != "nt":
        return
    root = os.path.realpath(root)  # long names (a runner's TEMP can be an 8.3 path)
    stubs, vsdev = make_stubs(root)
    # the workflow side: the step's lines read from the workflow file, up to its first cmake line
    body = step_run_lines(workflow_text(), WF_STEP)
    i_cm = next((i for i, l in enumerate(body) if l.startswith("cmake --preset")), None)
    n_vs = body.count(WF_VSDEVCMD)
    check(i_cm is not None and n_vs == 1, f"wiring fixture: the step has a cmake --preset line ({i_cm}) and one VsDevCmd line ({n_vs}): {body}")
    if i_cm is None or n_vs != 1:
        return
    body = [f'set "DOA4_VSDEVCMD={vsdev}"' if l == WF_VSDEVCMD else re.sub(r"\$\{\{[^}]*\}\}", "x", l) for l in body[:i_cm + 1]]
    # the bat side: a fixture repository with np_build.bat (its absolute VsDevCmd path pointed at the stub) and
    # pgo_source_id.py in tools/, the seven id paths from ID_FILES, and netplay_sdk.txt naming the fixture SDK's HEAD
    bat = read(NP_BUILD)
    n_bat = bat.count(BAT_VSDEVCMD.encode("ascii"))
    check(n_bat == 1, f"wiring fixture: np_build.bat calls VsDevCmd at its one absolute path: {n_bat}")
    if n_bat != 1:
        return
    bat = bat.replace(BAT_VSDEVCMD.encode("ascii"), f'"{vsdev}"'.encode("utf-8"))
    sdk = make_repo(root, "sdk", {"a.txt": b"sdk\n"})
    ws = os.path.join(root, "ws")
    files = {**ID_FILES, "tools/y.py": b"print(1)\n", "README.md": b"# readme\n",
             "netplay_sdk.txt": f"repo=https://example.invalid/sdk\ncommit={head(sdk)}\n".encode("ascii"),
             "tools/pgo_source_id.py": read(PGO_ID), "tools/np_build.bat": bat}
    repo = make_repo(ws, "doa4-recomp", files)
    fx_bat = os.path.join(repo, "tools", "np_build.bat")
    rt = os.path.join(root, "runner_temp")
    os.makedirs(rt)
    base = {k: v for k, v in os.environ.items() if not k.upper().startswith("PGO_") and k.upper() != "DOA4_PGO"}
    base.update(PATH=stubs + os.pathsep + os.path.dirname(sys.executable) + os.pathsep + os.environ.get("PATH", ""),
                RUNNER_TEMP=rt, GITHUB_WORKSPACE=r"D:\a\r\r", DOA4_SDK=sdk.replace(os.sep, "/"))
    n = [0]

    def run_side(side, extra=None, wf_body=None, bat_path=None):
        n[0] += 1
        rec = os.path.join(root, f"rec_{n[0]}.txt")
        env = dict(base, DOA4_TEST_CMAKE_REC=rec, **(extra or {}))
        if side == "workflow":
            script = os.path.join(root, f"wf_{n[0]}.cmd")
            with open(script, "w", encoding="utf-8", newline="\r\n") as f:
                f.write("\n".join(["@echo off", f'cd /d "{ws}"', *(wf_body or body)]) + "\n")
            line = f'cmd /D /E:ON /V:OFF /S /C "CALL "{script}""'
        else:
            line = f'cmd /D /E:ON /V:OFF /S /C "CALL "{bat_path or fx_bat}" configure"'
        try:
            p = subprocess.run(line, cwd=root, env=env, capture_output=True, text=True, encoding="utf-8",
                               errors="replace", timeout=180)
            code, out = p.returncode, p.stdout + p.stderr
        except subprocess.TimeoutExpired:
            code, out = "timeout", ""
        return code, out, recorded_calls(rec)

    def reached(what, res):
        code, out, calls = res
        ok = calls is not None and len(calls) == 1 and calls[0][:2] == ["--preset", "win-amd64-release"]
        check(ok and code == 0, f"wiring {what}: cmake reached once with --preset, exit 0: exit {code}, calls {calls}\n{out}")
        return (dflags(calls[0]), calls[0]) if ok else ({}, [])

    def stopped(what, res, text):
        code, out, calls = res
        check(calls is None, f"wiring {what}: cmake never reached: {calls}\n{out}")
        check(code not in (0, "timeout"), f"wiring {what}: exit code not 0: {code}\n{out}")
        check(text in out, f"wiring {what}: prints {text!r}:\n{out}")

    def plain(what, f, args, bat_side):
        check(f.get("CMAKE_C_FLAGS") == PLAIN and f.get("CMAKE_CXX_FLAGS") == PLAIN,
              f"wiring {what}: plain C and C++ flags: {f.get('CMAKE_C_FLAGS')!r}, {f.get('CMAKE_CXX_FLAGS')!r}")
        check(not any("-fprofile" in a or "-bogus" in a or "static-func" in a or "prefix-map" in a for a in args),
              f"wiring {what}: no -fprofile, no profile-name flag and nothing inherited anywhere: {args}")
        check(f.get("CMAKE_SHARED_LINKER_FLAGS", "") == "" and f.get("CMAKE_EXE_LINKER_FLAGS", "") == "",
              f"wiring {what}: linker flags empty: {f.get('CMAKE_SHARED_LINKER_FLAGS')!r}, {f.get('CMAKE_EXE_LINKER_FLAGS')!r}")
        if bat_side:
            check("CMAKE_SHARED_LINKER_FLAGS" in f and "CMAKE_EXE_LINKER_FLAGS" in f, f"wiring {what}: both linker flags passed (empty): {args}")

    sides = ("workflow", "np_build.bat")
    # none: no pgo/ folder; inherited: PGO_C / PGO_USE set in the environment, no pgo/ folder
    for side in sides:
        f, args = reached(f"{side} none", run_side(side))
        plain(f"{side} none", f, args, side != "workflow")
        f, args = reached(f"{side} inherited", run_side(side, {"PGO_C": " -bogus", "PGO_USE": " -bogus"}))
        plain(f"{side} inherited PGO_C/PGO_USE= -bogus", f, args, side != "workflow")
    # gen (np_build.bat only), an absolute folder with backslashes
    gen_dir = os.path.join(root, "prof")
    res = run_side("np_build.bat", {"DOA4_PGO": "gen:" + gen_dir})
    f, args = reached("np_build.bat gen", res)
    bat_maps = maps_want(repo.replace(os.sep, "/"), sdk.replace(os.sep, "/"))
    wf_maps = maps_want("D:/a/r/r/doa4-recomp", "D:/a/r/r/rexglue-sdk")  # GITHUB_WORKSPACE=D:\a\r\r above
    want = f"{PLAIN} -fprofile-generate={gen_dir.replace(os.sep, '/')} -DDOA4_PGO_DUMP {NAMES}{bat_maps}"
    check(f.get("CMAKE_C_FLAGS") == want and f.get("CMAKE_CXX_FLAGS") == want,
          f"wiring np_build.bat gen: C and C++ flags are {want!r}: {f.get('CMAKE_C_FLAGS')!r}, {f.get('CMAKE_CXX_FLAGS')!r}")
    check(f.get("CMAKE_SHARED_LINKER_FLAGS") == SHLINK, f"wiring np_build.bat gen: shared linker flags {SHLINK!r}: {f.get('CMAKE_SHARED_LINKER_FLAGS')!r}")
    check(f.get("CMAKE_EXE_LINKER_FLAGS") == "", f"wiring np_build.bat gen: exe linker flags passed empty: {f.get('CMAKE_EXE_LINKER_FLAGS')!r}")
    # use: matching pgo/ files (made against the fixture SDK's HEAD, which the pin names too)
    code, src = compute(repo, sdk=head(sdk))
    check(code == 0, f"wiring fixture: compute: exit {code}\n{src}")
    put_pgo(repo, source=src)
    profile = os.path.join(repo, "pgo", "doa4.profdata")
    for side in sides:
        res = run_side(side)
        f, args = reached(f"{side} use", res)
        m = re.search(r"^pgo: use (\S.*?)\s*$", res[1], re.M)
        path = m.group(1) if m else ""
        check(m is not None and "\\" not in path and os.path.isabs(path) and os.path.isfile(path) and os.path.samefile(path, profile),
              f"wiring {side} use: 'pgo: use <absolute path, forward slashes>' names the fixture's profile: {path!r}\n{res[1]}")
        want = f"{PLAIN} -fprofile-use={path} {NAMES}" + (wf_maps if side == "workflow" else bat_maps)
        check(bool(path) and f.get("CMAKE_C_FLAGS") == want and f.get("CMAKE_CXX_FLAGS") == want,
              f"wiring {side} use: C and C++ flags are {want!r}: {f.get('CMAKE_C_FLAGS')!r}, {f.get('CMAKE_CXX_FLAGS')!r}")
        check("-fprofile" not in f.get("CMAKE_SHARED_LINKER_FLAGS", "") + f.get("CMAKE_EXE_LINKER_FLAGS", ""),
              f"wiring {side} use: no profile flag in the linker flags: {args}")
        if side != "workflow":
            check(f.get("CMAKE_SHARED_LINKER_FLAGS") == "" and f.get("CMAKE_EXE_LINKER_FLAGS") == "", f"wiring {side} use: linker flags passed empty: {args}")
    # round-3 review R3-1: a caller's DOA4_SDK with backslashes and a trailing separator still gives the forward-slash
    # SDK map (clang matches the map against CMake's forward-slash compile paths and says nothing when it does not match)
    # (the third spelling is relative to the cmd's folder, root: made absolute before np_build.bat changes folder)
    for spelled in (sdk.replace("/", os.sep) + os.sep, sdk.replace(os.sep, "/") + "/", os.path.relpath(sdk, root) + os.sep):
        res = run_side("np_build.bat", {"DOA4_SDK": spelled})
        f, args = reached(f"np_build.bat use, DOA4_SDK={spelled!r}", res)
        m = re.search(r"^pgo: use (\S.*?)\s*$", res[1], re.M)
        want = f"{PLAIN} -fprofile-use={m.group(1) if m else '?'} {NAMES}{bat_maps}"
        check(f.get("CMAKE_C_FLAGS") == want and f.get("REXSDK_DIR") == sdk.replace(os.sep, "/"),
              f"wiring np_build.bat use, DOA4_SDK={spelled!r}: the map and REXSDK_DIR name the SDK in forward slashes, no trailing "
              f"slash: {f.get('CMAKE_C_FLAGS')!r}, REXSDK_DIR {f.get('REXSDK_DIR')!r}")
    # off (np_build.bat only), with the matching pgo/ files present: still plain
    res = run_side("np_build.bat", {"DOA4_PGO": "off"})
    f, args = reached("np_build.bat off", res)
    plain("np_build.bat off", f, args, True)
    check("pgo: off" in res[1], f"wiring np_build.bat off: prints 'pgo: off':\n{res[1]}")
    # the guard: the flag line dropped from a copy, so a 'pgo: use' line sets no flag: the job stops before cmake
    wf_flag = [i for i, l in enumerate(body) if "-fprofile-use=%%c" in l]
    bat_lines = bat.split(b"\r\n")
    bat_flag = [i for i, l in enumerate(bat_lines) if b"-fprofile-use=%%c" in l]
    check(len(wf_flag) == 1 and len(bat_flag) == 1, f"wiring fixture: one flag line each: {wf_flag}, {bat_flag}")
    if len(wf_flag) == 1 and len(bat_flag) == 1:
        noflag_body = body[:wf_flag[0]] + ["rem the flag line, dropped by the test"] + body[wf_flag[0] + 1:]
        stopped("workflow use, flag line dropped", run_side("workflow", wf_body=noflag_body), GUARD)
        noflag_bat = os.path.join(repo, "tools", "np_build_noflag.bat")
        write(noflag_bat, b"\r\n".join(bat_lines[:bat_flag[0]] + [b"rem the flag line, dropped by the test"] + bat_lines[bat_flag[0] + 1:]))
        stopped("np_build.bat use, flag line dropped", run_side("np_build.bat", bat_path=noflag_bat), GUARD)
        os.remove(noflag_bat)
    # half: the profile alone
    os.remove(os.path.join(repo, "pgo", "source.txt"))
    for side in sides:
        stopped(f"{side} half", run_side(side), "pgo: refused: profile without source.txt")
    # mismatch: a fix committed after the profile was made
    put_pgo(repo, source=src)
    write(os.path.join(repo, "src", "x.cpp"), b"int x = 10;\n")
    commit(repo, "hot fix")
    for side in sides:
        stopped(f"{side} mismatch", run_side(side), "pgo: refused: game tree differs")


def test_pgo_source_unreadable(root):
    """M1: a source.txt that exists but cannot be read is a refusal, never a traceback (a deny-read ACE, as the
    review's probe made it; a directory named source.txt never reaches the read: it is not a file)."""
    repo = make_repo(root)
    code, src = compute(repo)
    put_pgo(repo, source=src)
    path = os.path.join(repo, "pgo", "source.txt")
    user = os.environ.get("USERNAME", "")
    d = subprocess.run(["icacls", path, "/deny", f"{user}:(R)"], capture_output=True, text=True, errors="replace", timeout=60)
    check(d.returncode == 0, f"fixture: icacls /deny {user}:(R): exit {d.returncode}\n{d.stdout}{d.stderr}")
    try:
        try:
            with open(path, "rb") as f:
                f.read()
            readable = True
        except OSError:
            readable = False
        check(not readable, "fixture: source.txt with the deny-read ACE cannot be read here")
        code, text = pgo("check", "--repo", repo, "--sdk-from", "pin")
        check(code == 1 and "pgo: refused: source.txt could not be read:" in text and "Traceback" not in text and "pgo: use" not in text,
              f"check, unreadable source.txt: refused, no traceback: exit {code}\n{text}")
    finally:
        subprocess.run(["icacls", path, "/remove:d", user], capture_output=True, timeout=60)


def test_pgo_llvm_version_agrees(root):
    """M2: LLVM in pgo_source_id.py is the workflow's LLVM: its step's name and its download URL."""
    spec = importlib.util.spec_from_file_location("pgo_source_id_m2", PGO_ID)
    ids = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ids)
    text = workflow_text()
    names = re.findall(r"^\s*- name: LLVM (\d+\.\d+\.\d+)\s*$", text, re.M)
    urls = re.findall(r"llvmorg-(\d+\.\d+\.\d+)", text)
    check(len(names) == 1 and names[0] == ids.LLVM, f"LLVM: the workflow's one 'LLVM x.y.z' step names {ids.LLVM}: {names}")
    check(len(urls) >= 1 and all(u == ids.LLVM for u in urls), f"LLVM: the workflow's download URL is llvmorg-{ids.LLVM}: {urls}")


WAY_OUT = "(to build without the profile: DOA4_PGO=off locally; in CI delete pgo/doa4.profdata and pgo/source.txt)"


def test_pgo_refusal_way_out(root):
    """M4: every refusal printed by check ends with the way out, and BUILDING.md section 6 says it too."""
    repo = make_repo(root)
    code, src = compute(repo)
    cases = []
    put_pgo(repo, source=None)
    cases.append(("profile alone", pgo("check", "--repo", repo, "--sdk-from", "pin")))
    os.remove(os.path.join(repo, "pgo", "doa4.profdata"))
    put_pgo(repo, source=src, profile=False)
    cases.append(("source.txt alone", pgo("check", "--repo", repo, "--sdk-from", "pin")))
    put_pgo(repo, source=src)
    cases.append(("bad --sdk-from", pgo("check", "--repo", repo, "--sdk-from", "nowhere")))
    put_pgo(repo, source=src.replace("llvm=23.1.4", "llvm=23.1.3"))
    cases.append(("llvm", pgo("check", "--repo", repo, "--sdk-from", "pin")))
    put_pgo(repo, source="game=1\n")
    cases.append(("malformed", pgo("check", "--repo", repo, "--sdk-from", "pin")))
    put_pgo(repo, source=src.replace(SDK_A, SDK_B))
    cases.append(("sdk", pgo("check", "--repo", repo, "--sdk-from", "pin")))
    put_pgo(repo, source=src)
    write(os.path.join(repo, "src", "x.cpp"), b"int x = 11;\n")
    cases.append(("dirty", pgo("check", "--repo", repo, "--sdk-from", "pin")))
    commit(repo, "hot fix")
    cases.append(("game", pgo("check", "--repo", repo, "--sdk-from", "pin")))
    for what, (code, text) in cases:
        lines = [l for l in text.splitlines() if l.startswith("pgo: refused:")]
        check(code == 1 and len(lines) == 1 and lines[0].endswith(" " + WAY_OUT),
              f"check refusal ({what}) ends with the way out: exit {code}\n{text}")
    building = read(os.path.join(REPO, "docs", "BUILDING.md")).decode("utf-8")
    s6 = building.split("## 6. Build", 1)[-1].split("## 7.", 1)[0]
    check("DOA4_PGO=off" in s6 and "pgo/doa4.profdata" in s6 and "pgo/source.txt" in s6,
          "docs/BUILDING.md section 6: a refusal's way out (DOA4_PGO=off; in CI delete both pgo/ files)")


def test_pgo_docs(root):
    doc = os.path.join(REPO, "docs", "PGO.md")
    text = read(doc).decode("utf-8").replace("\r\n", "\n") if os.path.isfile(doc) else ""
    check(0 < text.count("\n") < 60, f"docs/PGO.md: exists, under 60 lines: {text.count(chr(10))}")
    for needle in ("tools/pgo_recipe.py", "pgo/source.txt", "pgo/doa4.profdata", "DOA4_PGO",
                   "out/build/win-amd64-release", "ninja", "HEAD commit", NAMES, "names=portable", "-fmacro-prefix-map"):
        check(needle in text, f"docs/PGO.md names {needle}")
    building = read(os.path.join(REPO, "docs", "BUILDING.md")).decode("utf-8")
    s6 = building.split("## 6. Build", 1)[-1].split("## 7.", 1)[0]
    check("PGO.md" in s6, "docs/BUILDING.md section 6 links docs/PGO.md")


GOOD_LOG = "12:00:00.1 quit at frame 4000: quit frame\n12:00:00.2 pgo dump: exe 0, rexruntime.dll 0\n"


def test_pgo_recipe_run_judge(root):
    rc = load("pgo_recipe")
    judge = rc.judge_run
    ok, why = judge(0, "status ok\nexit 0 after 74 s\n", GOOD_LOG, 2)
    check(ok is True, f"run judge: a good run is good: {why}")
    ok, why = judge(0, "status ok\r\nexit 0\r\n", GOOD_LOG.replace("\n", "\r\n"), 2)
    check(ok is True, f"run judge: CRLF files read the same: {why}")
    bad = [((1, "status ok\n", GOOD_LOG, 2), "exit"),
           ((0, "status failed\n", GOOD_LOG, 2), "status"),
           ((0, "note\nstatus ok\n", GOOD_LOG, 2), "status"),
           ((0, None, GOOD_LOG, 2), "status"),
           ((0, "status ok\n", GOOD_LOG.replace("rexruntime.dll 0", "rexruntime.dll -101"), 2), "dump"),
           ((0, "status ok\n", GOOD_LOG.replace("rexruntime.dll 0", "rexruntime.dll -100"), 2), "dump"),
           ((0, "status ok\n", GOOD_LOG.replace("exe 0,", "exe 3,"), 2), "dump"),
           ((0, "status ok\n", GOOD_LOG.replace("rexruntime.dll 0", "rexruntime.dll 01"), 2), "dump"),
           ((0, "status ok\n", "12:00:00.1 quit at frame 4000: quit frame\n", 2), "dump"),
           ((0, "status ok\n", None, 2), "dump"),
           ((0, "status ok\n", GOOD_LOG, 1), "profraw"),
           ((0, "status ok\n", GOOD_LOG, 3), "profraw"),
           ((0, "status ok\n", GOOD_LOG, 0), "profraw")]
    for args, token in bad:
        ok, why = judge(*args)
        check(ok is False and token in why, f"run judge {args[0]}, {args[1]!r}, dump line {('dump' in (args[2] or ''))}, {args[3]} profraw: refused naming {token!r}: {ok} {why!r}")
    # source.txt text: compute's four lines, normalised to LF; anything else refused
    good = f"game={'a' * 64}\nsdk={SDK_B}\nllvm=23.1.4\nnames=portable\n"
    check(rc.source_text(good.replace("\n", "\r\n")) == good, "recipe: source.txt text from compute's CRLF output is LF")
    for wrong in (good.replace("llvm=23.1.4\n", ""), good.replace("a" * 64, "a" * 63), "pgo: refused: x\n", good + "x=1\n",
                  good.replace("names=portable\n", ""), good.replace("names=portable", "names=path"),
                  good.replace("names=portable", "names=basename")):
        try:
            rc.source_text(wrong)
            got = "accepted"
        except ValueError as e:
            got = f"ValueError: {e}"
        check(got.startswith("ValueError"), f"recipe: source.txt text refused for {wrong!r}: {got}")


def recipe_cli(root, toml_text, stray=False):
    out = os.path.join(root, "out")
    if stray:
        write(os.path.join(out, "prof", "left.profraw"), b"old")
    toml = os.path.join(root, "dead_or_alive_4.toml")
    write(toml, toml_text.encode("ascii"))
    wt = os.path.join(root, "not a worktree")
    os.makedirs(wt)
    code, text = run(PGO_RECIPE, "--worktree", wt, "--sdk", os.path.join(root, "no sdk"), "--out", out,
                     "--tag-prefix", "t1", "--toml", toml, "--userdata", os.path.join(root, "ud"))
    log = read(os.path.join(out, "recipe.log")).decode("utf-8") if os.path.isfile(os.path.join(out, "recipe.log")) else ""
    return code, text, log, out


def test_pgo_recipe_stray_profraw(root):
    rc = load("pgo_recipe")
    prof = os.path.join(root, "prof")
    check(rc.stray_profraw(prof) == [], "stray: a missing folder has none")
    os.makedirs(prof)
    check(rc.stray_profraw(prof) == [], "stray: an empty folder has none")
    write(os.path.join(prof, "tag", "default_1_0.profraw"), b"moved")
    write(os.path.join(prof, "notes.profraw.txt"), b"not one")
    check(rc.stray_profraw(prof) == [], f"stray: files moved into a script's folder are not stray: {rc.stray_profraw(prof)}")
    write(os.path.join(prof, "default_2_0.profraw"), b"left")
    check(rc.stray_profraw(prof) == ["default_2_0.profraw"], f"stray: a .profraw in the folder itself is: {rc.stray_profraw(prof)}")
    code, text, log, out = recipe_cli(root, "audio_mute = true\n", stray=True)
    last = log.strip().split("\n")[-1] if log.strip() else ""
    check(code == 1 and "recipe stopped:" in last and "left.profraw" in last, f"recipe with a stray .profraw: stopped before anything: exit {code}\n{log}\n{text}")
    check(sorted(os.listdir(out)) == ["prof", "recipe.log"], f"recipe with a stray .profraw: nothing else written: {sorted(os.listdir(out))}")


def test_pgo_recipe_toml_unmuted_refused(root):
    rc = load("pgo_recipe")
    check(rc.toml_muted("a = 1\naudio_mute = true\n") and rc.toml_muted("audio_mute = true\r\n"), "toml: audio_mute = true is muted (LF and CRLF)")
    for text in ("audio_mute = false\n", "# audio_mute = true\n", "audio_mute = truer\n", "", "x_audio_mute = true\n"):
        check(not rc.toml_muted(text), f"toml: {text!r} is not muted")
    code, text, log, out = recipe_cli(root, "audio_mute = false\n")
    last = log.strip().split("\n")[-1] if log.strip() else ""
    check(code == 1 and "recipe stopped:" in last and "audio_mute = true" in last, f"recipe, unmuted toml: stopped: exit {code}\n{log}\n{text}")
    check(re.match(r"\d{4}-\d\d-\d\d \d\d:\d\d:\d\d ", last) is not None, f"recipe.log lines carry their time: {last!r}")
    check(sorted(os.listdir(out)) == ["recipe.log"], f"recipe, unmuted toml: nothing else written: {sorted(os.listdir(out))}")


def test_pgo_recipe_reused_out(root):
    """M7a: preflight refuses an --out folder an earlier recipe used: runs/ exists, or a .profraw anywhere under prof/."""
    rc = load("pgo_recipe")
    out = os.path.join(root, "o")
    check(rc.reused_out(out) is None, "reused out: a missing folder is fresh")
    write(os.path.join(out, "prof", "notes.txt"), b"x")
    write(os.path.join(out, "prof", "tag", "readme.txt"), b"x")
    check(rc.reused_out(out) is None, f"reused out: prof/ without .profraw is fresh: {rc.reused_out(out)}")
    write(os.path.join(out, "prof", "tag", "default_1_0.profraw"), b"old")
    why = rc.reused_out(out) or ""
    check("default_1_0.profraw" in why and os.path.join("prof", "tag") in why, f"reused out: a .profraw in prof/tag/ is named: {why!r}")
    shutil.rmtree(os.path.join(out, "prof"))
    os.makedirs(os.path.join(out, "runs"))
    why = rc.reused_out(out) or ""
    check(os.path.join(out, "runs") in why, f"reused out: an existing runs/ is named: {why!r}")
    # through the CLI: stopped before anything else, the reason in recipe.log's last line
    os.makedirs(os.path.join(root, "out", "runs"))
    code, text, log, out2 = recipe_cli(root, "audio_mute = true\n")
    last = log.strip().split("\n")[-1] if log.strip() else ""
    check(code == 1 and "recipe stopped:" in last and os.path.join(out2, "runs") in last, f"recipe with an existing runs/: stopped naming it: exit {code}\n{log}\n{text}")
    check(sorted(os.listdir(out2)) == ["recipe.log", "runs"], f"recipe with an existing runs/: nothing else written: {sorted(os.listdir(out2))}")


def test_pgo_recipe_snapshot_games(root):
    """M7b: after a timeout only games started from the instrumented snapshot are killed, never another session's."""
    rc = load("pgo_recipe")
    snap = os.path.join(root, "wt", "out", "build", "win-amd64-release", "np_bin", "pgI")
    text = ("101|" + os.path.join(snap, "dead_or_alive_4.exe") + "\r\n"
            "102|" + os.path.join(root, "wt-lp", "out", "np_bin", "lp-rc1", "dead_or_alive_4.exe") + "\r\n"
            "103|\r\n"
            "\r\n"
            "104|" + os.path.join(snap + "2", "dead_or_alive_4.exe") + "\n"
            "105|" + snap.upper().replace(os.sep, "/") + "/DEAD_OR_ALIVE_4.EXE\n"
            "106|" + os.path.join(snap, "TracyClient.exe") + "\n"
            "107|" + os.path.join(snap, "sub", "dead_or_alive_4.exe") + "\n")
    procs = rc.parse_procs(text)
    check(procs[:3] == [(101, os.path.join(snap, "dead_or_alive_4.exe")),
                        (102, os.path.join(root, "wt-lp", "out", "np_bin", "lp-rc1", "dead_or_alive_4.exe")), (103, None)]
          and len(procs) == 7, f"parse_procs: pid and image path per line, None for no path, blank lines skipped: {procs}")
    pids = rc.games_in_snapshot(procs, snap)
    check(pids == [101, 105, 107], f"games in the snapshot: 101, 105 (case, slashes) and 107 (a subfolder); never 102 (another worktree), 103 (no path), 104 (a sibling folder), 106 (not the game): {pids}")
    check(rc.games_in_snapshot([(102, procs[1][1])], snap) == [], "games in the snapshot: a game outside it alone: none")


def test_pgo_recipe_timeout_kills_tree(root):
    """M7b: a run past its timeout has its whole process tree killed (taskkill /T /F), the child included."""
    rc = load("pgo_recipe")
    out = os.path.join(root, "out")
    os.makedirs(out)  # the recipe's first log line makes it before any run
    pidfile = os.path.join(root, "child.pid")
    child = os.path.join(root, "child.py")
    with open(child, "w", encoding="ascii", newline="\n") as f:
        f.write("import os, sys, time\nopen(sys.argv[1], 'w').write(str(os.getpid()))\ntime.sleep(120)\n")
    parent = os.path.join(root, "parent.py")
    with open(parent, "w", encoding="ascii", newline="\n") as f:
        f.write("import subprocess, sys\nsubprocess.run([sys.executable, '-I', sys.argv[1], sys.argv[2]])\n")
    r = rc.Recipe(rc.argparse.Namespace(out=out, worktree=root, sdk=root, tag_prefix="t"))
    start = time.monotonic()
    code = r.to_log("slow.log", [sys.executable, "-I", parent, child, pidfile], timeout=4)
    took = time.monotonic() - start
    check(code == "timeout" and took < 90, f"to_log past its timeout: 'timeout' soon after: {code!r} after {took:.0f} s")
    log = read(r.log_path).decode("utf-8") if os.path.isfile(r.log_path) else ""
    check("taskkill /T /F" in log, f"recipe.log names the tree kill:\n{log}")
    pid = read(pidfile).decode("ascii").strip() if os.path.isfile(pidfile) else ""
    alive = True
    for _ in range(20):
        t = subprocess.run(["tasklist", "/FI", f"PID eq {pid}", "/NH"], capture_output=True, text=True, errors="replace", timeout=60)
        alive = bool(pid) and pid in t.stdout.split()
        if not alive:
            break
        time.sleep(0.5)
    check(pid.isdigit() and not alive, f"the child of the timed-out run (pid {pid!r}) is gone")
    if pid.isdigit() and alive:
        subprocess.run(["taskkill", "/PID", pid, "/F"], capture_output=True, timeout=60)


def test_pgo_recipe_configure_off(root):
    """M7c: the recipe ends, after the instrumented build ran, with a DOA4_PGO=off configure; its exit goes into
    recipe.log, before the last line, and a failed one does not touch pgo/."""
    rc = load("pgo_recipe")
    wt = os.path.join(root, "wt")
    write(os.path.join(wt, "pgo", "doa4.profdata"), PROFILE_BYTES)
    write(os.path.join(wt, "pgo", "source.txt"), b"game=x\n")

    def stub(code):
        write(os.path.join(wt, "tools", "np_build.bat"),
              f"@echo off\r\necho stub %1 DOA4_PGO=%DOA4_PGO% DOA4_SDK=%DOA4_SDK%\r\nexit /b {code}\r\n".encode("ascii"))

    def recipe(name, instrumented):
        out = os.path.join(root, name)
        r = rc.Recipe(rc.argparse.Namespace(out=out, worktree=wt, sdk=os.path.join(root, "sdk"), tag_prefix="t"))
        r.instrumented = instrumented
        r.log("recipe start: test")  # as main() does first, which makes the folder
        return r, out

    def log_lines(r):
        return read(r.log_path).decode("utf-8").strip().split("\n") if os.path.isfile(r.log_path) else []

    stub(3)
    r, out = recipe("o1", True)
    r.end(None)
    lines = log_lines(r)
    cfg = read(os.path.join(out, "configure_off.log")).decode("utf-8", "replace") if os.path.isfile(os.path.join(out, "configure_off.log")) else ""
    check("stub configure DOA4_PGO=off" in cfg and "exit 3" in cfg, f"configure off: np_build.bat configure with DOA4_PGO=off, logged with its exit:\n{cfg}")
    check(len(lines) >= 2 and "configure off: exit 3" in lines[-2] and lines[-1].endswith(" recipe done"),
          f"configure off failed: its exit in recipe.log, then 'recipe done': {lines}")
    check(read(os.path.join(wt, "pgo", "doa4.profdata")) == PROFILE_BYTES and read(os.path.join(wt, "pgo", "source.txt")) == b"game=x\n",
          "configure off failed: pgo/ is not undone")
    stub(0)
    r, out = recipe("o2", True)
    r.end("a reason")
    lines = log_lines(r)
    check(len(lines) >= 2 and "configure off: exit 0" in lines[-2] and lines[-1].endswith(" recipe stopped: a reason"),
          f"stopped after the build: configure off, then 'recipe stopped': {lines}")
    r, out = recipe("o3", False)
    r.end("before the build")
    lines = log_lines(r)
    check(not os.path.exists(os.path.join(out, "configure_off.log")) and not any("configure off" in l for l in lines)
          and lines[-1:] and lines[-1].endswith(" recipe stopped: before the build"), f"stopped before the build: no configure: {lines}")


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

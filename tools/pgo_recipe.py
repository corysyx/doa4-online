"""Makes the PGO profile (docs/PGO.md): an instrumented build, four training fights, the merge, pgo/ written.

  py -3 -I tools/pgo_recipe.py --worktree W --sdk S --out DIR --tag-prefix P --toml FILE --userdata UD [--cache C]
                               [--timeout 1800] [--llvm-bin "C:/Program Files/LLVM/bin"]

Steps, each one line with its time in DIR/recipe.log, which ends with 'recipe done' or 'recipe stopped: <why>':
  1. refusals before anything runs: FILE has the line 'audio_mute = true' (training runs are muted), DIR is fresh (no
     DIR/runs, no .profraw anywhere under DIR/prof), no dead_or_alive_4.exe runs, and W's game tree equals its HEAD
     (pgo_source_id.py compute);
  2. the instrumented build <P>I: tools/np_build.bat with DOA4_PGO=gen:DIR/prof (configure, then build <P>I), FILE
     copied into its snapshot;
  3. the training records tag, single, single_c, single_savanna (np_run.py record, --headless=true, the user data
     given by --userdata, which np_run.py copies and never writes). A run is good only if np_run.py exits 0, its
     run_result.txt starts with 'status ok', its np_log.txt has 'pgo dump: exe 0, rexruntime.dll 0' and exactly two
     .profraw files came (the exe's and rexruntime.dll's); they are moved to DIR/prof/<script>/. A run past its
     timeout has np_run.py's whole process tree killed (taskkill /T /F), then any game still running from the
     snapshot folder (never a game started from anywhere else);
  4. llvm-profdata merge into DIR/doa4.profdata, llvm-profdata show (and the --covered count) into
     DIR/profile_summary.txt;
  5. W's game tree is still the one of step 1; DIR/doa4.profdata is copied to W/pgo/doa4.profdata and W/pgo/source.txt
     is written from 'pgo_source_id.py compute --sdk <S's HEAD>'. Nothing is committed.
Once the instrumented configure has run, the recipe ends (done or stopped) with np_build.bat configure under
DOA4_PGO=off (DIR/configure_off.log, its exit in recipe.log), so W's build tree is plain again; a failed one is
reported and pgo/ is kept. The instrumented build is never timed, gated or shipped.
"""
import argparse
import datetime
import glob
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = ("tag", "single", "single_c", "single_savanna")
DUMP_RE = re.compile(r"pgo dump: exe 0, rexruntime\.dll 0\s*$")
# names=portable: the instrumented build (np_build.bat gen) always passes the path-free naming flags
# (pgo_source_id.NAMES_FLAG and the MACRO_ROOTS maps), so a source id naming another mode is not one this recipe made
SOURCE_RE = re.compile(r"game=[0-9a-f]{64}\nsdk=[0-9a-f]{40}\nllvm=\d+\.\d+\.\d+\nnames=portable\n")
NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


class Stop(Exception):
    pass


# ---- pure parts (tests/tools/test_release_tools.py) -----------------------------------------------------------------

def judge_run(exit_code, run_result, np_log, profraw_count):
    """(good, why) for one training run. run_result / np_log: the files' text, or None when missing."""
    if exit_code != 0:
        return False, f"np_run.py exit {exit_code}"
    first = run_result.splitlines()[0].strip() if run_result and run_result.splitlines() else None
    if first != "status ok":
        return False, f"run_result.txt status line is {first!r}, not 'status ok'"
    dump = [l for l in (np_log or "").splitlines() if "pgo dump:" in l]
    if not any(DUMP_RE.search(l) for l in dump):
        return False, f"np_log.txt has no 'pgo dump: exe 0, rexruntime.dll 0' line (pgo dump lines: {dump})"
    if profraw_count != 2:
        return False, f"{profraw_count} .profraw files came, not 2 (the exe's and rexruntime.dll's)"
    return True, "good"


def stray_profraw(folder):
    """The .profraw files directly in folder (not those moved into a script's subfolder), sorted."""
    if not os.path.isdir(folder):
        return []
    return sorted(n for n in os.listdir(folder) if n.endswith(".profraw") and os.path.isfile(os.path.join(folder, n)))


def reused_out(out):
    """Why the --out folder is not fresh (an earlier recipe's runs/ or a .profraw anywhere under prof/), or None."""
    runs = os.path.join(out, "runs")
    if os.path.exists(runs):
        return f"{runs} exists (an earlier recipe's runs): give a new --out"
    for dirpath, _dirs, names in os.walk(os.path.join(out, "prof")):
        for n in sorted(names):
            if n.endswith(".profraw"):
                return f"{os.path.join(dirpath, n)} is left from an earlier run: give a new --out"
    return None


def parse_procs(text):
    """[(pid, image path or None)] from lines 'pid|path' (the PowerShell query in Recipe.game_processes)."""
    procs = []
    for line in text.splitlines():
        pid, sep, path = line.strip().partition("|")
        if sep and pid.isdigit():
            procs.append((int(pid), path.strip() or None))
    return procs


def games_in_snapshot(procs, snapshot):
    """The pids of dead_or_alive_4.exe processes whose image is inside the snapshot folder. A game started from
    anywhere else (another session's worktree, a player's copy) or with no readable path is never listed."""
    snap = os.path.normcase(os.path.normpath(os.path.abspath(snapshot))).rstrip(os.sep) + os.sep
    pids = []
    for pid, path in procs:
        if not path:
            continue
        p = os.path.normcase(os.path.normpath(path))
        if os.path.basename(p) == os.path.normcase("dead_or_alive_4.exe") and p.startswith(snap):
            pids.append(pid)
    return pids


def toml_muted(text):
    return any(line.strip() == "audio_mute = true" for line in text.splitlines())


def source_text(compute_output):
    """pgo/source.txt's text from pgo_source_id.py compute's output: its four lines, LF; ValueError otherwise."""
    text = compute_output.replace("\r\n", "\n")
    if not SOURCE_RE.fullmatch(text):
        raise ValueError(f"not the four lines of a source id: {compute_output!r}")
    return text


# ---- the run --------------------------------------------------------------------------------------------------------

class Recipe:
    def __init__(self, args):
        self.a = args
        self.out = os.path.abspath(args.out)
        self.wt = os.path.abspath(args.worktree)
        self.sdk = os.path.abspath(args.sdk)
        self.prof = os.path.join(self.out, "prof")
        self.tag = args.tag_prefix + "I"
        self.log_path = os.path.join(self.out, "recipe.log")
        self.instrumented = False  # set before the gen configure: from then on the recipe ends with an off configure

    def log(self, text):
        os.makedirs(self.out, exist_ok=True)
        with open(self.log_path, "a", encoding="utf-8", newline="\n") as f:
            f.write(f"{datetime.datetime.now():%Y-%m-%d %H:%M:%S} {text}\n")

    def compute(self, sdk_head):
        p = subprocess.run([sys.executable, "-I", "-B", os.path.join(self.wt, "tools", "pgo_source_id.py"), "compute",
                            "--repo", self.wt, "--sdk", sdk_head], capture_output=True, text=True, creationflags=NO_WINDOW)
        if p.returncode != 0:
            raise Stop(f"pgo_source_id.py compute: exit {p.returncode}: {(p.stdout + p.stderr).strip()}")
        try:
            return source_text(p.stdout)
        except ValueError as e:
            raise Stop(f"pgo_source_id.py compute: {e}")

    def to_log(self, name, cmd, env=None, cwd=None, timeout=None):
        """Runs cmd with its output in DIR/<name>, an 'exit N' line appended; returns the exit code."""
        path = os.path.join(self.out, name)
        with open(path, "wb") as f:
            try:
                p = subprocess.Popen(cmd, stdout=f, stderr=subprocess.STDOUT, env=env, cwd=cwd, creationflags=NO_WINDOW)
            except OSError as e:
                f.write(f"could not start: {e}\n".encode("utf-8"))
                p = None
                code = "not started"
            if p is not None:
                try:
                    code = p.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    code = "timeout"
                    # the whole tree: np_run.py's game and helpers too, not only the process started here
                    k = self.quiet(["taskkill", "/PID", str(p.pid), "/T", "/F"])
                    self.log(f"{name}: timeout after {timeout} s: taskkill /T /F of pid {p.pid}: {k}")
                    try:
                        p.wait(timeout=60)
                    except subprocess.TimeoutExpired:
                        p.kill()
        with open(path, "ab") as f:
            f.write(f"exit {code}\n".encode("ascii"))
        return code

    def quiet(self, cmd, timeout=120):
        """Runs a short helper hidden; returns 'exit N' with its output's first line, or why it did not run."""
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=timeout,
                               creationflags=NO_WINDOW)
        except (OSError, subprocess.TimeoutExpired) as e:
            return f"did not run: {e}"
        first = (p.stdout + p.stderr).strip().splitlines()
        return f"exit {p.returncode}" + (f" ({first[0]})" if first else "")

    def game_processes(self):
        """[(pid, image path or None)] of every dead_or_alive_4.exe running, from a hidden PowerShell query."""
        q = ("Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'dead_or_alive_4.exe' } | "
             "ForEach-Object { [string]$_.ProcessId + '|' + [string]$_.ExecutablePath }")
        try:
            p = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-Command", q], capture_output=True,
                               text=True, errors="replace", timeout=120, creationflags=NO_WINDOW)
        except (OSError, subprocess.TimeoutExpired) as e:
            self.log(f"game process query did not run: {e}")
            return []
        return parse_procs(p.stdout)

    def kill_snapshot_games(self, snapshot):
        """After a timed-out run: kills, by pid, a game still running from the instrumented snapshot; never another."""
        for pid in games_in_snapshot(self.game_processes(), snapshot):
            k = self.quiet(["taskkill", "/PID", str(pid), "/F"])
            self.log(f"killed dead_or_alive_4.exe pid {pid} started from {snapshot}: taskkill {k}")

    def configure_off(self):
        """np_build.bat configure with DOA4_PGO=off, so the build tree is plain again; its exit goes into recipe.log."""
        env = dict(os.environ, DOA4_SDK=self.sdk.replace("\\", "/"), DOA4_PGO="off")
        bat = os.path.join(self.wt, "tools", "np_build.bat")
        code = self.to_log("configure_off.log", ["cmd", "/d", "/c", bat, "configure"], env=env, cwd=self.wt, timeout=1800)
        if code == 0:
            self.log("configure off: exit 0 (the build tree is plain again)")
        else:
            self.log(f"configure off: exit {code}: the build tree may still be instrumented; pgo/ is kept; "
                     f"run DOA4_PGO=off tools\\np_build.bat configure before any other build (see configure_off.log)")

    def end(self, reason):
        """The last steps: the off configure once the instrumented configure ran, then 'recipe done' or 'recipe stopped'."""
        if self.instrumented:
            try:
                self.configure_off()
            except Exception as e:  # the off configure never hides the recipe's own outcome
                self.log(f"configure off: {type(e).__name__}: {e}")
        self.log("recipe done" if reason is None else f"recipe stopped: {reason}")

    def game_running(self):
        p = subprocess.run(["tasklist", "/FI", "IMAGENAME eq dead_or_alive_4.exe", "/NH"], capture_output=True,
                           text=True, creationflags=NO_WINDOW)
        return "dead_or_alive_4.exe" in p.stdout.lower()

    def preflight(self):
        try:
            with open(self.a.toml, "r", encoding="utf-8", errors="replace") as f:
                muted = toml_muted(f.read())
        except OSError as e:
            raise Stop(f"--toml {self.a.toml} could not be read: {e}")
        if not muted:
            raise Stop(f"--toml {self.a.toml} has no line 'audio_mute = true' (training runs are muted)")
        reused = reused_out(self.out)
        if reused:
            raise Stop(reused)
        for what, path in (("--worktree", os.path.join(self.wt, "tools", "np_build.bat")), ("--userdata", self.a.userdata),
                           ("--sdk", self.sdk)):
            if not os.path.exists(path):
                raise Stop(f"{what}: {path} not found")
        llvm_profdata = os.path.join(self.a.llvm_bin, "llvm-profdata.exe")
        if not os.path.isfile(llvm_profdata):
            raise Stop(f"{llvm_profdata} not found (--llvm-bin)")
        if self.game_running():
            raise Stop("a dead_or_alive_4.exe is running (training runs share UDP 7200 and the PC): close it first")
        p = subprocess.run(["git", "-C", self.sdk, "rev-parse", "HEAD"], capture_output=True, text=True, creationflags=NO_WINDOW)
        if p.returncode != 0:
            raise Stop(f"git rev-parse HEAD in {self.sdk}: exit {p.returncode}")
        return p.stdout.strip(), llvm_profdata

    def build(self):
        env = dict(os.environ, DOA4_SDK=self.sdk.replace("\\", "/"), DOA4_PGO="gen:" + self.prof.replace("\\", "/"))
        bat = os.path.join(self.wt, "tools", "np_build.bat")
        self.instrumented = True
        code = self.to_log(f"{self.tag}_configure.log", ["cmd", "/d", "/c", bat, "configure"], env=env, cwd=self.wt)
        with open(os.path.join(self.out, f"{self.tag}_configure.log"), encoding="utf-8", errors="replace") as f:
            pgo_line = next((l.strip() for l in f if l.startswith("pgo: ")), None)
        self.log(f"configure {self.tag}: exit {code}, {pgo_line}")
        if code != 0 or not (pgo_line or "").startswith("pgo: gen "):
            raise Stop(f"configure of {self.tag} failed (exit {code}, {pgo_line}); see {self.tag}_configure.log")
        code = self.to_log(f"{self.tag}_build.log", ["cmd", "/d", "/c", bat, "build", self.tag], env=env, cwd=self.wt)
        self.log(f"build {self.tag}: exit {code}")
        if code != 0:
            raise Stop(f"build of {self.tag} failed (exit {code}); see {self.tag}_build.log")
        snap = os.path.join(self.wt, "out", "build", "win-amd64-release", "np_bin", self.tag)
        shutil.copyfile(self.a.toml, os.path.join(snap, "dead_or_alive_4.toml"))
        self.log(f"toml copied into {snap}")
        return os.path.join(snap, "dead_or_alive_4.exe")

    def train(self, exe):
        runs = os.path.join(self.out, "runs")
        for s in SCRIPTS:
            stray = stray_profraw(self.prof)
            if stray:
                raise Stop(f"{s}: stray .profraw in {self.prof} before the run: {', '.join(stray)}")
            if self.game_running():
                raise Stop(f"{s}: a dead_or_alive_4.exe is running before the run")
            self.log(f"{s} start")
            cmd = [sys.executable, "-I", "-B", os.path.join("tools", "np_run.py"), "record", f"train_{s}",
                   "--script", os.path.join("tools", "np_scripts", f"{s}.txt"), "--exe", exe,
                   "--userdata", self.a.userdata, "--runs", runs, "--arg=--headless=true", "--timeout", str(self.a.timeout)]
            if self.a.cache:
                cmd += ["--cache", self.a.cache]
            code = self.to_log(f"train_{s}.stdout.log", cmd, cwd=self.wt, timeout=self.a.timeout + 300)
            if code == "timeout":
                self.kill_snapshot_games(os.path.dirname(exe))
            run_out = os.path.join(runs, f"train_{s}", "out")
            texts = []
            for name in ("run_result.txt", "np_log.txt"):
                try:
                    with open(os.path.join(run_out, name), "r", encoding="utf-8", errors="replace") as f:
                        texts.append(f.read())
                except OSError:
                    texts.append(None)
            dest = os.path.join(self.prof, s)
            os.makedirs(dest, exist_ok=True)
            moved = stray_profraw(self.prof)
            for n in moved:
                shutil.move(os.path.join(self.prof, n), os.path.join(dest, n))
            good, why = judge_run(code, texts[0], texts[1], len(moved))
            self.log(f"{s} end: exit {code}, profraw {len(moved)} moved to prof/{s}, {why}")
            if not good:
                raise Stop(f"{s}: not a good training run: {why}")

    def merge(self, llvm_profdata):
        raws = sorted(glob.glob(os.path.join(self.prof, "*", "*.profraw")))
        if len(raws) != 2 * len(SCRIPTS):
            raise Stop(f"{len(raws)} .profraw files under {self.prof}, not {2 * len(SCRIPTS)}")
        merged = os.path.join(self.out, "doa4.profdata")
        code = self.to_log("merge.log", [llvm_profdata, "merge", "-o", merged, *raws])
        self.log(f"merge of {len(raws)} .profraw: exit {code}")
        if code != 0 or not os.path.isfile(merged):
            raise Stop(f"llvm-profdata merge failed (exit {code}); see merge.log")
        summary = os.path.join(self.out, "profile_summary.txt")
        code = self.to_log("profile_summary.txt", [llvm_profdata, "show", merged])
        covered = subprocess.run([llvm_profdata, "show", "--covered", merged], capture_output=True, text=True,
                                 errors="replace", creationflags=NO_WINDOW)
        n = len([l for l in covered.stdout.splitlines() if l.strip()])
        with open(summary, "a", encoding="utf-8", newline="\n") as f:
            f.write(f"functions with a non-zero count (show --covered): {n} (exit {covered.returncode})\n")
        self.log(f"show: exit {code}, covered functions {n}, {os.path.getsize(merged)} bytes")
        if code != 0 or covered.returncode != 0 or n == 0:
            raise Stop("llvm-profdata show failed or the profile covers no function; see profile_summary.txt")
        return merged

    def run(self):
        sdk_head, llvm_profdata = self.preflight()
        before = self.compute(sdk_head)
        self.log(f"preflight ok: worktree {self.wt}, sdk {self.sdk} at {sdk_head}, {before.splitlines()[0]}")
        exe = self.build()
        self.train(exe)
        merged = self.merge(llvm_profdata)
        after = self.compute(sdk_head)
        if after != before:
            raise Stop(f"the worktree's game tree changed during the recipe ({before.splitlines()[0]} -> {after.splitlines()[0]})")
        pgo = os.path.join(self.wt, "pgo")
        os.makedirs(pgo, exist_ok=True)
        shutil.copyfile(merged, os.path.join(pgo, "doa4.profdata.tmp"))
        os.replace(os.path.join(pgo, "doa4.profdata.tmp"), os.path.join(pgo, "doa4.profdata"))
        with open(os.path.join(pgo, "source.txt.tmp"), "w", encoding="ascii", newline="\n") as f:
            f.write(after)
        os.replace(os.path.join(pgo, "source.txt.tmp"), os.path.join(pgo, "source.txt"))
        self.log(f"wrote {pgo}/doa4.profdata and source.txt ({after.splitlines()[0]}, sdk={sdk_head})")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--worktree", required=True, help="the checkout to build and to write pgo/ into")
    ap.add_argument("--sdk", required=True, help="the SDK checkout it builds against (DOA4_SDK)")
    ap.add_argument("--out", required=True, help="the recipe's folder: logs, prof/, runs/, the merged profile")
    ap.add_argument("--tag-prefix", required=True, help="the instrumented snapshot is <prefix>I")
    ap.add_argument("--toml", required=True, help="dead_or_alive_4.toml for the snapshot; must say audio_mute = true")
    ap.add_argument("--userdata", required=True, help="pristine user data (np_run.py copies it, never writes it)")
    ap.add_argument("--cache", help="shared shader cache folder for np_run.py")
    ap.add_argument("--timeout", type=float, default=1800, help="seconds per training run (np_run.py --timeout)")
    ap.add_argument("--llvm-bin", default="C:/Program Files/LLVM/bin", help="the folder with llvm-profdata.exe")
    args = ap.parse_args(argv)
    r = Recipe(args)
    try:
        r.log(f"recipe start: {' '.join(sys.argv[1:])}")
        r.run()
    except Stop as e:
        r.end(str(e))
        print(f"recipe stopped: {e}")
        return 1
    except Exception as e:  # anything unforeseen still ends the log with its reason
        r.end(f"{type(e).__name__}: {e}")
        raise
    r.end(None)
    print("recipe done")
    return 0


if __name__ == "__main__":
    sys.exit(main())

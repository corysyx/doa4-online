"""The input-lag matrix of the DOA4 Online update (plan Task 7, spec G4a): runs one display configuration with the latency
trace on (DOA4_LAG_TRACE=1, src/doa4_lagtrace.h), and turns the traces (lagtrace_<pid>.csv) and a PresentMon capture into
the per-stage breakdown.

  py -3 -I tools/np_lag.py run CONFIG --mode windowed|borderless|exclusive --kind plain|offline|pair --bin <snapshot folder>
      [--seconds 300 (plain)]
      --out <folder> [--repeats 3] [--seed 7] [--affinity MASK] [--script tools/np_scripts/single.txt]
      [--userdata DIR] [--cache DIR] [--timeout S] [--first N]
  py -3 -I tools/np_lag.py analyse --out <folder> [--presentmon <capture.csv>] [--window-start 3394] [--window-frames 3600]
  py -3 -I tools/np_lag.py check <lagtrace csv> [--window-start N --window-frames N] [--min-coverage 99]

run: one configuration per invocation, --repeats runs (named CONFIG_r1.. from --first). Each run gets a fresh copy of the
snapshot (<out>/bin_<run>[_host|_joiner]/) with a dead_or_alive_4.toml written here (audio_mute = true, the mode's keys), and
a fresh trace folder <out>/lag/<run>/ (DOA4_LAG_DIR). plain: an ordinary launch, visible, with no netplay session (no
DOA4_NP: no record/replay, no determinism layer), ended by the driver after --seconds; the title idles into the game's own
attract demo, whose rounds are the measured frames (launch_plain). offline: a record SESSION (the harness reference: the
determinism layer paces it): tools/np_run.py record --show-window with --script and --seed
(--affinity passed through). pair: tools/np_pair.py --show-host (the host is measured, in the mode; the joiner stays
minimized, windowed); --affinity is refused (np_pair limits only the joiner). Before each run: dead_or_alive_4.exe,
deadlock.exe and RocketLeague.exe must not run and UDP 7200 must be free (exit 9, nothing started); during it the process
list is sampled every 10 s, and the run is labelled "shared" if any of those games other than the run's own processes
appeared, else "clean". <out>/<run>_meta.txt holds the configuration, the toml and its SHA-256, the command and the label;
<out>/<run>_console.log the runner's output. Exclusive fullscreen has no setting yet (Task 11 adds its toml keys here).

analyse: every run with a _meta.txt in --out. Rows are the trace's frames whose np_frame (the session's frame) lies in the
window (default 3394..6993: the single.txt fight's 60 s of random input); for a plain run, the first --window-frames
frames whose fight_state is 2 (a round being fought). The points of a frame are put in their measured
order (median offset from the frame start) and every stage between consecutive points gets its median, p95 and p99 (ms),
per run and pooled, with the per-run medians and their spread (max - min). PresentMon rows (--presentmon, a capture made
with --v1_metrics --qpc_time) of the run's game process are joined to the game's presents by the present call's QPC time
(nearest within 1 ms) and give the display stage (msUntilDisplayed), msBetweenDisplayChange, PresentMode and
AllowsTearing. Writes <out>/analysis.md and prints it.

check: the instrument's own check (plan Task 7 Step 1) on one trace: the share of the window's frames whose swap was joined
to a present (>= --min-coverage %), and the count of negative stage times (must be 0). Exit 0 only if both hold.
"""
import argparse
import csv
import hashlib
import os
import shutil
import statistics
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import np_run  # noqa: E402

WINDOW_START = 3394  # single.txt: the first frame of "random 600" (the fight is on; shot f003394 shows it)
WINDOW_FRAMES = 3600  # 60 s of random fighting at 60 fps; the script quits at frame 6994
FIGHT_ROUND = 2  # src/net/np_digest.h kFightState: 2 = a round being fought (the trace's fight_state column)
BUSY_IMAGES = ("dead_or_alive_4.exe", "deadlock.exe", "RocketLeague.exe")
SAMPLE_S = 10
SNAPSHOT_FILES = ("dead_or_alive_4.exe", "rexruntime.dll", "TracyClient.dll")
# The toml keys of each display mode (the game's own settings file beside the exe). None: not available yet.
MODE_KEYS = {"windowed": {"fullscreen": "false"}, "borderless": {"fullscreen": "true"}, "exclusive": None}

# The trace's time columns (src/doa4_lagtrace.cpp CsvHeader) by point name.
POINTS = {
    "frame start": "t_frame_start_us",
    "session step done": "t_np_done_us",
    "frame function done": "t_frame_fn_done_us",
    "pad read": "t_input_us",
    "swap call": "t_swap_call_us",
    "guest VdSwap": "t_guest_swap_us",
    "swap return": "t_swap_return_us",
    "host Present call": "t_present_call_us",
    "host Present return": "t_host_present_us",
    # Task 8 (R56): the GPU command processor thread's marks of the joined present's swap packet (the last WAIT_REG_MEM
    # that had to wait before it: its entry and wake; the guest vblank before that wake; the swap packet's handler).
    # Absent in a Task 7 trace: those points are then left out of the order.
    "GPU wait entry": "t_gpu_wait_enter_us",
    "guest vblank": "t_vblank_us",
    "GPU wait wake": "t_gpu_wait_wake_us",
    "GPU swap packet": "t_gpu_swap_us",
    # A cut build (present_before_vblank_wait): a frame presented early (gpu_flags bit 0) has its swap record stamped at the
    # early IssueSwap, at the wait's entry, before the wait sleeps; read_trace moves that time to its own column.
    "GPU early IssueSwap": "t_gpu_early_swap_us",
}
DERIVED_COLUMNS = ("t_gpu_early_swap_us",)  # not in the CSV: read_trace derives them
GPU_FLAG_EARLY = 1  # gpu_flags bit 0 (sdk pace_stats.h kLagGpuSwapEarly)
DISPLAYED = "displayed"  # from PresentMon: the host Present call + msUntilDisplayed
# What lies inside each stage, as far as the code shows (a "residual" is end minus start with unnamed work inside).
STAGE_NOTES = {
    ("frame start", "session step done"): "the language step and the session's frame step (np::OnFrame; online: the wait for the peer's input)",
    ("session step done", "frame function done"): "the frame function sub_824F4498 (GPU frame-time read, frame bookkeeping)",
    ("frame function done", "pad read"): "residual: the main loop from the frame function to the first XamInputGetState",
    ("pad read", "swap call"): "residual: the game's update and draw after the pad read, up to its present call",
    ("swap call", "guest VdSwap"): "the guest D3D present path before VdSwap (sub_8275AEE0; any wait for a free front buffer)",
    ("guest VdSwap", "swap return"): "GPU back-pressure: the guest present blocks until the GPU thread has presented the "
                                     "previous swap (it ends ~6 us after host Present(N-1) returns when the det layer is off)",
    ("guest VdSwap", "host Present call"): "guest GPU command queue up to the swap packet (GPU thread), IssueSwap, the presenter's paint",
    ("swap return", "host Present call"): "residual on the GPU thread (a trace without the GPU marks): holds the 60 Hz pacing "
                                          "(the vblank-woken WAIT_REG_MEM before the swap packet), then IssueSwap and the paint",
    ("host Present call", "host Present return"): "IDXGISwapChain::Present (sync interval 0, RESTART; the flip-model queue)",
    ("swap return", "GPU wait entry"): "GPU thread: the frame's commands up to the wait before its swap packet (it reaches them "
                                       "after presenting the previous swap)",
    ("GPU wait entry", "guest vblank"): "GPU thread: the WAIT_REG_MEM before the swap packet, asleep until the guest vblank "
                                        "(command_processor.cpp ExecutePacketType3_WAIT_REG_MEM)",
    ("guest vblank", "GPU wait wake"): "the vblank interrupt (MarkVblank) and the wake of the wait (WakeRegisterWaits)",
    ("GPU wait wake", "GPU swap packet"): "GPU thread: the packets after the wait up to the swap packet (gpu_after_wait_packets)",
    ("GPU swap packet", "host Present call"): "GPU thread: IssueSwap (the swap texture) and the presenter's paint",
    ("GPU wait entry", "GPU early IssueSwap"): "GPU thread: the shape check of the packets after the wait (early frames)",
    ("GPU early IssueSwap", "host Present call"): "GPU thread: the early IssueSwap (the swap texture) and the paint, run at "
                                                  "the wait's entry before it sleeps (present_before_vblank_wait)",
    ("host Present return", DISPLAYED): "the swap chain to the display (PresentMon msUntilDisplayed from the call, minus the call)",
}
# Notes that differ when most of a configuration's frames were presented early (a cut build).
EARLY_STAGE_NOTES = {
    ("guest VdSwap", "swap return"): "GPU back-pressure: the guest present blocks until the GPU thread has run the PREVIOUS "
                                     "swap's packet, after that swap's vblank wait (its host present came earlier, at the "
                                     "wait's entry: present_before_vblank_wait)",
    ("swap return", "GPU wait entry"): "GPU thread: the frame's commands up to the wait before its swap packet (it reaches "
                                       "them after the previous swap's packet, at the vblank)",
}
GPU_WORK = ("host Present return (previous frame)", "GPU wait entry")  # the GPU thread's own work on a frame (Task 8)
GPU_WORK_NOTE = ("GPU thread: from presenting the previous swap to the wait before this frame's swap packet (its commands "
                 "for this frame; consecutive frames whose previous present was not early only)")
LOOP_TAIL = ("swap return", "next frame start")  # the rest of the loop pass; holds the 60 Hz pacing wait if it is there
LOOP_NOTE = "residual: the main loop after the guest present until the next pass starts (any frame pacing wait)"
# Constraints a frame's points must meet (later - earlier >= 0); a violation is a negative stage time.
ORDER_CHECKS = [("frame start", "session step done"), ("session step done", "frame function done"),
                ("frame start", "pad read"), ("frame start", "swap call"), ("swap call", "guest VdSwap"),
                ("guest VdSwap", "swap return"), ("pad read", "guest VdSwap"), ("guest VdSwap", "host Present call"),
                ("host Present call", "host Present return"), ("GPU wait entry", "GPU wait wake"),
                ("GPU wait wake", "GPU swap packet"), ("GPU wait entry", "GPU early IssueSwap"),
                ("GPU early IssueSwap", "host Present call")]


# ---- statistics -----------------------------------------------------------------------------------------------------

def percentile(values, q):
    """The q-th percentile (0..100) of `values` by linear interpolation between the closest ranks (numpy's default)."""
    v = sorted(values)
    if not v:
        return None
    pos = (len(v) - 1) * q / 100.0
    lo = int(pos)
    hi = min(lo + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (pos - lo)


def summary(values):
    """{'n', 'median', 'p95', 'p99'} of `values` (None for an empty list)."""
    if not values:
        return None
    return {"n": len(values), "median": percentile(values, 50), "p95": percentile(values, 95), "p99": percentile(values, 99)}


def spread(per_run_medians):
    """max - min of the runs' medians (0 for one run; None for none)."""
    vals = [m for m in per_run_medians if m is not None]
    return (max(vals) - min(vals)) if vals else None


# ---- the trace ------------------------------------------------------------------------------------------------------

def read_trace(path):
    """The trace's rows as dicts of int (empty fields: None)."""
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            row = {k: (int(v) if v not in ("", None) else None) for k, v in r.items()}
            # an early frame's swap time is the early IssueSwap (at the wait's entry), not the swap packet
            row["t_gpu_early_swap_us"] = None
            if (row.get("gpu_flags") or 0) & GPU_FLAG_EARLY:
                row["t_gpu_early_swap_us"], row["t_gpu_swap_us"] = row.get("t_gpu_swap_us"), None
            rows.append(row)
    return rows


def early_frames(rows):
    """How many of `rows` were presented early (gpu_flags bit 0)."""
    return sum(1 for r in rows if (r.get("gpu_flags") or 0) & GPU_FLAG_EARLY)


def window_rows(rows, start=WINDOW_START, frames=WINDOW_FRAMES, fight=False):
    """The measured rows. Default: np_frame start..start+frames-1 (a session's script frames). fight=True (a plain launch,
    no session, so no np_frame): the first `frames` rows whose fight_state is FIGHT_ROUND (a round being fought)."""
    if fight:
        return [r for r in rows if r.get("fight_state") == FIGHT_ROUND][:frames]
    return [r for r in rows if r["np_frame"] is not None and start <= r["np_frame"] < start + frames]


def point(row, name):
    if name == DISPLAYED:
        return row.get("displayed_us")
    return row.get(POINTS[name])


def check_trace(rows):
    """(frames, with a swap, joined, negative stage times [(frame, earlier, later, ms)]) over `rows`."""
    with_swap = sum(1 for r in rows if r["guest_swap_index"])
    joined = sum(1 for r in rows if r["guest_swap_index"] and r["t_host_present_us"] is not None)
    negatives = []
    for r in rows:
        for a, b in ORDER_CHECKS:
            ta, tb = point(r, a), point(r, b)
            if ta is not None and tb is not None and tb < ta:
                negatives.append((r["frame"], a, b, (tb - ta) / 1000.0))
    return len(rows), with_swap, joined, negatives


def measured_order(rows, names):
    """`names` sorted by their median offset from the frame start over `rows` (points missing in every row are left out)."""
    offs = {}
    for n in names:
        vals = [point(r, n) - r["t_frame_start_us"] for r in rows if point(r, n) is not None]
        if vals:
            offs[n] = statistics.median(vals)
    return sorted(offs, key=lambda n: offs[n]), offs


def stage_values(rows, a, b):
    """Stage a -> b in ms for every row that has both points."""
    out = []
    for r in rows:
        ta, tb = point(r, a), point(r, b)
        if ta is not None and tb is not None:
            out.append((tb - ta) / 1000.0)
    return out


def loop_tail(rows):
    """swap return -> the next row's frame start, in ms (consecutive frames only)."""
    out = []
    for r, nxt in zip(rows, rows[1:]):
        if nxt["frame"] == r["frame"] + 1 and r["t_swap_return_us"] is not None:
            out.append((nxt["t_frame_start_us"] - r["t_swap_return_us"]) / 1000.0)
    return out


def gpu_work(rows):
    """The previous row's host Present return -> this row's GPU wait entry, in ms (consecutive frames with both points):
    the GPU thread's own work on a frame (Task 8, R56). Not over a previous frame presented early: its present came
    before its vblank wait, so the span would hold that sleep (on a cut build the frame's own work is the stage
    "swap return -> GPU wait entry")."""
    out = []
    for prev, r in zip(rows, rows[1:]):
        if (r["frame"] == prev["frame"] + 1 and prev.get("t_host_present_us") is not None and
                r.get("t_gpu_wait_enter_us") is not None and not (prev.get("gpu_flags") or 0) & GPU_FLAG_EARLY):
            out.append((r["t_gpu_wait_enter_us"] - prev["t_host_present_us"]) / 1000.0)
    return out


def fps(rows):
    """1e6 / the mean interval between consecutive frames' starts (rows that follow each other in the trace only, so a
    window made of several fights does not count the gaps between them)."""
    d = [b["t_frame_start_us"] - a["t_frame_start_us"] for a, b in zip(rows, rows[1:]) if b["frame"] == a["frame"] + 1]
    return 1e6 * len(d) / sum(d) if d and sum(d) > 0 else None


# ---- PresentMon -----------------------------------------------------------------------------------------------------

def _col(header, *names):
    low = {h.lower(): h for h in header}
    for n in names:
        if n.lower() in low:
            return low[n.lower()]
    return None


def read_presentmon(path, pid, qpc_frequency):
    """The capture's rows of process `pid`: dicts with t_us (the Present call on the QPC clock, None if the capture has no
    QPC column), until_displayed_ms, between_display_ms, mode, tearing, dropped. Raises ValueError on a capture without
    the columns this needs."""
    with open(path, newline="", encoding="utf-8-sig", errors="replace") as f:
        rd = csv.DictReader(f)
        header = rd.fieldnames or []
        c_pid = _col(header, "ProcessID")
        c_qpc = _col(header, "QPCTime", "TimeInQPC")
        c_until = _col(header, "msUntilDisplayed", "MsUntilDisplayed")
        c_between = _col(header, "msBetweenDisplayChange", "MsBetweenDisplayChange")
        c_mode = _col(header, "PresentMode")
        c_tear = _col(header, "AllowsTearing")
        c_drop = _col(header, "Dropped")
        if not (c_pid and c_mode and c_between):
            raise ValueError(f"{path}: no ProcessID / PresentMode / msBetweenDisplayChange column in {header}")
        out = []
        for r in rd:
            if r.get(c_pid) != str(pid):
                continue

            def num(c):
                try:
                    return float(r[c]) if c and r.get(c) not in (None, "", "NA") else None
                except ValueError:
                    return None
            t = num(c_qpc)
            out.append({"t_us": (t * 1e6 / qpc_frequency) if t is not None and qpc_frequency else None,
                        "until_displayed_ms": num(c_until), "between_display_ms": num(c_between),
                        "mode": r.get(c_mode), "tearing": r.get(c_tear) if c_tear else None,
                        "dropped": r.get(c_drop) if c_drop else None})
    return out


def join_presentmon(rows, pm, tolerance_us=1000):
    """Joins each trace row's host Present call (t_present_call_us) to the nearest PresentMon row in time (|dt| <=
    tolerance_us) and sets row['displayed_us'] = Present call + msUntilDisplayed when that row was displayed. Both lists
    are in time order. Returns (rows joined, [|dt| in us])."""
    pm_t = [p for p in pm if p["t_us"] is not None]
    pm_t.sort(key=lambda p: p["t_us"])
    j = 0
    joined = 0
    dts = []
    for r in rows:
        t = r.get("t_present_call_us")
        r["displayed_us"] = None
        if t is None or not pm_t:
            continue
        while j + 1 < len(pm_t) and abs(pm_t[j + 1]["t_us"] - t) <= abs(pm_t[j]["t_us"] - t):
            j += 1
        dt = abs(pm_t[j]["t_us"] - t)
        if dt > tolerance_us:
            continue
        joined += 1
        dts.append(dt)
        ud = pm_t[j]["until_displayed_ms"]
        if ud is not None and ud > 0:
            r["displayed_us"] = int(round(t + ud * 1000.0))
    return joined, dts


# ---- run ------------------------------------------------------------------------------------------------------------

def tasklist_pids():
    """{image (lower case): set of pids} for BUSY_IMAGES."""
    o = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True, text=True, creationflags=0x08000000).stdout
    seen = {i.lower(): set() for i in BUSY_IMAGES}
    for line in o.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) >= 2 and parts[0].strip('"').lower() in seen:
            try:
                seen[parts[0].strip('"').lower()].add(int(parts[1]))
            except ValueError:
                pass
    return seen


def udp7200_lines():
    o = subprocess.run(["netstat", "-ano", "-p", "UDP"], capture_output=True, text=True, creationflags=0x08000000).stdout
    return [l.strip() for l in o.splitlines() if ":7200 " in l]


class Sampler(threading.Thread):
    def __init__(self):
        super().__init__(daemon=True)
        self.stop = threading.Event()
        self.samples = []  # (unix s, {image: pids})

    def run(self):
        while True:
            self.samples.append((time.time(), tasklist_pids()))
            if self.stop.wait(SAMPLE_S):
                return


def process_affinity(pid):
    """The live process mask of `pid` (GetProcessAffinityMask: a job object's affinity limit shows in it), or None if
    the process cannot be opened or read."""
    import ctypes
    from ctypes import wintypes
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.restype = wintypes.HANDLE
    k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    k32.GetProcessAffinityMask.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    h = k32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not h:
        return None
    try:
        proc, system = ctypes.c_size_t(0), ctypes.c_size_t(0)
        if not k32.GetProcessAffinityMask(h, ctypes.byref(proc), ctypes.byref(system)):
            return None
        return proc.value
    finally:
        k32.CloseHandle(h)


class AffinityReadback(threading.Thread):
    """R42 (#351, #352): reads the LIVE game's CPU mask back after its start-up and writes it to `out`. Waits for the
    game's pid in <run>/pid.txt (written by the runner once the game started), then `delay` seconds, then reads the
    process mask and writes one line: the pid, how long after the pid appeared it was read, the mask read, the mask
    requested and whether they match. Stopped before a pid appeared, it writes that there was no read-back."""

    def __init__(self, run_dir, out, requested, delay=45.0):
        super().__init__(daemon=True)
        self.run_dir, self.out, self.requested, self.delay = run_dir, out, requested, delay
        self.stop = threading.Event()

    def _write(self, line):
        with open(self.out, "w", newline="\n") as f:
            f.write(line + "\n")

    def run(self):
        pid_file = os.path.join(self.run_dir, "pid.txt")
        while not os.path.exists(pid_file):
            if self.stop.wait(0.2):
                self._write(f"no read-back: the game's pid.txt never appeared in {self.run_dir} (requested {self.requested})")
                return
        t0 = time.time()
        try:
            pid = int(open(pid_file).read().strip())
        except (OSError, ValueError) as e:
            self._write(f"no read-back: pid.txt unreadable ({e}) (requested {self.requested})")
            return
        if self.stop.wait(self.delay):
            self._write(f"no read-back: the run ended within {self.delay:g} s of the pid (pid {pid}, requested {self.requested})")
            return
        mask = process_affinity(pid)
        after = time.time() - t0
        try:
            want = int(self.requested, 0)
        except ValueError:
            want = None
        got = "unreadable" if mask is None else f"0x{mask:X}"
        self._write(f"pid {pid}: read {after:.1f} s after the pid appeared ({time.strftime('%H:%M:%S')}): process affinity mask "
                    f"{got} (requested {self.requested}; match {'yes' if mask is not None and mask == want else 'no'})")


def own_pids(run_dirs):
    """The pids a run's tools started or recorded: pid.txt and ctl/pids.txt of each run folder."""
    pids = set()
    for d in run_dirs:
        p = os.path.join(d, "pid.txt")
        if os.path.exists(p):
            pids.add(int(open(p).read().strip()))
        p = os.path.join(d, "ctl", "pids.txt")
        if os.path.exists(p):
            for line in open(p).read().splitlines():
                parts = line.split()
                if len(parts) >= 2 and parts[0] == "pid":
                    pids.add(int(parts[1]))
    return pids


def label_run(samples, ours):
    """('clean' | 'shared', [what was seen]) from the process samples and the run's own pids."""
    foreign = []
    for t, seen in samples:
        for image, pids in seen.items():
            other = pids - ours if image == "dead_or_alive_4.exe" else pids
            for pid in sorted(other):
                foreign.append(f"{image} pid {pid} at {time.strftime('%H:%M:%S', time.localtime(t))}")
    return ("shared" if foreign else "clean"), foreign


def make_bin(src, dest, keys):
    if os.path.exists(dest):
        sys.exit(f"bin folder exists: {dest}")
    os.makedirs(dest)
    for f in SNAPSHOT_FILES:
        shutil.copy2(os.path.join(src, f), os.path.join(dest, f))
    text = "audio_mute = true\n" + "".join(f"{k} = {v}\n" for k, v in keys.items())
    path = os.path.join(dest, "dead_or_alive_4.toml")
    with open(path, "w", newline="\n") as f:
        f.write(text)
    return text, hashlib.sha256(open(path, "rb").read()).hexdigest()


def session_env(lag, fix=None):
    """The KEY=VALUE list a traced session run gets: the trace on into `lag`, and DOA4_NP_FIX=`fix` when given."""
    env = ["DOA4_LAG_TRACE=1", f"DOA4_LAG_DIR={lag}"]
    if fix is not None:
        env.append(f"DOA4_NP_FIX={fix}")
    return env


def launch_plain(exe, run, extra_env, seconds, userdata, cache, affinity=None, show=True):
    """An ordinary launch (no netplay session: no DOA4_NP, so no record/replay and no determinism layer): a fresh copy of
    `userdata` in <run>/ud, the game log in <run>/game.log, command.txt and pid.txt as np_run writes them. The game's
    environment is this one without its DOA4_ variables, plus SDL_PADS (no real controller) and `extra_env`. show: the
    window state the game asks for (else minimized without activation). The game runs `seconds`, then this ends it
    (TerminateProcess: its shutdown code does not run). Returns (exit code, how it ended)."""
    if os.path.exists(run):
        sys.exit(f"run folder exists: {run}")
    ud = os.path.join(run, "ud")
    shutil.copytree(userdata, ud)
    env = {k: v for k, v in os.environ.items() if not k.startswith("DOA4_")}
    env.setdefault(*np_run.SDL_PADS)
    env.update(extra_env)
    args = [exe, f"--game_data_root={np_run.GAME_DIR}", f"--user_data_root={ud}", f"--log_file={os.path.join(run, 'game.log')}"]
    if cache:
        os.makedirs(cache, exist_ok=True)
        args.append(f"--cache_path={cache}")
    with open(os.path.join(run, "command.txt"), "w") as f:
        f.write(" ".join(args) + "\n")
        for k in sorted(env):
            if k.startswith("DOA4_") or k == np_run.SDL_PADS[0]:
                f.write(f"{k}={env[k]}\n")
    flags = 0x00000004 if affinity else 0  # CREATE_SUSPENDED: limit the CPUs before any thread runs
    p = subprocess.Popen(args, env=env, cwd=os.path.dirname(os.path.abspath(exe)), creationflags=flags,
                         startupinfo=None if show else np_run.hidden_startupinfo(),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if affinity:
        try:
            job = np_run.limit_cpus(int(p._handle), int(affinity, 0))  # noqa: F841 (the job lives while this runs)
        except OSError as e:
            p.kill()
            sys.exit(f"affinity {affinity} refused, the game was not run: {e}")
        import ctypes
        ctypes.windll.ntdll.NtResumeProcess(ctypes.c_void_p(int(p._handle)))
        with open(os.path.join(run, "command.txt"), "a") as f:
            f.write(f"affinity {affinity}\n")
    with open(os.path.join(run, "pid.txt"), "w") as f:
        f.write(str(p.pid))
    try:
        code = p.wait(timeout=seconds)
        return code, f"the game exited by itself ({code}) before {seconds:g} s"
    except subprocess.TimeoutExpired:
        p.kill()
        return p.wait(), f"ended by the driver after {seconds:g} s (TerminateProcess)"


def cmd_run(a):
    keys = MODE_KEYS[a.mode]
    if keys is None:
        sys.exit(f"--mode {a.mode}: no setting for it yet (Task 11 adds its toml keys to MODE_KEYS)")
    if a.kind == "plain" and a.seconds <= 0:
        sys.exit("--seconds must be positive")
    if a.kind == "pair" and a.affinity:
        sys.exit("--affinity: np_pair limits only the joiner; the pair configuration is run without it")
    os.makedirs(a.out, exist_ok=True)
    runs = os.path.join(a.out, "runs")
    for i in range(a.first, a.first + a.repeats):
        name = f"{a.config}_r{i}"
        seen = tasklist_pids()
        busy = [f"{img} {sorted(p)}" for img, p in seen.items() if p]
        udp = udp7200_lines()
        print(f"{name}: busy check {busy or 'none'}; udp 7200 {udp or 'free'}", flush=True)
        if busy or udp:
            print(f"BUSY: {name} not started", flush=True)
            return 9
        lag = os.path.join(a.out, "lag", name)
        os.makedirs(lag)
        env = session_env(lag, a.fix)
        tomls = {}
        if a.kind == "plain":
            b = os.path.join(a.out, f"bin_{name}")
            tomls["game"] = make_bin(a.bin, b, keys)
            run_dirs = [os.path.join(runs, name)]
            cmd = ["launch_plain", os.path.join(b, "dead_or_alive_4.exe"), run_dirs[0], f"{a.seconds:g} s"]
        elif a.kind == "offline":
            b = os.path.join(a.out, f"bin_{name}")
            tomls["game"] = make_bin(a.bin, b, keys)
            cmd = [sys.executable, "-I", os.path.join(HERE, "np_run.py"), "record", name,
                   "--exe", os.path.join(b, "dead_or_alive_4.exe"), "--userdata", a.userdata, "--runs", runs,
                   "--cache", a.cache, "--script", a.script, "--seed", str(a.seed), "--show-window",
                   "--arg=--headless=true", "--timeout", str(a.timeout)] + [f"--env={e}" for e in env]
            if a.affinity:
                cmd += ["--affinity", a.affinity]
            run_dirs = [os.path.join(runs, name)]
        else:
            bh, bj = os.path.join(a.out, f"bin_{name}_host"), os.path.join(a.out, f"bin_{name}_joiner")
            tomls["host"] = make_bin(a.bin, bh, keys)
            tomls["joiner"] = make_bin(a.bin, bj, MODE_KEYS["windowed"])
            cmd = [sys.executable, "-I", os.path.join(HERE, "np_pair.py"), name, "--script", a.script,
                   "--userdata", a.userdata, "--runs", runs, "--exe", os.path.join(bh, "dead_or_alive_4.exe"),
                   "--joiner-exe", os.path.join(bj, "dead_or_alive_4.exe"), "--cache", a.cache, "--seed", str(a.seed),
                   "--show-host", "--timeout", str(a.timeout)] + [f"--env-host={e}" for e in env]
            run_dirs = [os.path.join(runs, f"{name}_host"), os.path.join(runs, f"{name}_joiner")]
        sampler = Sampler()
        sampler.start()
        readback = None
        if a.affinity:  # R42: the live mask, read back after start-up, beside the run (the request is in _meta.txt)
            readback = AffinityReadback(run_dirs[0], os.path.join(a.out, f"{name}_affinity.txt"), a.affinity, a.affinity_delay)
            readback.start()
        t0 = time.time()
        with open(os.path.join(a.out, f"{name}_console.log"), "wb") as f:
            f.write((" ".join(cmd) + "\n").encode())
            f.flush()
            if a.kind == "plain":
                code, how = launch_plain(cmd[1], cmd[2], {"DOA4_LAG_TRACE": "1", "DOA4_LAG_DIR": lag}, a.seconds,
                                         a.userdata, a.cache, a.affinity, show=True)
                f.write(f"{how}\n".encode())
                p = subprocess.CompletedProcess(cmd, code)
            else:
                p = subprocess.run(cmd, cwd=REPO, stdout=f, stderr=subprocess.STDOUT, creationflags=0x08000000)
            f.write(f"exit {p.returncode}\n".encode())
        sampler.stop.set()
        sampler.join()
        if readback:
            readback.stop.set()
            readback.join()
        label, foreign = label_run(sampler.samples, own_pids(run_dirs))
        with open(os.path.join(a.out, f"{name}_meta.txt"), "w", newline="\n") as f:
            f.write(f"config {a.config}\nrun {name}\nmode {a.mode}\nkind {a.kind}\nseed {a.seed}\nbin {a.bin}\n")
            f.write(f"affinity {a.affinity or 'none'}\nfix {a.fix if a.fix is not None else 'default'}\n")
            f.write(f"exit {p.returncode}\nseconds {time.time() - t0:.0f}\n")
            f.write(f"label {label}\nsamples {len(sampler.samples)}\n")
            for what in foreign:
                f.write(f"seen {what}\n")
            for side, (text, sha) in tomls.items():
                f.write(f"toml {side} sha256 {sha}: {text.strip().replace(chr(10), '; ')}\n")
            f.write("command " + " ".join(cmd) + "\n")
        print(f"{name}: exit {p.returncode}, {label} ({len(sampler.samples)} samples)", flush=True)
    return 0


# ---- analyse --------------------------------------------------------------------------------------------------------

def read_meta(path):
    meta = {"seen": [], "toml": []}
    for line in open(path).read().splitlines():
        k, _, v = line.partition(" ")
        if k in ("seen", "toml"):
            meta[k].append(v)
        else:
            meta[k] = v
    return meta


def trace_for_run(out, meta):
    """(trace path, game pid) of a run: offline the np_run game's pid, pair the host's session process (ctl/pids.txt)."""
    runs = os.path.join(out, "runs")
    if meta["kind"] in ("offline", "plain"):
        pid = int(open(os.path.join(runs, meta["run"], "pid.txt")).read().strip())
    else:
        pid = None
        for line in open(os.path.join(runs, meta["run"] + "_host", "ctl", "pids.txt")).read().splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[2] == "session":
                pid = int(parts[1])
        if pid is None:
            return None, None
    return os.path.join(out, "lag", meta["run"], f"lagtrace_{pid}.csv"), pid


def qpc_frequency(game_log):
    """The QPC frequency from the trace's start line in game.log (None if absent)."""
    if not game_log or not os.path.exists(game_log):
        return None
    for line in open(game_log, encoding="utf-8", errors="replace"):
        at = line.find("(QPC frequency ")
        if at >= 0 and "lagtrace on:" in line:
            return int(line[at + len("(QPC frequency "):].split()[0])
    return None


def fmt(x, nd=2):
    return "-" if x is None else f"{x:.{nd}f}"


def analyse_run(out, meta, presentmon, start, frames):
    path, pid = trace_for_run(out, meta)
    res = {"meta": meta, "trace": path, "pid": pid}
    if not path or not os.path.exists(path):
        res["error"] = f"no trace ({path})"
        return res
    all_rows = read_trace(path)
    rows = window_rows(all_rows, start, frames, fight=meta["kind"] == "plain")
    res["rows"] = rows
    res["fight_rows"] = sum(1 for r in rows if r.get("fight_state") == FIGHT_ROUND)
    res["fight_total"] = sum(1 for r in all_rows if r.get("fight_state") == FIGHT_ROUND)
    res["has_fight"] = any(r.get("fight_state") is not None for r in all_rows)
    n, ws, joined, neg = check_trace(rows)
    res.update(frames=n, with_swap=ws, joined=joined, negatives=neg, fps=fps(rows), early=early_frames(rows))
    res["pad_main"] = statistics.median([r["pad_calls_main"] for r in rows]) if rows else None
    res["pad_other"] = sum(r["pad_calls_other"] for r in rows)
    res["swaps_other"] = sum(r["swap_calls_other"] for r in rows)
    res["swap_main_hist"] = {}
    for r in rows:
        res["swap_main_hist"][r["swap_calls_main"]] = res["swap_main_hist"].get(r["swap_calls_main"], 0) + 1
    run_dir = os.path.join(out, "runs", meta["run"] if meta["kind"] in ("offline", "plain") else meta["run"] + "_host")
    game_log = os.path.join(run_dir, "game.log")
    res["game_log"] = game_log
    res["pm"] = None
    if presentmon:
        freq = qpc_frequency(game_log)
        try:
            pm = read_presentmon(presentmon, pid, freq)
        except ValueError as e:
            res["pm_error"] = str(e)
            pm = []
        if pm and rows:  # a run with no window frames (a plain launch that never reached a round) gets no PresentMon join
            t0, t1 = rows[0]["t_frame_start_us"], rows[-1]["t_frame_start_us"]
            pm_win = [p for p in pm if p["t_us"] is None or t0 <= p["t_us"] <= t1 + 50000]
            pj, dts = join_presentmon(rows, pm_win)
            modes = {}
            for p in pm_win:
                modes[p["mode"]] = modes.get(p["mode"], 0) + 1
            tear = {}
            for p in pm_win:
                tear[p["tearing"]] = tear.get(p["tearing"], 0) + 1
            res["pm"] = {"rows": len(pm), "window_rows": len(pm_win), "joined": pj, "dt_median_us": statistics.median(dts) if dts else None,
                         "modes": modes, "tearing": tear,
                         "between_display": summary([p["between_display_ms"] for p in pm_win if p["between_display_ms"]]),
                         "until_displayed": summary([p["until_displayed_ms"] for p in pm_win if p["until_displayed_ms"]]),
                         "freq": freq}
    return res


def analyse(out, presentmon, start, frames):
    metas = [read_meta(os.path.join(out, f)) for f in sorted(os.listdir(out)) if f.endswith("_meta.txt")]
    results = [analyse_run(out, m, presentmon, start, frames) for m in metas]
    lines = [f"# Lag analysis: {out}", "",
             f"Window: session runs (offline, pair) np_frame {start}..{start + frames - 1} ({frames} frames); plain runs the "
             f"first {frames} frames with fight_state {FIGHT_ROUND}. Times in ms. PresentMon: {presentmon or 'none'}.", ""]
    configs = {}
    for r in results:
        configs.setdefault(r["meta"]["config"], []).append(r)
    for config, rs in configs.items():
        good = [r for r in rs if "error" not in r and r["meta"].get("label") == "clean"]
        lines += [f"## {config}", ""]
        for r in rs:
            m = r["meta"]
            if "error" in r:
                lines.append(f"- {m['run']}: {r['error']}")
                continue
            cov = 100.0 * r["joined"] / r["frames"] if r["frames"] else 0.0
            lines.append(f"- {m['run']}: {m['mode']} {m['kind']}, pid {r['pid']}, {m.get('label')}, exit {m.get('exit')}, "
                         f"{r['frames']} frames, fps {fmt(r['fps'])}, join {r['joined']}/{r['frames']} ({cov:.2f}%), "
                         + (f"in a round (fight_state {FIGHT_ROUND}) {r['fight_rows']} of the window's frames, {r['fight_total']} in "
                            f"the whole trace, " if r["has_fight"] else "no fight_state column, ") +
                         f"negative stage times {len(r['negatives'])}, pad reads per frame (main, median) {r['pad_main']}, "
                         f"pad reads on other threads {r['pad_other']}, guest swaps per frame (main) {r['swap_main_hist']}, "
                         f"swap calls on other threads {r['swaps_other']}, early-presented {r['early']} of {r['frames']} frames "
                         f"(gpu_flags bit 0, present_before_vblank_wait)")
            pm = r.get("pm")
            if pm:
                lines.append(f"  - PresentMon: {pm['window_rows']} rows of pid {r['pid']} in the window ({pm['rows']} in all), "
                             f"PresentMode {pm['modes']}, AllowsTearing {pm['tearing']}, joined {pm['joined']} "
                             f"(median |dt| {fmt(pm['dt_median_us'], 1)} us), msBetweenDisplayChange "
                             f"{_s(pm['between_display'])}, msUntilDisplayed {_s(pm['until_displayed'])}")
            elif r.get("pm_error"):
                lines.append(f"  - PresentMon: {r['pm_error']}")
        if not good:
            lines += ["", "No clean run with a trace.", ""]
            continue
        # The game-side points are pooled over the runs; the display point is not (R53): a run's display stage depends on
        # its presentation mode, so every row that ends at the screen is given per majority PresentMode.
        order, offs = measured_order([x for g in good for x in g["rows"]], list(POINTS))
        by_mode = {}
        for g in good:
            if any(x.get("displayed_us") for x in g["rows"]):
                by_mode.setdefault(majority_mode(g), []).append(g)
        lines += ["", "Measured order (median offset from the frame start, ms): " +
                  ", ".join(f"{n} {offs[n] / 1000.0:.2f}" for n in order) +
                  "".join(f"; displayed [{mode}] {measured_order([x for g in gs for x in g['rows']], [DISPLAYED])[1][DISPLAYED] / 1000.0:.2f}"
                          f" ({', '.join(g['meta']['run'] for g in gs)})" for mode, gs in by_mode.items()), ""]
        stages = list(zip(order, order[1:]))
        lines += ["| stage | median | p95 | p99 | per-run medians | spread | inside |", "|---|---|---|---|---|---|---|"]

        def row_for(label, per_run_vals, note):
            pooled = summary([v for vals in per_run_vals for v in vals])
            meds = [percentile(vals, 50) if vals else None for vals in per_run_vals]
            if pooled is None:
                return f"| {label} | - | - | - | - | - | {note} |"
            return (f"| {label} | {fmt(pooled['median'])} | {fmt(pooled['p95'])} | {fmt(pooled['p99'])} | "
                    f"{' / '.join(fmt(x) for x in meds)} | {fmt(spread(meds))} | {note} |")
        mostly_early = sum(g["early"] for g in good) * 2 > sum(g["frames"] for g in good)
        for a, b in stages:
            note = (EARLY_STAGE_NOTES.get((a, b)) if mostly_early else None) or STAGE_NOTES.get(
                (a, b), "residual: end minus start, the work between is not named by the trace")
            lines.append(row_for(f"{a} -> {b}", [stage_values(g["rows"], a, b) for g in good], note))
        lines.append(row_for(f"{LOOP_TAIL[0]} -> {LOOP_TAIL[1]}", [loop_tail(g["rows"]) for g in good], LOOP_NOTE))
        if any(gpu_work(g["rows"]) for g in good):
            lines.append(row_for(f"{GPU_WORK[0]} -> {GPU_WORK[1]}", [gpu_work(g["rows"]) for g in good], GPU_WORK_NOTE))
        for a, b, label in (("pad read", "host Present return", "TOTAL pad read -> host Present return"),
                            ("frame start", "host Present return", "frame start -> host Present return")):
            lines.append(row_for(label, [stage_values(g["rows"], a, b) for g in good], "the sum along the path"))
        for mode, gs in by_mode.items():
            lines.append(row_for(f"host Present return -> displayed [{mode}]",
                                 [stage_values(g["rows"], "host Present return", DISPLAYED) for g in gs],
                                 STAGE_NOTES[("host Present return", DISPLAYED)] + f"; runs {', '.join(g['meta']['run'] for g in gs)}"))
            lines.append(row_for(f"TOTAL pad read -> displayed [{mode}]", [stage_values(g["rows"], "pad read", DISPLAYED) for g in gs],
                                 "the sum along the path, runs of this presentation mode only"))
        lines.append("")
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, "analysis.md"), "w", newline="\n") as f:
        f.write(text)
    print(text)
    return 0


def majority_mode(res):
    """The PresentMode of most of a run's PresentMon rows in its window ("no PresentMon" without a capture)."""
    modes = (res.get("pm") or {}).get("modes") or {}
    return max(modes, key=modes.get) if modes else "no PresentMon"


def _s(s):
    return "-" if not s else f"median {fmt(s['median'])} p95 {fmt(s['p95'])} p99 {fmt(s['p99'])} (n {s['n']})"


def cmd_check(a):
    rows = window_rows(read_trace(a.csv), a.window_start, a.window_frames, fight=a.fight)
    n, ws, joined, neg = check_trace(rows)
    cov = 100.0 * joined / n if n else 0.0
    where = (f"window: the first {a.window_frames} frames with fight_state {FIGHT_ROUND}" if a.fight else
             f"window np_frame {a.window_start}..{a.window_start + a.window_frames - 1}")
    print(f"{a.csv}: {where}: {n} frames, "
          f"{ws} with a guest swap, {joined} joined to a host present ({cov:.2f}% of the frames); "
          f"negative stage times {len(neg)}")
    for f, x, y, ms in neg[:10]:
        print(f"  negative: frame {f}: {x} -> {y} = {ms:.3f} ms")
    problems = []
    if n == 0:
        problems.append("no frame in the window")
    if cov < a.min_coverage:
        problems.append(f"join coverage {cov:.2f}% < {a.min_coverage}%")
    if neg:
        problems.append(f"{len(neg)} negative stage times")
    print("check " + ("ok" if not problems else "FAILED: " + "; ".join(problems)))
    return 0 if not problems else 1


def main(argv=None):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("config")
    r.add_argument("--mode", required=True, choices=list(MODE_KEYS))
    r.add_argument("--kind", required=True, choices=["plain", "offline", "pair"])
    r.add_argument("--seconds", type=float, default=300, help="plain: how long the game runs before the driver ends it")
    r.add_argument("--bin", required=True, help="the snapshot folder (np_bin/<tag>: exe, rexruntime.dll, TracyClient.dll)")
    r.add_argument("--out", required=True)
    r.add_argument("--repeats", type=int, default=3)
    r.add_argument("--first", type=int, default=1, help="the first repeat's number (a re-run continues the count)")
    r.add_argument("--seed", type=int, default=7)
    r.add_argument("--fix", help="offline/pair: the session's DOA4_NP_FIX (e.g. none: the determinism layer off, so "
                                 "the record session keeps the script input but not the layer's pacing)")
    r.add_argument("--affinity", help="logical CPU mask passed to np_run.py --affinity (offline only); the live game's mask "
                                      "is read back --affinity-delay s after its pid appears into <out>/<run>_affinity.txt")
    r.add_argument("--affinity-delay", type=float, default=45.0, help="seconds after the game's start for the mask read-back")
    r.add_argument("--script", default=os.path.join(HERE, "np_scripts", "single.txt"))
    r.add_argument("--userdata", default=os.path.join(np_run.DOA4_ROOT, "np_phase0", "ud_pristine"))
    r.add_argument("--cache", default=os.path.join(np_run.DOA4_ROOT, "ou_runs", "cache"))
    r.add_argument("--timeout", type=float, default=600)
    an = sub.add_parser("analyse")
    an.add_argument("--out", required=True)
    an.add_argument("--presentmon")
    an.add_argument("--window-start", type=int, default=WINDOW_START)
    an.add_argument("--window-frames", type=int, default=WINDOW_FRAMES)
    c = sub.add_parser("check")
    c.add_argument("csv")
    c.add_argument("--window-start", type=int, default=WINDOW_START)
    c.add_argument("--window-frames", type=int, default=WINDOW_FRAMES)
    c.add_argument("--min-coverage", type=float, default=99.0)
    c.add_argument("--fight", action="store_true", help="window: the first --window-frames frames of a round (plain launch)")
    a = ap.parse_args(argv)
    if a.cmd == "run":
        return cmd_run(a)
    if a.cmd == "analyse":
        return analyse(a.out, a.presentmon, a.window_start, a.window_frames)
    return cmd_check(a)


if __name__ == "__main__":
    sys.exit(main())

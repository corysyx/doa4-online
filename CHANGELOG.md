# Changelog

## 0.3.117 (2026-11-12)

corysyx rollback research build (otherwise the same as 0.3.110).

### Changed
- SnapTest puts the audio lockstep's counters back with a snapshot. The guest audio callback (the sound library's
  render step) takes its time from the SDK's callback number, which a restore did not take back: every replayed
  callback saw a time 25 callbacks (8 frames) later than the first time. Its time is now the callback number less the
  callbacks rewound so far, and the 1/60 s units granted go back too. Round 13: with the middleware threads held in
  their system calls (all 8, at the same place at the save and the load) the frame 4096 crash and the differences from
  the first replayed frame stayed the same. `DOA4_NP_SNAPTEST_AUDIO=leave` keeps the old behaviour.
- SnapTest lists the SDK's host threads once (some run guest code: the audio callbacks, the GPU interrupt).
- SnapTest round 14 runner: frame 3600, frame 4096, and frame 3600 with the audio counters left (for comparison).

## 0.3.116 (2026-11-12)

corysyx rollback research build (otherwise the same as 0.3.110).

### Changed
- SnapTest holds the game's other guest threads (its middleware threads: all but the main thread and the job workers)
  during a save and a load, each once it waits in a system call, and saves and restores their stacks and PPC context
  with the snapshot (only when a thread is held at the same place at the load). Round 12: with every committed page
  in the snapshot (`full`) and with physical memory put back, the frame 4096 crash stayed the same, while words kept
  changing during the save's settle: those threads run on through a rewind. `DOA4_NP_SNAPTEST_HOLD=off` leaves them.
- The job workers' PPC context goes back with their stacks.
- SnapTest round 13 runner: frame 3600 (every 240 frames), frame 4096, and frame 4096 with every committed page.

## 0.3.115 (2026-11-12)

corysyx rollback research build (otherwise the same as 0.3.110).

### Changed
- SnapTest copies the physical memory (0xA0000000-0xDFFFFFFF) the game's code wrote at a save and reports at the load
  how much of it changed; `DOA4_NP_SNAPTEST_PHYS=restore` also puts it back. Round 11: restoring the thread stacks did
  not stop the frame 4096 crash, and the first replayed frame already differs from the first pass.
- SnapTest round 12 runner: three runs that all rewind at frame 4096 - as before, with every committed page in the
  snapshot (`DOA4_NP_SNAPTEST_MODE=full`), and with physical memory put back.

## 0.3.114 (2026-11-12)

corysyx rollback research build (otherwise the same as 0.3.110).

### Changed
- SnapTest saves the main thread's and the 25 job workers' stacks with a snapshot and puts them back at a load
  (reporting how many words changed first). Stacks were outside the snapshot, yet a parked worker keeps locals in its
  own frames from one frame to the next, so a rewind left the worker with pointers from the future: round 10's two
  crashes were that worker freeing and locking heap objects the restored heap no longer had.
  `DOA4_NP_SNAPTEST_STACKS=leave` only reports.

## 0.3.113 (2026-11-11)

corysyx rollback research build (otherwise the same as 0.3.110).

### Changed
- SnapTest: the kernel's objects (events, semaphores, threads) live outside guest memory, so a snapshot cannot hold
  them. A load now reports which were made, which are gone and which events were set or reset since the save, puts the
  events back to their state at the save, and clears the handle stashes in the restored memory that name an object
  made or gone since then (the game then makes a fresh one at the next use). `DOA4_NP_SNAPTEST_KOBJ=leave` only reports.
- `crash.txt` also shows the memory the registers point at.
- SnapTest round 10: the runner logs the kernel's lazily made objects (`REX_LOG_LAZY_OBJECTS=1`).

## 0.3.112 (2026-11-11)

corysyx rollback research build (otherwise the same as 0.3.110).

### Fixed
- `crash.txt` is written again: the 0.3.111 report died at its first unreadable guest word (this build's compiler
  does not catch a hardware fault inside `__try`), so every guest read is now checked first. The report is written
  in parts from a thread of its own, so a stack overflow or a fault part way still leaves what came before.

## 0.3.111 (2026-11-11)

corysyx rollback research build (otherwise the same as 0.3.110).

### Changed
- A crash now also writes `crash.txt` (in the session output folder, or beside the game) with the game's registers
  and call chain, written directly so it survives when the normal log does not.
- SnapTest round 8: both runs rewind only the graphics device's render state; the second starts at frame 4096.

## 0.3.110 (2026-11-11)

corysyx build: dwreckgg's 0.3.1 and 0.3.2 merged into this fork's lobby, quality-of-life and rollback work.

### New (from dwreckgg 0.3.1 and 0.3.2)
- Much lower CPU use: the file loader and the main thread's wait for the graphics now sleep instead of spinning.
- The F7 FPS counter no longer makes the game send extra frames to the screen.
- Display mode settings in the Game tab (windowed, borderless, exclusive fullscreen), VSync and output size.
- The game's text and subtitle languages are remembered.
- A notice at start-up when a newer version is out (this build checks corysyx/doa4-online).

### This fork
- DOA4 Lobby launcher support: host and join automatically from the lobby (`netplay_autohost`, `netplay_autojoin`).
- Skip the intro is on by default (it now uses dwreckgg's Game tab setting).
- Rollback research builds: the one-PC snapshot test (`DOA4-SnapTest.zip`); not used in normal play.

## 0.3.2 (2026-11-11)

### Fixed
- The F7 FPS counter no longer makes the game send extra frames to the screen. With F7 on, the game showed each frame
  about 3 to 4 times (about 225 presents a second on a 144-164 Hz monitor), so other FPS overlays read 120-138 fps while
  F7 read 60, and motion could look uneven. Now every frame sent to the screen is a new one: 60 a second.
- Much lower CPU use, so the CPU runs cooler. Two of the game's threads (the file loader and the main thread's wait for
  the graphics) kept a CPU core each busy at 100% the whole time, even on the title screen. They now sleep while they wait.

### CPU use, measured (same scripted fight, same PC)
- Offline fight: about 2.7 CPU cores busy before, about 0.9 after (about a third).
- Online match (host): 1.8-2.4 cores before, about 0.7 after.
- The two threads that held a full core each now use almost none.
- Temperatures were not measured; the drop in CPU use is what lowers them.

### Unchanged (checked)
- Gameplay: the same fight recorded with the change off and on gives identical game state on every one of its 6995
  frames, and online matches stay in sync.
- Input lag: pad read to the frame handed to Windows, offline 20.5 ms before and after (online test 11.2 -> 10.9 ms).
- Frame rate and loading times.

### Notes
- If the new CPU behaviour ever causes trouble, add the line `doa4_idle_waits = false` to `dead_or_alive_4.toml`
  (in the game's folder) to go back to the old behaviour.

## 0.3.1 (2026-11-11)

### New
- Language and subtitles are saved, with a dropdown in the Esc menu's Game tab.
- Exclusive fullscreen (experimental), a VSync option (off by default), and customizable video output size.
- Shorter input lag (offline about 33 -> 20 ms).
- "Update available" notice on the title screen.
- The boot intro can be skipped.

### Fixed
- The white box behind PRESS START.

### Notes
- The first start after updating rebuilds the shader cache, so it is slower once.

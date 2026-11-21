# Changelog

## 0.3.142 (2026-11-21)

corysyx rollback research build.

- SnapTest: an address into a cloth worker's stack (the 4 workers with thread start 82370638) is hashed and compared as
  its offset from that stack's top (`DOA4_NP_SNAPTEST_WSTACKS=hash`: as it is). Rounds 43-44: with the workers in lanes
  the cloth job runs on 4 threads at once, and sub_82359578 links each item's record to list heads in its own stack
  frame (r1+128, +176, +224, +272). The record is reset before it is used again, so the link is stale between frames,
  but which worker took the item differs from run to run. That was the only new difference with lanes: the serial
  order (`DOA4_NP_SERIAL=3`) shows none, and 29 of its 30 rewinds end identical.
- Rounds 43-44, timing: the main thread's wait for the job workers in a fight frame is p50 1.75 ms / p99 2.75 ms in
  lanes, against 8.5 / 11.5 ms one at a time.
- A desync check between two machines will need the same treatment of these words.

## 0.3.141 (2026-11-20)

corysyx rollback research build.

- Merged dwreckgg/doa4-online alpha-v0.3.7 (with 0.3.5 and 0.3.6; their notes are below, under this build's
  0.3.1xx sections): the job workers run in two lanes side by side online and in an offline record (0.3.6), pads
  are read just before use (one frame less input lag, 0.3.7), the profile-guided build (0.3.7), a volume slider,
  online starting rules, batch GPU uploads, the Kyoto canal, Safari buffalo and Intel fixes.
- The worker file is dwreckgg's lanes version (with this build's `IsWorkerEntry`, which the snapshot code uses); this build's 0.3.138 per-worker timing is dropped, and the 0.3.140
  SnapTest job skip (`DOA4_NP_SNAPTEST_SKIPJOB`) sits on top of it.
- Round 42 (0.3.140): replays that left the 82370638 job out differed only in that job's own memory (the low heap's
  0x10292000 and 0x109D4000 runs, and its output at 0x100F1000, which is equal again after two frames with the job
  back), never in the rest of the game's state.

## 0.3.140 (2026-11-17)

corysyx rollback research build (otherwise the same as 0.3.139).

- Rollback: a SnapTest replay can leave the heaviest job out (`DOA4_NP_SNAPTEST_SKIPJOB=<n>`: on every replayed
  frame but the last n). Round 41's census: the 82370638 pool's worker (6 of a fight frame's 8.8 ms of job work)
  makes a million stores a frame, mostly from sub_8235D4A0, sub_82359578 and sub_82364700 under the two per-item
  functions sub_82356F90 and sub_82357438, into 107 pages of the low heap (some of which the main thread writes
  too) and 107 pages of physical memory. Reading it does not say whether the game's state needs it; leaving it out
  of the first replayed frames and comparing the end does. With the option set, those two functions return at once
  on that worker; the item is still taken off the queue.

## 0.3.139 (2026-11-16)

corysyx rollback research build (otherwise the same as 0.3.138).

- Rollback: a census of what each thread writes (`DOA4_NP_SNAPTEST_CENSUS=<frame>`). Round 40: of the 8.8 ms a
  fight frame's job workers take one at a time, the first worker of the 82370638 pool takes about 6 ms, the first
  of 8234A160 1.3 ms, two of 825689E8 0.5 ms each, and the other 21 next to nothing. Whether a replayed frame can
  leave a job out depends on what it writes, so for 4 frames from the given frame every store is counted by
  thread: the pages it wrote, by area (heaps, stacks, XEX data, physical memory), and the functions most of its
  stores came from.

## 0.3.138 (2026-11-16)

corysyx rollback research build (otherwise the same as 0.3.137).

- Rollback: the job workers' time by worker. Round 39's sampling profile: of a fight frame's 16.7 ms the main
  thread spends 8.0 ms waiting at 824F491C - for the 25 job workers, which a session runs one at a time for
  determinism (the session log's own count: 8.8 ms a frame in a fight, 1.3 ms in the menus) - 4.1 ms waiting in
  the swap, 1.4 ms in the frame hook and about 1.7 ms in its own update and draw code. So a replayed frame costs
  what the workers cost. Each worker's own work (from its token to its signal, less its wait for the mid event)
  is now summed by rank and logged every 600 frames with its thread's start function ("job workers: their own
  work").

## 0.3.137 (2026-11-15)

corysyx rollback research build (otherwise the same as 0.3.136).

- Rollback: a sampling profile of the frame (with `DOA4_FIBER_PROF=1`, off by default). Round 38: the fibers are
  not the split - the 12 task fibers take 0.5 ms a frame together, and the main fiber itself spends the rest and
  writes all 1.4 MB of D3D commands. A thread now samples the main thread's guest call chain 2000 times a second and
  counts the paths below the main loop (sub_824F41E0), logged every 1800 frames ("[SAMP]").

## 0.3.136 (2026-11-15)

corysyx rollback research build (otherwise the same as 0.3.135).

- Rollback: a profile of the frame by task (`DOA4_FIBER_PROF=1`, off by default). Round 36: the copy of the whole
  14 MB state takes 1.1 ms (the harness's 57 ms save is 42 ms of asking which pages are committed), so a rollback
  can save everything every frame. Round 37: the main thread spends 11.2 ms between the frame hook and the swap,
  and a rollback replays several frames inside one, so the replay must leave out whatever only draws. The game
  runs its frame as fibers, one per task; each switch now charges the time and the D3D command bytes since the
  last one to the fiber that ran, and the frame hook logs the totals every 600 frames ("[FIBERPROF]").

## 0.3.135 (2026-11-15)

corysyx rollback research build (otherwise the same as 0.3.134).

- SnapTest: timing. Rounds 34 and 35 ran without a crash and every rewind started identical; a save takes about
  57 ms and a load about 50 ms in the harness, against a 16.7 ms frame. Each save and load now logs where its time
  goes, step by step ("save timing", "load timing"), and a save also times the bare work a rollback's save would
  do ("save bench": the copy into a buffer that is already there, a page-by-page compare, a buffer-to-buffer
  copy, and building the list of committed runs).

## 0.3.134 (2026-11-15)

corysyx rollback research build (otherwise the same as 0.3.133).

- SnapTest: the D3D device's references on bound objects go back with the snapshot. Round 33 (0.3.133: the host
  fiber stacks work, 12 fibers go back at every load) still crashed at frames 5264 and 5946, where the bound
  shaders change: the device words beyond the render state were left live, so the replay's first set call released
  the object the first pass had bound last, its count reached 0 and it was freed while in use. The same for the
  front buffer (+13976): after an odd number of frames the swap took its reference off the wrong buffer of the
  pair (every 15-frame test). A device word there that held a game-heap pointer or null at the save and holds
  another now goes back. `DOA4_NP_SNAPTEST_DEVREFS=leave` switches it off.
- SnapTest: the sound mix page is left out of the comparison again: it differed for one boundary in the middle of
  most 8-frame tests (the sound thread's timing) and was equal again at the end.

## 0.3.133 (2026-11-15)

corysyx rollback research build (otherwise the same as 0.3.132).

- SnapTest: fixed 0.3.132's crash at the first save (round 32: all three runs, at frame 3600 and 3700). A fiber's
  host stack top was read once, but FIX 7 gives a fiber a new host fiber on another stack, so the save copied from
  the new stack up to the old one's top, across unmapped memory. The top is now read at every park. Normal play was
  not affected (nothing is copied without a snapshot).

## 0.3.132 (2026-11-14)

corysyx rollback research build (otherwise the same as 0.3.131).

- Rollback: a parked fiber's host stack goes back with the snapshot. Round 30 passed 12 of 12 twice; round 31's soak
  (54 rewinds of 8 frames, 29 of 15) crashed at the end of round 1: the round-end script fiber had moved from its
  first wait loop to its second during the first pass, and after the load it went on from the second loop's place in
  the recompiled code with the first loop's memory. Each guest fiber runs on a Win32 fiber, and where it called the
  swap lives on that host stack, which guest memory going back does not move. Now every park captures the host
  registers, a save copies each parked fiber's host stack, and after a load a fiber that parked again since puts
  its copy back when it is next switched to and continues from the park of the save. A host fiber that FIX 7
  re-made since the save is swapped back for the old one; a fiber made since is removed.
  `DOA4_NP_SNAPTEST_FIBERS=leave` switches it off.
- SnapTest: the timebase samples left out of the comparison run on to 8357A960 (they were most of round 31's
  failures); the sound mix page is compared again (round 31's third run showed no difference in it).

## 0.3.131 (2026-11-14)

corysyx rollback research build (otherwise the same as 0.3.130).

- SnapTest: the game's fiber stacks go back with the snapshot. Round 28 passed 10 of 12 and 9 of 12 (the sound mix
  page left out); round 29's store watch traced the last two differences to the fiber switch (sub_82793F80): the main
  thread runs the game's tasks as fibers on stacks from MmCreateKernelStack, which belong to no thread and so were
  not saved. `DOA4_NP_SNAPTEST_FIBERS=leave` switches it off.

## 0.3.130 (2026-11-14)

corysyx rollback research build (otherwise the same as 0.3.129).

- SnapTest: the 64 bytes of an XMA context that is free at the save go back with the snapshot too. Round 27 (first
  passes: 6 of 12 with the sound mix page left out) showed every remaining failure was a voice started after the
  save: its new context still held the first pass's data, so in the replay the game's sound code found input buffer 0
  valid, queued the packet as buffer 1 and the voice never started.

## 0.3.129 (2026-11-14)

corysyx rollback research build, merged with dwreckgg's 0.3.4 (Alpha-152 playable, the throw finisher crash, the
pause streaks: see 0.3.4 below).

- SnapTest: host threads that keep running across a load keep their own PCR and thread object too. Round 26 found
  0.3.128 left none live: the sound thread (tid 2) is the SDK's audio thread, a host thread running the game's sound
  callback, so its IRQL from the save still went back and new voices still did not start in the replay.

## 0.3.128 (2026-11-14)

corysyx rollback research build (otherwise the same as 0.3.127).

- SnapTest: the guest threads that keep running across a load (not the main thread, not held, not a worker whose
  stack goes back; the sound thread) keep their own PCR and thread object as they are. The load used to put back
  the sound thread's IRQL from the save (taken while it was inside a spin lock), so in the replay the sound code ran
  raised and new sound voices never started (DOA4_NP_SNAPTEST_THREADS=restore: put back as before).

## 0.3.127 (2026-11-14)

corysyx rollback research build (otherwise the same as 0.3.126).

- SnapTest: the SDK's handle stash in guest event headers ("REX" and the handle) is left out of the hashes and word
  lists. The load clears a stash whose handle now names another object, and the game's next use makes it again with
  a new handle, so these words differed without the game's state differing (DOA4_NP_SNAPTEST_STASHES=hash: hashed).
- SnapTest: at the end of each replay the heap page tables are compared with the end of the first pass, to show
  whether an allocation went another way (or failed) in the replay.

## 0.3.126 (2026-11-14)

corysyx rollback research build (otherwise the same as 0.3.125).

### Fixed
- SnapTest now saves the page tables of the physical memory heaps (the 0xA0000000, 0xC0000000 and 0xE0000000 views
  and the physical heap behind them) with the snapshot and puts them back at a load (DOA4_NP_SNAPTEST_HEAPS=leave:
  only compared). The SDK keeps MmAllocatePhysicalMemoryEx's allocator state in the host, so a load did not undo the
  first pass's allocations. Round 23's store watch: a sound voice started at frame 4344 got its buffer at FFBF7000 in
  the first pass and at FFBEF000 in the replay, and then played differently (the remaining after-8-frames
  differences, from frames 4344, 4840 and 5336). Each rewind also leaked what the first pass had allocated. The
  virtual heaps' changes are listed, not put back.

### Changed
- SnapTest lists the differing words at the boundary DOA4_NP_SNAPTEST_DIFFAT (1-based, default 1), and right after
  the load when that hash differs from the save's.
- SnapTest round 24 runner: lists the words at boundary 3, the one that still differs in every test now that
  boundary 1 matches (round 23: the main thread's registers put back made the first replayed frame match).

## 0.3.125 (2026-11-13)

corysyx rollback research build (otherwise the same as 0.3.124).

### Fixed
- SnapTest now saves the main thread's registers (its whole PPCContext) with the snapshot and puts them back at a load
  (DOA4_NP_SNAPTEST_REGS=leave: left as they are). Round 22: every replay's first frame differed in 35 words at
  400D72F0-400D7380, where a fiber switch (sub_82793F80) stores the VMX registers. The frame hook runs inside the
  game's loop, so the replay started with the registers of the frame the first pass ended on (last frame's camera
  values) instead of the ones it started with.

### Changed
- SnapTest's store watch can be armed in any test (DOA4_NP_SNAPTEST_WATCH_TEST, 1-based) and over all of its frames
  (DOA4_NP_SNAPTEST_WATCH_FRAMES=all). Repeated stores of the same value by the same code are folded into one line
  (xN), and up to 400 are listed.
- SnapTest round 23 runner: watches the sound voice words that differed after 8 frames from frame 4344 in both of
  round 22's runs.

## 0.3.124 (2026-11-13)

corysyx rollback research build (otherwise the same as 0.3.123).

### Fixed
- SnapTest's physical restore (DOA4_NP_SNAPTEST_PHYS=restore) no longer puts back the GPU command stream: the part
  between the device's command write pointer (+0) at the save and at the load, and the commands just behind it that the
  game has written but not yet handed to the GPU. Round 21: putting them back made the GPU run old bytes in the
  replay's first submission ("INDIRECT RINGBUFFER: Failed to execute packet") and the game wait on a fence for ever
  (the run from frame 4096 froze).

### Changed
- SnapTest leaves the words round 21's store watch traced to the wall clock or the GPU ring live and out of the
  comparison (DOA4_NP_SNAPTEST_NOISE, "off" for none): two guest worker threads' loop counts (82F6C9D0/D4), the
  timebase samples at 8357A500-8357A900 and the main thread's pointer into the command stream (8357C190).
- SnapTest round 22 runner.

## 0.3.123 (2026-11-13)

corysyx rollback research build (otherwise the same as 0.3.122).

### Fixed
- SnapTest's XMA snapshot read the wrong memory: the SDK maps the XMA context array's heap (0xE0000000, physical
  memory in 4 KB pages) 4 KB further on in the host, and the snapshot added the guest address to the membase by hand.
  0.3.119-0.3.122 saved and put back 64 unrelated bytes per context and took the buffer pointers from them (round 19:
  "0 bytes changed since the save" after 8 frames of decoding). It now goes through the SDK's own address translation.
- SnapTest's physical-memory copy (the load's report, and DOA4_NP_SNAPTEST_PHYS=restore) left out the 0xE0000000 heap;
  it now covers 0xA0000000-0xFFFFFFFF, with that heap's host offset.
- SnapTest round 20 runner.

## 0.3.122 (2026-11-13)

corysyx rollback research build (otherwise the same as 0.3.121).

### Changed
- SnapTest's XMA snapshot also copies every allocated context's two input buffers (the compressed packets the decoder
  reads). The game's streaming fills them by file reads the host does, so no write of the game's code marks them, and
  after a load the decoder read later packets at the saved offset. Round 18: the replay's first audio callback found
  one output block less than the first pass (voice positions +0x80 instead of +0x100) and a voice went to state 4.
- Fixed: the XMA snapshot took the output buffer from DWORD 8 (the work buffer); it is DWORD 7.
- The load's report says how many bytes of the context data, output buffers and input buffers had changed since the
  save before they were put back.
- SnapTest round 19 runner.

## 0.3.121 (2026-11-13)

corysyx rollback research build (otherwise the same as 0.3.120).

### Changed
- SnapTest's XMA snapshot also copies the guest side of every allocated context: its 64-byte XMA_CONTEXT_DATA (read and
  write offsets, valid bits) and its output buffer, both in physical memory and written by the SDK's decoder, so no
  write of the game's code marks them. Round 17: putting back the host state of 6 contexts alone changed nothing (the
  first replayed frame and the frame 4096 crash stayed the same).
- SnapTest round 18 runner: frame 3600, frame 4096, and frame 3600 with physical memory put back.

## 0.3.120 (2026-11-13)

corysyx build (otherwise the same as 0.3.119).

### New
- A **Shader** tab in the Esc menu: picture effects on top of the game's own image - Sharpen, Clarity, Bloom (with
  threshold, size and haze), Highlight roll-off, Exposure, Contrast, Saturation, Vibrance, Warmth, Shadow lift and
  Vignette, presets (Original, Modern Look, Vivid, Cinematic, Soft Glow), a split-screen compare and Reset to defaults.
  Sliders apply while they move and are saved when let go (`doa4_picture_fx`). The effects run as one pass over the
  game's picture before it is scaled to the window; they see only the finished picture, so there is no ambient
  occlusion, depth of field, haze or light shafts.
- `sdk_overrides/`: SDK files replaced at configure time (the D3D12 presenter's picture effects pass, `rex_post_fx_set`),
  each only over the exact SDK file it was made from.

## 0.3.119 (2026-11-13)

corysyx rollback research build, with dwreckgg's alpha-v0.3.3 merged in (Quit Game button, Paste buttons, Discord
status, larger Esc menu text, DOA ONLINE item blocked, even RANDOM costumes; SDK pin 66daf4d).

### Changed
- SnapTest puts the SDK's XMA decoder host state back with a snapshot (`src/net/rb_xma.cpp`): for all 320 contexts the
  allocation and enable flags, the carried-over packet bytes, the current frame's decoded samples, the subframes still
  to write out and the loop state, plus the decoder's register file and allocation bitmap (the ffmpeg decoder inside
  is not copied). Round 16's store watch: the voice positions the sound library writes from the SDK audio worker's
  callback (sub_82976898, e.g. 400E809C) moved 0x80, 0xFF, 0x100 in the replay's first frame against 0x100 each in the
  first pass, read from the XMA output write offsets; and the camera block 400D72F0.. is copied by the main thread
  (sub_82793F80 from 824F3270). `DOA4_NP_SNAPTEST_XMA=leave` keeps the old behaviour.
- SnapTest round 17 runner: frame 3600 and frame 4096.

## 0.3.118 (2026-11-12)

corysyx rollback research build (otherwise the same as 0.3.110).

### Changed
- SnapTest store watch: every store of the generated code to a watched page calls `doa4_wwatch_hit` (`REX_WHIT` in
  the store macros, also in `tools/np_wmap_patch.py`). In the first test, from the save to the first pass's first
  boundary and from the load to the replay's first boundary, each store to a watched range is logged with the guest
  thread, the generated function, its lr and the back chain. Default ranges: 400D72F0-400D7380, 400E8098-400E80A0,
  400E67C8-400E67D0; `DOA4_NP_SNAPTEST_WATCH=<lo>-<hi>,...` or `off`. Rounds 14-15: with the audio counters rewound,
  with physical memory put back and with every committed page in the snapshot, the first replayed frame at 3600 still
  differs in the same words: 400D72F0.. take in the replay the values the device's shader constants had at the load
  (8 frames later), not at the save.
- SnapTest round 16 runner: one run from frame 3600 with the store watch.

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
## 0.3.7 (2026-11-19)

### New
- **A faster build of the game (profile-guided optimisation).** The game is now compiled with a profile of how it
  actually runs: four scripted fights were played on an instrumented build, and the compiler used what they recorded
  to lay out and inline the code that runs most. On the developer's PC, over seven scripted fights on a development
  build (four of them the training fights, three not), the main thread's work per frame took 1.8 to 5.7 % less CPU
  time on all seven, and the game's background jobs 0.8 to 6.2 % less on six of seven (the seventh unchanged). This
  release's own build, checked on two of those fights: 3.4 and 4.5 % less for the main thread, 1.0 and 3.8 % less
  for the background jobs. It is CPU time saved each frame; how much of it shows as frame rate depends on the PC (most
  where the processor is the slow part). Parts of the game no training fight plays (story, menus, other characters)
  are compiled as before. How the profile is made: `docs/PGO.md`.
- **One frame less input lag with a pad.** The game now reads the pads just before it uses them, instead of using
  what was read up to one frame earlier. On the developer's PC with four different pads (Xbox One S, 8BitDo in
  XInput mode, Switch Pro and a hitbox), the time from a press to the game's read fell from about 16.6 ms to under
  0.1 ms. That frame counts online too, at any distance: it is the
  same as one frame less input delay. The automatic input delay adds a frame for about every 33 ms of round-trip
  ping, so a connection one step farther away now feels about as responsive as the nearer one did in 0.3.6. If
  anything feels wrong with a pad, `pad_fresh_read = false` in `dead_or_alive_4.toml` goes back to 0.3.6's
  behaviour. Thanks to [Project Slippi](https://github.com/project-slippi/Ishiiruka) and Arte
  ([JulienBernard3383279](https://github.com/JulienBernard3383279)): this fix was found by checking Slippi 3.7.0's
  "input suggestions" (Arte's "subframe rollback" idea) against this game. The change itself is a different one:
  Slippi sends mid-frame inputs to the other player, this reads your own pad fresh. No code was taken.
- **Intel graphics: playable menus** ([issue #21](https://github.com/dwreckgg/doa4-online/issues/21)). On Intel GPUs
  the game now uses its faster drawing mode, with a fix for an Intel driver problem that left half the screen black
  in that mode. If anything looks wrong on an Intel GPU, `d3d12_clear_depth_via_drawing_intel = false` in
  `dead_or_alive_4.toml` goes back to 0.3.6's behaviour.

### Fixed
- Savanna Safari: slits could show through the buffaloes
  ([issue #10](https://github.com/dwreckgg/doa4-online/issues/10)).

### Notes
- Both players need 0.3.7: it does not connect to 0.3.6.

## 0.3.6 (2026-11-18)

### New
- **Online: the game's background jobs run side by side.** An online session used to run the game's 25 job workers
  (the threads that do cloth and other per-frame work) one at a time, and the game waited for them every frame. They now run
  in two lanes side by side, in one fixed order that both PCs follow, so both games still compute the same fight. On
  the developer's PC, in tests on a development build of the same order, the game's wait for the workers fell from
  1.8 to 7.7 ms a frame to 1.4 to 2.7 ms. It has not been measured on a slower PC yet: reports welcome. The host can
  go back to 0.3.5's order for a session with `netplay_worker_order = "serial"` in `dead_or_alive_4.toml`.
- **Batch GPU uploads.** The Esc menu's Game tab has a **Batch GPU uploads (for slower graphics cards)** checkbox
  under **Performance**. It is off by default. It can raise the frame rate when the graphics card is the slow part;
  as a side effect, hair and other moving parts can be drawn one frame late now and then. Online, the host's choice
  is used for both players. On the developer's PC, in tests on a development build of the same change over four
  scripted fights, the graphics card's work fell from 3.4 to 4.5 ms a frame to 1.7 to 2.2 ms (37 to 52 % less): the
  same data goes up in 6 to 23 times fewer uploads, the main thread's cost did not get worse, and every fight came out
  the same as without it. That PC's graphics card was far from its limit, so it shows no frame-rate change there;
  the gain on a slower graphics card has not been measured yet. **If an earlier version ran slowly for you because
  of the graphics card (high GPU usage, a low frame rate), please turn it on and tell us how it goes in
  [issue #20](https://github.com/dwreckgg/doa4-online/issues/20)**: your graphics card, the frame rate with it off
  and on, and anything that looks wrong.

### Fixed
- Kyoto in Bloom (day and night): after a fall into the canal, the water could stay flat and streaked for the rest of
  the fight, mostly on the first starts while the shader cache was still empty. The game now prepares each effect
  before drawing it; the cost is a short hitch the first time an effect is drawn, until the shader cache holds it.
- Online: when a session ended right at its start, before the other player's game had connected, the other player's
  game waited a minute and then said the other player did not connect. It is now told at once, and both games
  restart.

### Notes
- Online: a new message, "This PC could not keep the session's timing. The session ended; both games restart." (and
  "The other player's PC could not keep the session's timing. ..." on the other PC). It shows when one PC falls more
  than 8 seconds behind in one frame, for example during a virus scan or another heavy program, and ends the session
  cleanly before the games could go out of sync. A hitch longer than 10 seconds can show "The other player stopped
  responding" on the other PC instead.
- Both players need 0.3.6: it does not connect to 0.3.5.

## 0.3.5 (2026-11-17)

### New
- **Volume slider.** The Esc menu's Game tab has a **Volume** slider (0 to 200 %, 100 % by default) with a Reset
  button. It sets the game's overall loudness on your PC only and acts at once. A volume changed during an online
  session is kept until the game closes. (issue #16)
- **Online matches start on the same rules for everyone.** Every online session now starts with the life gauge at
  its largest setting, Single matches at first to 3 rounds with a 60 second time limit, and Tag matches at 99 seconds.
  These are starting values only: they can still be changed in the game's Settings during the session. Your own saved
  settings are never changed.

### Fixed
- Game audio was too quiet. On stereo output (speakers or headphones) the game is now about 8 dB louder, 2.5 times
  the old level. 5.1 surround output is unchanged. (issue #16)
- The "Select a Device" pop-up on the first start showed "?" characters in place of its text, and the mouse pointer
  was hidden. The text is readable now and the pointer shows while the pop-up is open. The pop-up is in English in
  every game language.
- Online: the joining player pressing START on the title screen at the start of a session froze both games. The
  joining player's controller is now ignored on the title screen and before it; the host presses START.

### Notes
- There is now a subreddit, https://www.reddit.com/r/doa4online, for sharing feedback and finding other players to
  play with online. Bugs still go to the GitHub Issues page: https://github.com/dwreckgg/doa4-online/issues
- Online: when a session returns to the title screen later on, a press by the joining player in the instant the title
  appears can still open the menu in the host's place. Nothing freezes.
- The log now has a line at start-up with the processor's name, its cores and threads, and the PC's memory. It helps
  with slowdown reports.

## 0.3.4 (2026-11-12)

### New
- **Alpha-152 is playable.** Tick "Alpha-152 playable" in the Esc menu's Extras tab, then restart the game (the
  "Restart now" button beside it does that). It is off by default. Online, the host's setting is used for both players.
  - She takes the old RANDOM cell on character select. RANDOM moves to a new last cell, and it can now pick her.
  - She has one costume.
  - Her model is not shown on character select in an online match where the two players' DOAX2 swimsuit imports
    differ, or in Tag's second-fighter panel. She is still playable there.
  - In Team Battle her icon in the team list shows "?".

### Fixed
- Fixed a crash when a throw is the final hit (seen at the end of Ayane's story against Alpha-152).
- Pressing START and BACK together in Sparring no longer leaves streaks on the paused picture (the pause now opens one
  frame after the position reset).

## 0.3.3 (2026-11-12)

### New
- A red **Quit Game** button at the top right of the Esc menu closes the game, offline or online.
- **Paste** buttons beside the join-code field and the Reach out field in the Online tab put a copied code in.
- Discord shows **"Playing Dead or Alive 4 Online"** (with the time played) while the game runs and the Discord app is
  open. Turn it off in the Esc menu's Game tab.
- The Esc menu's text is larger in fullscreen (borderless and exclusive). In a window it keeps the old size.

### Fixed
- The game's own DOA ONLINE (Xbox Live) item on MODE SELECT can no longer be entered: it froze the game. Everything else
  on that screen works as before.
- Ctrl+V, Ctrl+C, Ctrl+X and Ctrl+A now work in the Esc menu's text fields, so a join code can be pasted.
- Spaces and line breaks copied along with a code are removed, whether it is pasted with Ctrl+V or the Paste button.
- RANDOM on character select now gives every costume the same chance. Before, COS. 1 came up far more often than the rest.

### Notes
- RANDOM includes the DOAX2 swimsuits when you imported them. In an online match where only one player has the
  swimsuits (or the two imports differ), RANDOM gives both players only the normal costumes, because a randomly picked
  swimsuit there could make the match go out of sync. Picking a swimsuit by hand works as before.

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

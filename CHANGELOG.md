# Changelog

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

# Changelog

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

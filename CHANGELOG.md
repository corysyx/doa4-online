# Changelog

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

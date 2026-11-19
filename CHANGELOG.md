# Changelog

## 0.3.7 (2026-11-19)

<!-- DRAFT for the user's look before release: wording, and the PGO figures after round 2's spot check. -->

### New
- **A faster build of the game (profile-guided optimisation).** The game is now compiled with a profile of how it
  actually runs: four scripted fights were played on an instrumented build, and the compiler used what they recorded
  to lay out and inline the code that runs most. On the developer's PC, over seven scripted fights (four of them the
  training fights, three not), the main thread's work per frame took 1.8 to 5.7 % less CPU time on all seven, and the
  game's background jobs 0.8 to 6.2 % less on six of seven (the seventh unchanged). Parts of the game no training
  fight plays (story, menus, other characters) are compiled as before. How the profile is made: `docs/PGO.md`.
- **One frame less input lag with a pad.** The game now reads the pads just before it uses them, instead of using
  what was read up to one frame earlier. On the developer's PC with four different pads, the time from a press to
  the game's read fell from about 16.6 ms to under 0.1 ms. If anything feels wrong with a pad, `pad_fresh_read =
  false` in `dead_or_alive_4.toml` goes back to 0.3.6's behaviour.
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

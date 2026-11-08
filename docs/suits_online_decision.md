# Imported swimsuits online: the decision record (Task 7, Task 7b)

Sections 1, 3c and 6 are Task 7b's, on `public-suits` after 34d7204, snapshot `t8b` (a snapshot of the working tree before the fixes were committed as 5f00ad4, not of a commit: **every number in `t8/gsum_all.txt` and in this record's tables is from `t8b`**); section 3d is Task 7c's fix round, and section 9 is the final-commit gate, **which has not run yet**; the rest describes Task 7's runs. Task 7's build: `public-suits` at 061820b, tools/np_build.bat, SDK sdk-np1-ft, snapshot `t7b` (rebuilt from the clean tree; `git status` shows only `tools/__pycache__`). Import under test: the `all34_m1` import (the m1 recipe, per-suit bust strengths; local test data, not in this repository), copied into a test user data folder for whichever side holds it. The evidence folder and the report are not in this repository.

## 1. Verdict

**The gate no longer calls for the fallback: on this PC a mixed session passes, Tag included, with two small fixes, both confined to mixed sessions (Task 7b, `public-suits`, commit in the report).** The script that failed (`A4`, section 3b) now runs clean in both holder directions, and the wider gate is in section 3c: 21 mixed pairs, 281,115 frames, 126,616 round frames, position, life and state equal on every round frame. **Repeats of a script are the same run** (one PC, one seed, pads from the script): counted once, that is 15 scripts, 213,311 frames and **89,180 distinct round frames** (`A4` three times, `M1`, `M3`, `M4` and `M5` twice each, the repeats out). The totals are the larger number; the evidence is the smaller one. **The spec's gate also names two PCs over a real network; that has not been run** (the friend is not available, section 5). So this is a verdict about one PC.

**Why it desynced: two causes, both reached only by a tag, each proven by removing it (section 3c).**
1. **A tag to a suit partner was refused on the PC without the suit.** The load guard rewrote the model in the load request, and the request's model is also the key the game stores in its load slots and later matches against what it wants. On that PC the slot said COS. 1's model and the fighter record said the suit's. Two presses reproduce it with no random input.
2. **A fighter's tag-out step moved with the camera's shake, and the shake draws from the scene generator.** That is a gameplay consumer of 0x8357B5C0, by experiment: in a session with **no suits at all**, one PC's generator one draw ahead moves a fighter during a tag-out from a fight; the same run with only the camera's draws made private does not.

**Spec check, for the user:** "a draw that affects gameplay ... means the fallback ... no moving of draws unless it's confined to mixed sessions and trivial." A draw that affects gameplay has now been found. The fix does not move it: in a mixed session both peers set the generator to the same state at each frame boundary, about 10 lines, mixed only. I read that as inside the spec's exception. It is the user's call.

**What it leaves:** section 3c, "what the resync cannot do", and sections 5 and 6.

## 2. The audit: who draws from 0x8357B5C0

**Method.** `DOA4_NP_TRACE=clock` (the brief's `mt8357B5C0` does not enable the trace: `np_det_trace.cpp` tests for the substring `clock`, and the trace lines it writes do carry the `mt8357B5C0` call counts by return address). Every run below was traced on both peers: 17 pairs, 34 traces, Single and Tag, through the menus, the loads, the rounds, the KO replay, the result, the post-match select (6000 frames of random pads) and a rematch into a second full match. Each return address was mapped to its containing function (`t7/callers.py`, using `asm_index.json`). A "round" frame is one with the fight state byte 0x830ACF85 equal 2.

**Reached: 114 functions** (125 function-by-thread rows; 31 functions draw on a worker thread). **53 draw inside a round, 61 only outside.**

| Level | Meaning | Reached in a round | Only outside a round |
|---|---|---|---|
| R | I read the draw site and what the drawn value is stored to | 19 | 4 |
| F | family inferred from the address neighbourhood, the callees and the global footprint; the draw site not read | 32 | 34 |
| U | unclassified | 2 | 23 |

Full table (function, thread, sites, draws in a round, draws outside, class, level, what the draw writes to): `t7/audit_table.md`, appended in section 8.

**What the read callers do with the value (R):**
- **Effects (12 read, plus the F ones).** `sub_8259E2A8` (180 particle slots of 80 bytes: the draw is added to the float at slot+0x44, 9.5 M draws in rounds), `sub_82596C40` (60 slots), `sub_825A4F60`, the spawners `sub_82594A88/B68/C68/DF8/F98`, `sub_8259DD48`, `sub_825AF838`. Each stores a float or a 2/3-bit sprite variant into an effect slot or a stack vector handed to a spawn call. Their callee closures (8 deep) contain no writer of the fighter table 0x83033B40..0x83033DB0 and no `sub_824061A0` (the only function that touches the main generator 0x833F7600).
- **Camera.** `sub_82318730` (35 draws a frame): shake offsets stored at game_obj+0xC70/0xC74. Its callee closure reaches `sub_824061A0` (it also draws from the gameplay generator, through a callee, which is the same on both peers). Whether any fighter code reads the camera fields was not traced; the Tag runs are the evidence (section 4).
- **Animation timers.** `sub_82372840` (blink and morph timer, 60 to 300 frames, at +540), `sub_8257E7C0` (a rand-mod-n helper) with `sub_8257ED70` and `sub_8257F200` (key times and interpolated floats), `sub_8257D058`, `sub_826868A8` (via `sub_82686B10`, a face record, floats +4 and +20).
- **Audio.** The worker-thread cue pickers `sub_8268D1A0`, `sub_8268CF10`, `sub_8268CFE0`, `sub_8268D0C0`, `sub_8268D4D8`: a random index mod the cue count goes to the sound-cue calls `sub_8255AC50` / `sub_8255AD10` / `sub_825740C0`, and a random timer 150..449 at +156 of the picker. 5.2 M draws in rounds (Tag), on a worker. **Corrected in Task 7b:** I said the order of draws between threads is timing dependent. It is not, in a session: the workers run one at a time in a fixed order and never beside the main thread (`np_det_workers.cpp`, mode 3), `A4n` has the generator equal on both peers on all 17,911 frames, and the holder's stream in `A4b` equals `A4s`'s on every frame. The streams part because their draw counts differ, deterministically.
- **`sub_823740D0`** (Task 6's lead) is classed F: the morph-state setup that calls the `sub_82372840` family.

**`sub_82387350` (R), corrected in fix round 1.** It picks an unused index among N: 23 fighters (`sub_82387688`) or 9 or 3 stages (`sub_82387518`), into a table at 0x8303B53C and the picks at 0x8303B555... I first took that for the RANDOM tile's pick. It is not: **the only function that reads the picks is `sub_82387078`, the enter handler of state 4 of the attract-flow state machine** (section 2b), and `M8`'s RANDOM fighter came from elsewhere. The trace shows exactly 3 draws at return address 0x823873CC on frame 8 in every one of the 34 original traces and nowhere else. Frame 8 is the boot call (`sub_82402BF8`, the update thread's entry, calls `sub_82402C58`, then `sub_82386240`, then `sub_82387518`).

**Unclassified (U), 25 in all at the first pass; 2 reached in a fight, `sub_825B0CE0` and `sub_825B0EA0`, now read (section 2c): actor motion pickers.** Outside a fight: `sub_8252AB50` (consumer `sub_82521478`, which also writes the fighter-table range by footprint; I did not follow what it stores at +84), `sub_825CA110` / `sub_825CB2B0` (a state machine returning action codes), `sub_822F1650`, `sub_8267D060` (its callee closure reaches the main generator) and `sub_82692178`, and 16 callers in 0x825B..0x825E. They are listed, not passed.

**Limits of the audit.** F is not a reading. A reader of an effect pool or an animation record was not enumerated for any caller (the global footprint only sees constant addresses; the pools are indexed through a moving pointer). Callers outside the matrix (other modes, other characters, the Story, Survival and Watch modes, the rest of the 514 sites) are unaudited.

## 2b. Can a session reach `sub_82387280` after the generators differ? (fix round 1, question 1)

**What it is.** `sub_82387280` is the **exit handler of state 4** of a state machine whose object is at 0x8303B560 (+0 enabled, +1 phase, +2 "advance" flag, +4 current state, +5 position in the sequence table at 0x82C7EDB4 (states 2, 3, 2, 4, 2, 3, 2, 3, 2, 4, then 255 = wrap), +8 and +12 tick counters). The handler table is 16-byte rows at 0x82C7F690 + 16 x state, offsets +4 enter, +8 update, +12 exit; row 4 is `82386FA8 / 82387078 / 823871D8 / 82387280`, so 0x82C7F6DC is state 4's exit. The dispatcher is `sub_823865B0`, ticked by `sub_823860E0`. The machine is started by `sub_82386308` (called from `sub_82403700`) and stopped by `sub_82386440` (called on input from `sub_82403800` and `sub_82405B80`); both run the current state's exit handler when they end a running state.

**What the exit handler does:** `sub_823E2688`, `sub_823DF880`, toggles the byte at 0x8303B554 (2 or 4 fighters), calls `sub_82387518` (the draws), then the reset. **The only reader of the picks is `sub_82387078`, state 4's enter handler**, so the draws feed the fighters and stage of the next state-4 attract demo, not a player's match and not the RANDOM tile.

**When a player can reach it.** Observed in runs (`A1`, Kasumi COS. 10 browsed so the suit loaded, then B out to the title, 19,702 frames, mixed, `verdict ok`): the machine is enabled from frame 22 (title) and again at the mode select after backing out (frame 3431); a player's START stops it (1538). Idle at the mode select, it left state 2 for state 3 after 3190 frames (53 s); state 3 shows the intro movie and then a demo fight (shot at frame 19302), and the machine byte +4 stayed 3 to the end of the run (13,000 frames): **state 4 was not reached naturally in the longest idle I ran.** So the natural path (idle on the title or mode select for minutes, through state 3, into state 4 and out of it, or a button press while state 4 runs, which stops the machine through its exit handler) is reachable in a session by idling; how long it takes was not measured.

**Forced, to watch the draw (`A2`, same script, then both peers poked identically at frame 5000: +4 := 4 and +2 := 1).** The exit ran on both peers at frame 5000: 5 draws at 0x823873CC on each, the table at 0x8303B53C changed identically (equal on the host and joiner at every sampled frame, 4900, 5001, 5002, 5010, 5100, 7000, 12000), the flag at 0x8303B554 toggled on both, the machine moved to state 3 on both; the session ran to 13,702 frames, `verdict ok`. **The generators were equal at frame 5000**: the generator difference in that run was one window of 20 frames, 2868 to 2887, during the costume browse. A second forced run (`A3`, `A3b`, poked inside the window at frame 2875) did not execute the handler at all: the machine is only ticked on the title and mode-select screens, so a draw cannot fall in a costume-browse window. What this shows and what it does not: in Single, the generators re-converge after each load window, so a natural draw at the title lands on equal generators. **In Tag they did not re-converge**: `G_M5` shows the generator different on every frame from 5193 to the end of the run (8262), through the rounds, the KO and the result; a draw taken after a Tag match at the title would read different generators and write different tables, and `sub_82387078` would then start a different demo on each peer. I did not run that chain (a Tag match, the way back to the title, the forced exit, the demo): the Tag match did not finish in the scripts I had, and `A4` ended in the desync of section 3b first.

**Does the bag refill during play?** The 23 flags at 0x8303B53C..0x8303B552 are read and written only inside `sub_82387438` and `sub_82387518`; the refill (all flags cleared when none is unused) is inside `sub_82387518` itself, which runs only from the boot call and from `sub_82387280`. Nothing but the state-4 handlers (`82386FA8`, `82387078`, `82387280`) reads 0x8303B53C..0x8303B560, so no RANDOM pick consumes this bag and there is no live refill path to test; `M8` shows a RANDOM pick drawing nothing from it.

## 2c. The unread callers, by reading (fix round 1, question 3)

- **`sub_825B0EA0` and `sub_825B0CE0`** (worker thread, 105 and 184 draws in rounds). `sub_825B0EA0` is a per-frame behaviour method of an object of at least 9.5 KB with a vtable (counters at +272/+356, flags at +9488/+9500, a position vector at +304). It draws a value 0..7 and a repeat count 1..2, looks the value up in a table at 0x82BE.... and calls **the object's own virtual method at vtable+80 with (action id, repeat count)**, a start-motion call; `sub_825B0CE0` returns a random table entry different from the current one (+268). The value is stored nowhere but in the object's motion state. I did not identify the class (no constructor I could place); every draw of this family is on a worker thread, while the fighter table is written from the main update thread (`sub_82403200`). **Classified: an actor's motion picker, not a fighter reader. The identity of the actor is not established.**
- **`sub_825CA110`** (worker thread, outside rounds): the same pattern, a 12-way state switch returning an action id 1..15 that its callers `sub_825CAFE8` and `sub_825CC328` pass to `vtable+80` of their own object (`mr r4,r3`, then the call). Same classification.
- **`sub_825CB2B0`**: the state machine that writes +268..284 of such an object and makes indirect calls; I did not find the draw site. **Unclassified.**
- **`sub_8252AB50`** (feeds `sub_82521478`'s +84): `sub_82521478` is the fight-setup function (about 3000 lines; it clears the fighter table and reads the game mode from `sub_8253E3C8`). `sub_8252AB50(mode)` draws mod 2 or 3 and looks a value 0..9 up in a byte table per mode; `sub_82521478` clamps it to the count at +86 and stores it as the u16 at +84 of the record at 0x831037A0, then calls `sub_823B80B8` / `sub_823B8170` with category 7 and `sub_827925E8`. No code reads 0x831037F4 by a constant address, so I could not find what reads it. It runs once per fight start, before the round. **Not classified**; the pairs' fighters were equal afterwards, but I did not log 0x831037F4 itself.
- **`sub_8267D060` / `sub_82692178`**: `sub_8267D060` is an initialiser: a loop over 220 records of 40 bytes (fields +12 and +16 set from tables), then 13 groups; the draws are initial random picks among list entries. Main thread, outside a round (132 draws). **Classified: scene-subsystem init, not in a round.**
- **The camera, `sub_82318730`.** It writes floats at +3136/+3140, +3152/+3156, +3184/+3188 and +3200..+3208 of a 4336-byte camera block (five of them, from 0x8320D110, stride 4336). Readers by offset in the game code (0x822F..0x829F): the only loads of +3136/+3140/+3152/+3156 are in `sub_82318730` itself; the view matrix at +3136 is read through the getter `sub_8231F750` (callers `sub_82313398`, the camera update, and `sub_82697200`) and written through the setter `sub_8231F8B0` (callers `sub_82314278`, `sub_8231EF38`, `sub_82332E80`, `sub_8240CE58`). **No load of +3184/+3188 or +3200.. exists in the game code** (the 3188(r30) loads are in the SDK library range 0x82A5.., on another base), and no constant-address reader of the five blocks' +0xC40..+0xCD0. `sub_82697200` and `sub_82313398` do not write the fighter table by footprint. I found no position-relative input or AI reading the camera by offsets. This is a search by offset and by constant address; a read through a pointer with the offset built in a register would be missed. **Task 7b: one is:** a tag-out step moves with the camera's draws (section 3c), and this search did not find its reader.

## 3. The gate: results

All pairs mixed unless stated; `np_pair --expect ok`; fighter fields by `DOA4_NP_TEST_FIELDS=830ACF84:4,83033B40:138` and `np_fields.py --rounds`; fight gate (`fight gate open on N frames`) equal on both peers. In every row: position, life and state of both fighters **equal on all N round frames**, and the fight state differed on 0 frames. The record in each row names the character and model byte the fighter record held and the suit's `read:` line on the holder or the redirect on the other side.

| Pair | Setup | Holder | Frames | Round frames | KO round endings | Evidence the suit was used |
|---|---|---|---|---|---|---|
| M1a, M1b, M1c | P1 Kasumi COS. 10 (model 31) vs P2 Ayane | joiner | 7380 each | 3309 | 2 | joiner `read: vks_on_o`; host redirect on 4 loads |
| M2 (`m2b`) | P1 Tina COS. 8 (model 7) vs P2 Bayman, throws | host | 7990 | 3203 | 2 | host `read: vtn_sh_c`; joiner 4 redirects |
| M3 | P1 Helena COS. 8 (model 8) vs P2 Lei COS. 9 (model 0) | joiner | 8288 | 2915 | 2 | joiner `read: vhe_sk_a`, `vle_on_i`; host: `charId 0x7 model 8 ... model 5 instead` |
| M4 | P1 Hitomi COS. 11 (model 11) vs P2 Kokoro COS. 9 (model 20) | host | 8048 | 3408 | 1 | host `read: vht_on_e`, `vco_sk_c`; joiner: `charId 0x15 model 11 ... model 3 instead`, `0xa model 20 ... model 0` |
| M5a, M5b, M5c | Tag: Kasumi COS. 10 + Hayabusa vs Ayane COS. 7 + Hayate | joiner | 8262 each | 3406 | 3 | joiner `read: vks_on_o`, `vay_cs_b`; host 6 redirects; **2 swaps seen on side 0, 1 on side 1** |
| M6 (`M6`) | same import on both, Tina vs Bayman, throws; `host same, joiner same`, sweep 4 blocks/frame, `verdict ok`, no masks | both | 7990 | 3203 | 2 | both read `vtn_sh_c`, 0 redirects |
| M7 | neither: Kasumi COS. 10 script, no import; `host none, joiner none`, sweep 4 blocks/frame | none | 7380 | 3309 | 2 | 0 reads, 0 redirects |
| M8 | RANDOM fighter (character 19) and RANDOM stage, vs Ayane | joiner | 7696 | 2983 | 2 | see section 2 |
| X browse | Lei COS. 9 vs Ayane, then 6000 frames of random pads on the post-match select | joiner | 13000 | 2415 | 2 | `np_pair` ok |
| X rematch | Lei vs Ayane, match, rematch through the post-match select, a second whole match | joiner | 11726 | 5501 | 4 | ok |
| `m2a` (extra) | the first, wrong, Tina script: both sides held the import | both | 11298 | 6860 | 1 | `host same, joiner same`, ok |
| `G_M1`, `G_M5` | M1 and M5 once more with the generator in the fields log | joiner | 7381 / 8263 | 3309 / 3406 | | ok |

Fighter fields were compared by value in 17 runs (14 mixed, 3 same or none), 60,657 round frames counting each run once. No row differed on any run.

**Throws (M2).** The first run's Free button was assumed to be X: it is not. A probe (23 inputs, then a shot probe) found that **A** throws: `A` and `LEFT+A` landed, 53 damage, 116 frames after the press. In `m2b`: P1 (Tina, the holder) pressed A at 4028; side 1's life went 240 to 187 at frame 4144, with side 1's +0x30 state word 0 to 0x01DF01CB and 28 of its state words changing between 4039 and 4199; the shot at frame 4132 shows Tina throwing Bayman. Four throws by P1 landed in all (lives 240, 187, 134, 81, then 14 and 0 at 5761 and 5845), ending round 1 by KO, and **the records of both peers were byte equal on every frame of 4020..4199**. Round 2 ran on random pads: side 0 (Tina) lost 32 and 18 (240 to 190), side 1 lost 32, 31, 36, 18, 67, 25, 25 and 6 down to 0 at frame 7345.

**The non-holder's throws (fix round 1, question 4).** In `m2b` P2's four throws did not land, and the control (`M6`, the same import on both sides, the same script) gave the **identical** life events, so it was not a mixed-session effect: Bayman walked in for only 30 frames and pressed A 240 frames after P1's throw. With a 60-frame approach and 200 frames of rest (`m2t.txt`: P1 idle, P2 presses A, LEFT+A, RIGHT+A, DOWN+A, UP+A, LEFT+X, X+A, twice), **Bayman's throws landed on Tina in both runs**: control `T_ctl` (both hold) and mixed `T_mix` (host holds, so Tina is the suited fighter being thrown). The life events were identical in the two runs (side 0: 240 to 184 at frame 4139, 128 at 4463, 72 at 4787, 5 at 5425, 0 at 6067; round 2: 184 at 7065, 117 at 7693; 56 damage per throw), `verdict ok` for both, position, life and state equal on all 4211 round frames, side 0's +0x30 equal on both peers (0x01AF01AB from frame 4100), the records of both peers byte equal on frames 4040..4299, and the shot at frame 4122 shows Bayman throwing the suited Tina. The first three landings are A at 4058, LEFT+A at 4382 and RIGHT+A at 4706, each 81 frames after the press; I did not tabulate the later ones.

**Repeats (M1, M5 three times).** The spread is nil: the three M1 runs ended on the same frame (7380), with the same 3309 round frames, equal rows, and the trace's generator difference on the same 3 frames (2876 and two more); the joiner's total draws were 2138392, 2138390, 2138390 and the host's 2138653 each time. The three M5 runs matched to the draw (host 2207964, joiner 2209051, 3506 differing frames each). The runs are not independent samples: one script, one seed, one PC; their agreement shows the pair is deterministic, and a flip would have shown. No verdict flipped.

**Positive companions.**
- The generator **does** differ where the fighter fields were compared. G_M5 (Tag): the generator 0x8357B5C0 (0x9C8 bytes) differs on **3373 of the 3406 round frames** (first differing round frame 3956, first differing frame overall 2897; 5036 of 8263 frames overall), while position, life and state are equal on all 3406. G_M1 (Single): the generator is equal on all 3309 round frames and differs on 20 of 7381 frames overall (first 2877), outside rounds.
- So the evidence about "values drawn differently, gameplay unaffected" is the Tag runs. In Single rounds the draws were the same on both peers, and nothing there tests whether a Single caller's value reaches a fighter.
- Frames compared, gate-open frames, and the suit's `read:` line and the redirect on the other side are all above 0 in every row (the table).

## 3b. The failure: a mixed Tag session past frame 8262 (fix round 1)

The M5 script quits at frame 8262, in the middle of the third round. `A4` is the same Tag script with four more 600-frame random-pad blocks, then B presses and idling at the title (17,910 frames), traced on both peers, fields log with the generator.

| Run | Import | Result |
|---|---|---|
| `A4` | joiner holds | **desync at frame 8411** (this side 5AC2BCF6, other side 6FE0A48E); the digests differ at 8412; side 1's position differs from 8412 |
| `A4b` | joiner holds (repeat) | **desync at frame 8411, the same two hashes** |
| `A4h` | host holds | **desync at frame 8411** (reported by the other player) |
| `A4s` | both | 17,910 frames, `verdict ok` |
| `A4n` | neither | 17,910 frames, `verdict ok` |
| `A5` | joiner holds, Single M1 with 24 random blocks | 18,180 frames, 4156 round frames, `verdict ok` |

What the fields log shows at the failing frame (side 1, character 11, model 0: the partner who is fighting, no suit; side 0 is Kasumi in her suit, about 2 units away): both lives (200 and 190), the fight state and all state words equal on frames 8409 to 8413; the +0x30 state word changes on both peers at 8411 (0x013501BB to 0x011C010A), then to 0x003200B0 at 8412; side 1's position x then moves -0.008 per frame on the host and +0.054 per frame on the joiner. **Corrected in Task 7b:** that last sentence was wrong: the +0x30 word differs at 8412 too (0x22260104 on the holder, 0x003200B0 on the other). One peer takes a tag and the other does not; I read the row before it.

**Cause: found in Task 7b, section 3c.** What Task 7 looked at, for the record, with both of its guesses wrong in the detail: the generator differs on 3373 of 3406 round frames in Tag, as in `M5`, so the stream is not the same on the two peers; in frames 8396 to 8410 the only main-thread draw count that differs between the peers is `sub_826868A8` (the face-record updater, 1 against 0 on frames 8397, 8403, 8407, 8410), the rest are the worker-thread audio pickers `sub_8268CF10` and `sub_8268CFE0`. So either a consumer of the shared stream that I classified as cosmetic feeds a fighter's motion in Tag (the face record is the one main-thread candidate), or something timing-dependent that a suit changes (the asynchronous file loads differ on the holder) reaches the fighter's motion. Nothing here separates the two. It is the test that fails; the cause is an open item, and it decides whether the preferred rule can be rescued.

## 3c. Cause and fix (Task 7b)

The report and its evidence folder are not in this repository.

**What the failure is.** Section 3b's "state words equal" was read at the row before the difference. At row 8412 the state word differs too: the holder's fighter is in 0x22260104 (the tag-out run, seen again at every later tag), the other peer's in 0x003200B0. Player 2 held A+B from frame 8404 and the direction changed at 8411. The shots show Ayane coming in on the holder only. `A4s` and `A4n` both take the tag at that frame; the holder's trajectory and generator equal `A4s`'s on every frame, and only the PC without the suit differs. M5 and the other earlier scripts never had a redirected partner tag back in during a round: **M5's "swaps" were tag-outs and new rounds.** That is why it needed 150 more frames of random input.

**Reproduction with no random input** (`tools/np_scripts/suit_tag_in.txt`): the Tag select, then A+B to tag Kasumi out, A+B to tag her back. Frame 5170, the same two words.

### The experiments
| Run | Setup | Result |
|---|---|---|
| `P1`, `P2` | **no suits**, A4 script, check off, the joiner's generator poked 1 or 2 draws ahead in the first round | Generator different on nearly every round frame. Only tag-outs differ: side 1 from frame 8412 by up to 0.002 for all 35 frames of the move, side 0 on 0 to 35 frames. State words and lives equal on all 12,199 round frames. No tag refused |
| `T1` | mixed, Kasumi COS. 10, by hand | The tag back in refused on the PC without the suit, at frame 5170 |
| `T2` | mixed session, both fighters COS. 1, the same presses | 8 tags, equal |
| `T3` | as `T1`, and on the PC without the suit the slots' model keys poked to what the holder has, at frame 4000 | 8 tags, position equal on all 3307 round frames, **with the generator different on 3224 of them** |
| `F2` | the slot fix, no generator fix, A4 | Still `desync at frame 8411`. Both peers now in 0x22260104, position apart by 0.00017. **The second cause** |
| `B1` / `B2` | `P1`, with the camera's draws (return addresses 0x82318730..0x82319FFF) or an animation helper's (0x8257C000..0x8257FFFF) taken from a private generator | Camera: tag-outs equal. Animation: tag-outs differ on all frames, as `P1` |
| `F3` | slot fix + the generator made equal at each frame boundary, A4 | 17,910 frames `verdict ok`, position, life and state equal on all 12,199 round frames |
| `F4` | slot fix + camera draws private, no resync, A4 | 17,910 frames `verdict ok`, **generator different on all 12,199 round frames**, fighters equal on all of them. So in this script the camera was the whole of the generator's reach |
| `O_slot`, `O_sync`, `O_both` | the final build with a fix switched off (`DOA4_NP_TEST_MIXED_OFF`), A4 | All `desync at frame 8411`. `O_both` gives **`A4`'s hashes exactly, 5AC2BCF6 and 6FE0A48E**. `O_slot`: 0x003200B0 against 0x22260104. `O_sync`: 0x22260104 on both, a small position difference |

### The fixes (mixed sessions only)
1. **The load slots keep the model the game asked for.** The slots (0x830ADBEC, 44 bytes a row: +3, +4 being loaded, +5, +6 loaded) are what `sub_823FE9D0` matches, from the fight's plan `sub_823E4748` among others. The request hook no longer rewrites an empty slot's model in a mixed session; the loader's tick `sub_823FE150` is the one place that turns a model into a record of files, and only for that call the row reads the model this PC has (`costumes::BeginLoaderTick`, `EndLoaderTick`). It runs on the main thread, with the workers stopped; a line says so if it ever does not (0 in every pair). **At strength:** that a key difference causes the refusal is proven by experiment (`T3`, `O_slot`). The steps from the failed match to the refused tag were not traced.
2. **The scene generator is made equal on both peers at each frame boundary**, after the row and the field log, from the main generator's state and the frame (`DeriveSceneGenerator`). Lock held at the boundary: 0 frames in every pair, on both peers, checked at run time; `np_pair` fails a pair otherwise, and fails a mixed session with no such line. **Changed in Task 7c (section 3d):** a boundary that finds the lock held is now skipped, not written, and `np_pair` also fails a resync that did not cover every frame.

`kNone` and `kSame` take neither path: `M6` and `M7` have all 7990 and 7380 digest rows byte equal against Task 7's build, on both peers, `mt_8357B5C0` and `game_obj` among them. Task 7's sweep files were pruned, so the sweep rows were not compared across builds; both runs' peers agree (`verdict ok`).

### What the resync cannot do
Inside a frame the stream can still part: from the first draw that differs to the frame's end, something reading it later in that frame reads a different number. A read earlier in the frame sees the same stream on both PCs.
- **How often a frame ends apart:** 37 of 12,199 round frames on A4, 7 of 3406 on M5, 551 of 7386 on `td1`, 631 of 12,095 on `L2`, **2111 of 7014 on `td2`**, 0 on the Single scripts, from the morph and face timers (`sub_82372840`, `sub_826868A8`) and the audio pickers.
- **Does it reach the camera?** The whole camera block `game_obj` was logged with the resync on: **equal on all 16,680 round frames logged** (`F5` 4006, `G_td1c` 5660, `G_td2c` 7014), with 2,538 of those frames ending apart. In `F5`, 6 of its 14 such frames also had the camera drawing and it was unaffected, so there its draws come first; I did not repeat that count on the other two. `G_td1c` was stopped by my 900 s limit at frame 10,794 because that log slows the run; what it logged counts, its verdict does not. **`Z_A4` was stopped by a time limit too:** the clean rebuild `t8c`'s A4 pair, run while the user's game was on, hit my 800 s limit at about frame 15,200 (host 15,212, joiner 15,207) and `np_pair` says `FAILED` for that and for the different end frames it causes. It passed frame 8412 and its tag-outs with the gate open on 9,501 and 9,496 round frames and no desync line; its field log was not kept, so its position, life and state rows were not compared, and it is **not** one of the 21 pairs or in any total here. `Z_in`, the same build's short script, is `verdict ok`.
- **What is not known:** an unknown consumer that reads late in a frame would differ on those frames. None was seen: no fighter difference in 126,616 round frames (89,180 of them distinct, section 1), and `F4` bounds it for A4's script. A private generator for the camera (`F4`) was the other shape. I chose the resync because it also keeps the stream equal for any consumer not found, and it was the one run on the whole gate.

### Single matches
Asked: how strong is the case that Single can't fail the same way? **Strong on what was run, and not a proof about every move.**
- **Both causes are the tag move.** `T1` needs a tag; the camera's reach showed only in 0x22260104 (`P1`, `P2`: 24,398 round frames, the stream apart on 24,355 of them, every position difference inside that move). Nothing in a Single match enters it in any Single run (0 tag-outs on all of them).
- **"Single re-converges" was not a property of Single.** The first match does. In a rematch the stream differs naturally: `S2` (Lei vs Ayane, no poke, before the fix): different on 3013 of 5501 round frames, first frame 7584, the first frame of the second match. Fighters equal on all 5501, `verdict ok`.
- **The direct test, forcing it (`S1`, before the fix):** A5's Single fight, mixed, check on, the joiner's generator poked one draw ahead at frame 3500: different on 3898 of 4156 round frames, position, life and state equal on all 4156. The session ended `left`. The pair's verdict is `FAILED` on one tool rule only: `pr_cri_passes` and `pr_spin_steps` split by 12 against the mixed bound of 2, counters that are not in the game's checksum.
- **Limits:** three Single scripts' worth of moves, a few characters, and the camera's reach is not explained: a Single move that reads it the way a tag-out does would differ, and none was seen. With the fixes, Single also gets the resync (`G_S1`: the same poke, taken back by the next boundary).

### If the narrower rule is still wanted
Suits off in Tag would need a value that says Tag on both peers before the costume list matters. **The byte 0x8303683A:** in `G_A4` (Tag) and `G_M1a` (Single), both peers: 0 at frames 0 and 1860, then 1 in Tag and 0 in Single at frames 2000 and 3000; the mode's second tile is confirmed between those frames. Many functions read it, including the fight's load planner `sub_823E4748`. Equal on both peers by lockstep input, in every dump compared. Not measured: its value after a Tag match on the way back to the mode select, other modes, and what it says on the mode select before the confirm (it is still 0 at 1860, so a check would have to read it after the confirm and act on the character select). Not implemented.

## 3d. Task 7c: the fix round after review

The report and its evidence folder are not in this repository; the code is a commit on this branch.

**The tick hook's guard is testable.** `BeginLoaderTick` refuses a slot of `kLoadSlotCount` (4) or more itself, and the hook passes `SessionSuitsMixed()` as its `mixed` argument instead of returning early; both are pinned in `tests/host/costumes_host_test.cpp` (an empty-slot row is untouched and not swapped with `mixed` false, and at slots 4, 5 and 255). **What rests on reading only:** the hook's one argument. If it were wrong (true outside a mixed session), the request and the tick would keep the asked model in the row and swap it per tick in such a session. *Can `kEmptySlot` arise there at all? In `kSame`, yes; in `kNone`, not shown.* `DecideLoad` returns it whenever `state.empty_slot[character]` holds the model (`src/doa4_costumes.cpp:292`); `MarkEmptySlots` fills that set from the archives for every record whose body files are empty slots, whoever imported what (`src/doa4_suits.cpp:320`); and the guard is installed in every session, imports or none (`src/dead_or_alive_4_app.h:1634`). `kSame` builds its tables from the picks, so a pick this PC has no files for stays an empty slot (`src/dead_or_alive_4_app.h:1527-1529`). `kNone` applies no tables, and a model past the vanilla count takes `kPastRecords` first (`src/doa4_costumes.cpp:291`), so there it needs an empty-slot record in the vanilla image, which wasn't checked. Task 7b's report said "no empty slot is asked for in `kNone` or `kSame`": that held for the runs, not for the code. Today the request hook rewrites such a request (`RequestModel(d, false)`), so the row never holds an empty-slot model there and the tick has nothing to swap; with a wrong argument the row would keep the key and that session's digest rows would differ from today's, with both peers still alike. Not a desync: a change of behaviour, which the rule for `kNone` and `kSame` forbids. No host test reaches the hook's call site; the live `M6` and `M7` compare against Task 7's rows (section 9) is what would show it.

**A resync skips a frame on which the generator's lock is held.** `ResyncSceneGenerator` (`src/net/np_digest.h`) reads the lock count at `scene + 2520` first: if any of its four bytes is not 0xFF it returns false and writes nothing; the session counts true as `gen_sync_frames` and false as `gen_sync_locked`. What a skip costs: nothing is corrupted, but the two PCs then differ from that frame's first draw, the camera's included, so a skip on a tag-out frame could move a fighter and end in a desync; the next boundary makes them equal again. The test and the write aren't atomic, so this narrows the window and doesn't close it. The workers run beside main only after the wait times out or when a worker has no rank (`kFbUnranked`). Why it matters: `np_det_workers` mode 3 gives main a 2 s wait after which the workers run beside it, which on a slower PC can happen, and a write under a holder hands it a state it is halfway through using. The 7b runs found the lock free on every frame of every pair, so none of this has been seen to happen. `np_pair` still fails a pair on any held frame.

**The resync must have covered every frame (`np_pair`).** Each side: frames made equal plus frames found locked must be at least that side's terminal frame plus one. Real logs: `made equal on 8049 frames`, terminal frame 8048, one more than the frame, on every pair in `t8` (controls and desync pairs included, checked by value). A resync that ran on a few frames passed a bare `\d+` before. The fake game now writes the line only when a boundary was made equal or found locked, as the game does.

**The `EndLoaderTick` case no test covered.** A cached row (character C, the real file model F in +5/+6), asked again for the suit S, takes the tick's "same files" shortcut (`sub_823FE150`, `loc_823FE240`: +0 becomes 11, +5 and +6 are not touched), so End rewrites +6 to S although the tick did not write it. Re-derived from the generated code: the matcher `sub_823FE9D0` reads +5/+6 only while +0 is 255 (`loc_823FEA24` is reached only then); the next tick sees status 11, above the dispatch table's 10, and goes to `loc_823FE83C`, which copies +3/+4 into +5/+6 and sets +0 to 255; End then puts +4 and +6 back to S, so both PCs' idle rows end (C, S). The only difference between the peers is the one frame between the two ticks, in a row the matcher does not read: the load-timing difference that already existed. Pinned by a fixture, and the header's comment says what the code does. The other reader of +5 with +0 idle, `sub_823FEA60`, reads +5 only; not changed.

**`kPastRecords` still rewrites the request in a mixed session.** Both peers hold identical tables (the picks, not the import), so "the model is past her records" fires on both alike; `G_diff` (the host with 33 suits, the joiner 34) passed.

**The per-slot table of `sub_82354F80`, and how far I looked.** The loader's tick calls `sub_82354F80(slot)` (`.9.cpp:53753`), which copies the row's +3 and +4 into a per-slot table at 0x8302BB34 (8 bytes a slot: a halfword of model and character, the character at +2, the model at +3, a flag at +4) and sets a word table from a switch. It runs inside the tick, so on the PC without the suit it sees F, the files' model, and on the holder S, by design. Its reader is `sub_82354678`, called each frame from the main loop (`.17.cpp:72319`): for each idle row whose +5 is not 255 it compares the table's character and model with a pair of globals and calls `sub_82354970`. **What I did:** read `sub_82354F80` whole and the first half of `sub_82354678`; enumerated, by the address-formation pattern, the functions that build an address in 0x8302BB34..0x8302BB78: 27 of them (these two and 25 others in `.3`, `.5`, `.9`, `.10`, `.19`, `.28`), and read none of the other 25. **Result:** no gameplay read of the table was found, and none was looked for in the 25. The table is not in the digest's regions. It is a difference the design makes and the gate has not shown to matter (no fighter differed in 126,616 round frames). Open: the other 25 formations and the globals `sub_82354678` compares against.

## 4. What the generator evidence rests on, in Tag

3406 round frames × 4 Tag runs (M5a, b, c and G_M5) with the generator differing on 98% of the round frames, and the partners swapping in and out (character and model bytes (5, 31) to (4, 0) on side 0, (13, 6) to (11, 0) on side 1). Every callee reached in a Tag round (the audio pickers on a worker thread, the effect library, the camera, the face timers) drew different values on the two peers while position, life and state, as far as the side record follows the fighter on screen, stayed equal. This is the strongest single piece of evidence in the record and it is about the Tag round callers specifically; the caveat that a resting partner's record is not in the table applies to it (section 5). **Fix round 1 changes how far it carries:** the same Tag path, run 150 frames longer in a mixed session, ends in a position difference at frame 8412 (section 3b). Equal on 3406 round frames did not bound the next 150. **Task 7b:** the differences this section relies on were the generator's, and the camera's reach went through it only in a tag-out, which M5 did not have during a fight (section 3c). The earlier claim that every Tag callee was harmless was wrong for the camera.

## 5. Limits, from the brief (plainly)

- **One PC, one seed.** All pairs ran on this PC; the seed is 12345 in every run; the repeats are identical because they are the same run.
- **A two-PC match is untested**, by the user's decision (the friend is not available). A real network, other CPUs, and the floating-point results of the converter on another CPU are untested.
- **A slower PC is untested for the cloth budget.** Frame times in these runs were 60 fps on this PC (`frame period avg 16.667 ms`); nothing was measured on a slower machine.
- **The pairs share every per-install input** (#0455).

**From Task 4.** The sync pairs cannot show that peers agree on the cloth budget rule: a one-sided rule did not desync. Agreement rests on the build check and the switch guard, not on a pair. Hair collision stays off in the bits-1-and-4 mode, at lr 0x8234F344..0x8234F36C; that mode is unidentified.

**From Task 6.**
- **What a mixed session gives up:** anything going wrong in `.data` that never reaches a listed row goes unnoticed (camera, scene generator, effects, models); the guest heap is not hashed; outside a round the side records are not read; a resting tag partner's record is not in the check.
- **The reset before a round is proven for +0x30 only** (`c2`): a natural difference in fight states 3 to 5 would carry into the next round as a false desync. None was seen in about 24,000 frames and 45 round endings in Task 6; in this task, 17 pairs, about 146,000 frames and 40 round endings (33 of them in mixed sessions), also none.
- **Not re-measured after the fixes:** the counter bound of 2 held on all 21 gate pairs on the final build; `S1`, before the fix, passed it, and the tool reported it, without the game ending anything.
- **The statement "none seen in about 146,000 frames" is about false desyncs from natural differences in fight states 3 to 5.** It is not about the real divergence of section 3b, which was in a round (state 2) and which the per-frame side record caught two frames after it began.
- **The counter bound of 2** is the observed maximum, not a derived one.
- **Bypass byte 0x82FCC966 = 2** (its mode) is unidentified.
- **Heap addresses differ between peers once a suit loads**, with the allocation not identified.
- **A resting tag partner's life and position** are not in the table (M5). **Task 7b:** a key difference on a resting partner's slot is what made a tag miss; the slot keys are now equal, and they are not in the check either. Each side's record follows whoever is on screen. I counted the swaps seen: 2 on side 0 and 1 on side 1 per M5 run, 3 round endings; the comparison covers the fighter on screen. I did not look for the partner's record: no cheap route found.

## 6. What decides each

| Open item | Would it change the verdict? |
|---|---|
| **The mixed Tag desync at frame 8411** | **Closed by the two fixes (section 3c):** `A4`, `A4h` and a repeat run clean. It would reopen if a tag or a late stream reader differs on another character or a real network |
| `sub_82387350` and the attract machine | No longer decides (ruling 18). The open chain (a Tag match, the title, the exit of state 4) was a draw on different generators: with the resync they are equal at the boundary it draws after, unless a frame before it ended apart inside that same frame. Not run. The natural time to state 4 was not measured |
| `sub_825B0CE0`, `sub_825B0EA0`, `sub_825CA110` | Read: actor motion pickers on worker threads, not fighter readers; the actor class was not identified. Would change the verdict only if the actor were a fighter |
| `sub_8252AB50` (fight-setup, +84 at 0x831037A0) | Open: the consumer was not found; the value is drawn once per fight start; log it in a pair to close it |
| `sub_825CB2B0`, 23 callers outside a fight, `sub_82521478`'s other effects | Unclassified, none in a round |
| camera readers | **Task 7b: a reader exists, by experiment** (`B1`, `F4`): a tag-out step moves with the camera's draws. Which code reads it was not found: section 2c's search by offset missed it, as it warned it could. The resync covers it; a new reader in other moves would be judged by the fields check |
| F-level callers (66) | Only if one is shown to feed a fighter and reads late in a frame. `F4` (fighters equal with every non-camera caller on a different stream, 12,199 round frames, A4's script) bounds them there |
| non-holder throws | Closed: they land in the control and in the mixed pair, equal (section 3) |
| one PC, one seed; no two-PC test | **Yes, and it is the spec's own gate item.** Untested by decision. The fixes change no input between PCs; the camera's reach into a position would turn any float difference there into a desync, and a suit's load timing differs more on another PC |
| slower PC | No effect on sync; affects whether the budget rule is acceptable |
| Task 4 pair blindness (budget agreement) | Yes if the build check or switch guard fails; not a measured failure |
| Task 6 reset proven for +0x30 only; the counter bound; bypass byte; heap addresses; resting partner | Each is a way a mixed session could end falsely or miss a real divergence; `A4` is a real divergence that the check caught (the side record's position, per frame), which is the check working |
| Single-round draws equal on both peers | **Strong on what was run, not a proof about every move** (section 3c, "Single matches"): `S1` and `S2` ran Single rounds on a different stream, fighters equal, and no Single run had a tag-out; a Single move that reads the camera the way a tag-out does would differ, and none was seen. With the fixes Single gets the resync too |

## 7. Corrections made to the brief

- The brief's trace switch `mt8357B5C0` does nothing; `clock` produces the same lines.
- The Free button is not X. A and LEFT+A throw; X did not.
- `helena_hitomi.txt` was not copied; the M3 script (Helena and Lei) at first stopped on the stage select because the load took longer than 360 frames, and was re-run with 800 (section on vacuous checks in the report).
- The import is `all34_m1`, not the one the brief names.

## 8. Audit table

Written to `t7/audit_table.md`; its rows follow.
| function | thread | sites | draws in a round | draws outside | class | level | what the draw writes to |
|---|---|---|---|---|---|---|---|
| sub_82318730 | main | 7 | 388101 | 2065155 | camera | R | per-frame camera update on game_obj (r31): 35 draws/frame become shake offsets stored at game_obj+0xC70/0xC74 (stfs 3184/3188(r31)); a fighter reading the camera was not traced |
| sub_82372840 | main | 2 | 574 | 496 | animation | R | blink/morph timer: 60..300 frames stored at +540(r31) and a pick of one of 4 morph channels |
| sub_8257D058 | main | 1 | 89 | 51 | animation | R | random start offset added to a 16-bit key at +20 of an animation record |
| sub_8257E7C0 | main | 1 | 202959 | 200764 | animation | R | helper: draw mod r31 (rand % n) returned to 8257ED70 / 8257F200 |
| sub_8257ED70 | main | 1 | 292285 | 228987 | animation | R | key-time animator: stores a 16-bit key (+24/26/30) and an interpolated float at +48 |
| sub_8258BF70 | main | 4 | 6040 | 0 | effects | F | 8258xxxx effect slot initialisers: draws into floats at +0/+4/+8(r31), counters li 40/120 |
| sub_82594A88 | main | 2 | 30772 | 12512 | effects | R | spawn: draw -> angle -> sin/cos (sub_82A00130/208) -> stack vector for a slot |
| sub_82594B68 | main | 2 | 31412 | 13986 | effects | R | spawn, same pattern as 82594A88 |
| sub_82594C68 | main | 8 | 78360 | 33288 | effects | R | spawn: draws scale a vector (sin/cos) stored to a slot with stvx; calls 82594B68 |
| sub_82594DF8 | main | 4 | 98864 | 38880 | effects | R | spawn: draws -> floats 32/36/40(r31) of a slot and a 3-bit variant |
| sub_82594F98 | main | 5 | 358606 | 130837 | effects | R | draws & 3 stored at 16/20/24(r31) of slot records: a 2-bit sprite variant per slot |
| sub_825963B8 | main | 3 | 360 | 0 | effects | F | 8259xxxx effect spawners and pool updaters |
| sub_82596778 | main | 26 | 5564 | 0 | effects | F | 8259xxxx effect spawners and pool updaters |
| sub_82596C40 | main | 1 | 4126932 | 1231920 | effects | R | 60-slot twin of the above: same float in its pool |
| sub_8259DD48 | main | 26 | 124376 | 60134 | effects | R | three draws -> offsets in a stack vector handed to a spawn call |
| sub_8259E2A8 | main | 1 | 12258974 | 3580532 | effects | R | 180-slot particle pool (80-byte slots, lifetime counters): the draw is added to the float at slot+0x44 (fade/scale) |
| sub_825A2AA8 | main | 4 | 6040 | 0 | effects | F | 8259xxxx effect spawners and pool updaters |
| sub_825A4260 | main | 3 | 4500 | 2700 | effects | F | 8259xxxx effect spawners and pool updaters |
| sub_825A4570 | main | 12 | 240 | 144 | effects | F | 8259xxxx effect spawners and pool updaters |
| sub_825A4F60 | main | 4 | 756155 | 225494 | effects | R | four draw sites: floats at -12/-8(r31) of pool slots and one global float; no integer state |
| sub_825AF838 | main | 3 | 74407 | 26477 | effects | R | chance test (draw mod N below a threshold) per emitter slot, then spawn via 825AF678 (effect pool lists at 0x8309xxxx) and 82719118 |
| sub_825B0CE0 | main+other | 2 | 105 | 108 | unclassified | U | 825Bxxxx: lookup into tables at 0x8309xxxx with a compare; not read |
| sub_825B0EA0 | other | 8 | 184 | 140 | unclassified | U | 825Bxxxx: lookup into tables at 0x8309xxxx with a compare; not read |
| sub_82615420 | main+other | 22 | 41162 | 79024 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_82617750 | main | 1 | 35796 | 13524 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_8261A2A8 | main | 1 | 357960 | 135240 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_8261BC98 | main+other | 10 | 105940 | 124010 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_8261C5D8 | other | 2 | 4676 | 2040 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_8261C708 | other | 2 | 2460 | 416 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_8261C978 | main | 10 | 57642 | 25598 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_8261D500 | main+other | 34 | 108800 | 380800 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_8261DE90 | other | 17 | 185708 | 120020 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_82620508 | other | 2 | 165884 | 71156 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_82628788 | other | 2 | 876 | 17454 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_82628DB0 | other | 1 | 438 | 8727 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_82684430 | main | 6 | 360 | 186 | animation | F | 8268xxxx face/animation timers and picks |
| sub_82685D28 | main | 2 | 139860 | 45876 | animation | F | 8268xxxx face/animation timers and picks |
| sub_826868A8 | main | 2 | 4632 | 1473 | animation | R | face-record updater (via 82686B10 over 56-byte records): floats +4 and +20 of the record |
| sub_8268CF10 | main+other | 3 | 15450 | 10942 | audio | R | random timer 150..449 stored at +156 of the cue picker |
| sub_8268CFE0 | other | 1 | 15540 | 5924 | audio | R | random pick of 3 states for the cue picker |
| sub_8268D1A0 | other | 4 | 5189982 | 1823760 | audio | R | random index mod a cue count -> sound cue call (8255AC50/8255AD10, priorities 10/30/90) and timers at +152/156 |
| sub_8268D720 | main | 6 | 936 | 234 | animation | F | 8268xxxx face/animation timers and picks |
| sub_826A1FD8 | main | 6 | 2256 | 888 | effects | F | 826Axxxx emitter variants |
| sub_826A25E0 | main | 1 | 42 | 1368 | effects | F | 826Axxxx emitter variants |
| sub_82719118 | main | 10 | 387370 | 138060 | effects | F | 8271xxxx effect helpers (82312288/82312358 callees) |
| sub_8271A188 | main | 3 | 82 | 0 | effects | F | 8271xxxx effect helpers (82312288/82312358 callees) |
| sub_82738EA8 | main | 7 | 5187 | 2058 | effects | F | 8273xxxx-8274xxxx effect emitters and their random-in-range helpers (8274AE78/8274AF78/826A25E0) |
| sub_8273EAE8 | main | 7 | 81900 | 16800 | effects | F | 8273xxxx-8274xxxx effect emitters and their random-in-range helpers (8274AE78/8274AF78/826A25E0) |
| sub_827404A0 | main | 7 | 672 | 0 | effects | F | 8273xxxx-8274xxxx effect emitters and their random-in-range helpers (8274AE78/8274AF78/826A25E0) |
| sub_82744250 | main | 1 | 72 | 36 | effects | F | 8273xxxx-8274xxxx effect emitters and their random-in-range helpers (8274AE78/8274AF78/826A25E0) |
| sub_8274A5A0 | main | 2 | 388092 | 159210 | effects | F | 8273xxxx-8274xxxx effect emitters and their random-in-range helpers (8274AE78/8274AF78/826A25E0) |
| sub_8274AE78 | main | 1 | 13209 | 9354 | effects | F | 8273xxxx-8274xxxx effect emitters and their random-in-range helpers (8274AE78/8274AF78/826A25E0) |
| sub_8274AF78 | main | 1 | 12993 | 2874 | effects | F | 8273xxxx-8274xxxx effect emitters and their random-in-range helpers (8274AE78/8274AF78/826A25E0) |
| sub_822F1650 | main | 1 | 0 | 116 | unclassified | U | table lookups on bytes at 0x8305xxxx with a draw; callers 822F02D0/822F1588 not read; never in a round |
| sub_823740D0 | main | 1 | 0 | 881 | animation | F | morph-state setup, calls the 82372840 family (Task 6 lead) |
| sub_82387350 | main | 1 | 0 | 90 | GAMEPLAY BY CODE | R | bag draw: picks an unused index among N for the RANDOM tile (23 fighters, 9 or 3 stages) into a table at 0x8304xxxx; reached only at frame 8 |
| sub_82455060 | main | 5 | 0 | 9830400 | effects | F | one-time 65536-draw table fill at frame 1 (5 sites), boot only |
| sub_8252AB50 | main | 1 | 0 | 42 | unclassified | U | draw picks from a 2-3 entry byte table, clamped 0..9; consumer is sub_82521478 (a loader that also writes the fighter-table range by footprint) which stores it at +84 of a record; not followed. Reached before a round only (20 draws) |
| sub_8257F200 | main | 1 | 0 | 314 | animation | R | same animator family: 16-bit key at +26, floats +40/+44 |
| sub_8258BE08 | main | 4 | 0 | 44352 | effects | F | 8258xxxx effect slot initialisers: draws into floats at +0/+4/+8(r31), counters li 40/120 |
| sub_8258EEB8 | main | 4 | 0 | 5040 | effects | F | 8258xxxx effect slot initialisers: draws into floats at +0/+4/+8(r31), counters li 40/120 |
| sub_8258F810 | main | 6 | 0 | 35028 | effects | F | 8258xxxx effect slot initialisers: draws into floats at +0/+4/+8(r31), counters li 40/120 |
| sub_8258FA88 | main | 1 | 0 | 1050 | effects | F | 8258xxxx effect slot initialisers: draws into floats at +0/+4/+8(r31), counters li 40/120 |
| sub_8258FBE0 | main | 4 | 0 | 9912 | effects | F | 8258xxxx effect slot initialisers: draws into floats at +0/+4/+8(r31), counters li 40/120 |
| sub_8258FDB0 | main | 6 | 0 | 2507400 | effects | F | 8258xxxx effect slot initialisers: draws into floats at +0/+4/+8(r31), counters li 40/120 |
| sub_8258FFE8 | main | 3 | 0 | 25074 | effects | F | 8258xxxx effect slot initialisers: draws into floats at +0/+4/+8(r31), counters li 40/120 |
| sub_825930E8 | main | 3 | 0 | 15120 | effects | F | 8258xxxx effect slot initialisers: draws into floats at +0/+4/+8(r31), counters li 40/120 |
| sub_825B23D8 | main | 4 | 0 | 80 | unclassified | U | 825Bxxxx: lookup into tables at 0x8309xxxx with a compare; not read |
| sub_825B3900 | other | 9 | 0 | 234 | unclassified | U | 825Bxxxx: lookup into tables at 0x8309xxxx with a compare; not read |
| sub_825B3C70 | other | 1 | 0 | 12 | unclassified | U | 825Bxxxx: lookup into tables at 0x8309xxxx with a compare; not read |
| sub_825B3FF0 | other | 7 | 0 | 1680 | unclassified | U | 825Bxxxx: lookup into tables at 0x8309xxxx with a compare; not read |
| sub_825B4250 | other | 2 | 0 | 33600 | unclassified | U | 825Bxxxx: lookup into tables at 0x8309xxxx with a compare; not read |
| sub_825CA110 | main+other | 3 | 0 | 68 | unclassified | U | returns an action code 1..15 chosen by draw from a 12-way state switch (+7200 of its argument); consumers 825CAFE8/825CC328 not read; other thread, never in a round |
| sub_825CB2B0 | main | 1 | 0 | 240 | unclassified | U | state machine writing +268..284 of a record, indirect calls; draw site not read; never in a round |
| sub_825DB308 | main | 5 | 0 | 3000 | unclassified | U | 825Dxxxx: not read; never in a round |
| sub_825DC828 | main | 1 | 0 | 52 | unclassified | U | 825Dxxxx: not read; never in a round |
| sub_825DCC00 | main | 4 | 0 | 160 | unclassified | U | 825Dxxxx: not read; never in a round |
| sub_825DDAC8 | main | 1 | 0 | 24 | unclassified | U | 825Dxxxx: not read; never in a round |
| sub_825DE728 | main | 1 | 0 | 20 | unclassified | U | 825Dxxxx: not read; never in a round |
| sub_825DEC08 | other | 1 | 0 | 104 | unclassified | U | 825Dxxxx: not read; never in a round |
| sub_825DF108 | main | 2 | 0 | 24 | unclassified | U | 825Dxxxx: not read; never in a round |
| sub_825DFA50 | other | 2 | 0 | 312 | unclassified | U | 825Dxxxx: not read; never in a round |
| sub_825E0760 | main | 1 | 0 | 12 | unclassified | U | 825Dxxxx: not read; never in a round |
| sub_825E0AE0 | other | 1 | 0 | 156 | unclassified | U | 825Dxxxx: not read; never in a round |
| sub_825E0E88 | main | 1 | 0 | 12 | unclassified | U | 825Dxxxx: not read; never in a round |
| sub_825E4CC0 | main | 2 | 0 | 72 | unclassified | U | no family rule; not read |
| sub_82619DE8 | main | 7 | 0 | 5880 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_82628068 | main+other | 22 | 0 | 1597257 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_8262A360 | main | 1 | 0 | 357450 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_8262CAE8 | main | 7 | 0 | 36960 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_8262CFA8 | main | 1 | 0 | 3574500 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_8262ECE0 | main | 8 | 0 | 135885 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_8262F8C0 | main+other | 34 | 0 | 4406400 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_82630250 | other | 17 | 0 | 1635203 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_826321B8 | other | 2 | 0 | 1337577 | effects | F | 8261xxxx-8263xxxx vector emitters (vmaddfp, sub_82486970/82312xxx callees) |
| sub_82645F40 | main | 2 | 0 | 1204 | effects | F | 8264xxxx: callees and shapes like the 8262 emitters; other thread; not read |
| sub_82646030 | main | 4 | 0 | 2400 | effects | F | 8264xxxx: callees and shapes like the 8262 emitters; other thread; not read |
| sub_82648E58 | main+other | 2 | 0 | 158151 | effects | F | 8264xxxx: callees and shapes like the 8262 emitters; other thread; not read |
| sub_82649A18 | main | 1 | 0 | 288 | effects | F | 8264xxxx: callees and shapes like the 8262 emitters; other thread; not read |
| sub_8264A150 | main+other | 4 | 0 | 511296 | effects | F | 8264xxxx: callees and shapes like the 8262 emitters; other thread; not read |
| sub_8264F4E8 | main | 4 | 0 | 192 | effects | F | 8264xxxx: callees and shapes like the 8262 emitters; other thread; not read |
| sub_826525B0 | other | 1 | 0 | 212 | effects | F | 8265xxxx: other thread, vector shapes; not read |
| sub_826547A0 | other | 7 | 0 | 1489600 | effects | F | 8265xxxx: other thread, vector shapes; not read |
| sub_8267D060 | main | 1 | 0 | 176 | unclassified | U | its callee closure reaches the main generator function sub_824061A0 (so it also draws from the gameplay generator); not read; never in a round |
| sub_82686990 | main | 7 | 0 | 3360 | animation | F | 8268xxxx face/animation timers and picks |
| sub_8268D0C0 | main | 2 | 0 | 18288 | audio | R | cue picker variant: random index -> 8255AC50 |
| sub_8268D4D8 | main | 2 | 0 | 9072 | audio | R | cue picker variant: random index -> 8255AC50 / 82579298 |
| sub_82690EB0 | main | 6 | 0 | 19962 | animation | F | 8269xxxx per-record parameter jitter (floats at +4/+20) |
| sub_82691080 | main | 4 | 0 | 7752 | animation | F | 8269xxxx per-record parameter jitter (floats at +4/+20) |
| sub_82692178 | main | 2 | 0 | 64 | unclassified | U | called by 8267D060; not read; never in a round |
| sub_826958E8 | main | 1 | 0 | 32768 | animation | F | 8269xxxx per-record parameter jitter (floats at +4/+20) |
| sub_82695D70 | main+other | 20 | 0 | 382730 | animation | F | 8269xxxx per-record parameter jitter (floats at +4/+20) |
| sub_826A2640 | main | 1 | 0 | 1368 | effects | F | 826Axxxx emitter variants |
| sub_8273A440 | main | 5 | 0 | 30 | effects | F | 8273xxxx-8274xxxx effect emitters and their random-in-range helpers (8274AE78/8274AF78/826A25E0) |

## 9. The final-commit gate (Task 7c)

Snapshot `t9`, built from commit 3ec2343, SDK `sdk-np1-ft` c276e9b. `t9/batch7.sh`, 24 pairs, 2026-11-07, 90 minutes, nothing else running. Read with `t9/summ.py` (`t9/summ_all.txt`). The rows of sections 3 and 3c are from `t8b`; this section is what a commit has run.

| Pairs | Script | Holder | Frames | Round frames | Result |
|---|---|---|---|---|---|
| `H_A4`, `H_A4h` | A4, the failure | joiner, host | 17,911 | 12,199 | ok |
| `H_TM`, `H_TMh` | **a Tag match to its end** | joiner, host | 19,043 | 6,970 | ok; fight state 5 at 12106, 0 at 12374; 2 tag-outs |
| `H_M1a`, `b`, `c` | M1 | joiner | 7,381 | 3,309 | ok, the brief's three repeats on the final build |
| `H_M5a`, `b`, `c` | M5, Tag | joiner | 8,263 | 3,406 | ok, three repeats |
| `H_M2`, `H_M2t` | throws, both ways | host | 7,991, 8,535 | 3,203, 4,211 | ok |
| `H_M3`, `H_M4` | Helena v Lei; Hitomi v Kokoro | joiner, host | 8,289, 8,049 | 2,915, 3,408 | ok |
| `H_td1`, `H_td2` | Tag, other characters | joiner, host | 12,527, 12,327 | 7,386, 7,014 | ok |
| `H_L1` | longer Tag, several matches | joiner | 35,911 | 14,655 | ok; fight state back to 0 at 15581, 25116, 28843; 11 tag-outs, all in the first match |

In all of them: `verdict ok`, position, life and state rows differing 0, made-equal count the terminal frame plus one on every side, lock held 0, off-main 0. **17 mixed pairs, 214,469 frames, 101,275 round frames; 11 distinct scripts once each: 146,227 frames, 68,676 round frames.**

**Controls, all `verdict desync` at frame 5000:** `H_Cforce`, `H_Clife`, `H_Cpos`, `H_Cgate`. **Before-half:** `H_Oboth`, both fixes off on this build: `desync at frame 8411`, hashes 5AC2BCF6 and 6FE0A48E, `A4`'s exactly.

**Sessions that must not change:** `H_M6` (both, same import) against Task 7's `M6`: 7990 common digest rows, 0 differ, both sides. `H_M7` (neither) against `M7`: 7380, 0 differ, both sides. Regions the same. Sweep rows of Task 7's runs were pruned, so sweep wasn't compared across builds.

**What this doesn't show:** a lock-held frame, since none has happened, so the skip path has only host tests; two PCs, other CPUs, a slower PC. `H_TM`'s two tag-outs are few: `H_L1` and `H_td1` carry the tag-out count.

**After this gate,** comments, `tools/np_pair.py`, the fake, its tests, this record and the SDK pin changed (`git diff 3ec2343`); the code the game runs did not. The final commit's short run is below.

**The final commit, a5d9135, snapshot `t10`, 2026-11-07 (`t9/batch8.sh`, `t9/summ_z.txt`).**

| Pair | Script | Holder | Frames | Round frames | Result |
|---|---|---|---|---|---|
| `Z_A4` | A4 | joiner | 17,911 | 12,199 | ok |
| `Z_2M`, `Z_2Mh` | **a second Tag match with tags in it** (`suit_tag_secondmatch.txt`) | joiner, host | 17,835 | 11,368 | ok; first match to fight state 0 at 12374, second begins at 12886; 10 tag-outs, 8 in the second match |
| `Z_M2` | throws | host | 7,991 | 3,203 | ok |
| `Z_M7` | neither has suits | neither | 7,381 | 3,309 | ok; against Task 7's `M7`: 7380 digest rows, 0 differ, both sides |
| `Z_Cforce` | control, frame 5000 | joiner | 5,002 | 1,759 | `verdict desync` |

Mixed rows: position, life and state differing 0, made-equal count the terminal frame plus one on every side, lock held 0, off-main 0. **This is what section 1's verdict rests on, for one PC.** Two PCs, other CPUs and a slower PC have not run.

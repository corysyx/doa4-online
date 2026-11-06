# Imported swimsuits online: the decision record (Task 7)

Build: `public-suits` at 82d585b, tools/np_build.bat, SDK sdk-np1-ft, snapshot `t7b` (rebuilt from the clean tree; `git status` shows only `tools/__pycache__`). Import under test: `physics_spike/imports/all34_m1` (the m1 recipe, per-suit bust strengths), copied into a test user data folder for whichever side holds it. Evidence folder: `notes/plans/2026-11-06-public-importer-plan1-foundation/t7/`. Report: `../plans/2026-11-06-public-importer-plan1-foundation/task-7-report.md`.

## 1. Verdict

**FALLBACK, by the rule as written, on the audit and not on the gate.** Every measured pair was `verdict ok` with position, life and state equal on every in-round frame (section 3), so the gate did not fail. The audit found one caller of the scene generator 0x8357B5C0 that is gameplay by its code, `sub_82387350` (the draw behind the RANDOM tile's fighter and stage pick), and it was reached (frame 8, every run). Two more callers reached in a fight are unclassified (`sub_825B0CE0`, `sub_825B0EA0`), plus 23 reached outside a fight. The spec says a draw that affects gameplay is the fallback; moving it is not "confined to mixed sessions and trivial", since the draw runs at boot before any session exists. Per the brief I stopped here: **no fallback code written.**

What would reverse it (section 6): evidence that `sub_82387350` can only be reached once, at boot, where both peers are equal, and that the two in-fight unclassified callers are not gameplay. The measurements below are the evidence for that reading, at the strength stated there; they do not prove it.

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
- **Audio.** The worker-thread cue pickers `sub_8268D1A0`, `sub_8268CF10`, `sub_8268CFE0`, `sub_8268D0C0`, `sub_8268D4D8`: a random index mod the cue count goes to the sound-cue calls `sub_8255AC50` / `sub_8255AD10` / `sub_825740C0`, and a random timer 150..449 at +156 of the picker. 5.2 M draws in rounds (Tag), on a thread, so the order of draws between threads is timing dependent.
- **`sub_823740D0`** (Task 6's lead) is classed F: the morph-state setup that calls the `sub_82372840` family.

**Gameplay by its code: `sub_82387350` (R).** It picks an unused index among N: 23 fighters (`sub_82387688`) or 9 or 3 stages (`sub_82387518`), writing the choice into a table at 0x8304xxxx, from the scene generator. That table is what the RANDOM tile yields, so it decides which fighter and stage a match starts with. Where it is reached: the trace shows exactly **3 draws at return address 0x823873CC on frame 8, in every one of the 34 traces, and nowhere else**. The path to frame 8 is `sub_82402BF8` (the update thread's entry: it calls `sub_82402C58` once, which calls `sub_82386240`, which calls `sub_82387518`, then loops `sub_82403200` each frame). The other entry into `sub_82387518` is `sub_82387280`, which has no direct caller (reached through a pointer) and was **not reached by any run**; who calls it was not found. Measured around it:
- the generator was **equal on both peers from frame 0 to its first difference**: 2877 (G_M1) and 2897 (G_M5), well after frame 8;
- `M8` (joiner holds; P1 picks the RANDOM tile, the stage cursor left on the RANDOM stage): `verdict ok`, both peers fought the same fighter (character 19), the same stage, state differing on 0 frames, and **no draw at 0x823873CC after frame 8**. So the pick consumed the table made at boot and drew nothing at pick time, on that path.

**Unclassified (U), 25 in all; 2 reached in a fight:** `sub_825B0CE0` and `sub_825B0EA0` (worker thread, 105 and 184 draws in rounds, a random pick among table entries different from the current one, consumer not followed). Outside a fight: `sub_8252AB50` (consumer `sub_82521478`, which also writes the fighter-table range by footprint; I did not follow what it stores at +84), `sub_825CA110` / `sub_825CB2B0` (a state machine returning action codes), `sub_822F1650`, `sub_8267D060` (its callee closure reaches the main generator) and `sub_82692178`, and 16 callers in 0x825B..0x825E. They are listed, not passed.

**Limits of the audit.** F is not a reading. A reader of an effect pool or an animation record was not enumerated for any caller (the global footprint only sees constant addresses; the pools are indexed through a moving pointer). Callers outside the matrix (other modes, other characters, the Story, Survival and Watch modes, the rest of the 514 sites) are unaudited.

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

**Throws (M2).** The first run's Free button was assumed to be X: it is not. A probe (23 inputs, then a shot probe) found that **A** throws: `A` and `LEFT+A` landed, 53 damage, 116 frames after the press. In `m2b`: P1 (Tina, the holder) pressed A at 4028; side 1's life went 240 to 187 at frame 4144, with side 1's +0x30 state word 0 to 0x01DF01CB and 28 of its state words changing between 4039 and 4199; the shot at frame 4132 shows Tina throwing Bayman. Four throws by P1 landed in all (lives 240, 187, 134, 81, then 14 and 0 at 5761 and 5845: the fourth is the longer throw, 67 plus 14), ending round 1 by KO, and **the records of both peers were byte equal on every frame of 4020..4199**. Round 2 ran on random pads: side 0 (Tina) lost 32 and 18 (240 to 190), side 1 lost 32, 31, 36, 18, 67, 25, 25 and 6 down to 0 at frame 7345. **The non-holder's throws did not land:** P2 (Bayman) pressed A four times with P1 standing and Tina's life stayed 240; the shot after the first shows Bayman's hands on her arms with no damage. So throws by the holder on the non-holder are shown; throws by the non-holder on the holder are not.

**Repeats (M1, M5 three times).** The spread is nil: the three M1 runs ended on the same frame (7380), with the same 3309 round frames, equal rows, and the trace's generator difference on the same 3 frames (2876 and two more); the joiner's total draws were 2138392, 2138390, 2138390 and the host's 2138653 each time. The three M5 runs matched to the draw (host 2207964, joiner 2209051, 3506 differing frames each). The runs are not independent samples: one script, one seed, one PC; their agreement shows the pair is deterministic, and a flip would have shown. No verdict flipped.

**Positive companions.**
- The generator **does** differ where the fighter fields were compared. G_M5 (Tag): the generator 0x8357B5C0 (0x9C8 bytes) differs on **3373 of the 3406 round frames** (first differing round frame 3956, first differing frame overall 2897; 5036 of 8263 frames overall), while position, life and state are equal on all 3406. G_M1 (Single): the generator is equal on all 3309 round frames and differs on 20 of 7381 frames overall (first 2877), outside rounds.
- So the evidence about "values drawn differently, gameplay unaffected" is the Tag runs. In Single rounds the draws were the same on both peers, and nothing there tests whether a Single caller's value reaches a fighter.
- Frames compared, gate-open frames, and the suit's `read:` line and the redirect on the other side are all above 0 in every row (the table).

## 4. What the generator evidence rests on, in Tag

3406 round frames × 4 Tag runs (M5a, b, c and G_M5) with the generator differing on 98% of the round frames, and the partners swapping in and out (character and model bytes (5, 31) to (4, 0) on side 0, (13, 6) to (11, 0) on side 1). Every callee reached in a Tag round (the audio pickers on a worker thread, the effect library, the camera, the face timers) drew different values on the two peers while position, life and state, as far as the side record follows the fighter on screen, stayed equal. This is the strongest single piece of evidence in the record and it is about the Tag round callers specifically; the caveat that a resting partner's record is not in the table applies to it (section 5).

## 5. Limits, from the brief (plainly)

- **One PC, one seed.** All pairs ran on this PC; the seed is 12345 in every run; the repeats are identical because they are the same run.
- **A two-PC match is untested**, by the user's decision (the friend is not available). A real network, other CPUs, and the floating-point results of the converter on another CPU are untested.
- **A slower PC is untested for the cloth budget.** Frame times in these runs were 60 fps on this PC (`frame period avg 16.667 ms`); nothing was measured on a slower machine.
- **The pairs share every per-install input** (#0455).

**From Task 4.** The sync pairs cannot show that peers agree on the cloth budget rule: a one-sided rule did not desync. Agreement rests on the build check and the switch guard, not on a pair. Hair collision stays off in the bits-1-and-4 mode, at lr 0x8234F344..0x8234F36C; that mode is unidentified.

**From Task 6.**
- **What a mixed session gives up:** anything going wrong in `.data` that never reaches a listed row goes unnoticed (camera, scene generator, effects, models); the guest heap is not hashed; outside a round the side records are not read; a resting tag partner's record is not in the check.
- **The reset before a round is proven for +0x30 only** (`c2`): a natural difference in fight states 3 to 5 would carry into the next round as a false desync. None was seen in about 24,000 frames and 45 round endings in Task 6; in this task, 17 pairs, about 146,000 frames and 40 round endings (33 of them in mixed sessions), also none.
- **The counter bound of 2** is the observed maximum, not a derived one.
- **Bypass byte 0x82FCC966 = 2** (its mode) is unidentified.
- **Heap addresses differ between peers once a suit loads**, with the allocation not identified.
- **A resting tag partner's life and position** are not in the table (M5). Each side's record follows whoever is on screen. I counted the swaps seen: 2 on side 0 and 1 on side 1 per M5 run, 3 round endings; the comparison covers the fighter on screen. I did not look for the partner's record: no cheap route found.

## 6. What decides each

| Open item | Would it change the verdict? |
|---|---|
| `sub_82387350`, gameplay by code, reached at frame 8 only | **This one decides the verdict.** If `sub_82387280` (no direct caller) is shown unreachable in a session, or its draw is moved off 0x8357B5C0 in a way the user accepts as trivial, and no other caller reads the table, the audit reads clean for this caller. Evidence for reading it as boot-only: 34 traces, one call site, generator equal until frame 2877; against: the other entry was not identified and M8 covers one path |
| `sub_825B0CE0`, `sub_825B0EA0` (in a fight, unclassified) | Yes: a read showing they are cosmetic closes this; a read showing a gameplay consumer makes the fallback certain |
| the 23 callers outside a fight, unclassified | Yes in principle (`sub_8252AB50` feeds a function that writes the fighter-table range); none reached in a fight |
| F-level callers (66) | Only if one is shown to feed a fighter. The Tag runs bound the in-fight ones |
| effect pools and the camera readers not enumerated | Same |
| non-holder throws not landed | Probably not: the gate measures what landed; a landed non-holder throw would add a case |
| one PC, one seed; no two-PC test | **Yes if a two-PC run differed.** Untested by decision |
| slower PC | No effect on sync; affects whether the budget rule is acceptable |
| Task 4 pair blindness (budget agreement) | Yes if the build check or switch guard fails; not a measured failure |
| Task 6 reset proven for +0x30 only; the counter bound; bypass byte; heap addresses; resting partner | Each is a way a mixed session could end falsely or miss a real divergence; none was seen in about 146,000 frames |
| Single-round draws equal on both peers | A Single caller's value reaching a fighter was not tested by a difference in Single; the Tag runs are the evidence |

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

DOA4 rollback test (milestone 2): does the game come out the same when it is rewound and replayed?

1. Put run_snaptest.bat and snaptest_single.txt in the game folder (next to dead_or_alive_4.exe, alpha-v0.3.109 or later).
2. Double-click run_snaptest.bat. The game plays a scripted Versus fight by itself twice (about 4 minutes) and closes.
   Do not touch it. Your own save is not used: each run gets a fresh user data folder. A run still going after
   4 minutes is closed.
3. Send snaptest_results.zip (in the game folder).

What it does (src/net/rb_snap.h): a record run (DOA4_NP=record) with DOA4_NP_SNAPTEST=1 saves the game's state 12 times
during the fight, plays 8 frames, restores the state, plays the same 8 frames again with the same input and compares.
Each test logs PASS or FAIL in np_log.txt ("[doa4][RB] snaptest"), with the memory that differed after the first
replayed frame and at the end when it fails. The first run puts back only the D3D device's render state
(DOA4_NP_SNAPTEST_DEVICE=state), the second all of the device except its ring pointers (=restore); both put the
fence counter back.
DOA4_NP_SNAPTEST=<at>,<frames>[,<every>[,<count>]] set before starting the .bat changes the schedule.

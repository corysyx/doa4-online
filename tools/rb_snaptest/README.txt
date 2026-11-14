DOA4 rollback test (milestone 2): does the game come out the same when it is rewound and replayed?

1. Put run_snaptest.bat and snaptest_single.txt in the game folder (next to dead_or_alive_4.exe, alpha-v0.3.129 or later).
2. Double-click run_snaptest.bat. The game plays a scripted Versus fight by itself two times (about 8 minutes) and closes.
   Do not touch it. Your own save is not used: each run gets a fresh user data folder. A run still going after
   4 minutes is closed.
3. Send snaptest_results.zip (in the game folder).

What it does (src/net/rb_snap.h): a record run (DOA4_NP=record) with DOA4_NP_SNAPTEST=1 saves the game's state 12 times
during the fight, plays 8 frames, restores the state, plays the same 8 frames again with the same input and compares.
Each test logs PASS or FAIL in np_log.txt ("[doa4][RB] snaptest"), with the memory that differed after the first
replayed frame and at the end when it fails. All runs put the D3D device's render state back
(DOA4_NP_SNAPTEST_DEVICE=state) and the fence counter; the first run tests from frame 3600, the second from 4096
(DOA4_NP_SNAPTEST=4096,8,240,12), the third from 3600 with physical memory put back (DOA4_NP_SNAPTEST_PHYS=restore). The XMA decoder's host state goes back too (DOA4_NP_SNAPTEST_XMA=leave: it doesn't). A crash writes out\crash.txt in the run's folder.
DOA4_NP_SNAPTEST=<at>,<frames>[,<every>[,<count>]] set before starting the .bat changes the schedule.

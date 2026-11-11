// Host test for the boot intro skip's rule (src/doa4_intro_skip.h/.cpp, update G6, Task 14). Built and run by
// run_intro_skip_test.bat.
//
// The contract:
// - a movie that starts before the first title logo draw of the run is skipped when doa4_skip_intro was on at start-up and
//   plays when it was off; skipped means the start does nothing, "finished?" answers yes and "stop" does nothing;
// - a movie that starts after the first title logo draw always plays, and its "finished?" and "stop" go to the game even
//   after a skipped pre-title movie (the title's attract sequence is drawn in real time, no movie: Task 14 Step 1; the rule
//   covers any later movie of the run);
// - the session's own gate still skips every movie (the rule ORs with it); in a session the player's own value never
//   changes what happens (a session with its movie gate off plays every movie, whatever doa4_skip_intro says);
// - the latch is the first draw of sprite 0x30000 (doa4_title_word.h:42 kLogoSprite), once per process, never reset.

#include "../../src/doa4_intro_skip.h"

#include <cstdio>
#include <initializer_list>

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using doa4::intro_skip::MovieRule;
using doa4::intro_skip::TitleLatch;

// Offline (not in a session): MoviesSkipped() is false there, so session_skips is false.
static constexpr bool kOffline = false, kNoSessionGate = false;

static void TestBeforeTheTitle() {
  // on: skipped, "finished?" is yes, "stop" does nothing
  MovieRule on(true);
  CHECK(on.SkipStart(kOffline, kNoSessionGate, /*title_seen=*/false));
  CHECK(on.AnswerFinished(kNoSessionGate));
  CHECK(on.SkipStop(kNoSessionGate));
  // off: plays, every call goes to the game (today's boot)
  MovieRule off(false);
  CHECK(!off.SkipStart(kOffline, kNoSessionGate, false));
  CHECK(!off.AnswerFinished(kNoSessionGate));
  CHECK(!off.SkipStop(kNoSessionGate));
}

static void TestAfterTheTitle() {
  // a movie started after the title always plays, on or off
  for (bool skip_intro : {false, true}) {
    MovieRule rule(skip_intro);
    CHECK(!rule.SkipStart(kOffline, kNoSessionGate, /*title_seen=*/true));
    CHECK(!rule.AnswerFinished(kNoSessionGate));
    CHECK(!rule.SkipStop(kNoSessionGate));
  }
  // after a skipped pre-title movie the next start decides afresh: a later movie's "finished?" and "stop" go to the game
  // (excluded wrong version: the skipped state kept from the earlier movie, which would end the later movie at once)
  MovieRule on(true);
  CHECK(on.SkipStart(kOffline, kNoSessionGate, false));
  CHECK(on.AnswerFinished(kNoSessionGate));
  CHECK(!on.SkipStart(kOffline, kNoSessionGate, true));
  CHECK(!on.AnswerFinished(kNoSessionGate));
  CHECK(!on.SkipStop(kNoSessionGate));
}

static void TestTheSessionGate() {
  // the session's gate skips every movie, before and after the title, whatever doa4_skip_intro says
  for (bool skip_intro : {false, true}) {
    for (bool title_seen : {false, true}) {
      MovieRule rule(skip_intro);
      CHECK(rule.SkipStart(/*in_session=*/true, /*session_skips=*/true, title_seen));
      CHECK(rule.AnswerFinished(true));
      CHECK(rule.SkipStop(true));
    }
  }
  // in a session the player's own value never changes the session's path: with the session's movie gate off (a test
  // switch), a pre-title movie plays even with doa4_skip_intro on (excluded wrong version: the intro rule applied in a
  // session, which would make the PCs differ by a setting that is not carried)
  MovieRule on(true);
  CHECK(!on.SkipStart(/*in_session=*/true, /*session_skips=*/false, /*title_seen=*/false));
  CHECK(!on.AnswerFinished(false));
  CHECK(!on.SkipStop(false));
  // and the session gate answers "finished?" for a movie the intro rule did not skip (the OR, not a replacement)
  MovieRule off(false);
  CHECK(!off.SkipStart(kOffline, kNoSessionGate, false));
  CHECK(off.AnswerFinished(/*session_skips=*/true));
  CHECK(off.SkipStop(true));
}

static void TestTheBootSequence() {
  // what Step 1 measured, as calls: movie 18 started before the title, polled, stopped; the title; a later movie
  TitleLatch title;
  MovieRule on(true);
  CHECK(on.SkipStart(kOffline, kNoSessionGate, title.Seen()));  // movie 18: skipped
  CHECK(on.AnswerFinished(kNoSessionGate));                      // the caller's first poll ends its loop
  CHECK(on.SkipStop(kNoSessionGate));
  CHECK(title.NoteDraw(doa4::intro_skip::kTitleLogoSprite));    // the title
  CHECK(!on.SkipStart(kOffline, kNoSessionGate, title.Seen()));  // a later movie plays
  CHECK(!on.AnswerFinished(kNoSessionGate));
}

static void TestTheLatch() {
  TitleLatch latch;
  CHECK(!latch.Seen());
  // other sprites (PRESS START 0x30001/0x30002, the copyright 0x30003, the word 0x30004) never set it
  for (unsigned sprite : {0x30001u, 0x30002u, 0x30003u, 0x30004u, 0x20000u, 0u}) CHECK(!latch.NoteDraw(sprite));
  CHECK(!latch.Seen());
  CHECK(doa4::intro_skip::kTitleLogoSprite == 0x30000u);  // doa4_title_word.h:42 kLogoSprite
  CHECK(latch.NoteDraw(0x30000));   // the first logo draw: true once
  CHECK(latch.Seen());
  CHECK(!latch.NoteDraw(0x30000));  // every later logo draw: false
  CHECK(!latch.NoteDraw(0x30001));
  CHECK(latch.Seen());              // never reset
}

// The process's instances (once per process: this group runs last).
static void TestTheProcessInstances() {
  CHECK(!doa4::intro_skip::TitleSeen());
  CHECK(doa4::intro_skip::Arm(true));    // the start-up value
  CHECK(!doa4::intro_skip::Arm(false));  // a later call changes nothing
  CHECK(doa4::intro_skip::Movies().skip_intro());
  CHECK(doa4::intro_skip::Movies().SkipStart(kOffline, kNoSessionGate, doa4::intro_skip::TitleSeen()));
  CHECK(!doa4::intro_skip::NoteSpriteDraw(0x30001));
  CHECK(doa4::intro_skip::NoteSpriteDraw(0x30000));
  CHECK(!doa4::intro_skip::NoteSpriteDraw(0x30000));
  CHECK(doa4::intro_skip::TitleSeen() && doa4::intro_skip::Title().Seen());
  CHECK(!doa4::intro_skip::Movies().SkipStart(kOffline, kNoSessionGate, doa4::intro_skip::TitleSeen()));
  // the clocks count
  const unsigned s = doa4::intro_skip::Swaps(), f = doa4::intro_skip::Frames();
  doa4::intro_skip::CountSwap();
  doa4::intro_skip::CountFrame();
  doa4::intro_skip::CountFrame();
  CHECK(doa4::intro_skip::Swaps() == s + 1 && doa4::intro_skip::Frames() == f + 2);
}

int main() {
  TestBeforeTheTitle();
  TestAfterTheTitle();
  TestTheSessionGate();
  TestTheBootSequence();
  TestTheLatch();
  TestTheProcessInstances();
  if (g_failures) {
    std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
    return 1;
  }
  std::printf("PASSED: %d checks, 0 failures\n", g_checks);
  return 0;
}

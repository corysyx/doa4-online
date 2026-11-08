// The DOAX2 swimsuit import's ImGui view: draws a suits_import::Flow inside a window the caller has begun (the first-run setup
// screen and the start-up offer; the Esc menu draws it too, later). No state of its own: the Flow is the state. The ImGui
// code is not host-tested; the live runs prove it.

#pragma once

#include <functional>

#include "doa4_suits_import_flow.h"

namespace doa4::suits_import {

struct DrawHooks {
  // Runs `fn` on the UI thread after the current frame (file dialogs and closing must not happen while drawing).
  std::function<void(std::function<void()>)> defer;
  // Called while drawing, on the UI thread, at the click (or every frame of an automatic start): the callee makes it
  // idempotent and does the real work after the frame (it defers, as the dialog does).
  std::function<void()> start_game;   // "Start the game" and an automatic start: resume the start
  std::function<void()> restart;      // kMenu only: "Restart"; may be empty elsewhere
  std::function<void()> start_import; // when set, kChoose also shows "Import" (enabled once a source is chosen)
};

// kChoose: the section (title, the two choose buttons, the chosen or not-chosen line, Clear, the unlock note) and, with
// hooks.start_import, "Import". kRunning: the chosen line, progress bar, phase text, Cancel. kResult: the result lines,
// "Start the game" (or an automatic start), "Choose another..." when Flow::can_retry().
void DrawSuitImport(Flow& flow, const DrawHooks& hooks);

// Only the "Swimsuits from: ..." line (or the not-chosen line), for the stages where nothing can be changed.
void DrawSuitChosenLine(const Flow& flow);

}  // namespace doa4::suits_import

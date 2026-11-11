// The online banner ("Waiting for the other player...", the end reasons) and the F9 ping / input-delay box
// (netplay phase 1, Task 14), the F7 FPS line, and the start-up update notice on the title (update G7, Task 15). An
// ImGuiDialog that exists only while one of them is shown: while any ImGui dialog exists the presenter paints from the
// UI thread (doa4_photo_notice.h), so offline play never pays for it.
// The overlay is display-only (WantsContinuousPaint false, issue #4): the guest output's frames repaint it, so the host
// presents once per guest frame instead of re-presenting each frame at the UI tick rate (~225 presents/s measured with
// the F7 line on, 2026-11-11). While no guest frame arrives (a session hold) the app's UI tick asks NeedsPaint and
// requests a paint (doa4_overlay_paint.h says when one is needed).
// The app creates / destroys it (UpdateNetOverlay); what it shows is decided by doa4_online_view.

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

#include "doa4_overlay_paint.h"

namespace doa4 {

class NetOverlay : public rex::ui::ImGuiDialog {
 public:
  using Content = overlay_paint::Content;
  // `poll` runs on the UI thread every frame the overlay draws.
  NetOverlay(rex::ui::ImGuiDrawer* drawer, std::function<Content()> poll);

  bool WantsContinuousPaint() const override { return false; }
  // Whether `now` (the content the poll returns now) still needs a paint to be shown (UI thread).
  bool NeedsPaint(const Content& now) const { return gate_.NeedsPaint(now); }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  std::function<Content()> poll_;
  overlay_paint::Gate gate_;
  std::string drawn_banner_;  // the banner of the last draw, for the "drew banner" line
};

}  // namespace doa4

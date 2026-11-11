// The online banner and the F9 box. See doa4_net_overlay.h.

#include "doa4_net_overlay.h"

#include <imgui.h>

#include <utility>

#include "doa4_display.h"

namespace doa4 {

NetOverlay::NetOverlay(rex::ui::ImGuiDrawer* drawer, std::function<Content()> poll)
    : rex::ui::ImGuiDialog(drawer), poll_(std::move(poll)) {}

// "A touch larger" than the F9 lines (user request): the FPS count alone, not the whole box.
static constexpr float kFpsFontScale = 1.35f;

void NetOverlay::OnDraw(ImGuiIO& io) {
  const Content content = poll_ ? poll_() : Content{};
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
  if (!content.banner.empty()) {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, 24.0f), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    if (ImGui::Begin("##doa4_net_banner", nullptr, flags)) {
      ImGui::TextUnformatted(content.banner.c_str());
    }
    ImGui::End();
  }
  if (!content.notice.empty()) {
    // Centred between PRESS START and the copyright line, 1.3x Task 15's text and padding (Task 12 fix round 2; the
    // arithmetic, host-tested: display::NoticeLayoutFor).
    const ImGuiStyle& style = ImGui::GetStyle();
    const display::NoticeLayout layout =
        display::NoticeLayoutFor(io.DisplaySize.y, ImGui::GetFontSize(), style.WindowPadding.x, style.WindowPadding.y);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, layout.center_y), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(layout.pad_x, layout.pad_y));
    if (ImGui::Begin("##doa4_update_notice", nullptr, flags)) {
      ImGui::SetWindowFontScale(layout.font_scale);
      ImGui::TextUnformatted(content.notice.c_str());
      ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
    ImGui::PopStyleVar();
  }
  if (!content.fps.empty() || !content.stats.empty()) {
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 16.0f, 16.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.6f);
    if (ImGui::Begin("##doa4_net_stats", nullptr, flags)) {
      if (!content.fps.empty()) {
        ImGui::SetWindowFontScale(kFpsFontScale);
        ImGui::TextUnformatted(content.fps.c_str());
        ImGui::SetWindowFontScale(1.0f);
      }
      for (const std::string& line : content.stats) ImGui::TextUnformatted(line.c_str());
    }
    ImGui::End();
  }
}

}  // namespace doa4

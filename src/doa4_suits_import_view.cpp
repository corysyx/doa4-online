// The DOAX2 swimsuit import's ImGui view. See doa4_suits_import_view.h.

#include "doa4_suits_import_view.h"

#include <imgui.h>

#include <string>

#include "doa4_utf8.h"

namespace doa4::suits_import {

namespace {

void Wrapped(const std::string& text, const ImVec4* color = nullptr) {
  ImGui::PushTextWrapPos(0.0f);
  if (color) {
    ImGui::TextColored(*color, "%s", text.c_str());
  } else {
    ImGui::TextUnformatted(text.c_str());
  }
  ImGui::PopTextWrapPos();
}

void Section(Flow& flow, const DrawHooks& hooks) {
  Wrapped(std::string(kSectionTitle));
  if (ImGui::Button("Choose disc image (.iso)...##suits")) hooks.defer([&flow] { flow.ChooseImage(); });
  ImGui::SameLine();
  if (ImGui::Button("Choose extracted folder...##suits")) hooks.defer([&flow] { flow.ChooseFolder(); });
  DrawSuitChosenLine(flow);
  if (!flow.source().empty()) {
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear##suits")) flow.Clear();
  }
  Wrapped(std::string(kUnlockNote));
}

}  // namespace

void DrawSuitChosenLine(const Flow& flow) {
  if (flow.source().empty()) {
    Wrapped(std::string(kNotChosen));
  } else {
    Wrapped(std::string(kChosenPrefix) + Utf8(flow.source()));
  }
}

void DrawSuitImport(Flow& flow, const DrawHooks& hooks) {
  const ImVec4 red(1.0f, 0.45f, 0.4f, 1.0f), green(0.55f, 0.9f, 0.55f, 1.0f);
  switch (flow.stage()) {
    case Stage::kChoose:
      Section(flow, hooks);
      if (hooks.start_import) {
        ImGui::BeginDisabled(flow.source().empty());
        const std::string label = (hooks.import_label.empty() ? std::string("Import") : hooks.import_label) + "##suits";
        if (ImGui::Button(label.c_str())) hooks.defer(hooks.start_import);
        ImGui::EndDisabled();
      }
      break;
    case Stage::kRunning: {
      DrawSuitChosenLine(flow);
      const suitconv::Progress p = flow.progress();
      ImGui::ProgressBar(float(p.fraction), ImVec2(-1.0f, 0.0f));
      ImGui::TextUnformatted(PhaseText(p).c_str());
      if (ImGui::Button("Cancel##suits")) flow.Cancel();
      break;
    }
    case Stage::kResult: {
      const bool good = flow.result() && (flow.result()->outcome == suitconv::Outcome::kImported ||
                                          flow.result()->outcome == suitconv::Outcome::kImportedWithSkips ||
                                          flow.result()->outcome == suitconv::Outcome::kSwapPending);
      for (const std::string& line : flow.ResultLines()) Wrapped(line, good ? &green : &red);
      if (flow.auto_start()) {
        hooks.start_game();
        ImGui::TextUnformatted("Starting...");
        break;
      }
      if (flow.mode() == Mode::kMenu) {
        if (flow.restart_needed() && hooks.restart && ImGui::Button("Restart now##suits")) hooks.restart();
        if (flow.can_retry() || !flow.restart_needed()) {  // an ending with nothing to retry still lets the player go back
          if (flow.restart_needed() && hooks.restart) ImGui::SameLine();
          if (ImGui::Button("Choose another...##suits")) flow.Back();
        }
        break;
      }
      if (ImGui::Button("Start the game##suits")) hooks.start_game();
      if (flow.can_retry()) {
        ImGui::SameLine();
        if (ImGui::Button("Choose another...##suits")) flow.Back();
      }
      break;
    }
  }
}

}  // namespace doa4::suits_import

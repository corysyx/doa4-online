// The first-run setup screen. See doa4_setup_dialog.h.

#include "doa4_setup_dialog.h"

#include <imgui.h>

#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <utility>

#include "doa4_photo_win.h"
#include "doa4_setup_win.h"
#include "doa4_suits_import_view.h"
#include "doa4_utf8.h"

namespace doa4 {

namespace fs = std::filesystem;
using setup::Stage;

namespace {

const char* StageName(Stage stage) {
  switch (stage) {
    case Stage::kChoose:
      return "choose";
    case Stage::kChecking:
      return "checking";
    case Stage::kTarget:
      return "target";
    case Stage::kExtracting:
      return "extracting";
    case Stage::kFinished:
      return "finished";
  }
  return "?";
}

void Wrapped(const ImVec4* color, const std::string& text) {
  ImGui::PushTextWrapPos(0.0f);
  if (color) {
    ImGui::TextColored(*color, "%s", text.c_str());
  } else {
    ImGui::TextUnformatted(text.c_str());
  }
  ImGui::PopTextWrapPos();
}

}  // namespace

SetupDialog::SetupDialog(rex::ui::ImGuiDrawer* drawer, SetupHost host)
    : rex::ui::ImGuiDialog(drawer),
      host_(std::move(host)),
      flow_(MakeEffects()),
      test_(setup::ParseTestScript(std::getenv("DOA4_SETUP_TEST"))) {
  if ((test_.active() || test_.doax2_active()) && host_.log) host_.log("test script active (DOA4_SETUP_TEST)");
  if (host_.mode == SetupHost::Mode::kSuitsOffer && test_.active() && host_.log) {
    host_.log("test script: the DOA4 part is ignored in the suits offer");
  }
  game_folder_ = host_.current;
}

// host_.defer, but a call queued before this dialog was destroyed does nothing. The import flow is destroyed only after the
// dialog (ResumeFromSetup, OnShutdown), so a call that passes this check still has its flow.
std::function<void(std::function<void()>)> SetupDialog::GuardedDefer() {
  return [this, alive = alive_weak_](std::function<void()> fn) {
    host_.defer([alive, fn = std::move(fn)] {
      if (auto a = alive.lock(); a && *a) fn();
    });
  };
}

SetupDialog::~SetupDialog() {
  const bool extracting = flow_.stage() == Stage::kExtracting;
  flow_.Cancel();
  *alive_ = false;
  alive_.reset();
  if (worker_.joinable()) worker_.join();  // a cancelled copy has removed its files by now
  if (extracting && host_.log) host_.log("closed while extracting: cancelled, partial copy removed");
}

setup::Effects SetupDialog::MakeEffects() {
  setup::Effects fx;
  fx.pick_iso = [this](const fs::path& start) -> std::optional<fs::path> {
    if (test_.active()) return test_.iso;
    return setup::PickIsoFile(host_.hwnd, start);
  };
  fx.pick_folder = [this](const fs::path& start) -> std::optional<fs::path> {
    if (test_.active()) return flow_.stage() == Stage::kTarget ? test_.target : test_.folder;
    return photo::PickFolder(host_.hwnd, start, L"Dead or Alive 4 - choose a folder");
  };
  fx.run_async = [this](std::function<void()> work) {
    if (worker_.joinable()) worker_.join();  // the previous step has posted its result
    worker_ = std::thread(std::move(work));
  };
  fx.post = [this](std::function<void()> fn) {
    std::weak_ptr<bool> alive = alive_weak_;
    host_.post([alive, fn = std::move(fn)] {
      if (auto a = alive.lock(); a && *a) fn();
    });
  };
  fx.check_image = [](const fs::path& iso) { return gamefiles::CheckImage(iso); };
  fx.check_folder = [](const fs::path& dir) { return gamefiles::CheckFolder(dir); };
  fx.inspect_target = [](const fs::path& t) { return gamefiles::InspectTarget(t); };
  fx.free_bytes = [](const fs::path& t) { return setup::FreeBytes(t); };
  fx.path_fits = [](const fs::path& t) { return gamefiles::PathFitsCodePage(t); };
  fx.extract = [](const fs::path& iso, const xdvdfs::Volume& volume, const fs::path& target,
                  const std::atomic<bool>& cancel,
                  const std::function<void(const gamefiles::Progress&)>& progress) {
    xdvdfs::FileSource src;
    if (!src.Open(iso)) {
      return gamefiles::ExtractResult{gamefiles::ExtractOutcome::kFailed,
                                      "The disc image could not be opened."};
    }
    return gamefiles::Extract(src, volume, target, gamefiles::kSupportedXexSha1, cancel,
                              progress);
  };
  fx.done = [this](const fs::path& folder) {
    if (host_.log) host_.log("chosen: " + Utf8(folder));
    game_folder_ = folder;
    if (host_.done) save_error_ = host_.done(folder);
  };
  return fx;
}

void SetupDialog::LogStage() {
  if (!host_.log) return;
  if (flow_.stage() != logged_stage_) {
    logged_stage_ = flow_.stage();
    std::string line = std::string("stage ") + StageName(logged_stage_);
    if (logged_stage_ == Stage::kTarget) {
      line += ": extract " + Utf8(flow_.image()) + " to " + Utf8(flow_.target()) + ", needs " +
              setup::FormatGB(flow_.need_bytes()) +
              (flow_.free_bytes() ? ", " + setup::FormatGB(*flow_.free_bytes()) + " free" : "");
    }
    host_.log(line);
  }
  if (flow_.message() != logged_message_) {
    logged_message_ = flow_.message();
    if (!logged_message_.empty()) host_.log("message: " + logged_message_);
  }
}

void SetupDialog::StartImport() {
  if (!host_.suits) return;
  if (!host_.suits->Start(game_folder_, host_.user_data, 0) && host_.log) host_.log("import: not started");
}

// Once: the section is over. The app destroys the import flow, joins its worker, closes this dialog and resumes the start.
void SetupDialog::FinishSuits(bool skip) {
  if (finish_requested_) return;
  finish_requested_ = true;
  if (host_.log) host_.log(skip ? "suits: start without swimsuits" : "suits: start the game");
  host_.defer(skip ? host_.suits_skip : host_.suits_finish);
}

// The screen after the DOA4 part (kFirstRun, when the import ran or came back) and the start-up offer (kSuitsOffer).
void SetupDialog::DrawSuitsScreen(bool offer_title) {
  if (offer_title) Wrapped(nullptr, std::string(suits_import::kOfferTitle));
  suits_import::DrawHooks hooks;
  hooks.defer = GuardedDefer();
  hooks.start_game = [this] { FinishSuits(false); };
  hooks.start_import = [this] { StartImport(); };
  suits_import::DrawSuitImport(*host_.suits, hooks);
  if (host_.suits->stage() == suits_import::Stage::kChoose) {
    ImGui::Spacing();
    if (ImGui::Button("Start without swimsuits")) FinishSuits(true);
  }
}

// The scripted run: choose, (for an image) change the target, extract. Stops
// at the first refusal; the log says why.
void SetupDialog::RunTestScript() {
  // The DOAX2 part first: in the first-run screen the source must be chosen before the DOA4 part ends (its done callback
  // starts the import when a source is chosen).
  if (host_.suits && test_.doax2_active()) {
    suits_import::Flow& suits = *host_.suits;
    if (suits.stage() == suits_import::Stage::kResult) {
      if (test_.doax2 && !test_doax2_done_) {
        test_doax2_done_ = true;
        if (host_.log) host_.log("test: suits result screen: Start the game");
        FinishSuits(false);
      }
    } else if (suits.stage() == suits_import::Stage::kChoose && !test_doax2_chosen_) {
      test_doax2_chosen_ = true;
      if (test_.doax2_skip) {
        if (host_.log) {
          host_.log(host_.mode == SetupHost::Mode::kSuitsOffer ? "test: doax2=skip: Start without swimsuits"
                                                               : "test: doax2=skip: the swimsuit section is left alone");
        }
        if (host_.mode == SetupHost::Mode::kSuitsOffer) FinishSuits(true);
      } else {
        GuardedDefer()([this] {
          const fs::path source = *test_.doax2;
          std::error_code ec;
          const bool folder = fs::is_directory(source, ec);
          if (host_.log) host_.log(std::string("test: doax2 source (") + (folder ? "folder" : "image") + "): " + Utf8(source));
          if (folder) {
            host_.suits->ChooseFolder();
          } else {
            host_.suits->ChooseImage();
          }
          if (host_.mode == SetupHost::Mode::kSuitsOffer) StartImport();
        });
      }
    }
  }
  if (!test_.active() || host_.mode == SetupHost::Mode::kSuitsOffer) return;  // the offer has no DOA4 part (no done callback)
  if (flow_.stage() == Stage::kChoose && !test_started_) {
    test_started_ = true;
    host_.defer([this] {
      if (test_.folder) {
        flow_.ChooseFolder();
      } else {
        flow_.ChooseImage();
      }
    });
  } else if (flow_.stage() == Stage::kTarget && !test_extract_started_) {
    test_extract_started_ = true;
    host_.defer([this] {
      flow_.ChangeTarget();
      if (!flow_.target_note().empty() && host_.log) host_.log("target refused: " + flow_.target_note());
      flow_.Extract();
    });
  }
}

void SetupDialog::OnDraw(ImGuiIO& io) {
  LogStage();
  if (host_.suits) {  // every result is logged once (the live runs grep for it)
    const bool has_result = host_.suits->stage() == suits_import::Stage::kResult && host_.suits->result();
    if (has_result && !suits_result_logged_ && host_.log) {
      host_.log("suits: " + std::string(suits_import::OutcomeName(host_.suits->result()->outcome)));
    }
    suits_result_logged_ = has_result;
    if (host_.mode == SetupHost::Mode::kFirstRun && flow_.stage() == Stage::kFinished &&
        host_.suits->stage() != suits_import::Stage::kChoose) {
      suits_seen_ = true;
    }
  }
  RunTestScript();

  const ImVec4 red(1.0f, 0.45f, 0.4f, 1.0f), amber(1.0f, 0.8f, 0.4f, 1.0f);
  const float width = std::min(io.DisplaySize.x - 32.0f, 760.0f);
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_AlwaysAutoResize;
  const bool offer = host_.mode == SetupHost::Mode::kSuitsOffer && host_.suits;
  const char* title = offer ? "Dead or Alive 4 - swimsuits##doa4_setup" : "Dead or Alive 4 - game files##doa4_setup";
  if (offer) {
    if (ImGui::Begin(title, nullptr, flags)) DrawSuitsScreen(true);
    ImGui::End();
    return;
  }
  // The DOAX2 section under the DOA4 buttons (kFirstRun only; the import itself starts after the DOA4 part).
  const bool section = host_.mode == SetupHost::Mode::kFirstRun && host_.suits;
  if (ImGui::Begin(title, nullptr, flags)) {
    Wrapped(nullptr,
            "Choose your own " + std::string(gamefiles::kSupportedDisc) +
                " disc image, or the folder you extracted it to.");
    if (host_.mode == SetupHost::Mode::kChange) {
      Wrapped(nullptr, "Now using: " + Utf8(host_.current));
    }
    if (!host_.reason.empty()) Wrapped(&amber, host_.reason);
    if (!flow_.message().empty()) Wrapped(&red, flow_.message());
    ImGui::Separator();

    switch (flow_.stage()) {
      case Stage::kChoose:
        if (ImGui::Button("Choose disc image (.iso)...")) host_.defer([this] { flow_.ChooseImage(); });
        ImGui::SameLine();
        if (ImGui::Button("Choose extracted folder...")) host_.defer([this] { flow_.ChooseFolder(); });
        if (section) {
          ImGui::Separator();
          suits_import::DrawHooks hooks;
          hooks.defer = GuardedDefer();
          suits_import::DrawSuitImport(*host_.suits, hooks);
        }
        ImGui::Spacing();
        if (ImGui::Button(host_.mode == SetupHost::Mode::kFirstRun ? "Quit" : "Cancel")) {
          host_.defer(host_.close);
        }
        break;
      case Stage::kChecking:
        ImGui::TextUnformatted("Checking...");
        if (section) suits_import::DrawSuitChosenLine(*host_.suits);
        break;
      case Stage::kTarget: {
        Wrapped(nullptr, "Disc image: " + Utf8(flow_.image()));
        Wrapped(nullptr, "Extract to: " + Utf8(flow_.target()));
        std::string space = "Needs " + setup::FormatGB(flow_.need_bytes());
        if (flow_.free_bytes()) space += "; " + setup::FormatGB(*flow_.free_bytes()) + " free";
        ImGui::TextUnformatted(space.c_str());
        if (!flow_.target_note().empty()) Wrapped(&red, flow_.target_note());
        ImGui::BeginDisabled(!flow_.CanExtract());
        if (ImGui::Button("Extract")) flow_.Extract();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Change folder...")) host_.defer([this] { flow_.ChangeTarget(); });
        ImGui::SameLine();
        if (ImGui::Button("Back")) flow_.Back();
        if (section) {
          ImGui::Separator();
          suits_import::DrawHooks hooks;
          hooks.defer = GuardedDefer();
          suits_import::DrawSuitImport(*host_.suits, hooks);
        }
        break;
      }
      case Stage::kExtracting: {
        const gamefiles::Progress p = flow_.progress();
        const float fraction = p.total ? float(double(p.done) / double(p.total)) : 0.0f;
        const std::string overlay = setup::FormatGB(p.done) + " / " + setup::FormatGB(p.total);
        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay.c_str());
        ImGui::TextUnformatted(p.file.c_str());
        if (ImGui::Button("Cancel")) flow_.Cancel();
        if (section) suits_import::DrawSuitChosenLine(*host_.suits);
        break;
      }
      case Stage::kFinished:
        if (host_.mode == SetupHost::Mode::kFirstRun) {
          // With a source chosen the import runs now (progress, then the result); otherwise the app resumes at once.
          if (section && suits_seen_) {
            DrawSuitsScreen(false);
          } else {
            ImGui::TextUnformatted("Starting...");
          }
        } else if (save_error_.empty()) {
          ImGui::TextUnformatted("Saved. The new location is used from the next start.");
        } else {
          Wrapped(&red, "The new location could not be saved: " + save_error_);
        }
        if (host_.mode == SetupHost::Mode::kChange && ImGui::Button("Close")) {
          host_.defer(host_.close);
        }
        break;
    }
  }
  ImGui::End();
}

}  // namespace doa4

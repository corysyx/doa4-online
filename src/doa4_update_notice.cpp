// The start-up update notice. See doa4_update_notice.h.

#include "doa4_update_notice.h"

#include <windows.h>

#include <array>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>
#include <utility>

#include "net/np_rules.h"

namespace doa4::update_notice {

namespace {
bool IsSet(const char* v) { return v && *v; }
}  // namespace

std::string VersionText(std::string_view tag) {
  std::array<int, 3> v{};
  if (!np::rules::ParseVersion(tag, &v)) return std::string(tag);
  return std::to_string(v[0]) + "." + std::to_string(v[1]) + "." + std::to_string(v[2]);
}

std::string DisplayText(std::string_view tag) { return "Update available: " + VersionText(tag); }

bool SessionRelaunch(const char* handoff, const char* after_session, const char* wait_pid, bool netplay_active) {
  return IsSet(handoff) || IsSet(after_session) || IsSet(wait_pid) || netplay_active;
}

StartPlan AtStart(bool enabled, std::string_view running, bool dev_build, bool session_relaunch,
                  std::string_view remembered) {
  StartPlan p;
  if (!enabled) {
    p.why = "off (doa4_update_notice = false): no check";
    return p;
  }
  if (session_relaunch) {
    p.why = "a session relaunch: no check";
    return p;
  }
  if (dev_build) {
    p.why = "a self-built game (" + std::string(running) + "): no notice, no check";
    return p;
  }
  std::array<int, 3> v{};
  if (!remembered.empty() && np::rules::ParseVersion(remembered, &v) &&
      np::rules::CompareVersions(remembered, running) > 0) {
    p.show = std::string(remembered);
    p.why = "Update available: " + VersionText(remembered) + " (" + std::string(remembered) + " from memory; running " +
            std::string(running) + "): no check";
    return p;
  }
  p.check = true;
  if (!remembered.empty()) {
    p.clear_memory = true;
    p.why = "the remembered " + std::string(remembered) + " is not newer than " + std::string(running) +
            ": memory cleared, checking";
  } else {
    p.why = "nothing remembered: checking";
  }
  return p;
}

MemoryPlan AfterCheck(updater::Found found, std::string_view tag, std::string_view remembered) {
  MemoryPlan p;
  switch (found) {
    case updater::Found::kNewer:
      p.show = std::string(tag);
      p.write = tag != remembered;
      p.memory = std::string(tag);
      p.why = "newer release " + std::string(tag) + (p.write ? ": remembered" : ": already remembered");
      break;
    case updater::Found::kUpToDate:
      p.write = !remembered.empty();
      p.why = std::string("up to date") + (p.write ? ": memory cleared" : "");
      break;
    case updater::Found::kFailed:
      p.why = "no answer (or no release of the channel): nothing shown, memory unchanged";
      break;
  }
  return p;
}

MemoryPlan RefreshFromButton(updater::Found found, std::string_view tag, std::string_view remembered) {
  MemoryPlan p = AfterCheck(found, tag, remembered);
  p.show.clear();
  return p;
}

std::string ReadMemory(const std::filesystem::path& dir) {
  std::ifstream f(dir / kMemoryFile, std::ios::binary);
  if (!f) return {};
  std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
  const size_t end = text.find('\n');
  if (end != std::string::npos) text.resize(end);
  const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
  while (!text.empty() && space(text.back())) text.pop_back();
  size_t first = 0;
  while (first < text.size() && space(text[first])) ++first;
  return text.substr(first);
}

std::string WriteMemory(const std::filesystem::path& dir, std::string_view tag) {
  const std::filesystem::path file = dir / kMemoryFile;
  if (tag.empty()) {
    if (DeleteFileW(file.c_str())) return {};
    const DWORD e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND) return {};
    return "cannot delete " + file.filename().string() + " (error " + std::to_string(e) + ")";
  }
  const std::filesystem::path tmp = dir / (std::string(kMemoryFile) + ".tmp");
  const std::string bytes = std::string(tag) + "\n";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    return "cannot create " + tmp.filename().string() + " (error " + std::to_string(GetLastError()) + ")";
  }
  DWORD wrote = 0;
  const bool ok = WriteFile(h, bytes.data(), DWORD(bytes.size()), &wrote, nullptr) && wrote == bytes.size() &&
                  FlushFileBuffers(h);
  const DWORD write_error = ok ? 0 : GetLastError();
  CloseHandle(h);
  if (!ok) {
    DeleteFileW(tmp.c_str());
    return "cannot write " + tmp.filename().string() + " (error " + std::to_string(write_error) + ")";
  }
  if (!MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const DWORD e = GetLastError();
    DeleteFileW(tmp.c_str());
    return "cannot replace " + file.filename().string() + " (error " + std::to_string(e) + ")";
  }
  return {};
}

namespace {
std::mutex g_mu;
std::vector<Arrival> g_arrivals;
}  // namespace

void Post(Arrival a) {
  std::lock_guard<std::mutex> lock(g_mu);
  g_arrivals.push_back(std::move(a));
}

std::vector<Arrival> TakeArrivals() {
  std::lock_guard<std::mutex> lock(g_mu);
  std::vector<Arrival> out;
  out.swap(g_arrivals);
  return out;
}

TitleWatch::Event TitleWatch::NoteDraw(uint32_t sprite, bool visible, uint32_t swap) {
  if (sprite != kPressStartSprite) return Event::kNone;
  if (!visible) {
    hidden_n_.fetch_add(1, std::memory_order_relaxed);
    const bool was = last_was_visible_.exchange(false, std::memory_order_relaxed);
    return was ? Event::kHidden : Event::kNone;
  }
  visible_n_.fetch_add(1, std::memory_order_relaxed);
  const uint32_t before = last_visible_.exchange(swap + 1, std::memory_order_acq_rel);
  const bool was = last_was_visible_.exchange(true, std::memory_order_relaxed);
  if (before == 0) return Event::kFirstVisible;
  return was ? Event::kNone : Event::kVisibleAgain;
}

namespace {
// PRESS START drawn at `swap`: drawn at most kGapSwaps swaps before it (a swap stamped before the last draw counts too).
bool DrawnAt(uint32_t last_visible, uint32_t swap) {
  if (last_visible == 0) return false;
  const uint32_t drawn_at = last_visible - 1;
  return swap <= drawn_at || swap - drawn_at <= kGapSwaps;
}
}  // namespace

bool TitleWatch::NotePad(uint32_t user, uint16_t buttons, uint32_t swap) {
  if (user >= 4) return false;
  const uint32_t bit = 1u << user;
  const bool down = (buttons & kPressButtons) != 0;
  const uint32_t before = down ? pad_down_.fetch_or(bit, std::memory_order_acq_rel)
                               : pad_down_.fetch_and(~bit, std::memory_order_acq_rel);
  if (!down || (before & bit)) return false;  // released, or held since an earlier read
  if (!DrawnAt(last_visible_.load(std::memory_order_acquire), swap)) return false;  // not on the title (the attract)
  press_.store(swap + 1, std::memory_order_release);
  return true;
}

uint32_t TitleWatch::PressSwap() const { return press_.load(std::memory_order_acquire); }

bool TitleWatch::Ended(uint32_t now_swap) {
  if (ended_.load(std::memory_order_acquire)) return true;
  const uint32_t last = last_visible_.load(std::memory_order_acquire);
  const uint32_t press = press_.load(std::memory_order_acquire);
  if (press == 0 || DrawnAt(last, now_swap)) return false;  // no press on the title, or PRESS START still drawn
  const uint32_t drawn_at = last - 1, pressed_at = press - 1;
  if (drawn_at > pressed_at && drawn_at - pressed_at > kPressLeadSwaps) return false;  // the title ignored that press
  ended_.store(true, std::memory_order_release);
  return true;
}

bool TitleWatch::DrawnNow(uint32_t now_swap) {
  return DrawnAt(last_visible_.load(std::memory_order_acquire), now_swap) && !Ended(now_swap);
}

uint32_t TitleWatch::LastVisibleSwap() const { return last_visible_.load(std::memory_order_acquire); }

TitleWatch& Title() {
  static TitleWatch w;
  return w;
}

}  // namespace doa4::update_notice

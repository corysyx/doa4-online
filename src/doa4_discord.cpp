// Discord status: the worker thread and the pipe (doa4_discord.h).

#include "doa4_discord.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace doa4::discord {

namespace {

std::atomic<bool> g_enabled{false};
std::atomic<bool> g_stop{false};
// A heap thread that is never deleted (review2 I1): the game's return to the dashboard (HalReturnToFirmware) calls exit(0),
// which runs the static destructors without OnShutdown, and a joinable std::thread's destructor would call std::terminate.
// Stop joins it on a normal shutdown; on exit(0) the process ends with the thread still sleeping.
std::thread* g_thread = nullptr;

#if defined(_WIN32)
class Pipe {
 public:
  ~Pipe() { Close(); }
  bool Open() {
    for (int n = 0; n < 10; ++n) {
      const std::wstring name = L"\\\\.\\pipe\\discord-ipc-" + std::to_wstring(n);
      HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
      if (h != INVALID_HANDLE_VALUE) {
        handle_ = h;
        index_ = n;
        in_.clear();
        return true;
      }
    }
    return false;
  }
  bool IsOpen() const { return handle_ != INVALID_HANDLE_VALUE; }
  int index() const { return index_; }
  void Close() {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
    in_.clear();
  }
  bool Write(uint32_t opcode, std::string_view payload) {
    const std::string frame = EncodeFrame(opcode, payload);
    DWORD written = 0;
    return WriteFile(handle_, frame.data(), DWORD(frame.size()), &written, nullptr) && written == frame.size();
  }
  // Reads whatever Discord has sent, without waiting; false when the pipe is broken. Each whole frame goes to `on_frame`.
  template <typename F>
  bool Drain(F&& on_frame) {
    for (;;) {
      DWORD available = 0;
      if (!PeekNamedPipe(handle_, nullptr, 0, nullptr, &available, nullptr)) return false;
      if (available == 0) break;
      std::string chunk(available, '\0');
      DWORD got = 0;
      if (!ReadFile(handle_, chunk.data(), available, &got, nullptr)) return false;
      in_.append(chunk.data(), got);
    }
    uint32_t opcode = 0;
    std::string payload;
    size_t size = 0;
    while (DecodeFrame(in_, &opcode, &payload, &size)) {
      in_.erase(0, size);
      if (!on_frame(opcode, payload)) return false;
    }
    return true;
  }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
  int index_ = -1;
  std::string in_;
};

void Run(int64_t start_unix) {
  const uint32_t pid = GetCurrentProcessId();
  const std::string handshake = HandshakeJson(kAppId);
  Pipe pipe;
  LinkState st;
  uint64_t nonce = 0;
  int failures_logged = 0, errors_logged = 0;
  const auto activity = [&](bool clear) { return ActivityJson(pid, start_unix, kLogoAsset, clear, ++nonce); };
  const auto log = [&](LinkEvent e, std::string_view detail) {
    switch (e) {
      case LinkEvent::kConnected:
        REXLOG_INFO("[doa4][DISCORD] connected to discord-ipc-{}", pipe.index());
        break;
      case LinkEvent::kConnectFailed:
        if (failures_logged++ == 0) REXLOG_INFO("[doa4][DISCORD] Discord not reached (not running?); retrying every {} s", kRetrySeconds);
        break;
      case LinkEvent::kShown: REXLOG_INFO("[doa4][DISCORD] status shown"); break;
      case LinkEvent::kCleared: REXLOG_INFO("[doa4][DISCORD] status turned off: cleared"); break;
      case LinkEvent::kDropped:
        REXLOG_INFO("[doa4][DISCORD] disconnected ({}); retrying every {} s while the status is on", detail, kRetrySeconds);
        break;
      case LinkEvent::kClosedByDiscord:
        if (errors_logged++ < 4) REXLOG_INFO("[doa4][DISCORD] Discord closed the connection: {}", detail.substr(0, 200));
        break;
      case LinkEvent::kError:
        if (errors_logged++ < 4) REXLOG_INFO("[doa4][DISCORD] Discord answered an error: {}", detail.substr(0, 200));
        break;
    }
  };
  const auto now_ms = [] {
    return int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
  };
  while (!g_stop.load(std::memory_order_acquire)) {
    Tick(pipe, st, g_enabled.load(std::memory_order_acquire), now_ms(), handshake, activity, log);
    for (int i = 0; i < 10 && !g_stop.load(std::memory_order_acquire); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  if (pipe.IsOpen()) pipe.Write(kFrame, activity(true));
}

// A harness run (np_run.py sets DOA4_NP, np_pair.py DOA4_NP_TEST_DIR; np_lag.py runs through them) never shows the user as playing
// (review2 I2). A player's own session relaunch sets neither (np_restart.cpp: DOA4_NP_HANDOFF, DOA4_NP_AFTER_SESSION).
bool HarnessRun() {
  for (const char* name : {"DOA4_NP", "DOA4_NP_TEST_DIR"}) {
    const char* v = std::getenv(name);
    if (v && *v) return true;
  }
  return false;
}
#endif

}  // namespace

bool Available() { return !kAppId.empty(); }

void Start() {
#if defined(_WIN32)
  if (!Available()) {
    REXLOG_INFO("[doa4][DISCORD] no Discord application id in this build: status off");
    return;
  }
  if (HarnessRun()) {
    REXLOG_INFO("[doa4][DISCORD] a test run (DOA4_NP / DOA4_NP_TEST_DIR set): status off");
    return;
  }
  if (g_thread) return;
  g_enabled.store(rex::cvar::Query<bool>("doa4_discord_status"), std::memory_order_release);
  const int64_t start_unix = int64_t(std::time(nullptr));
  g_thread = new std::thread([start_unix] { Run(start_unix); });
#endif
}

void SetEnabled(bool on) { g_enabled.store(on, std::memory_order_release); }

void Stop() {
  g_stop.store(true, std::memory_order_release);
  if (g_thread && g_thread->joinable()) g_thread->join();
}

}  // namespace doa4::discord

// Discord status ("Playing Dead or Alive 4 Online"), the user's request of 2026-11-11, on by default, off in the Game tab
// (cvar doa4_discord_status).
//
// Discord's local RPC: the Discord desktop app listens on a named pipe \\.\pipe\discord-ipc-N (N = 0..9). A client writes
// frames of (u32 opcode, u32 length, little-endian, then that many bytes of JSON): a handshake {"v":1,"client_id":<the
// Discord application's id>} (opcode 0), then commands (opcode 1) such as SET_ACTIVITY. Discord answers each with a frame
// of its own (READY after the handshake). The status shows the application's name (registered on Discord's developer site)
// and, from the activity's start timestamp, the time played. A SET_ACTIVITY without an activity clears it.
//
// Nothing here touches guest memory: a host-side setting, never carried to the other player in an online session.
// The worker thread starts once at start-up; it tries the pipes every kRetrySeconds while the setting is on and Discord is
// not reached (Discord started later, or restarted), so a PC without Discord costs one failed CreateFile every 15 s.
// Not on other systems than Windows (no pipe; Start does nothing).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace doa4::discord {

// The Discord application "Dead or Alive 4 Online" (its Application ID, the user's app, 2026-11-12). Empty: Start does nothing.
inline constexpr std::string_view kAppId = "1570319852537651312";
// The art asset's key on that application (uploaded under Rich Presence > Art Assets as logo.png, the "4" logo); empty: no picture.
inline constexpr std::string_view kLogoAsset = "logo";
inline constexpr int kRetrySeconds = 15;

enum Opcode : uint32_t { kHandshake = 0, kFrame = 1, kClose = 2, kPing = 3, kPong = 4 };

// One frame as written to the pipe: opcode and length (u32 little-endian each), then the payload.
std::string EncodeFrame(uint32_t opcode, std::string_view payload);
// A whole frame at the start of `bytes`: true and the opcode, the payload and the frame's size; false while it is incomplete.
bool DecodeFrame(std::string_view bytes, uint32_t* opcode, std::string* payload, size_t* size);
// A JSON string literal (quotes included) for `text`.
std::string JsonString(std::string_view text);
std::string HandshakeJson(std::string_view app_id);
// SET_ACTIVITY for process `pid`: playing since `start_unix` (seconds), the logo asset when there is one; with
// `clear`, no activity (the status goes away).
std::string ActivityJson(uint32_t pid, int64_t start_unix, std::string_view logo, bool clear, uint64_t nonce);
// True when Discord's JSON `payload` has "evt" set to the string `evt` (spaces around the colon allowed).
bool HasEvent(std::string_view payload, std::string_view evt);

// The worker's link to Discord, one step at a time (Tick), without the pipe or the clock, so the host test drives it with a
// stand-in pipe. The worker (doa4_discord.cpp) calls Tick about every 250 ms.
struct LinkState {
  bool ready = false;       // Discord answered the handshake with READY
  bool shown = false;       // our activity is set
  int64_t next_try_ms = 0;  // no connect before this time (steady clock, ms)
};
enum class LinkEvent { kConnected, kConnectFailed, kShown, kCleared, kDropped, kClosedByDiscord, kError };

// One step. `want`: the setting is on. Pipe: bool Open(), bool IsOpen() const, void Close(),
// bool Write(uint32_t opcode, std::string_view payload), bool Drain(F) (hands each whole frame to F(opcode, payload),
// false when the pipe is broken or F returned false). `activity(clear)`: the SET_ACTIVITY JSON. `log(event, detail)`.
// - setting on, no link, retry time reached: open and send the handshake; a failure waits kRetrySeconds;
// - frames from Discord: CLOSE drops the link; PING is answered with PONG (same payload); READY allows the activity;
// - setting off with a link: clear the activity and close (a later "on" connects at once);
// - READY and not shown yet: send the activity once; a broken pipe or a failed write drops the link (wait kRetrySeconds).
template <typename Pipe, typename Activity, typename Log>
void Tick(Pipe& pipe, LinkState& st, bool want, int64_t now_ms, std::string_view handshake, Activity&& activity, Log&& log) {
  const auto drop = [&](std::string_view why) {
    pipe.Close();
    st.ready = st.shown = false;
    st.next_try_ms = now_ms + int64_t(kRetrySeconds) * 1000;
    log(LinkEvent::kDropped, why);
  };
  if (want && !pipe.IsOpen() && now_ms >= st.next_try_ms) {
    st.ready = st.shown = false;
    if (pipe.Open() && pipe.Write(kHandshake, handshake)) {
      log(LinkEvent::kConnected, "");
    } else {
      pipe.Close();
      st.next_try_ms = now_ms + int64_t(kRetrySeconds) * 1000;
      log(LinkEvent::kConnectFailed, "");
    }
  }
  if (!pipe.IsOpen()) return;
  const bool alive = pipe.Drain([&](uint32_t opcode, const std::string& payload) {
    if (opcode == kClose) {
      log(LinkEvent::kClosedByDiscord, payload);
      return false;
    }
    if (opcode == kPing) return pipe.Write(kPong, payload);
    if (HasEvent(payload, "READY")) st.ready = true;
    if (HasEvent(payload, "ERROR")) log(LinkEvent::kError, payload);
    return true;
  });
  if (!alive) {
    drop("pipe closed");
  } else if (!want) {
    pipe.Write(kFrame, activity(true));
    pipe.Close();
    st.ready = st.shown = false;
    log(LinkEvent::kCleared, "");
  } else if (st.ready && !st.shown) {
    if (pipe.Write(kFrame, activity(false))) {
      st.shown = true;
      log(LinkEvent::kShown, "");
    } else {
      drop("write failed");
    }
  }
}

// The app: Start once after the cvars are loaded (reads doa4_discord_status; does nothing without an application id or in
// a harness run, DOA4_NP or DOA4_NP_TEST_DIR set), Stop at shutdown (joins the worker within about a quarter second).
// SetEnabled from the menu's checkbox: turning it off clears the status and closes the pipe. Available: this build has
// an application id (the menu greys the checkbox out without one).
bool Available();
void Start();
void SetEnabled(bool on);
void Stop();

}  // namespace doa4::discord

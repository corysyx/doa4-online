// Host test for the Discord status's frame format and JSON (src/doa4_discord.h, src/doa4_discord_proto.cpp). Built and run
// by run_discord_test.bat.
//
// The contract (Discord's local RPC): a frame is u32 opcode, u32 payload length, both LITTLE-endian, then the payload; a
// reader takes whole frames only and leaves a partial one for later; the handshake carries v 1 and the client id; a
// SET_ACTIVITY carries the pid and a nonce, the start timestamp and the logo when there is one, and no activity at all to
// clear the status; strings are JSON-escaped; an event is matched on the "evt" key's value, compact or spaced JSON; the
// worker's steps (Tick) as listed above RunLinkTests.

#include "../../src/doa4_discord.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

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

using namespace doa4::discord;

static bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

// A stand-in for the pipe to Discord's app: Open succeeds while `discord_up`; what Discord sends waits in `inbound` (whole
// frames, handed over by Drain); `broken` makes the next Drain report a broken pipe; every write is kept.
struct FakePipe {
  bool discord_up = false;
  bool open = false;
  bool broken = false;
  int opens = 0;
  std::vector<std::pair<uint32_t, std::string>> inbound, written;
  bool Open() {
    ++opens;
    open = discord_up;
    return open;
  }
  bool IsOpen() const { return open; }
  void Close() { open = false; }
  bool Write(uint32_t opcode, std::string_view payload) {
    if (!open) return false;
    written.push_back({opcode, std::string(payload)});
    return true;
  }
  template <typename F>
  bool Drain(F&& on_frame) {
    if (broken) return false;
    auto frames = std::move(inbound);
    inbound.clear();
    for (auto& [op, payload] : frames)
      if (!on_frame(op, payload)) return false;
    return true;
  }
  int Count(uint32_t opcode, const std::string& part) const {
    int n = 0;
    for (const auto& [op, payload] : written) n += op == opcode && Has(payload, part);
    return n;
  }
};

static std::string Act(bool clear) { return clear ? "{\"cmd\":\"SET_ACTIVITY\",\"args\":{}}" : "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"activity\":{}}}"; }

// The worker's steps (Tick): retry every kRetrySeconds while Discord is away; the handshake on connect; the activity only
// after READY and only once; untick clears and closes, a re-tick connects at once; Discord's CLOSE or a broken pipe
// drops the link and waits kRetrySeconds; a PING is answered with a PONG carrying the same payload.
static void RunLinkTests() {
  const std::string hs = HandshakeJson("42");
  std::vector<LinkEvent> events;
  auto log = [&](LinkEvent e, std::string_view) { events.push_back(e); };
  const int64_t retry_ms = int64_t(kRetrySeconds) * 1000;
  {  // Discord not running: one try, then nothing until the retry time, then one more
    FakePipe p;
    LinkState st;
    Tick(p, st, true, 1000, hs, Act, log);
    CHECK(p.opens == 1 && !p.IsOpen() && st.next_try_ms == 1000 + retry_ms);
    Tick(p, st, true, 1000 + retry_ms - 1, hs, Act, log);
    CHECK(p.opens == 1);
    p.discord_up = true;
    Tick(p, st, true, 1000 + retry_ms, hs, Act, log);
    CHECK(p.opens == 2 && p.IsOpen() && p.Count(kHandshake, "\"client_id\":\"42\"") == 1);
  }
  {  // the status only after READY, only once; untick clears and closes; re-tick connects at once
    FakePipe p;
    p.discord_up = true;
    LinkState st;
    Tick(p, st, true, 0, hs, Act, log);
    Tick(p, st, true, 250, hs, Act, log);
    CHECK(p.IsOpen() && p.Count(kFrame, "SET_ACTIVITY") == 0);  // no READY yet
    p.inbound.push_back({kFrame, "{\"cmd\":\"DISPATCH\",\"evt\":\"READY\"}"});
    Tick(p, st, true, 500, hs, Act, log);
    CHECK(p.Count(kFrame, "\"activity\"") == 1 && st.shown);
    Tick(p, st, true, 750, hs, Act, log);
    CHECK(p.Count(kFrame, "\"activity\"") == 1);  // shown once
    Tick(p, st, false, 1000, hs, Act, log);
    CHECK(p.Count(kFrame, "\"args\":{}") == 1 && !p.IsOpen() && !st.ready && !st.shown);
    Tick(p, st, false, 1250, hs, Act, log);
    CHECK(p.opens == 1);  // off: no connect
    Tick(p, st, true, 1500, hs, Act, log);
    CHECK(p.opens == 2 && p.IsOpen() && p.Count(kHandshake, "client_id") == 2);
    p.inbound.push_back({kFrame, "{\"evt\": \"READY\"}"});
    Tick(p, st, true, 1750, hs, Act, log);
    CHECK(p.Count(kFrame, "\"activity\"") == 2);
  }
  {  // Discord closes the link (a bad id: CLOSE 4000), then comes back after the retry time
    FakePipe p;
    p.discord_up = true;
    LinkState st;
    Tick(p, st, true, 0, hs, Act, log);
    p.inbound.push_back({kFrame, "{\"evt\":\"READY\"}"});
    Tick(p, st, true, 250, hs, Act, log);
    CHECK(st.shown);
    p.inbound.push_back({kClose, "{\"code\":4000,\"message\":\"Invalid Client ID\"}"});
    events.clear();
    Tick(p, st, true, 500, hs, Act, log);
    CHECK(!p.IsOpen() && !st.ready && !st.shown && st.next_try_ms == 500 + retry_ms);
    CHECK(events.size() == 2 && events[0] == LinkEvent::kClosedByDiscord && events[1] == LinkEvent::kDropped);
    Tick(p, st, true, 500 + retry_ms - 1, hs, Act, log);
    CHECK(p.opens == 1);
    Tick(p, st, true, 500 + retry_ms, hs, Act, log);
    CHECK(p.opens == 2 && p.IsOpen());
    CHECK(p.Count(kFrame, "\"activity\"") == 1);  // not shown again before a new READY
    p.inbound.push_back({kFrame, "{\"evt\":\"READY\"}"});
    Tick(p, st, true, 750 + retry_ms, hs, Act, log);
    CHECK(p.Count(kFrame, "\"activity\"") == 2);
  }
  {  // a broken pipe (Discord quit): dropped, retried after kRetrySeconds
    FakePipe p;
    p.discord_up = true;
    LinkState st;
    Tick(p, st, true, 0, hs, Act, log);
    p.broken = true;
    Tick(p, st, true, 250, hs, Act, log);
    CHECK(!p.IsOpen() && st.next_try_ms == 250 + retry_ms);
  }
  {  // PING -> PONG with the same payload; the link stays up
    FakePipe p;
    p.discord_up = true;
    LinkState st;
    Tick(p, st, true, 0, hs, Act, log);
    p.inbound.push_back({kPing, "{\"nonce\":\"p1\"}"});
    Tick(p, st, true, 250, hs, Act, log);
    CHECK(p.IsOpen() && p.Count(kPong, "\"nonce\":\"p1\"") == 1);
  }
}

int main() {
  {  // encoding: little-endian opcode and length (300 = 0x12C: a big-endian or swapped writer differs)
    const std::string payload(300, 'a');
    const std::string f = EncodeFrame(kFrame, payload);
    CHECK(f.size() == 308);
    CHECK(f.substr(0, 8) == std::string("\x01\x00\x00\x00\x2C\x01\x00\x00", 8));
    CHECK(f.substr(8) == payload);
    CHECK(EncodeFrame(kHandshake, "{}") == std::string("\x00\x00\x00\x00\x02\x00\x00\x00{}", 10));
  }
  {  // decoding: whole frames only, two in a row, a partial one waits
    const std::string two = EncodeFrame(kFrame, "{\"evt\":\"READY\"}") + EncodeFrame(kClose, "{\"code\":4000}");
    uint32_t op = 99;
    std::string payload;
    size_t size = 0;
    CHECK(DecodeFrame(two, &op, &payload, &size));
    CHECK(op == kFrame && payload == "{\"evt\":\"READY\"}" && size == 8 + payload.size());
    CHECK(DecodeFrame(std::string_view(two).substr(size), &op, &payload, &size));
    CHECK(op == kClose && payload == "{\"code\":4000}");
    const std::string one = EncodeFrame(kFrame, "{\"x\":1}");
    CHECK(!DecodeFrame(std::string_view(one).substr(0, 7), &op, &payload, &size));               // header cut
    CHECK(!DecodeFrame(std::string_view(one).substr(0, one.size() - 1), &op, &payload, &size));  // payload cut
    CHECK(DecodeFrame(EncodeFrame(kFrame, ""), &op, &payload, &size) && payload.empty() && size == 8);
  }
  {  // JSON strings
    CHECK(JsonString("abc") == "\"abc\"");
    CHECK(JsonString("a\"b\\c") == "\"a\\\"b\\\\c\"");
    CHECK(JsonString(std::string("\n\t\x01", 3)) == "\"\\n\\t\\u0001\"");
  }
  {  // handshake and activity
    CHECK(HandshakeJson("123456789012345678") == "{\"v\":1,\"client_id\":\"123456789012345678\"}");
    const std::string on = ActivityJson(4242, 1762900000, "logo", false, 7);
    CHECK(Has(on, "\"cmd\":\"SET_ACTIVITY\""));
    CHECK(Has(on, "\"pid\":4242"));
    CHECK(Has(on, "\"timestamps\":{\"start\":1762900000}"));
    CHECK(Has(on, "\"assets\":{\"large_image\":\"logo\"}"));
    CHECK(Has(on, "\"nonce\":\"7\""));
    const std::string no_logo = ActivityJson(4242, 1762900000, "", false, 8);
    CHECK(Has(no_logo, "\"activity\":{") && !Has(no_logo, "assets"));
    const std::string clear = ActivityJson(4242, 1762900000, "logo", true, 9);
    CHECK(Has(clear, "\"pid\":4242") && !Has(clear, "activity") && !Has(clear, "timestamps") && Has(clear, "\"nonce\":\"9\""));
  }
  {  // events: compact or spaced JSON, never a substring of another key or value
    CHECK(HasEvent("{\"cmd\":\"DISPATCH\",\"evt\":\"READY\",\"data\":{}}", "READY"));
    CHECK(HasEvent("{\"cmd\": \"DISPATCH\", \"evt\" : \"READY\"}", "READY"));
    CHECK(!HasEvent("{\"cmd\":\"DISPATCH\",\"evt\":\"READY_X\"}", "READY"));
    CHECK(!HasEvent("{\"cmd\":\"DISPATCH\",\"evt\":null,\"data\":{\"x\":\"READY\"}}", "READY"));
    CHECK(HasEvent("{\"evt\":\"ERROR\",\"data\":{\"code\":4000}}", "ERROR") && !HasEvent("{\"evt\":\"ERROR\"}", "READY"));
  }
  RunLinkTests();
  if (g_failures == 0) {
    std::printf("PASSED: %d checks, 0 failures\n", g_checks);
    return 0;
  }
  std::printf("FAILED: %d checks, %d failures\n", g_checks, g_failures);
  return 1;
}

// Discord status: the frame format and the JSON (doa4_discord.h). No dependencies, so the host test builds it alone.

#include "doa4_discord.h"

#include <cstdio>

namespace doa4::discord {

namespace {
void PutU32(std::string& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(char((v >> (8 * i)) & 0xFF));
}
uint32_t GetU32(std::string_view b, size_t at) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= uint32_t(uint8_t(b[at + i])) << (8 * i);
  return v;
}
}  // namespace

std::string EncodeFrame(uint32_t opcode, std::string_view payload) {
  std::string out;
  out.reserve(8 + payload.size());
  PutU32(out, opcode);
  PutU32(out, uint32_t(payload.size()));
  out.append(payload);
  return out;
}

bool DecodeFrame(std::string_view bytes, uint32_t* opcode, std::string* payload, size_t* size) {
  if (bytes.size() < 8) return false;
  const uint32_t length = GetU32(bytes, 4);
  if (bytes.size() - 8 < length) return false;
  *opcode = GetU32(bytes, 0);
  payload->assign(bytes.substr(8, length));
  *size = 8 + size_t(length);
  return true;
}

std::string JsonString(std::string_view text) {
  std::string out = "\"";
  for (const char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (uint8_t(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", unsigned(uint8_t(c)));
          out += buf;
        } else {
          out.push_back(c);
        }
    }
  }
  out.push_back('"');
  return out;
}

std::string HandshakeJson(std::string_view app_id) {
  return "{\"v\":1,\"client_id\":" + JsonString(app_id) + "}";
}

std::string ActivityJson(uint32_t pid, int64_t start_unix, std::string_view logo, bool clear, uint64_t nonce) {
  std::string args = "{\"pid\":" + std::to_string(pid);
  if (!clear) {
    args += ",\"activity\":{\"timestamps\":{\"start\":" + std::to_string(start_unix) + "}";
    if (!logo.empty()) args += ",\"assets\":{\"large_image\":" + JsonString(logo) + "}";
    args += "}";
  }
  args += "}";
  return "{\"cmd\":\"SET_ACTIVITY\",\"args\":" + args + ",\"nonce\":" + JsonString(std::to_string(nonce)) + "}";
}

bool HasEvent(std::string_view payload, std::string_view evt) {
  const auto space = [&](size_t i) {
    while (i < payload.size() && (payload[i] == ' ' || payload[i] == '\t' || payload[i] == '\r' || payload[i] == '\n')) ++i;
    return i;
  };
  for (size_t at = payload.find("\"evt\""); at != std::string_view::npos; at = payload.find("\"evt\"", at + 1)) {
    size_t i = space(at + 5);
    if (i >= payload.size() || payload[i] != ':') continue;
    i = space(i + 1);
    if (i >= payload.size() || payload[i] != '"') continue;
    const size_t end = i + 1 + evt.size();
    if (payload.substr(i + 1, evt.size()) == evt && end < payload.size() && payload[end] == '"') return true;
  }
  return false;
}

}  // namespace doa4::discord

#include "doa4_config.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

#include <toml++/toml.hpp>

#include "doa4_utf8.h"

namespace doa4 {

namespace {

std::string_view Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

// The SDK loads the config with toml++ and drops the WHOLE file on a parse
// error (rex::cvar::LoadConfig), so `updated` must parse and must hold exactly
// `key = value_literal`, or, with no value_literal, must not hold `key` at all. Returns "" when it does, else why not.
std::string CheckLoadable(const std::string& updated, std::string_view key,
                          std::optional<std::string_view> value_literal) {
  try {
    toml::table table = toml::parse(updated);
    if (!value_literal) {
      if (table.contains(key)) return "the file still holds " + std::string(key) + " after its line was removed";
      return {};
    }
    toml::table expected = toml::parse(std::string(key) + " = " + std::string(*value_literal));
    if (table[key] != expected[key]) {
      return "the file has a form the menu cannot edit (" + std::string(key) + ")";
    }
  } catch (const toml::parse_error& e) {
    return "the file would not load (" + std::string(e.description()) + ")";
  }
  return {};
}

// The line-matching rules UpsertTomlKey and RemoveTomlKey share. `content` is a line trimmed of its ending.
bool IsTableHeader(std::string_view content) { return !content.empty() && content.front() == '['; }
bool IsKeyLine(std::string_view content, std::string_view key) {
  size_t eq = content.find('=');
  return !content.empty() && content.front() != '#' && eq != std::string_view::npos && Trim(content.substr(0, eq)) == key;
}

// Reads the whole file; a missing file reads as "". Returns "" or an error.
std::string ReadWhole(const std::filesystem::path& path, std::string* text) {
  std::ifstream in(path, std::ios::binary);
  if (in) {
    text->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  } else if (std::filesystem::exists(path)) {
    return "cannot read " + Utf8(path);
  }
  return {};
}

// The one writer of dead_or_alive_4.toml: `updated` goes to path + ".tmp", then is renamed over `path`.
std::string WriteViaTemp(const std::filesystem::path& path, const std::string& updated) {
  std::filesystem::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return "cannot write " + Utf8(tmp);
    out << updated;
    out.flush();
    if (!out) return "cannot write " + Utf8(tmp);
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::string error = "cannot replace " + Utf8(path) + ": " + ec.message();
    std::filesystem::remove(tmp, ec);
    return error;
  }
  return {};
}

}  // namespace

std::string UpsertTomlKey(std::string_view text, std::string_view key,
                          std::string_view value_literal) {
  const std::string_view nl = text.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
  const std::string new_line = std::string(key) + " = " + std::string(value_literal);

  std::string out;
  out.reserve(text.size() + new_line.size() + 2);
  // Set once the key is written. The first table header writes a missing key,
  // so nothing inside a table is ever matched.
  bool done = false;
  size_t pos = 0;
  if (text.substr(0, kUtf8Bom.size()) == kUtf8Bom) {
    out += kUtf8Bom;  // kept, and not part of the first line's key
    pos = kUtf8Bom.size();
  }
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    size_t next = end == std::string_view::npos ? text.size() : end + 1;
    std::string_view line = text.substr(pos, next - pos);  // with its line ending
    std::string_view content = Trim(line);
    if (!done && IsTableHeader(content)) {
      out += new_line;
      out += nl;
      done = true;
    }
    if (!done && IsKeyLine(content, key)) {
      out += new_line;
      // Keep this line's own ending ("\r\n", "\n" or none at end of file).
      size_t body = line.find_last_not_of("\r\n");
      out += line.substr(body == std::string_view::npos ? 0 : body + 1);
      done = true;
    } else {
      out += line;
    }
    pos = next;
  }
  if (!done) {
    if (!out.empty() && out.back() != '\n') out += nl;
    out += new_line;
    out += nl;
  }
  return out;
}

std::string RemoveTomlKey(std::string_view text, std::string_view key, bool* removed) {
  std::string out;
  out.reserve(text.size());
  bool any = false;
  bool in_table = false;  // the first table header ends the top level, as in UpsertTomlKey
  size_t pos = 0;
  if (text.substr(0, kUtf8Bom.size()) == kUtf8Bom) {
    out += kUtf8Bom;
    pos = kUtf8Bom.size();
  }
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    size_t next = end == std::string_view::npos ? text.size() : end + 1;
    std::string_view line = text.substr(pos, next - pos);
    std::string_view content = Trim(line);
    if (IsTableHeader(content)) in_table = true;
    if (!in_table && IsKeyLine(content, key)) {
      any = true;  // the whole line goes, with its ending
    } else {
      out += line;
    }
    pos = next;
  }
  if (removed) *removed = any;
  return any ? out : std::string(text);
}

std::string TomlString(std::string_view value) {
  std::string out = "\"";
  for (char c : value) {
    if (c == '\\' || c == '"') out += '\\';
    out += c;
  }
  out += '"';
  return out;
}

std::string SaveTomlKey(const std::filesystem::path& path, std::string_view key,
                        std::string_view value_literal) {
  std::string text;
  if (std::string error = ReadWhole(path, &text); !error.empty()) return error;
  std::string updated = UpsertTomlKey(text, key, value_literal);
  if (std::string error = CheckLoadable(updated, key, value_literal); !error.empty()) {
    return error + "; dead_or_alive_4.toml was left unchanged";
  }
  return WriteViaTemp(path, updated);
}

std::string RemoveTomlKeyFromFile(const std::filesystem::path& path, std::string_view key, bool* removed) {
  if (removed) *removed = false;
  std::string text;
  if (std::string error = ReadWhole(path, &text); !error.empty()) return error;
  bool any = false;
  std::string updated = RemoveTomlKey(text, key, &any);
  if (!any) return {};  // nothing to remove: the file is not touched
  if (std::string error = CheckLoadable(updated, key, std::nullopt); !error.empty()) {
    return error + "; dead_or_alive_4.toml was left unchanged";
  }
  if (std::string error = WriteViaTemp(path, updated); !error.empty()) return error;
  if (removed) *removed = true;
  return {};
}

}  // namespace doa4

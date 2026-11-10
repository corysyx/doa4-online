// Writes single settings into dead_or_alive_4.toml (the config the SDK loads
// at start-up) without touching the rest of the file. rex::cvar::SaveConfig is
// not used: it writes every non-default cvar, including the launcher's
// --game_data_root / --user_data_root.

#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace doa4 {

// `text` with the top-level line for `key` set to `key = value_literal`.
// A top-level line is one before the first [table] header; its key is the
// text before its first '=', trimmed. Other lines are kept byte for byte.
// A missing key is inserted before the first table header, or appended (a
// missing final newline is added first). New lines use "\r\n" if `text`
// contains one, else "\n".
std::string UpsertTomlKey(std::string_view text, std::string_view key,
                          std::string_view value_literal);

// `text` without the top-level line(s) for `key` (same top-level-only rule as UpsertTomlKey: nothing after the
// first [table] header is matched). Every other byte is unchanged. *removed (if not null) says whether a line went;
// when none did the result is `text` itself.
std::string RemoveTomlKey(std::string_view text, std::string_view key, bool* removed);

// A TOML basic string: "..." with \ and " escaped.
std::string TomlString(std::string_view value);

// UpsertTomlKey on the file at `path` (created if missing), written to
// `path` + ".tmp" and renamed over `path`. Returns "" on success, else a
// description of the failure.
std::string SaveTomlKey(const std::filesystem::path& path, std::string_view key,
                        std::string_view value_literal);

// RemoveTomlKey on the file at `path`. Writes only if a line was removed, through the same temp-file-and-rename
// writer as SaveTomlKey and the same loadability check (the result must parse and no longer hold the key). A
// missing file or a file without the key is left alone. Returns "" or a description of the failure; *removed is
// true only if the file now lacks a line it had.
std::string RemoveTomlKeyFromFile(const std::filesystem::path& path, std::string_view key, bool* removed);

// The start-up removal of a saved audio_speakers line: RemoveTomlKeyFromFile(path, "audio_speakers", removed), except
// that a session process (one started from an online hand-off) leaves the file untouched, with *removed false, because
// the start-up snapshot of the player's settings is taken after this runs.
std::string RemoveSavedSpeakersLine(const std::filesystem::path& path, bool session_process, bool* removed);

}  // namespace doa4

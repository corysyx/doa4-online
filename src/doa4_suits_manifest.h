// The DOAX2 swimsuit import folder's manifest (<user data>/doax2_suits/manifest.txt).
//
// Text, one item per line:
//   doax2-suits 1
//   converter <name: 1-32 of A-Za-z0-9._->
//   suit <char> <own:N|append> <body tpr> <body cat> <part tpr|-> <part cat|->
//        <design> <colour> <tpr file> <tpr size> <tpr sha1> <cat file> <cat size> <cat sha1>
// (one line per suit; numbers decimal; blank lines and '#' lines ignored; CRLF line ends and a
// UTF-8 BOM accepted). Suit order is costume order: a character's suits become her next costumes
// in the order they appear; a design and colour (Suit::Label) may be listed once. Pure C++ (no SDK),
// so host tests build it alone.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace doa4::suits {

inline constexpr uint16_t kNoPart = 0xFFFF;
inline constexpr uint32_t kCharCount = 25;
inline constexpr uint32_t kFileCount = 1640;

struct SuitFile {
  std::string name;  // a plain file name inside the import folder (SafeFileName)
  uint64_t size = 0;
  std::string sha1;  // 40 lowercase hex digits
  bool operator==(const SuitFile&) const = default;
};

struct Suit {
  uint8_t character = 0;        // DOA4 charId (Tina 1, Kasumi 5, Helena 7, ...)
  int own_model = -1;           // her empty model record to fill, or -1 to append a record
  uint16_t body_tpr = 0;        // global file numbers of the placeholder pair the suit replaces
  uint16_t body_cat = 0;
  uint16_t part_tpr = kNoPart;  // her head/hair part, or kNoPart (Tina: the head is in the body)
  uint16_t part_cat = kNoPart;
  std::string design;           // DOAX2 design name, e.g. "vks_ch_d"
  int colour = 0;
  SuitFile tpr;
  SuitFile cat;
  bool operator==(const Suit&) const = default;
  std::string Label() const;  // "vks_ch_d c0"
};

struct Manifest {
  std::string converter;
  std::vector<Suit> suits;
  bool operator==(const Manifest&) const = default;
};

struct ParseResult {
  std::optional<Manifest> manifest;
  std::string error;  // why there is no manifest: "line N: ..." or a whole-file reason
};

ParseResult ParseManifest(std::string_view text);

// The canonical text (LF line ends, no comments). For a manifest ParseManifest accepted,
// ParseManifest(FormatManifest(m)) == m; a writer (Plan 2) re-parses its output before replacing
// manifest.txt.
std::string FormatManifest(const Manifest& manifest);

// A plain file name: 1-64 of [A-Za-z0-9_.-], not starting with '.'.
bool SafeFileName(std::string_view name);

}  // namespace doa4::suits

// The DOAX2 swimsuit import, read at start (spec sections 3 and 6). <user data>/doax2_suits holds
// manifest.txt and the converted files; LoadImport checks everything before the game sees any of
// it: the manifest, each suit against the game's tables (costumes::CheckSuit), each placeholder
// entry in the game's archives (16 bytes, every one 0x77), each file's size and SHA-1. A suit that
// fails a check is left out with a reason; the others load. Checked files stay open without write
// sharing, so they cannot change under the game.
//
// Order: LoadImport reads the game's tables (through CheckSuit) BEFORE any patch, so call it
// before costumes::Apply; after Apply a patched girl's list and variant pointers are on the heap
// and every one of her suits would be refused.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "doa4_afs_overlay.h"
#include "doa4_costumes.h"
#include "doa4_guest.h"
#include "doa4_suits_manifest.h"

namespace doa4::suits {

// An empty costume slot in the game's archives (dmy.bin): sixteen bytes, every one 0x77. In the
// real archives 68 of the 69 sixteen-byte entries are that; the 69th, chrone entry 0 (file 204),
// is 00 00 00 01 and zeros, real data, so the size alone does not make a slot empty.
inline constexpr uint64_t kPlaceholderSize = 16;
inline constexpr uint8_t kPlaceholderByte = 0x77;
inline constexpr uint64_t kManifestLimit = 1 << 20;

struct LoadedSuit {
  Suit suit;
  std::shared_ptr<const afs::Source> tpr;
  std::shared_ptr<const afs::Source> cat;
};

struct ImportStatus {
  bool folder = false;               // the import folder exists
  std::filesystem::path path;
  std::string fingerprint;           // SHA-1 of the canonical manifest text; empty without one
  std::vector<LoadedSuit> suits;     // passed every check, in manifest order
  std::vector<std::string> skipped;  // "<label>: <why>", one per suit left out
  std::string problem;               // why nothing loaded, or empty
  std::string Summary() const;
  void Fail(const std::string& why);  // nothing loads: the problem is set, the suits dropped
};

ImportStatus LoadImport(const std::filesystem::path& folder, const std::filesystem::path& game_root,
                        const GuestBytes& guest);

// The combined archives the loaded suits need, by archive name, over the game folder's archives.
// An empty map and *error when one cannot be built (nothing partial); `error` must not be null.
// The game's archives are opened read-only and stay open for as long as the overlays live. Each
// entry about to be replaced is checked again on that open (still an empty slot: 16 bytes of
// kPlaceholderByte); if the archive changed since LoadImport, the error is
// "<archive> changed since the import was checked (entry E)".
std::map<std::string, std::shared_ptr<const afs::Overlay>> BuildOverlays(
    const ImportStatus& status, const std::filesystem::path& game_root, const GuestBytes& guest,
    std::string* error);

// Per archive, per replaced entry: "<label> (tpr)" / "<label> (cat)", for the first-read logs.
std::map<std::string, std::map<uint32_t, std::string>> EntryLabels(const ImportStatus& status,
                                                                   const GuestBytes& guest);

std::vector<Suit> SuitsOf(const ImportStatus& status);

// The load-request guard's empty-slot marks: sets state->empty_slot to every model (below
// state->records[c], at most 256) of every character whose body .tpr or body .cat is an empty
// costume slot (ClassifySlot: 16 bytes of kPlaceholderByte) in the archive AS THE GAME READS IT:
// the combined archive in `overlays` (by archive name) when that archive has one, else the game
// folder's own archive, opened read-only once with its TOC read once. Everything is judged on the
// FINAL tables: each character's model records are read through `guest` now (after
// costumes::Apply these are her appended copy), and `state` should be Apply's guard (its counts
// include the appended models). Earlier marks are cleared first. A body file with no location (not
// in a character archive) or an entry past the archive's end marks nothing. An archive that cannot
// be opened or whose TOC cannot be read marks nothing for its files; the result then names it
// ("chrone.afs: ..."), one note per archive joined with "; ". Returns "" when every archive
// that was needed was read.
std::string MarkEmptySlots(costumes::GuardState* state, const GuestBytes& guest,
                           const std::filesystem::path& game_root,
                           const std::map<std::string, std::shared_ptr<const afs::Overlay>>& overlays);

}  // namespace doa4::suits

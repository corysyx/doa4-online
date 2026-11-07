// Suit converter (Plan 2): the checked disc and game-file inputs. Reads the player's DOAX2 disc (an image file or an
// extracted folder) and the three DOA4 game archives, and refuses anything that is not exactly the entries the recipe
// tables name: size first, then SHA-1.
//
// An image is read through ONE afs::FileSource over the image file (positional reads, safe from any thread), with an
// adapter for xdvdfs::ReadVolume, xdvdfs::FindFile for the three archives and afs::ReadToc per archive. A folder: the
// archives are looked up case-insensitively in its root (chr.afs, werone.afs, wertwo.afs / chrone.afs, chrtwo.afs,
// chrthree.afs), one afs::FileSource each. The files stay open for the life of the InputSet, with writing by others
// refused, so a checked file cannot change under the converter; Read re-hashes anyway.
//
// Blocking reads: Open, CheckDisc, CheckGame and Read all do blocking, synchronous file reads on the calling thread. The
// CancelToken is looked at between entries and between 4 MiB hash chunks only, so a read that stalls (a OneDrive
// placeholder being fetched, a sleeping disc) is NOT interrupted by the token. A caller that needs a bounded cancel
// cancels the reading thread's pending I/O with CancelSynchronousIo (the converter's driver does, Task 12); the read then
// fails and the check returns kReadError, which the caller reads as cancelled.
//
// Plain C++ plus Win32 file reads (afs::FileSource) and CNG SHA-1 (gamefiles::Sha1): host tests build it.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "doa4_afs_overlay.h"
#include "doa4_suitconv_recipe.h"

namespace doa4::suitconv {

// What the converter read is not what CheckDisc / CheckGame verified (bytes differ from the entry's SHA-1, or the
// entry cannot be read any more). what() names the entry by its label.
class InputChanged : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

enum class InputStatus {
  kOk,
  kNotFound,    // the path does not exist
  kNotXbox,     // a file that is not an XDVDFS image
  kNoArchives,  // a folder, or an image volume, without one of the archives
  kMismatch,    // an entry differs from the table, or its archive's TOC does not parse or has no such entry
  kReadError,   // an I/O failure (the file is in detail), or a damaged XDVDFS table
  kCancelled,
};

struct InputCheck {
  InputStatus status = InputStatus::kOk;
  std::vector<std::string> mismatched;  // entry labels, "<arc> <index>", in table order
  uint64_t bytes_checked = 0;           // bytes hashed
  std::string detail;                   // for the status other than kOk, in words
};

class InputSet {
 public:
  using ArchiveSources = std::vector<std::pair<ArcId, std::shared_ptr<const afs::Source>>>;

  // Opens the disc (an image file or a folder) and the game folder. Never reads an entry's bytes (CheckDisc and
  // CheckGame do); a path that cannot be used is reported by that check's status, so the caller sees one answer per
  // side. Returns null with *error only for tables that do not fit (a disc entry in a game archive and the reverse,
  // an sha1 that is not 40 hex digits). The tables' sha1 strings must outlive the InputSet.
  static std::unique_ptr<InputSet> Open(const std::filesystem::path& doax2, const std::filesystem::path& game_root,
                                        std::span<const EntryRef> disc, std::span<const EntryRef> game,
                                        std::string* error);

  // The same with the disc given as an already open image source (what Open builds for an image file); `label` names
  // it in reports. Used by Open's image path and by tests that need an image whose reads can fail.
  static std::unique_ptr<InputSet> OpenImageSource(std::shared_ptr<const afs::Source> image, const std::string& label,
                                                   const std::filesystem::path& game_root,
                                                   std::span<const EntryRef> disc, std::span<const EntryRef> game,
                                                   std::string* error);

  // The same over archives already open as sources (what Open builds from the image and the folders). Used by Open,
  // and by tests that need a source whose bytes can change or whose entries are large.
  static std::unique_ptr<InputSet> OpenSources(ArchiveSources disc_archives, ArchiveSources game_archives,
                                               std::span<const EntryRef> disc, std::span<const EntryRef> game,
                                               std::string* error);
  ~InputSet();
  InputSet(const InputSet&) = delete;
  InputSet& operator=(const InputSet&) = delete;

  // Every entry of the table: size first, then SHA-1 (hashed in chunks of at most 4 MiB, the token checked between
  // chunks and between entries). bytes_done gets the running total of bytes hashed, never decreasing; it may be empty.
  // Every mismatched entry is listed; a read error or a cancel returns at once.
  InputCheck CheckDisc(const CancelToken& cancel, const std::function<void(uint64_t)>& bytes_done);
  InputCheck CheckGame(const CancelToken& cancel, const std::function<void(uint64_t)>& bytes_done);

  // The entry's bytes, re-hashed against entry.sha1 (and its size against entry.size) on every call. Thread-safe.
  // Throws InputChanged on a difference or a failed read.
  std::shared_ptr<const std::vector<uint8_t>> Read(const EntryRef& entry);

 struct Impl;  // the implementation's state (public only so the .cpp's helpers can name it)

 private:
  explicit InputSet(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace doa4::suitconv

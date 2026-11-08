// A CRI AFS archive with some entries replaced, served on the fly: nothing is written to disk.
//
// DOA4's AFS reader (sub_829BB5D0) ignores every TOC offset after entry 0: it derives each
// entry's start as entry 0's offset + the sum of the earlier entries' sizes rounded up to 2048,
// and sizes its per-archive state for the original entry count. So the combined archive keeps
// the entry count and entry 0's offset, lays the entries out back to back on 2048-byte
// boundaries in index order (zero padding), rewrites the TOC offsets and sizes, and puts the
// name table (48 bytes per entry) after the last entry, padded to 2048; name record i's last u32
// holds TOC word i (word 0 = the entry count), as in the game's archives. With no replacements
// the combined archive is byte-identical to the original: the rule
// the stage-0 archive rebuild script (Python, not in this repository) checked on the game's own archives.
// Pure C++ apart from FileSource (Win32 reads), so host tests build it.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace doa4::afs {

inline constexpr uint64_t kSector = 2048;

// Positional reads. Read fills all n bytes or returns false; implementations allow concurrent
// Read calls (the game reads an archive from several threads).
class Source {
 public:
  virtual ~Source() = default;
  virtual uint64_t size() const = 0;
  virtual bool Read(uint64_t offset, void* dst, size_t n) const = 0;
};

class MemorySource final : public Source {
 public:
  explicit MemorySource(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
  uint64_t size() const override { return bytes_.size(); }
  bool Read(uint64_t offset, void* dst, size_t n) const override;

 private:
  std::vector<uint8_t> bytes_;
};

// A host file opened for reading. Others may read it but not write it while it is open, so a
// checked file cannot change under the game. Reads pass their offset in an OVERLAPPED, so
// concurrent calls are safe. Open returns null when the file cannot be opened.
class FileSource final : public Source {
 public:
  static std::shared_ptr<FileSource> Open(const std::filesystem::path& path);
  ~FileSource() override;
  FileSource(const FileSource&) = delete;
  FileSource& operator=(const FileSource&) = delete;
  uint64_t size() const override { return size_; }
  bool Read(uint64_t offset, void* dst, size_t n) const override;

 private:
  FileSource(void* handle, uint64_t size) : handle_(handle), size_(size) {}
  void* handle_;
  uint64_t size_;
};

struct Toc {
  uint32_t count = 0;
  std::vector<uint32_t> offsets;
  std::vector<uint32_t> sizes;
  uint32_t names_offset = 0;
  uint32_t names_size = 0;
};

// The archive's TOC, checked: "AFS\0", 1-65536 entries, every entry and the name table inside
// the file, entry 0 after the TOC. *error says why when it returns nothing.
std::optional<Toc> ReadToc(const Source& archive, std::string* error);

struct Replacement {
  uint32_t entry = 0;
  std::shared_ptr<const Source> data;
};

class Overlay {
 public:
  // The combined archive, or null with *error (error may be null).
  static std::shared_ptr<const Overlay> Build(std::shared_ptr<const Source> original,
                                              std::vector<Replacement> replacements,
                                              std::string* error);
  Overlay(const Overlay&) = delete;
  Overlay& operator=(const Overlay&) = delete;

  uint64_t size() const { return size_; }
  uint64_t original_size() const { return original_->size(); }
  // Copies [offset, offset + n) of the combined archive; false when that runs past the end or a
  // source read fails.
  bool Read(uint64_t offset, void* dst, size_t n) const;
  uint64_t entry_offset(uint32_t i) const { return entry_offsets_[i]; }
  uint64_t entry_size(uint32_t i) const { return entry_sizes_[i]; }
  // The replaced entry whose bytes hold `offset`, or -1.
  int ReplacedEntryAt(uint64_t offset) const;

 private:
  Overlay() = default;
  struct Segment {
    uint64_t start = 0;
    uint64_t length = 0;
    const Source* source = nullptr;             // read from a source, or
    uint64_t source_offset = 0;
    const std::vector<uint8_t>* bytes = nullptr;  // copy from memory, or (both null) zeros
    int replaced_entry = -1;
  };
  const Segment* Find(uint64_t offset) const;

  std::shared_ptr<const Source> original_;
  std::vector<Replacement> replacements_;  // keeps the replacement sources alive
  std::vector<uint8_t> header_;            // [0, entry 0) with the rewritten TOC
  std::vector<uint8_t> names_;             // the name table with its rewritten last words
  std::vector<Segment> segments_;          // in offset order, no gaps
  std::vector<uint64_t> entry_offsets_;
  std::vector<uint64_t> entry_sizes_;
  uint64_t size_ = 0;
};

}  // namespace doa4::afs

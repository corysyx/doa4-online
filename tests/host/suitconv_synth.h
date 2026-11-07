// Synthetic inputs for the suit converter's input-layer tests: seeded AFS archives, Xbox disc images (XDVDFS, through
// xdvdfs_fixtures.h) written as sparse files, folders, and an in-memory archive source whose bytes can change. No byte
// of any game file is used; every payload is made here from a seed. Header-only; Windows (sparse files).

#pragma once

#include <windows.h>
#include <winioctl.h>

#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "../../src/doa4_afs_overlay.h"
#include "../../src/doa4_gamefiles.h"
#include "../../src/doa4_suitconv_recipe.h"
#include "xdvdfs_fixtures.h"

namespace synth {

namespace fs = std::filesystem;
using doa4::suitconv::ArcId;
using doa4::suitconv::EntryRef;

// Deterministic payload: a 32-bit LCG's high byte, so neighbouring bytes differ and a flipped byte is never a repeat.
inline std::vector<uint8_t> Seeded(uint32_t seed, size_t n) {
  std::vector<uint8_t> v(n);
  uint32_t x = seed * 2654435761u + 12345u;
  for (size_t i = 0; i < n; ++i) {
    x = x * 1664525u + 1013904223u;
    v[i] = uint8_t(x >> 24);
  }
  return v;
}

inline void Put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }

// An AFS archive as the game's are laid out: "AFS\0", the count, (offset, size) pairs, the name table's offset and
// size, entry 0 at 0x800 (for up to 255 entries), every entry on a 2048-byte boundary, then the 48-byte-per-entry
// name table, the file padded to a sector.
inline std::vector<uint8_t> BuildAfs(const std::vector<std::vector<uint8_t>>& entries) {
  const size_t n = entries.size();
  std::vector<uint32_t> off(n);
  uint32_t at = 0x800;
  for (size_t i = 0; i < n; ++i) {
    off[i] = at;
    at += uint32_t((entries[i].size() + 2047) / 2048 * 2048);
  }
  const uint32_t names = at;
  const uint32_t total = (names + 48 * uint32_t(n) + 2047) / 2048 * 2048;
  std::vector<uint8_t> a(total, 0);
  std::memcpy(a.data(), "AFS\0", 4);
  Put32(a, 4, uint32_t(n));
  for (size_t i = 0; i < n; ++i) {
    Put32(a, 8 + 8 * i, off[i]);
    Put32(a, 12 + 8 * i, uint32_t(entries[i].size()));
    if (!entries[i].empty()) std::memcpy(&a[off[i]], entries[i].data(), entries[i].size());
  }
  Put32(a, 8 + 8 * n, names);
  Put32(a, 12 + 8 * n, uint32_t(48 * n));
  for (size_t i = 0; i < n; ++i) {
    const std::string name = "e" + std::to_string(i) + ".bin";
    std::memcpy(&a[names + 48 * i], name.data(), name.size());
  }
  return a;
}

// An archive and the entries it holds (kept so a test can build the table and flip a byte).
struct Archive {
  ArcId arc;
  std::vector<std::vector<uint8_t>> entries;
  std::vector<uint8_t> file;
};
inline Archive MakeArchive(ArcId arc, const std::vector<size_t>& sizes, uint32_t seed) {
  Archive a{arc, {}, {}};
  for (size_t i = 0; i < sizes.size(); ++i) a.entries.push_back(Seeded(seed * 100 + uint32_t(i), sizes[i]));
  a.file = BuildAfs(a.entries);
  return a;
}

// A table of EntryRefs whose sha1 strings it owns (EntryRef holds a const char*).
struct Table {
  std::deque<std::string> sha;
  std::vector<EntryRef> refs;
  void Add(const Archive& a, uint32_t index) {
    sha.push_back(doa4::gamefiles::Sha1Hex(a.entries[index].data(), a.entries[index].size()));
    refs.push_back(EntryRef{a.arc, index, a.entries[index].size(), sha.back().c_str()});
  }
  // An entry whose recorded size or sha1 is chosen by the test.
  void AddRaw(ArcId arc, uint32_t index, uint64_t size, const std::string& sha1) {
    sha.push_back(sha1);
    refs.push_back(EntryRef{arc, index, size, sha.back().c_str()});
  }
  uint64_t total() const {
    uint64_t t = 0;
    for (const EntryRef& r : refs) t += r.size;
    return t;
  }
};

inline void WriteFileBytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create " + path.string());
  DWORD put = 0;
  const bool ok = bytes.empty() || WriteFile(h, bytes.data(), DWORD(bytes.size()), &put, nullptr);
  CloseHandle(h);
  if (!ok || put != bytes.size()) throw std::runtime_error("cannot write " + path.string());
}

// An image file whose volume starts at `image.base`: sparse, so a volume at 0xFD90000 does not write 265 MB of
// zeros. Returns the file's size on disk (the allocated bytes) so the report can state it.
inline uint64_t WriteSparseImage(const fs::path& path, const doa4::xdvdfs::fixtures::BuiltImage& image) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create " + path.string());
  DWORD ret = 0;
  DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &ret, nullptr);
  LARGE_INTEGER at;
  at.QuadPart = LONGLONG(image.base);
  DWORD put = 0;
  const bool ok = SetFilePointerEx(h, at, nullptr, FILE_BEGIN) &&
                  WriteFile(h, image.bytes.data(), DWORD(image.bytes.size()), &put, nullptr);
  CloseHandle(h);
  if (!ok || put != image.bytes.size()) throw std::runtime_error("cannot write " + path.string());
  DWORD high = 0;
  const DWORD low = GetCompressedFileSizeW(path.c_str(), &high);
  return (uint64_t(high) << 32) | low;
}

// A volume holding the archives at its root under the given names.
inline doa4::xdvdfs::fixtures::BuiltImage BuildVolume(const std::vector<std::pair<std::string, const Archive*>>& files,
                                                      uint64_t base) {
  std::vector<doa4::xdvdfs::fixtures::FakeFile> f;
  for (const auto& [name, a] : files) f.push_back({name, std::string(a->file.begin(), a->file.end())});
  return doa4::xdvdfs::fixtures::BuildImage(f, base);
}

// An archive held in memory whose bytes a test can change after the fact; reads see the current bytes.
class MemArchive final : public doa4::afs::Source {
 public:
  explicit MemArchive(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
  uint64_t size() const override { return bytes_.size(); }
  bool Read(uint64_t offset, void* dst, size_t n) const override {
    if (offset > bytes_.size() || n > bytes_.size() - offset) return false;
    std::memcpy(dst, bytes_.data() + offset, n);
    return true;
  }
  std::vector<uint8_t>& bytes() { return bytes_; }

 private:
  std::vector<uint8_t> bytes_;
};

}  // namespace synth

// The combined archive. See doa4_afs_overlay.h.

#include "doa4_afs_overlay.h"

#include <algorithm>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace doa4::afs {

namespace {

uint64_t Align(uint64_t v) { return (v + kSector - 1) / kSector * kSector; }

uint32_t Get32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

void Put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}

}  // namespace

bool MemorySource::Read(uint64_t offset, void* dst, size_t n) const {
  if (offset > bytes_.size() || n > bytes_.size() - offset) return false;
  if (n) std::memcpy(dst, bytes_.data() + offset, n);
  return true;
}

std::shared_ptr<FileSource> FileSource::Open(const std::filesystem::path& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return nullptr;
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(h, &size)) {
    CloseHandle(h);
    return nullptr;
  }
  return std::shared_ptr<FileSource>(new FileSource(h, uint64_t(size.QuadPart)));
}

FileSource::~FileSource() { CloseHandle(static_cast<HANDLE>(handle_)); }

bool FileSource::Read(uint64_t offset, void* dst, size_t n) const {
  if (offset > size_ || n > size_ - offset) return false;
  auto* out = static_cast<uint8_t*>(dst);
  while (n > 0) {
    const DWORD chunk = DWORD(std::min<size_t>(n, size_t(1) << 30));
    OVERLAPPED ov{};
    ov.Offset = DWORD(offset);
    ov.OffsetHigh = DWORD(offset >> 32);
    DWORD got = 0;
    if (!ReadFile(static_cast<HANDLE>(handle_), out, chunk, &got, &ov) || got != chunk) {
      return false;
    }
    out += got;
    offset += got;
    n -= got;
  }
  return true;
}

std::optional<Toc> ReadToc(const Source& archive, std::string* error) {
  auto fail = [&](const std::string& why) -> std::optional<Toc> {
    if (error) *error = why;
    return std::nullopt;
  };
  uint8_t head[8];
  if (archive.size() < 16 || !archive.Read(0, head, 8)) return fail("too small to be an AFS archive");
  if (std::memcmp(head, "AFS\0", 4) != 0) return fail("not an AFS archive");
  const uint32_t n = Get32(head + 4);
  if (n == 0 || n > 0x10000) return fail("bad entry count " + std::to_string(n));
  std::vector<uint8_t> tab(8 * size_t(n) + 8);
  if (!archive.Read(8, tab.data(), tab.size())) return fail("the TOC runs past the end");
  Toc toc;
  toc.count = n;
  for (uint32_t i = 0; i < n; ++i) {
    toc.offsets.push_back(Get32(&tab[8 * size_t(i)]));
    toc.sizes.push_back(Get32(&tab[8 * size_t(i) + 4]));
    if (uint64_t(toc.offsets[i]) + toc.sizes[i] > archive.size()) {
      return fail("entry " + std::to_string(i) + " runs past the end");
    }
  }
  toc.names_offset = Get32(&tab[8 * size_t(n)]);
  toc.names_size = Get32(&tab[8 * size_t(n) + 4]);
  if (uint64_t(toc.names_offset) + toc.names_size > archive.size()) {
    return fail("the name table runs past the end");
  }
  if (toc.offsets[0] < 8 + 8 * uint64_t(n) + 8) return fail("entry 0 overlaps the TOC");
  return toc;
}

std::shared_ptr<const Overlay> Overlay::Build(std::shared_ptr<const Source> original,
                                              std::vector<Replacement> replacements,
                                              std::string* error) {
  auto fail = [&](const std::string& why) -> std::shared_ptr<const Overlay> {
    if (error) *error = why;
    return nullptr;
  };
  if (!original) return fail("no archive");
  std::string toc_error;
  const std::optional<Toc> toc = ReadToc(*original, &toc_error);
  if (!toc) return fail(toc_error);
  const uint32_t n = toc->count;
  if (uint64_t(toc->names_size) != 0x30 * uint64_t(n)) {
    return fail("the name table is not 48 bytes per entry");
  }
  std::vector<const Source*> by_entry(n, nullptr);
  for (const Replacement& r : replacements) {
    const std::string which = "replacement for entry " + std::to_string(r.entry);
    if (r.entry >= n) return fail(which + " of " + std::to_string(n));
    if (!r.data) return fail(which + " has no data");
    if (by_entry[r.entry]) return fail("entry " + std::to_string(r.entry) + " replaced twice");
    if (r.data->size() == 0) return fail(which + " is empty");
    if (r.data->size() > 0xFFFFFFFFull) return fail(which + " is over 4 GiB");
    by_entry[r.entry] = r.data.get();
  }

  std::shared_ptr<Overlay> o(new Overlay());
  o->original_ = original;
  o->replacements_ = std::move(replacements);
  const uint64_t first = toc->offsets[0];
  o->header_.resize(size_t(first));
  if (!original->Read(0, o->header_.data(), o->header_.size())) {
    return fail("cannot read the archive header");
  }
  o->names_.resize(toc->names_size);
  if (!original->Read(toc->names_offset, o->names_.data(), o->names_.size())) {
    return fail("cannot read the name table");
  }

  std::vector<uint32_t> words{n};  // TOC words: count, then offset/size per entry
  uint64_t pos = first;
  for (uint32_t i = 0; i < n; ++i) {
    const uint64_t size = by_entry[i] ? by_entry[i]->size() : toc->sizes[i];
    o->entry_offsets_.push_back(pos);
    o->entry_sizes_.push_back(size);
    words.push_back(uint32_t(pos));
    words.push_back(uint32_t(size));
    pos = Align(pos + size);
    if (pos > 0xFFFFFFFFull) return fail("the combined archive would be over 4 GiB");
  }
  const uint64_t names_at = pos;
  for (uint32_t i = 0; i < n; ++i) {
    Put32(&o->header_[8 + 8 * size_t(i)], uint32_t(o->entry_offsets_[i]));
    Put32(&o->header_[12 + 8 * size_t(i)], uint32_t(o->entry_sizes_[i]));
    Put32(&o->names_[0x30 * size_t(i) + 44], words[i]);
  }
  Put32(&o->header_[8 + 8 * size_t(n)], uint32_t(names_at));
  Put32(&o->header_[12 + 8 * size_t(n)], toc->names_size);

  auto add = [&](Segment s) {
    if (s.length) o->segments_.push_back(s);
  };
  add({0, first, nullptr, 0, &o->header_, -1});
  for (uint32_t i = 0; i < n; ++i) {
    const uint64_t start = o->entry_offsets_[i], length = o->entry_sizes_[i];
    if (by_entry[i]) {
      add({start, length, by_entry[i], 0, nullptr, int(i)});
    } else {
      add({start, length, original.get(), toc->offsets[i], nullptr, -1});
    }
    add({start + length, Align(start + length) - (start + length), nullptr, 0, nullptr, -1});
  }
  add({names_at, toc->names_size, nullptr, 0, &o->names_, -1});
  const uint64_t names_end = names_at + toc->names_size;
  add({names_end, Align(names_end) - names_end, nullptr, 0, nullptr, -1});
  o->size_ = Align(names_end);
  return o;
}

const Overlay::Segment* Overlay::Find(uint64_t offset) const {
  auto it = std::upper_bound(segments_.begin(), segments_.end(), offset,
                             [](uint64_t v, const Segment& s) { return v < s.start + s.length; });
  return it == segments_.end() ? nullptr : &*it;
}

bool Overlay::Read(uint64_t offset, void* dst, size_t n) const {
  if (offset > size_ || n > size_ - offset) return false;
  auto* out = static_cast<uint8_t*>(dst);
  const Segment* s = n ? Find(offset) : nullptr;
  while (n > 0) {
    if (!s || s == segments_.data() + segments_.size()) return false;
    const uint64_t within = offset - s->start;
    const size_t take = size_t(std::min<uint64_t>(n, s->length - within));
    if (s->source) {
      if (!s->source->Read(s->source_offset + within, out, take)) return false;
    } else if (s->bytes) {
      std::memcpy(out, s->bytes->data() + within, take);
    } else {
      std::memset(out, 0, take);
    }
    out += take;
    offset += take;
    n -= take;
    ++s;
  }
  return true;
}

int Overlay::ReplacedEntryAt(uint64_t offset) const {
  const Segment* s = Find(offset);
  return s ? s->replaced_entry : -1;
}

}  // namespace doa4::afs

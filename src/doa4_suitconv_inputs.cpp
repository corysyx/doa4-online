// Suit converter (Plan 2): the checked disc and game-file inputs. See doa4_suitconv_inputs.h.

#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_inputs.h"

#include <windows.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <system_error>

#include "doa4_gamefiles.h"
#include "doa4_xdvdfs.h"

namespace doa4::suitconv {

namespace fs = std::filesystem;

namespace {

constexpr size_t kChunk = size_t(4) << 20;  // the hash loop checks the cancel token at least every 4 MiB

std::string Utf8(const fs::path& p) {
  const std::wstring w = p.wstring();
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(size_t(n > 0 ? n : 0), '\0');
  if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::string LowerAscii(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  return s;
}

// A window onto one archive inside an image file: the archive's bytes, positionally, through the image's one shared
// afs::FileSource.
class SubSource final : public afs::Source {
 public:
  SubSource(std::shared_ptr<const afs::Source> parent, uint64_t base, uint64_t length)
      : parent_(std::move(parent)), base_(base), length_(length) {}
  uint64_t size() const override { return length_; }
  bool Read(uint64_t offset, void* dst, size_t n) const override {
    if (offset > length_ || n > length_ - offset) return false;
    return parent_->Read(base_ + offset, dst, n);
  }

 private:
  std::shared_ptr<const afs::Source> parent_;
  uint64_t base_, length_;
};

// xdvdfs::ReadVolume wants a non-const Source; this is the adapter over the image's afs::FileSource.
class XdvdfsAdapter final : public xdvdfs::Source {
 public:
  explicit XdvdfsAdapter(const afs::Source& s) : s_(s) {}
  uint64_t size() const override { return s_.size(); }
  bool Read(uint64_t offset, void* dst, size_t n) override { return s_.Read(offset, dst, n); }

 private:
  const afs::Source& s_;
};

struct Archive {
  std::shared_ptr<const afs::Source> src;
  std::optional<afs::Toc> toc;
  std::string toc_error;
  std::string where;  // the file the bytes come from, for reports
};

struct Side {
  InputStatus status = InputStatus::kOk;  // what opening this side found
  std::string detail;
  std::map<Arc, Archive> arcs;
  std::vector<EntryRef> entries;
};

std::vector<Arc> NeededArcs(const std::vector<EntryRef>& entries) {
  std::vector<Arc> out;
  for (const EntryRef& e : entries)
    if (std::find(out.begin(), out.end(), e.arc) == out.end()) out.push_back(e.arc);
  return out;
}

void Fail(Side& side, InputStatus status, std::string detail) {
  side.status = status;
  side.detail = std::move(detail);
  side.arcs.clear();
}

void ReadTocs(Side& side) {
  for (auto& [arc, a] : side.arcs) {
    a.toc = afs::ReadToc(*a.src, &a.toc_error);
  }
}

// The file in `dir` whose name equals `name` ignoring ASCII case.
std::optional<fs::path> FindInFolder(const fs::path& dir, const std::string& name) {
  std::error_code ec;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    std::error_code ec2;
    if (!it->is_regular_file(ec2)) continue;
    if (LowerAscii(it->path().filename().string()) == LowerAscii(name)) return it->path();
  }
  return std::nullopt;
}

void OpenFolder(Side& side, const fs::path& dir, const std::vector<Arc>& needed) {
  std::map<Arc, fs::path> found;
  std::string missing;
  for (Arc a : needed) {
    const auto p = FindInFolder(dir, ArcFileName(a));
    if (p)
      found[a] = *p;
    else
      missing += (missing.empty() ? "" : ", ") + std::string(ArcFileName(a));
  }
  if (!missing.empty()) return Fail(side, InputStatus::kNoArchives, "no " + missing + " in " + Utf8(dir));
  for (const auto& [a, p] : found) {
    std::shared_ptr<const afs::Source> src = afs::FileSource::Open(p);
    if (!src) return Fail(side, InputStatus::kReadError, "cannot open " + Utf8(p));
    side.arcs[a] = Archive{std::move(src), std::nullopt, {}, Utf8(p)};
  }
}

void OpenImage(Side& side, const fs::path& file, const std::vector<Arc>& needed) {
  std::shared_ptr<const afs::Source> image = afs::FileSource::Open(file);
  if (!image) return Fail(side, InputStatus::kReadError, "cannot open " + Utf8(file));
  XdvdfsAdapter adapter(*image);
  xdvdfs::Error error = xdvdfs::Error::kNone;
  const std::optional<xdvdfs::Volume> volume = xdvdfs::ReadVolume(adapter, &error);
  if (!volume) {
    if (error == xdvdfs::Error::kNotXbox) return Fail(side, InputStatus::kNotXbox, Utf8(file) + " is not an Xbox disc image");
    return Fail(side, InputStatus::kReadError, "cannot read the file system of " + Utf8(file));
  }
  std::string missing;
  std::map<Arc, const xdvdfs::Entry*> found;
  for (Arc a : needed) {
    const xdvdfs::Entry* e = xdvdfs::FindFile(*volume, ArcFileName(a));
    if (e)
      found[a] = e;
    else
      missing += (missing.empty() ? "" : ", ") + std::string(ArcFileName(a));
  }
  if (!missing.empty()) return Fail(side, InputStatus::kNoArchives, "no " + missing + " on the disc " + Utf8(file));
  for (const auto& [a, e] : found) {
    side.arcs[a] = Archive{std::make_shared<SubSource>(image, e->offset, e->size), std::nullopt, {}, Utf8(file)};
  }
}

bool IsHexSha1(const char* s) {
  if (!s) return false;
  const std::string t(s);
  if (t.size() != 40) return false;
  return std::all_of(t.begin(), t.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

bool ValidateTables(std::span<const EntryRef> disc, std::span<const EntryRef> game, std::string* error) {
  const auto bad = [&](const std::string& why) {
    if (error) *error = why;
    return false;
  };
  for (const EntryRef& e : disc) {
    if (!ArcIsDisc(e.arc)) return bad(EntryLabel(e) + " is a game archive in the disc table");
    if (!IsHexSha1(e.sha1)) return bad(EntryLabel(e) + " has no 40-digit SHA-1");
  }
  for (const EntryRef& e : game) {
    if (ArcIsDisc(e.arc)) return bad(EntryLabel(e) + " is a disc archive in the game table");
    if (!IsHexSha1(e.sha1)) return bad(EntryLabel(e) + " has no 40-digit SHA-1");
  }
  return true;
}

// Where an entry's bytes are: the archive and the absolute range inside its source. Empty detail on success.
struct Located {
  const Archive* archive = nullptr;
  uint64_t offset = 0;
  uint64_t size = 0;
  std::string problem;
};

Located Locate(const Side& side, const EntryRef& e) {
  Located l;
  const auto it = side.arcs.find(e.arc);
  if (it == side.arcs.end()) {
    l.problem = EntryLabel(e) + ": its archive is not open";
    return l;
  }
  const Archive& a = it->second;
  if (!a.toc) {
    l.problem = EntryLabel(e) + ": the archive's table of contents does not parse (" + a.toc_error + ")";
    return l;
  }
  if (e.index >= a.toc->count) {
    l.problem = EntryLabel(e) + ": the archive has " + std::to_string(a.toc->count) + " entries";
    return l;
  }
  l.archive = &a;
  l.offset = a.toc->offsets[e.index];
  l.size = a.toc->sizes[e.index];
  if (l.size != e.size) {
    l.problem = EntryLabel(e) + ": size " + std::to_string(l.size) + ", expected " + std::to_string(e.size);
    l.archive = nullptr;
  }
  return l;
}

}  // namespace

struct InputSet::Impl {
  Side disc, game;

  InputCheck Check(const Side& side, const CancelToken& cancel, const std::function<void(uint64_t)>& bytes_done) const {
    InputCheck r;
    if (side.status != InputStatus::kOk) {
      r.status = side.status;
      r.detail = side.detail;
      return r;
    }
    std::vector<uint8_t> buf;
    for (const EntryRef& e : side.entries) {
      if (cancel.cancelled()) {
        r.status = InputStatus::kCancelled;
        r.detail = "cancelled";
        return r;
      }
      const Located l = Locate(side, e);
      if (!l.archive) {
        r.mismatched.push_back(EntryLabel(e));
        if (r.detail.empty()) r.detail = l.problem;
        continue;
      }
      gamefiles::Sha1 hash;
      if (buf.size() < kChunk) buf.resize(kChunk);
      for (uint64_t done = 0; done < l.size;) {
        if (cancel.cancelled()) {
          r.status = InputStatus::kCancelled;
          r.detail = "cancelled";
          return r;
        }
        const size_t n = size_t(std::min<uint64_t>(kChunk, l.size - done));
        if (!l.archive->src->Read(l.offset + done, buf.data(), n)) {
          r.status = InputStatus::kReadError;
          r.detail = "cannot read " + EntryLabel(e) + " from " + l.archive->where;
          return r;
        }
        hash.Update(buf.data(), n);
        done += n;
        r.bytes_checked += n;
        if (bytes_done) bytes_done(r.bytes_checked);
      }
      if (hash.HexDigest() != e.sha1) {
        r.mismatched.push_back(EntryLabel(e));
        if (r.detail.empty()) r.detail = EntryLabel(e) + ": SHA-1 differs";
      }
    }
    r.status = r.mismatched.empty() ? InputStatus::kOk : InputStatus::kMismatch;
    return r;
  }
};

InputSet::InputSet(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
InputSet::~InputSet() = default;

namespace {

std::unique_ptr<InputSet::Impl> NewImpl(std::span<const EntryRef> disc, std::span<const EntryRef> game) {
  auto impl = std::make_unique<InputSet::Impl>();
  impl->disc.entries.assign(disc.begin(), disc.end());
  impl->game.entries.assign(game.begin(), game.end());
  return impl;
}

}  // namespace

std::unique_ptr<InputSet> InputSet::Open(const fs::path& doax2, const fs::path& game_root, std::span<const EntryRef> disc,
                                         std::span<const EntryRef> game, std::string* error) {
  if (!ValidateTables(disc, game, error)) return nullptr;
  auto impl = NewImpl(disc, game);
  const std::vector<Arc> disc_arcs = NeededArcs(impl->disc.entries);
  const std::vector<Arc> game_arcs = NeededArcs(impl->game.entries);
  std::error_code ec;
  if (!disc_arcs.empty()) {
    if (!fs::exists(doax2, ec)) {
      Fail(impl->disc, InputStatus::kNotFound, Utf8(doax2) + " does not exist");
    } else if (fs::is_directory(doax2, ec)) {
      OpenFolder(impl->disc, doax2, disc_arcs);
    } else {
      OpenImage(impl->disc, doax2, disc_arcs);
    }
  }
  if (!game_arcs.empty()) {
    if (!fs::exists(game_root, ec)) {
      Fail(impl->game, InputStatus::kNotFound, Utf8(game_root) + " does not exist");
    } else if (fs::is_directory(game_root, ec)) {
      OpenFolder(impl->game, game_root, game_arcs);
    } else {
      Fail(impl->game, InputStatus::kNoArchives, Utf8(game_root) + " is not a folder");
    }
  }
  ReadTocs(impl->disc);
  ReadTocs(impl->game);
  return std::unique_ptr<InputSet>(new InputSet(std::move(impl)));
}

std::unique_ptr<InputSet> InputSet::OpenSources(ArchiveSources disc_archives, ArchiveSources game_archives,
                                                std::span<const EntryRef> disc, std::span<const EntryRef> game,
                                                std::string* error) {
  if (!ValidateTables(disc, game, error)) return nullptr;
  auto impl = NewImpl(disc, game);
  const auto fill = [](Side& side, ArchiveSources& given) {
    for (auto& [arc, src] : given) side.arcs[arc] = Archive{src, std::nullopt, {}, std::string(ArcFileName(arc))};
    for (Arc a : NeededArcs(side.entries)) {
      if (!side.arcs.count(a)) {
        Fail(side, InputStatus::kNoArchives, std::string("no ") + ArcFileName(a));
        return;
      }
    }
  };
  fill(impl->disc, disc_archives);
  fill(impl->game, game_archives);
  ReadTocs(impl->disc);
  ReadTocs(impl->game);
  return std::unique_ptr<InputSet>(new InputSet(std::move(impl)));
}

InputCheck InputSet::CheckDisc(const CancelToken& cancel, const std::function<void(uint64_t)>& bytes_done) {
  return impl_->Check(impl_->disc, cancel, bytes_done);
}

InputCheck InputSet::CheckGame(const CancelToken& cancel, const std::function<void(uint64_t)>& bytes_done) {
  return impl_->Check(impl_->game, cancel, bytes_done);
}

std::shared_ptr<const std::vector<uint8_t>> InputSet::Read(const EntryRef& entry) {
  const Side& side = ArcIsDisc(entry.arc) ? impl_->disc : impl_->game;
  const Located l = Locate(side, entry);
  if (!l.archive) throw InputChanged(l.problem.empty() ? EntryLabel(entry) + ": not available" : l.problem);
  auto bytes = std::make_shared<std::vector<uint8_t>>(size_t(l.size));
  if (l.size > 0 && !l.archive->src->Read(l.offset, bytes->data(), bytes->size())) {
    throw InputChanged(EntryLabel(entry) + ": cannot be read from " + l.archive->where);
  }
  if (gamefiles::Sha1Hex(bytes->data(), bytes->size()) != entry.sha1) {
    throw InputChanged(EntryLabel(entry) + ": the bytes differ from the checked entry (SHA-1)");
  }
  return bytes;
}

}  // namespace doa4::suitconv

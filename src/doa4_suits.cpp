// The import loader. See doa4_suits.h.

#include "doa4_suits.h"

#include <algorithm>
#include <fstream>
#include <optional>

#include "doa4_costumes.h"
#include "doa4_gamefiles.h"
#include "doa4_suits_picks.h"
#include "doa4_utf8.h"

namespace doa4::suits {

namespace {

bool ReadSmallFile(const std::filesystem::path& path, uint64_t limit, std::string* out) {
  std::error_code ec;
  const uint64_t size = std::filesystem::file_size(path, ec);
  // file_size returns (uintmax_t)-1 when it fails, so the size test alone refuses that too; `ec`
  // says so outright (a redundant pair: dropping either leaves every check green).
  if (ec || size > limit) return false;
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out->assign(size_t(size), '\0');
  return bool(in.read(out->data(), std::streamsize(size)));
}

std::string HashSource(const afs::Source& source) {
  gamefiles::Sha1 sha;
  std::vector<uint8_t> buffer(size_t(1) << 20);
  for (uint64_t at = 0; at < source.size();) {
    const size_t n = size_t(std::min<uint64_t>(buffer.size(), source.size() - at));
    if (!source.Read(at, buffer.data(), n)) return {};
    sha.Update(buffer.data(), n);
    at += n;
  }
  return sha.HexDigest();
}

std::shared_ptr<const afs::Source> OpenChecked(const std::filesystem::path& folder, const SuitFile& f,
                                               std::string* why) {
  std::shared_ptr<afs::FileSource> source = afs::FileSource::Open(folder / PathFromUtf8(f.name));
  if (!source) {
    *why = f.name + " is missing or in use";
    return nullptr;
  }
  if (source->size() != f.size) {
    *why = f.name + " is " + std::to_string(source->size()) + " bytes, the manifest says " +
           std::to_string(f.size);
    return nullptr;
  }
  if (HashSource(*source) != f.sha1) {
    *why = f.name + " does not match its checksum";
    return nullptr;
  }
  return source;
}

// What an entry of a game archive is, for a suit that wants to replace it.
enum class Slot {
  kEmpty,      // 16 bytes, every one kPlaceholderByte: an empty costume slot
  kWrongSize,  // not 16 bytes
  kHoldsData,  // 16 bytes that are not all kPlaceholderByte (or cannot be read: not provably empty)
};

// `entry` must be below toc.count. Reads the entry's bytes at its TOC offset, the offset the
// combined archive copies the original entries from.
Slot ClassifySlot(const afs::Source& archive, const afs::Toc& toc, uint32_t entry) {
  if (toc.sizes[entry] != kPlaceholderSize) return Slot::kWrongSize;
  uint8_t bytes[kPlaceholderSize];
  if (!archive.Read(toc.offsets[entry], bytes, sizeof bytes)) return Slot::kHoldsData;
  for (uint8_t b : bytes) {
    if (b != kPlaceholderByte) return Slot::kHoldsData;
  }
  return Slot::kEmpty;
}

// The combined archive as a byte source, so its rewritten TOC and its entries are read the way the
// game reads them (through the overlay), with the same code as an original archive.
class OverlaySource final : public afs::Source {
 public:
  explicit OverlaySource(const afs::Overlay& overlay) : overlay_(overlay) {}
  uint64_t size() const override { return overlay_.size(); }
  bool Read(uint64_t offset, void* dst, size_t n) const override { return overlay_.Read(offset, dst, n); }

 private:
  const afs::Overlay& overlay_;
};

}  // namespace

std::string ImportStatus::Summary() const {
  if (!folder) return "DOAX2 swimsuits: none imported";
  if (!problem.empty()) return "DOAX2 swimsuits: none loaded (" + problem + ")";
  std::string text = "DOAX2 swimsuits: " + std::to_string(suits.size()) + " loaded";
  if (!skipped.empty()) text += ", " + std::to_string(skipped.size()) + " skipped";
  return text;
}

void ImportStatus::Fail(const std::string& why) {
  problem = why;
  suits.clear();
}

ImportStatus LoadImport(const std::filesystem::path& folder, const std::filesystem::path& game_root,
                        const GuestBytes& guest) {
  ImportStatus status;
  status.path = folder;
  std::error_code ec;
  if (!std::filesystem::is_directory(folder, ec)) return status;
  status.folder = true;
  std::string text;
  if (!ReadSmallFile(folder / "manifest.txt", kManifestLimit, &text)) {
    status.problem = "manifest.txt is missing or unreadable";
    return status;
  }
  const ParseResult parsed = ParseManifest(text);
  if (!parsed.manifest) {
    status.problem = "manifest.txt: " + parsed.error;
    return status;
  }
  const std::string canonical = FormatManifest(*parsed.manifest);
  status.fingerprint = gamefiles::Sha1Hex(canonical.data(), canonical.size());

  // Each game archive is opened once for the whole import: its TOC, and the open file the slot
  // contents are read through. Released when LoadImport returns.
  struct GameArchive {
    std::shared_ptr<afs::FileSource> source;
    std::optional<afs::Toc> toc;
  };
  std::map<std::string, GameArchive> archives;
  auto archive_of = [&](const std::string& name) -> const GameArchive* {
    auto it = archives.find(name);
    if (it == archives.end()) {
      GameArchive archive;
      archive.source = afs::FileSource::Open(game_root / name);
      if (archive.source) archive.toc = afs::ReadToc(*archive.source, nullptr);
      it = archives.emplace(name, std::move(archive)).first;
    }
    return it->second.toc ? &it->second : nullptr;
  };

  for (const Suit& s : parsed.manifest->suits) {
    std::string why = costumes::CheckSuit(guest, s);
    for (uint32_t file : {uint32_t(s.body_tpr), uint32_t(s.body_cat)}) {
      if (!why.empty()) break;
      // CheckSuit has just refused any suit whose body files are not in a character archive, so
      // `loc` is set here (it asks FileLocation about these same two numbers).
      const std::optional<costumes::FileLoc> loc = costumes::FileLocation(guest, file);
      const GameArchive* archive = archive_of(loc->archive);
      if (!archive) {
        why = std::string(loc->archive) + " is missing or not an AFS archive";
      } else if (loc->entry >= archive->toc->count) {
        why = "file " + std::to_string(file) + " is entry " + std::to_string(loc->entry) + " of " +
              loc->archive + ", which has " + std::to_string(archive->toc->count);
      } else {
        const std::string not_empty = "file " + std::to_string(file) + " (entry " +
                                      std::to_string(loc->entry) + " of " + loc->archive +
                                      ") is not an empty costume slot (";
        switch (ClassifySlot(*archive->source, *archive->toc, loc->entry)) {
          case Slot::kEmpty:
            break;
          case Slot::kWrongSize:
            why = not_empty + std::to_string(archive->toc->sizes[loc->entry]) + " bytes)";
            break;
          case Slot::kHoldsData:
            why = not_empty + "it holds data)";
            break;
        }
      }
    }
    LoadedSuit loaded{s, nullptr, nullptr};
    if (why.empty()) loaded.tpr = OpenChecked(folder, s.tpr, &why);
    if (why.empty()) loaded.cat = OpenChecked(folder, s.cat, &why);
    if (!why.empty()) {
      status.skipped.push_back(s.Label() + ": " + why);
      continue;
    }
    status.suits.push_back(std::move(loaded));
  }
  return status;
}

std::map<std::string, std::shared_ptr<const afs::Overlay>> BuildOverlays(
    const ImportStatus& status, const std::filesystem::path& game_root, const GuestBytes& guest,
    std::string* error) {
  std::map<std::string, std::vector<afs::Replacement>> by_archive;
  for (const LoadedSuit& l : status.suits) {
    const auto tpr = costumes::FileLocation(guest, l.suit.body_tpr);
    const auto cat = costumes::FileLocation(guest, l.suit.body_cat);
    if (!tpr || !cat) {
      *error = l.suit.Label() + ": its files are not character archive files";
      return {};
    }
    by_archive[tpr->archive].push_back({tpr->entry, l.tpr});
    by_archive[cat->archive].push_back({cat->entry, l.cat});
  }
  std::map<std::string, std::shared_ptr<const afs::Overlay>> out;
  for (auto& [name, replacements] : by_archive) {
    std::shared_ptr<afs::FileSource> original = afs::FileSource::Open(game_root / name);
    if (!original) {
      *error = "cannot open " + name + " in the game folder";
      return {};
    }
    // LoadImport read this archive in an earlier open; this is the open the overlay will serve
    // from (and nobody can write to it now), so what is about to be replaced is checked again.
    std::string why;
    const std::optional<afs::Toc> toc = afs::ReadToc(*original, &why);
    if (!toc) {
      *error = name + ": " + why;
      return {};
    }
    for (const afs::Replacement& r : replacements) {
      if (r.entry >= toc->count || ClassifySlot(*original, *toc, r.entry) != Slot::kEmpty) {
        *error = name + " changed since the import was checked (entry " + std::to_string(r.entry) + ")";
        return {};
      }
    }
    std::shared_ptr<const afs::Overlay> overlay = afs::Overlay::Build(original, std::move(replacements), &why);
    if (!overlay) {
      *error = name + ": " + why;
      return {};
    }
    out.emplace(name, std::move(overlay));
  }
  return out;
}

std::map<std::string, std::map<uint32_t, std::string>> EntryLabels(const ImportStatus& status,
                                                                   const GuestBytes& guest) {
  std::map<std::string, std::map<uint32_t, std::string>> out;
  for (const LoadedSuit& l : status.suits) {
    if (const auto t = costumes::FileLocation(guest, l.suit.body_tpr)) {
      out[t->archive][t->entry] = l.suit.Label() + " (tpr)";
    }
    if (const auto c = costumes::FileLocation(guest, l.suit.body_cat)) {
      out[c->archive][c->entry] = l.suit.Label() + " (cat)";
    }
  }
  return out;
}

std::vector<Suit> SuitsOf(const ImportStatus& status) {
  std::vector<Suit> out;
  for (const LoadedSuit& l : status.suits) out.push_back(l.suit);
  return out;
}

std::string ImportChecksum(const ImportStatus& status) {
  std::vector<std::string> lines;
  for (const LoadedSuit& l : status.suits) {
    if (IsPick(l.suit)) lines.push_back(FormatSuitLine(l.suit));
  }
  if (lines.empty()) return {};
  std::sort(lines.begin(), lines.end());
  std::string text;
  for (const std::string& line : lines) text += line;
  return gamefiles::Sha1Hex(text.data(), text.size());
}

std::vector<LoadedSuit> SessionFiles(const ImportStatus& status) {
  std::vector<LoadedSuit> out;
  for (const LoadedSuit& l : status.suits) {
    if (IsPick(l.suit)) out.push_back(l);
  }
  return out;
}

std::string MarkEmptySlots(costumes::GuardState* state, const GuestBytes& guest,
                           const std::filesystem::path& game_root,
                           const std::map<std::string, std::shared_ptr<const afs::Overlay>>& overlays) {
  state->empty_slot = {};
  // Each archive the models need is opened (or wrapped) once and its TOC read once; a failure is
  // remembered too, so the note names an unreadable archive once and not once per model.
  struct View {
    std::shared_ptr<afs::FileSource> own;
    std::unique_ptr<OverlaySource> through_overlay;
    const afs::Source* source = nullptr;
    std::optional<afs::Toc> toc;
  };
  std::map<std::string, View> views;
  std::vector<std::string> notes;
  const auto view_of = [&](const char* archive) -> const View* {
    auto it = views.find(archive);
    if (it == views.end()) {
      View v;
      std::string why;
      const auto overlay = overlays.find(archive);
      if (overlay != overlays.end() && overlay->second) {
        v.through_overlay = std::make_unique<OverlaySource>(*overlay->second);
        v.source = v.through_overlay.get();
      } else {
        v.own = afs::FileSource::Open(game_root / archive);
        v.source = v.own.get();
        if (!v.source) why = "cannot be opened in the game folder";
      }
      if (v.source) v.toc = afs::ReadToc(*v.source, &why);
      if (!v.toc) notes.push_back(std::string(archive) + ": " + why);
      it = views.emplace(archive, std::move(v)).first;
    }
    return it->second.toc ? &it->second : nullptr;
  };
  for (uint32_t c = 0; c < costumes::kCharCount; ++c) {
    const uint32_t count = std::min<uint32_t>(state->records[c], 256);
    // The CURRENT record block: after costumes::Apply it is her appended copy on the heap.
    const uint32_t records = guest.ReadBe32(costumes::kModelTable + 8 * c + 4);
    if (count == 0 || records == 0) continue;
    for (uint32_t m = 0; m < count; ++m) {
      const uint32_t r = records + 8 * m;
      const uint32_t files[2] = {uint32_t(guest.Read(r)) << 8 | guest.Read(r + 1),
                                 uint32_t(guest.Read(r + 2)) << 8 | guest.Read(r + 3)};
      for (const uint32_t file : files) {
        const std::optional<costumes::FileLoc> loc = costumes::FileLocation(guest, file);
        if (!loc) continue;
        const View* view = view_of(loc->archive);
        if (!view || loc->entry >= view->toc->count) continue;
        if (ClassifySlot(*view->source, *view->toc, loc->entry) == Slot::kEmpty) {
          state->empty_slot[c].insert(m);
          break;
        }
      }
    }
  }
  std::string out;
  for (const std::string& note : notes) out += (out.empty() ? "" : "; ") + note;
  return out;
}

}  // namespace doa4::suits

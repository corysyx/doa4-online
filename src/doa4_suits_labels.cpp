// The costume-select label table. See doa4_suits_labels.h.

#include "doa4_suits_labels.h"

#include <cstdio>
#include <cstring>

namespace doa4::labels {

namespace {

uint16_t Read16(const GuestBytes& guest, uint32_t a) {
  return uint16_t((uint32_t(guest.Read(a)) << 8) | guest.Read(a + 1));
}

void Write16(const GuestBytes& guest, uint32_t a, uint16_t v) {
  guest.Write(a, uint8_t(v >> 8));
  guest.Write(a + 1, uint8_t(v));
}

void Write32(const GuestBytes& guest, uint32_t a, uint32_t v) {
  for (uint32_t i = 0; i < 4; ++i) guest.Write(a + i, uint8_t(v >> (24 - 8 * i)));
}

std::string Hex(uint32_t v) {
  char text[16];
  std::snprintf(text, sizeof text, "0x%x", v);
  return text;
}

}  // namespace

Record BlankRecord() {
  const Record& cos10 = kOldLabelRecords[kOldLabels - 1];
  return {kBlankX, kBlankY, int16_t(kBlankX + (cos10.x1 - cos10.x0)), int16_t(kBlankY + (cos10.y1 - cos10.y0)),
          cos10.sheet, cos10.image};
}

std::optional<Record> ReadRecord(const GuestBytes& guest, uint32_t table, uint32_t count, uint32_t index) {
  if (index >= count) return std::nullopt;
  const uint32_t a = table + kRecordSize * index;
  return Record{int16_t(Read16(guest, a)),     int16_t(Read16(guest, a + 2)), int16_t(Read16(guest, a + 4)),
                int16_t(Read16(guest, a + 6)), Read16(guest, a + 8),          Read16(guest, a + 10)};
}

bool WriteRecord(const GuestBytes& guest, uint32_t table, uint32_t count, uint32_t index, const Record& r) {
  if (index >= count) return false;
  const uint32_t a = table + kRecordSize * index;
  Write16(guest, a, uint16_t(r.x0));
  Write16(guest, a + 2, uint16_t(r.y0));
  Write16(guest, a + 4, uint16_t(r.x1));
  Write16(guest, a + 6, uint16_t(r.y1));
  Write16(guest, a + 8, r.sheet);
  Write16(guest, a + 10, r.image);
  return true;
}

Extended ExtendTable(const GuestBytes& guest, const AllocFn& alloc, const std::array<Record, kNewLabels>& extra) {
  Extended out;
  out.original = guest.ReadBe32(kSheetPointer);
  // 64-bit end: a pointer near 4 GiB must not wrap back into range.
  if (out.original < kImageStart || uint64_t(out.original) + kTableBytes > kImageEnd) {
    out.note = "the sheet-6 sprite table pointer at " + Hex(kSheetPointer) + " is " + Hex(out.original) +
               ", not a " + std::to_string(kRecords) + "-record table inside the image";
    return out;
  }
  for (uint32_t k = 0; k < kOldLabels; ++k) {
    if (ReadRecord(guest, out.original, kRecords, kFirstOldLabel + k) != kOldLabelRecords[k]) {
      out.note = "records 123-132 of the sheet-6 sprite table are not the COS. 1-10 labels this build knows (record " +
                 std::to_string(kFirstOldLabel + k) + " differs)";
      return out;
    }
  }
  const uint32_t table = alloc(kExtendedBytes);
  if (table == 0) {
    out.note = "no guest memory for the extended sheet-6 sprite table (" + std::to_string(kExtendedBytes) + " bytes)";
    return out;
  }
  for (uint32_t i = 0; i < kTableBytes; ++i) guest.Write(table + i, guest.Read(out.original + i));
  for (uint32_t k = 0; k < kNewLabels; ++k) WriteRecord(guest, table, kExtendedRecords, kFirstNewLabel + k, extra[k]);
  // The one write outside the block: from here on every lookup of a sheet-6 id goes through the
  // extended table (records 0..196 the same bytes as before).
  Write32(guest, kSheetPointer, table);
  out.table = table;
  return out;
}

std::string DescribeInstalled(const GuestBytes& guest, uint32_t original) {
  const uint32_t table = guest.ReadBe32(kSheetPointer);
  if (table == original) {
    return "the sheet-6 sprite table pointer still names the original table (" + Hex(original) + ")";
  }
  uint32_t differ = 0;
  for (uint32_t i = 0; i < kRecords; ++i) {
    differ += ReadRecord(guest, table, kExtendedRecords, i) != ReadRecord(guest, original, kRecords, i);
  }
  const std::array<Record, kNewLabels> labels = NewRecords(true), blanks = NewRecords(false);
  bool are_labels = true, are_blank = true;
  for (uint32_t k = 0; k < kNewLabels; ++k) {
    const std::optional<Record> r = ReadRecord(guest, table, kExtendedRecords, kFirstNewLabel + k);
    are_labels = are_labels && r == labels[k];
    are_blank = are_blank && r == blanks[k];
  }
  std::string text = "the sheet-6 sprite table pointer names " + Hex(table) + ": ";
  text += differ == 0 ? std::string("records 0-196 equal the original")
                      : std::to_string(differ) + " of records 0-196 differ from the original";
  text += are_labels  ? "; records 197-203 show the COS. 11-17 cells"
          : are_blank ? "; records 197-203 show the blank cell (" + std::to_string(kBlankX) + "," +
                            std::to_string(kBlankY) + ")"
                      : std::string("; records 197-203 hold something else");
  return text;
}

uint32_t RewriteId(uint32_t id, bool listing) {
  const uint32_t first = (kSheet << 16) | kFirstAsked;
  if (!listing || id < first || id >= first + kNewLabels) return id;
  return (kSheet << 16) | (kFirstNewLabel + (id - first));
}

bool RewriteDrawStruct(const GuestBytes& guest, uint32_t draw) {
  const uint32_t id = guest.ReadBe32(draw + 4);
  const uint32_t to = RewriteId(id, true);
  if (to == id) return false;
  Write32(guest, draw + 4, to);
  return true;
}

namespace {
thread_local bool t_listing = false;
}  // namespace

ListingScope::ListingScope() : previous_(t_listing) { t_listing = true; }
ListingScope::~ListingScope() { t_listing = previous_; }
bool Listing() { return t_listing; }

std::array<Record, kNewLabels> NewRecords(bool art) {
  std::array<Record, kNewLabels> out;
  for (uint32_t k = 0; k < kNewLabels; ++k) out[k] = art ? *LabelRecord(k + 1) : BlankRecord();
  return out;
}

std::optional<Record> LabelRecord(uint32_t k) {
  if (k < 1 || k > kNewLabels) return std::nullopt;
  const Record& cos10 = kOldLabelRecords[kOldLabels - 1];
  const auto& cell = kNewCells[k - 1];
  return Record{cell[0], cell[1], int16_t(cell[0] + (cos10.x1 - cos10.x0)), int16_t(cell[1] + (cos10.y1 - cos10.y0)),
                cos10.sheet, cos10.image};
}

uint32_t TiledOffset8(uint32_t x, uint32_t y, uint32_t width) {
  const uint32_t aligned = (width + 31) & ~31u;
  const uint32_t macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << 7;
  const uint32_t micro = (x & 7) + ((y & 6) << 2);
  const uint32_t offset = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 8) << 3) + ((y & 1) << 4);
  return ((offset & ~511u) << 3) + ((offset & 448) << 2) + (offset & 63) + ((y & 16) << 7) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}

std::vector<uint8_t> Untile8(const uint8_t* tiled, uint32_t width, uint32_t height) {
  std::vector<uint8_t> linear(size_t(width) * height);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) linear[size_t(y) * width + x] = tiled[TiledOffset8(x, y, width)];
  }
  return linear;
}

std::vector<uint8_t> Retile8(const std::vector<uint8_t>& linear, uint32_t width, uint32_t height) {
  std::vector<uint8_t> tiled(size_t(width) * height);
  for (uint32_t y = 0; y < height; ++y) {
    for (uint32_t x = 0; x < width; ++x) tiled[TiledOffset8(x, y, width)] = linear[size_t(y) * width + x];
  }
  return tiled;
}

namespace {

constexpr uint32_t kAtlasWidth = 512;
constexpr uint32_t kAtlasHeight = 256;
constexpr uint32_t kFormatK8 = 2;  // the fetch constant's format field for k_8

uint32_t FileBe32(const std::vector<uint8_t>& b, size_t at) {
  return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
}

// Image 0's texels in an XPR2 file: the file offset of its tiled texels, or why it is not the label
// atlas. XPR2 (big-endian): "XPR2", u32 header size hs, u32 data size ds, u32 count, then count x
// {fourcc, offset, size, name} with offsets relative to byte 12; the data section starts at 12 + hs.
// A TX2D resource ends with its 24-byte fetch constant: dword 0 bit 31 tiled; dword 1 bits 0-5 the
// format, 6-7 the byte swap, 12-31 the texel base in the data section; dword 2 bits 0-12 width - 1
// and 13-25 height - 1; dword 5 bits 12-31 the mip levels' address (0: none stored).
std::optional<size_t> FindImage0(const std::vector<uint8_t>& b, std::string* why) {
  if (b.size() < 16 || std::memcmp(b.data(), "XPR2", 4) != 0) {
    *why = "not an XPR2 file";
    return std::nullopt;
  }
  const uint64_t header = FileBe32(b, 4), data = FileBe32(b, 8), count = FileBe32(b, 12);
  if (12 + header + data != b.size()) {
    *why = "its header and data sizes do not add up to the file's size";
    return std::nullopt;
  }
  const uint64_t header_end = 12 + header;
  for (uint64_t i = 0; i < count && 16 + 16 * i + 16 <= header_end; ++i) {
    const size_t entry = size_t(16 + 16 * i);
    if (std::memcmp(b.data() + entry, "TX2D", 4) != 0) continue;
    const uint64_t at = 12 + uint64_t(FileBe32(b, entry + 4)), size = FileBe32(b, entry + 8);
    if (size < 24 || at + size > header_end) {
      *why = "its first texture's descriptor runs past the header";
      return std::nullopt;
    }
    uint32_t fc[6];
    for (int w = 0; w < 6; ++w) fc[w] = FileBe32(b, size_t(at + size - 24 + 4 * w));
    const uint32_t format = fc[1] & 0x3F, swap = (fc[1] >> 6) & 3;
    const uint32_t width = (fc[2] & 0x1FFF) + 1, height = ((fc[2] >> 13) & 0x1FFF) + 1;
    if (format != kFormatK8) {
      *why = "image 0 is format " + std::to_string(format) + ", not k_8 (2)";
    } else if (width != kAtlasWidth || height != kAtlasHeight) {
      *why = "image 0 is " + std::to_string(width) + "x" + std::to_string(height) + ", not 512x256";
    } else if ((fc[0] >> 31) == 0) {
      *why = "image 0 is not tiled";
    } else if (swap != 0) {
      *why = "image 0 has byte swap " + std::to_string(swap) + ", not 0";
    } else if ((fc[5] & 0xFFFFF000u) != 0) {
      *why = "image 0 has mip levels stored";
    } else if (uint64_t(fc[1] & 0xFFFFF000u) + kAtlasWidth * kAtlasHeight > data) {
      *why = "image 0's texels run past the data section";
    } else {
      return size_t(header_end + (fc[1] & 0xFFFFF000u));
    }
    return std::nullopt;
  }
  *why = "it has no TX2D texture";
  return std::nullopt;
}

bool AllZero(const std::vector<uint8_t>& image, int x0, int y0, int width, int height) {
  for (int y = y0; y < y0 + height; ++y) {
    for (int x = x0; x < x0 + width; ++x) {
      if (image[size_t(y) * kAtlasWidth + x] != 0) return false;
    }
  }
  return true;
}

}  // namespace

std::string ComposeLabels(std::vector<uint8_t>* xpr) {
  std::string why;
  const std::optional<size_t> at = FindImage0(*xpr, &why);
  if (!at) return why;
  const std::vector<uint8_t> image = Untile8(xpr->data() + *at, kAtlasWidth, kAtlasHeight);
  for (uint32_t k = 0; k < kNewLabels; ++k) {
    if (!AllZero(image, kNewCells[k][0], kNewCells[k][1], kCellWidth, kCellHeight)) {
      return "cell " + std::to_string(k + 1) + " (" + std::to_string(kNewCells[k][0]) + "," +
             std::to_string(kNewCells[k][1]) + ") is not empty";
    }
  }
  const Record& ten = kOldLabelRecords[kOldLabels - 1];
  const int width = ten.x1 - ten.x0, height = ten.y1 - ten.y0;
  if (AllZero(image, ten.x0, ten.y0, width, height)) return "the COS. 10 label is empty";
  for (uint32_t k = 1; k <= kNewLabels; ++k) {
    const Record& from = kOldLabelRecords[k - 1];
    if (AllZero(image, from.x0 + kDigitFromX, from.y0 + kDigitY, kDigitWidth, kDigitHeight)) {
      return "the digit of COS. " + std::to_string(k) + " is empty";
    }
  }
  // Compose on the untiled copy, then write back only the texels of the seven cells. The whole grid
  // cell of COS. 10 is copied (106x32 from its record's corner), not only its 103-wide record: the
  // button's shadow runs to column 104 of the cell (in every label), so the texels just right of the
  // drawn rectangle match COS. 10's as well.
  std::vector<uint8_t> out = image;
  for (uint32_t k = 1; k <= kNewLabels; ++k) {
    const int tx = kNewCells[k - 1][0], ty = kNewCells[k - 1][1];
    const Record& from = kOldLabelRecords[k - 1];
    for (int y = 0; y < kCellHeight; ++y) {
      for (int x = 0; x < kCellWidth; ++x) {
        const bool digit = y >= kDigitY && y < kDigitY + kDigitHeight && x >= kDigitToX && x < kDigitToX + kDigitWidth;
        out[size_t(ty + y) * kAtlasWidth + tx + x] =
            digit ? image[size_t(from.y0 + y) * kAtlasWidth + from.x0 + kDigitFromX + (x - kDigitToX)]
                  : image[size_t(ten.y0 + y) * kAtlasWidth + ten.x0 + x];
      }
    }
    for (int y = 0; y < kCellHeight; ++y) {
      for (int x = 0; x < kCellWidth; ++x) {
        (*xpr)[*at + TiledOffset8(uint32_t(tx + x), uint32_t(ty + y), kAtlasWidth)] =
            out[size_t(ty + y) * kAtlasWidth + tx + x];
      }
    }
  }
  return {};
}

namespace {

// An overlay as the original of another overlay; keeps it alive.
class OverlayAsSource final : public afs::Source {
 public:
  explicit OverlayAsSource(std::shared_ptr<const afs::Overlay> overlay) : overlay_(std::move(overlay)) {}
  uint64_t size() const override { return overlay_->size(); }
  bool Read(uint64_t offset, void* dst, size_t n) const override { return overlay_->Read(offset, dst, n); }

 private:
  std::shared_ptr<const afs::Overlay> overlay_;
};

}  // namespace

std::shared_ptr<const afs::Overlay> BuildLabelOverlay(const std::filesystem::path& game_root, std::string* error,
                                                      std::shared_ptr<const afs::Overlay> over) {
  const std::string archive = kSprArchive;
  std::shared_ptr<const afs::Source> original;
  if (over) {
    original = std::make_shared<OverlayAsSource>(std::move(over));
  } else {
    original = afs::FileSource::Open(game_root / archive);
  }
  if (!original) {
    *error = "cannot open " + archive + " in the game folder";
    return nullptr;
  }
  std::string why;
  const std::optional<afs::Toc> toc = afs::ReadToc(*original, &why);
  if (!toc) {
    *error = archive + ": " + why;
    return nullptr;
  }
  if (toc->count <= kSprChrW) {
    *error = archive + " has " + std::to_string(toc->count) + " entries; spChrN.xpr and spChrW.xpr are entries 8 and 9";
    return nullptr;
  }
  std::vector<afs::Replacement> replacements;
  for (const uint32_t entry : {kSprChrN, kSprChrW}) {
    const std::string name = archive + " entry " + std::to_string(entry) + (entry == kSprChrN ? " (spChrN.xpr)" : " (spChrW.xpr)");
    std::vector<uint8_t> bytes(toc->sizes[entry]);
    if (!original->Read(toc->offsets[entry], bytes.data(), bytes.size())) {
      *error = name + ": cannot be read";
      return nullptr;
    }
    why = ComposeLabels(&bytes);
    if (!why.empty()) {
      *error = name + ": " + why;
      return nullptr;
    }
    replacements.push_back({entry, std::make_shared<afs::MemorySource>(std::move(bytes))});
  }
  std::shared_ptr<const afs::Overlay> overlay = afs::Overlay::Build(original, std::move(replacements), &why);
  if (!overlay) {
    *error = archive + ": " + why;
    return nullptr;
  }
  // Same-size entries in the layout the game derives from the sizes: the combined archive is the
  // original apart from the composed texels. An archive laid out otherwise would move.
  bool same = overlay->size() == original->size();
  for (uint32_t i = 0; same && i < toc->count; ++i) same = overlay->entry_offset(i) == toc->offsets[i];
  if (!same) {
    *error = archive + ": the combined archive would not keep the original layout";
    return nullptr;
  }
  return overlay;
}

}  // namespace doa4::labels

// doa4_suits_labels.h
// Costume-select labels past COS. 10 (notes/research/costume_select_ui.md, sections 1, 2, 4(b), 4(c1);
// redesigned in Task 9b fix round 1).
//
// The costume list drawer sub_82528190 (generated .19.cpp:42601) draws costume i as sprite
// (6 << 16) | (123 + i): `lis r8,6` + `ori r27,r8,123`, then `add r11,r31,r27` per costume. A sprite id
// is looked up twice, both times through the sheet pointer array 0x82C77D08 + 4 * sheet, record =
// table + 12 * index, 12 bytes {be16 x0, y0, x1, y1; be16 sheet; be16 image}:
//  - when the draw is queued: the resolver sub_823BF210 (.7.cpp:52485) reads the id from the draw
//    struct (`mr r30,r4` :52498, `lwz r11,4(r30)` :52508), takes the record's size and image word, and
//    stores the id into the 2D command (`lwz r7,4(r30)` :52940, `stw r7,32(r31)` :52942);
//  - when the frame's 2D commands are flushed (sub_823B9CC8 .6.cpp:595): the sprite cases
//    sub_823C1280 (.7.cpp:57304, command in r5) and sub_823C15E8 (:57808) read the id back
//    (`lwz r11,32(r31)` / `lwz r11,32(r30)`) and take the record's corners as texture coordinates.
// Sheet 6's pointer word is 0x82C77D20 (in .data; 0x82C706B8 in the image) and sheet 7's table starts
// at 0x82C70FF4, so sheet 6 has 197 records. Only ten are labels: COS. 1..10 = records 123..132, cells
// of image 0 of spChrN.xpr / spChrW.xpr. Record 133 is the Time Attack "RECORD" panel, 134 its edge
// strip, 135..139 cursor frames, which costumes #10.. drew in place of a label.
//
// The fix: an EXTENDED copy of sheet 6's table, installed once at start-up before guest code runs
// (the pointer word then names it for good): records 0..196 byte for byte the original, records
// 197..203 the labels COS. 11..17. While the costume list is drawn (ListingScope, set by the hook on
// sub_82528190, per host thread), the hook on the resolver rewrites ids 133..139 to 197..203 in the
// draw struct, so the queued command carries the new id and the flush finds the label too. Every
// other draw, including the real Record panel (sub_82528640 reads records 133/134 directly), sees the
// original records. A census of the 51 functions that form an address in the pointer array found no
// store into it, no loop over a table's records and no bound on a record index (Task 9b fix 1 report).
//
// The art: there is no font, each label is a pre-drawn cell of image 0 (512x256, k_8 = palette
// indices, Xenos-tiled) of spChrN.xpr / spChrW.xpr (spr.afs entries 8 and 9; images 0-2 are
// pixel-identical in the two). COS. 11..17 are composed at start-up from the player's own files:
// the COS. 10 cell with the digit block of COS. k over its "0", written into seven empty cells,
// and spr.afs is served with those two entries replaced (same sizes) through an afs::Overlay.
// Without the art (any check fails) records 197..203 show one blank cell instead.
//
// Pure C++ over GuestBytes and afs::Source (no rex includes), so host tests build it.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "doa4_afs_overlay.h"
#include "doa4_guest.h"

namespace doa4::labels {

inline constexpr uint32_t kSheetPointer = 0x82C77D20;  // *(0x82C77D08 + 4 * 6)
inline constexpr uint32_t kRecords = 197;              // records in sheet 6's own table
inline constexpr uint32_t kRecordSize = 12;
inline constexpr uint32_t kTableBytes = kRecords * kRecordSize;
inline constexpr uint32_t kFirstOldLabel = 123;  // COS. 1 .. COS. 10 = records 123..132
inline constexpr uint32_t kOldLabels = 10;
inline constexpr uint32_t kFirstAsked = 133;     // the ids the list asks for past COS. 10: 133..139
inline constexpr uint32_t kNewLabels = 7;
inline constexpr uint32_t kFirstNewLabel = 197;  // COS. 11 .. COS. 17 = records 197..203 of the extended table
inline constexpr uint32_t kExtendedRecords = kRecords + kNewLabels;
inline constexpr uint32_t kExtendedBytes = kExtendedRecords * kRecordSize;
inline constexpr uint32_t kSheet = 6;
// The XEX image (the same bounds as costumes::kImageStart / kImageEnd): the pointer word must name
// a table inside it.
inline constexpr uint32_t kImageStart = 0x82000000;
inline constexpr uint32_t kImageEnd = 0x83000000;

struct Record {
  int16_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // pixels of the sheet's image; x1 / y1 exclusive
  uint16_t sheet = 0, image = 0;
  bool operator==(const Record&) const = default;
};

// COS. 1 .. COS. 10 as records 123..132 hold them in the image (all on sheet 6, image 0). ExtendTable
// refuses a table whose labels are not these: the cells below are only known empty in this atlas.
inline constexpr std::array<Record, kOldLabels> kOldLabelRecords = {{
    {0, 37, 106, 69, 6, 0},   {106, 37, 212, 69, 6, 0},  {212, 37, 318, 69, 6, 0},
    {318, 37, 424, 69, 6, 0}, {0, 70, 106, 102, 6, 0},   {106, 70, 212, 102, 6, 0},
    {212, 70, 318, 102, 6, 0}, {318, 70, 424, 102, 6, 0}, {0, 103, 103, 135, 6, 0},
    {106, 103, 209, 135, 6, 0},
}};

// The blank cell: a 106x32 cell of image 0 that nothing draws from and that holds palette index 0
// (alpha 0) in the game's spChrN.xpr and spChrW.xpr (the env-gated host test reads them).
inline constexpr int16_t kBlankX = 212;
inline constexpr int16_t kBlankY = 174;

// The record that shows the blank cell: COS. 10's size, sheet and image word, at the blank cell.
Record BlankRecord();

// Record `index` of the table at `table`, which has `count` records; nothing when index >= count.
std::optional<Record> ReadRecord(const GuestBytes& guest, uint32_t table, uint32_t count, uint32_t index);
// Writes record `index`; false, and nothing written, when index >= count.
bool WriteRecord(const GuestBytes& guest, uint32_t table, uint32_t count, uint32_t index, const Record& record);

// Guest memory for the extended table: a block of `size` bytes, or 0.
using AllocFn = std::function<uint32_t(uint32_t size)>;

struct Extended {
  uint32_t original = 0;  // what the pointer word held
  uint32_t table = 0;     // the extended table, now named by the pointer word; 0 when none was made
  std::string note;       // why none was made; empty when it was
};
// Reads the pointer word at kSheetPointer; copies the table it names (kRecords records) into one block
// of kExtendedBytes from `alloc`; writes `extra` as records 197..203; then stores the block's address
// into the pointer word (the one write outside the block). The original table is not written.
// Refused (table 0, a note, nothing allocated, nothing written) when the table does not lie inside
// the image with all its records, or when records 123..132 are not kOldLabelRecords; table 0 and a
// note (nothing written) when the allocation fails. Call before any guest code runs.
Extended ExtendTable(const GuestBytes& guest, const AllocFn& alloc, const std::array<Record, kNewLabels>& extra);

// What the installed table shows, read from guest memory (the pointer word and the table it names),
// for the start-up log:
//  "the sheet-6 sprite table pointer still names the original table (0x82c706b8)", or
//  "the sheet-6 sprite table pointer names 0x...: records 0-196 equal the original" (or "N of records
//  0-196 differ from the original"), then "; records 197-203 show the COS. 11-17 cells", "... show
//  the blank cell (212,174)" or "... hold something else".
std::string DescribeInstalled(const GuestBytes& guest, uint32_t original);

// ---- Drawing the costume list.

// The id the resolver should use: while the costume list is drawn (`listing`), sheet 6's ids
// 133..139 become 197..203; any other id, and every id while not listing, is returned unchanged.
uint32_t RewriteId(uint32_t id, bool listing);

// Applies RewriteId(id, true) to the sprite id word at draw + 4 of a draw struct (the resolver's r4):
// true, and only that word written, when it changed. The caller checks Listing().
bool RewriteDrawStruct(const GuestBytes& guest, uint32_t draw);

// "This host thread is drawing the costume list": set for the scope's lifetime, then put back to what
// it was (a nested scope keeps it set). The hook on sub_82528190 holds one around the original call.
class ListingScope {
 public:
  ListingScope();
  ~ListingScope();
  ListingScope(const ListingScope&) = delete;
  ListingScope& operator=(const ListingScope&) = delete;

 private:
  bool previous_;
};
bool Listing();

// ---- The art: COS. 11..17.

// Where COS. 11..17 are composed: 106x32 cells of image 0 (the label grid's pitch) that no record
// draws from and that hold palette index 0 in the game's spChrN.xpr and spChrW.xpr.
inline constexpr std::array<std::array<int16_t, 2>, kNewLabels> kNewCells = {
    {{212, 103}, {318, 103}, {128, 136}, {234, 136}, {340, 136}, {0, 174}, {106, 174}}};
inline constexpr int kCellWidth = 106;
inline constexpr int kCellHeight = 32;
// The digit block: columns 62..74, rows 5..19 of COS. k's cell go over columns 69..81 of the copy of
// COS. 10's cell, which covers its "0" (columns 71..79; single digits sit at 64..71).
inline constexpr int kDigitFromX = 62;
inline constexpr int kDigitToX = 69;
inline constexpr int kDigitWidth = 13;
inline constexpr int kDigitY = 5;
inline constexpr int kDigitHeight = 15;

// The record that shows COS. 10 + k (k = 1..7): COS. 10's size, sheet and image word, at
// kNewCells[k - 1]. Nothing for any other k.
std::optional<Record> LabelRecord(uint32_t k);

// Records 197..203: the seven label cells when the art was composed (`art`), else the blank cell.
std::array<Record, kNewLabels> NewRecords(bool art);

// Xenos tiling of a k_8 texture (one byte per texel): the byte offset of texel (x, y) in a texture
// `width` texels wide. tex_png.py's tiled_offset(x, y, width, log_bpp = 0)
// (the stage-1d texture script, Python, not in this repository; the XDK's XGAddress2DTiledOffset).
uint32_t TiledOffset8(uint32_t x, uint32_t y, uint32_t width);
// The whole texture row-major / back to tiled. Width and height are multiples of 32 (then the tiled
// texture is exactly width * height bytes); `tiled` holds that many bytes.
std::vector<uint8_t> Untile8(const uint8_t* tiled, uint32_t width, uint32_t height);
std::vector<uint8_t> Retile8(const std::vector<uint8_t>& linear, uint32_t width, uint32_t height);

// Composes COS. 11..17 into image 0 of `xpr` (the bytes of spChrN.xpr or spChrW.xpr): for k = 1..7
// the COS. 10 cell (kCellWidth x kCellHeight from the corner of COS. 10's record: the record is 103
// wide, the button's shadow runs to column 104), with the digit block of COS. k over its "0", goes
// to kNewCells[k - 1]; only those texels are rewritten (retiled). Returns "" or why not, and then
// `xpr` is unchanged:
//  - not an XPR2 file, or its header and data sizes do not add up to the file's;
//  - image 0 (the first TX2D) is not a tiled 512x256 k_8 texture with no byte swap and no stored
//    mip levels, with all its texels inside the data section;
//  - one of the seven cells (106x32) holds a texel that is not 0;
//  - the COS. 10 cell, or the digit block of one of COS. 1..7, is all 0 (not the label atlas).
std::string ComposeLabels(std::vector<uint8_t>* xpr);

inline constexpr const char* kSprArchive = "spr.afs";  // AFS partition 9, beside the character archives
inline constexpr uint32_t kSprChrN = 8;                // spChrN.xpr
inline constexpr uint32_t kSprChrW = 9;                // spChrW.xpr

// <game_root>/spr.afs (opened read-only; the overlay keeps it open) with entries 8 and 9 replaced by
// their ComposeLabels output: the same sizes, so the combined archive is the original byte for byte
// outside the seven cells of each. Null and *error ("spr.afs entry 8 (spChrN.xpr): <why>", ...) when
// the archive cannot be read, either entry does not compose, or the combined archive would not keep
// the original's size. `error` must not be null. With `over` (spr.afs as the title word serves it,
// doa4_title_word.h), that is what gets entries 8 and 9 replaced instead of the game folder's file:
// the VFS holds one entry per archive, so what is installed last must carry the rest.
std::shared_ptr<const afs::Overlay> BuildLabelOverlay(const std::filesystem::path& game_root, std::string* error,
                                                      std::shared_ptr<const afs::Overlay> over = nullptr);

}  // namespace doa4::labels

// Unlocks for the Extras tab: characters, costumes, system voices, stages.
//
// Everything lives in the current profile, a struct in guest DATA that the game
// saves as ups.dat. The menu writes single bytes into it and the game's own
// autosave persists them. No game code is called. Layout and rules:
// notes/research/unlock_findings.md and notes/specs/2026-10-28-extras-tab-design.md.

#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "doa4_guest.h"

namespace doa4::unlocks {

// Guest addresses (Rev 1 image, all in DATA).
inline constexpr uint32_t kProfilePointer = 0x83035CC8;  // be32: P, the current profile
inline constexpr uint32_t kProfileSlot = 0x831A4F30;     // be32: record index 0..3
inline constexpr uint32_t kRecordBase = 0x831A90D8;
inline constexpr uint32_t kRecordStride = 0x10900;
inline constexpr uint32_t kRecordToProfile = 0x7968;     // P = record + this
inline constexpr uint32_t kRecordName = 8;  // record offset: profile name, UTF-16BE, 16 chars
inline constexpr uint32_t kProfileIndexToChar = 0x82C5A014;  // u8[23]
inline constexpr uint32_t kMaxCostumes = 0x82C34888;         // u8[25], by charId
inline constexpr uint32_t kCharToProfileIndex = 0x82C59FF8;  // u8[25], by charId (sub_82408398)
inline constexpr uint32_t kStoryRoster = 0x82C34400;         // 18 x 40 bytes, byte 0 = charId
inline constexpr int kStoryRosterCount = 18;
inline constexpr uint32_t kStoryRosterStride = 40;

// Profile layout: offsets from P; the arrays have one byte per profile index.
inline constexpr int kCharacterCount = 23;
inline constexpr int kCharIdCount = 25;
inline constexpr int kAlphaIndex = 21;  // Alpha-152: never unlocked in retail
inline constexpr int kEinIndex = 3;
inline constexpr int kSpartanIndex = 22;
inline constexpr uint32_t kFirstStoryClear = 0x3FC0;  // u8 flag
inline constexpr uint32_t kCostumeCount = 0x3FC2;
inline constexpr uint32_t kCharacterUnlocked = 0x3FD9;
inline constexpr uint32_t kStoryClears = 0x3FF0;
inline constexpr uint32_t kStoryClearedNormal = 0x4007;
inline constexpr uint32_t kVoiceUnlocked = 0x4035;
inline constexpr uint8_t kStartingCostumes = 2;

enum class Unlock { kCharacters, kCostumes, kVoices, kStages, kEverything };

// What one ApplyUnlock changed; all zero when it was already unlocked.
struct UnlockResult {
  int characters = 0;  // characters newly unlocked
  int costumes = 0;    // costumes newly available, summed over characters
  int voices = 0;      // system voices newly unlocked
  int stages = 0;      // of Nassau Station, Biolab Core, Tatami: newly open
};

// P when a loaded profile is in place: P matches the record formula for the
// slot index, the record has a profile name (the game's boot defaults have
// none until the save is read, after Start), and every costume count is
// between 2 and its character's maximum. Otherwise nullopt, and nothing may
// be written.
std::optional<uint32_t> FindProfile(const GuestBytes& guest);

// P when the save has been loaded (the same pointer, slot and name checks), whatever its costume counts say: the one
// use is correcting them (ClampCostumeCounts), since a count past its maximum is what FindProfile refuses.
std::optional<uint32_t> FindLoadedRecord(const GuestBytes& guest);

// Lowers every costume count above its character's maximum to that maximum: a save written while a costume tweak was
// on, loaded with it off. Returns how many counts changed.
int ClampCostumeCounts(const GuestBytes& guest, uint32_t profile);

// Raises the character's costume count (by charId) to her maximum, as a costume tweak needs: the game offers costumes
// in order, so the added ones come last. A character still locked (Helena, in a new save) is unlocked too. Returns
// whether it wrote. Her maximum is the game's own: the imported swimsuits (SetSuitCostumes) are not counted, because
// they are offered on the select screen without the save's count and the import must not change the save.
bool RaiseCostumes(const GuestBytes& guest, uint32_t profile, uint8_t char_id);

// How many imported swimsuits each character has (by charId): the table's maximum includes them, the save's count does
// not. Called once at start-up, after the suits are in the tables and before any guest code runs; all zero by default.
// RaiseCostumes, TatamiOpen, ApplyUnlock and IsUnlocked leave the suits out of the maximum: the "Unlock all" act writes each girl's
// own maximum, so the import never changes the save. ClampCostumeCounts and FindProfile keep the table's whole maximum (a
// count above it is what they correct).
void SetSuitCostumes(const std::array<uint8_t, kCharIdCount>& by_char);

// The Tatami stage's rule (the game's sub_82537350): every costume but Alpha-152's at the game's own maximum, every
// Story character cleared, every voice but Ein's. Needs a loaded profile.
bool TatamiOpen(const GuestBytes& guest, uint32_t profile);

// The game's sub_82408398(charId): "her costume count >= her maximum". The Story-clear unlock (sub_824082F0) and the
// award (sub_824C3238) add a costume only when it is false. `offline` with suits imported compares against her own
// maximum (SetSuitCostumes: the suits are not counted, so a clear or award never changes the save for the import's
// sake); otherwise the table's maximum, as the game. A charId past 24 has no maximum (always complete).
bool CostumesComplete(const GuestBytes& guest, uint32_t profile, uint8_t char_id, bool offline);

// Back to a new save's unlocks: costume counts 2, the 7 hidden characters locked, no voices, no Story clears. The
// other records (Time Attack clears and the rest of the profile) stay. Returns how many bytes changed.
int WipeUnlocks(const GuestBytes& guest, uint32_t profile);
bool IsWiped(const GuestBytes& guest, uint32_t profile);

bool IsUnlocked(const GuestBytes& guest, uint32_t profile, Unlock what);

// How many of the three unlockable stages are open, by the game's own rules
// (sub_82537458 / sub_82537350).
int OpenStageCount(const GuestBytes& guest, uint32_t profile);

// Writes the unlock. kStages (and kEverything) also unlocks characters,
// costumes and voices, and records one Story clear per Story character: the
// stages have no flags of their own. The costume counts go to each girl's own maximum (SetSuitCostumes), never the suits.
UnlockResult ApplyUnlock(const GuestBytes& guest, uint32_t profile, Unlock what);

}  // namespace doa4::unlocks

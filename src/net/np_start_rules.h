// Netplay starting rules (small fixes 2, Part 2): every netplay session starts on the standard match options, life
// gauge Largest (Single and Tag), Single first to 3 and 60 s, Tag 99 s. Starting values only: the players may change
// them in the game's options during the session, and the player's own save is never written.
//
// Every constant is from notes/research/sf2_match_options.md (Task 4's table, "the note" below).
//
// Two routes (ruling S14):
//  - the save edit: the host's lobby sets the three option bytes in the PACKED COPY of its profile record (ups.dat)
//    when it builds the hand-off (ApplyToSaveFiles, dead_or_alive_4_app.h LobbyProviders), so both sides boot on the
//    same edited bytes;
//  - the in-memory write, for a session whose save does not already hold the rules (a host with no record, or a record
//    too short to edit; RulesHeld false): at the session's first match-rule load the three bytes are written at
//    P + 0x15/0x17/0x18 (WriteToRecord, doa4_start_rules_hooks.cpp), once a session.
// Tag's time needs no code: the match-rule load gives Tag the constant time index 4 = 99 s (the note, section 3).
// No per-mode latch: the only value both modes share is the life gauge, and both modes want Largest (ruling S14 c).
//
// Pure: no SDK, no files. ArmMemoryWrite alone is defined in the game build (doa4_start_rules_hooks.cpp).

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "np_handoff.h"

namespace doa4::np::start_rules {

// The profile record file, below handoff::kSavePrefix (the layout of a save: <prefix>00000001/ups.dat/ups.dat, beside
// __thumbnail.png, and Headers/00000001/ups.dat.header; the note's sections 2 and 4).
inline constexpr char kRecordFile[] = "00000001/ups.dat/ups.dat";
// The current profile record's place in ups.dat: file offset = 0x7968 + record offset (the note, header).
inline constexpr size_t kRecordInFile = 0x7968;

// The options (record offsets from P; one plain byte each, byte = index; the note's section 2 table).
inline constexpr size_t kMatchPointSingle = 0x15;    // rounds to win - 1; file 0x797D
inline constexpr uint8_t kMatchPointSingleTarget = 0x02;  // first to 3
inline constexpr size_t kLifeGauge = 0x17;           // 0 Smallest .. 4 Largest, shared by Single and Tag; file 0x797F
inline constexpr uint8_t kLifeGaugeTarget = 0x04;    // Largest
inline constexpr size_t kRoundTime = 0x18;           // 0 = 30, 1 = 40, 2 = 50, 3 = 60, 4 = 99, 5 = no limit; Single only; file 0x7980
inline constexpr uint8_t kRoundTimeTarget = 0x03;    // 60 s
// (Match Point (Tag) at 0x16 has no rule and is left alone.)

// The record bytes the in-memory write needs from P (through the last option byte), and the shortest ups.dat that
// holds all three options.
inline constexpr size_t kRecordMemoryBytes = kRoundTime + 1;
inline constexpr size_t kMinRecordFileBytes = kRecordInFile + kRoundTime + 1;

// Guest addresses (the note, sections 2 and 3).
inline constexpr uint32_t kProfilePointer = 0x83035CC8u;  // be32: P, the current profile record
inline constexpr uint32_t kTagFlag = 0x8303683Au;         // u8: not 0 = Tag
inline constexpr uint32_t kLifeValue = 0x8303691Cu;       // be16: the match's life value (player 1's)
inline constexpr uint32_t kRoundsToWin = 0x82FCC8E3u;     // u8
inline constexpr uint32_t kTimeSeconds = 0x83033A7Au;     // u8: the round's time limit (255 = no limit)

// Sets the three option bytes in the profile record file of files (by its name, handoff::kSavePrefix + kRecordFile).
// "" on success, and when no such file exists (nothing to edit: every file unchanged). An error text when the record is
// shorter than kMinRecordFileBytes (every file unchanged). Every other file and byte stays as it was. Idempotent.
std::string ApplyToSaveFiles(std::vector<handoff::File>* files);

// True when files hold a profile record of at least kMinRecordFileBytes whose three option bytes are at their targets.
// False with no record file (another name never counts), a record too short, or any of the three bytes elsewhere.
bool RulesHeld(const std::vector<handoff::File>& files);

// The in-memory write: record points at P. Sets the three option bytes at their record offsets; false (nothing
// written) when record is shorter than kRecordMemoryBytes.
bool WriteToRecord(std::span<uint8_t> record);

// The match-rules log line (ruling S15), decimal: "match rules: mode <single|tag> life <v> rounds <v> time <v>".
std::string MatchRulesLine(bool tag, uint16_t life, uint8_t rounds, uint8_t time);

// Game build only (doa4_start_rules_hooks.cpp): whether this session process writes the rules in memory at its first
// match-rule load. Called once, when the session's save folder has been written, with !RulesHeld(session files).
void ArmMemoryWrite(bool armed);

}  // namespace doa4::np::start_rules

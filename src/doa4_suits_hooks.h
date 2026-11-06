// doa4_suits_hooks.h
// Guards for the swimsuit costumes (spec section 6) and two test switches.

#pragma once

#include <cstdint>

#include "doa4_costumes.h"

namespace doa4::suits {

// The load-request guard's state: the record counts it compares against and the empty-slot marks.
// Called from OnPostSetup before any guest code runs (first with the image's counts, again with
// the final state after the suits are added). The state is copied into non-atomic storage, which
// is safe only because no guest code runs yet: do not call this at run time.
void InstallGuards(const costumes::GuardState& state);

// How many costume records the INSTALLED guard state marks as empty slots (the sum over all
// characters), read back from the state the hook uses: the start-up log line reports this, so a
// state that was never installed shows as 0.
uint32_t InstalledEmptySlots();

// How many shell-pass ("fur") draws were skipped because the costume's .cat had no type-6 block.
uint64_t FurPassesSkipped();

// The costume-select labels past COS. 10 (doa4_suits_labels.h): the guest address of the extended
// sheet-6 sprite table that labels::ExtendTable installed (records 197..203 = COS. 11..17). Non-zero,
// the costume list drawer sub_82528190 runs inside a labels::ListingScope and the resolver hook
// rewrites ids 133..139 to 197..203; 0, the default, leaves both untouched. Called from OnPostSetup
// before any guest code runs.
void InstallLabelTable(uint32_t table);
// The installed table (0 = none), read back for the start-up log.
uint32_t InstalledLabelTable();

// Which girls have suits in the tables (costumes::CountSuits of Apply's result), for the select screen's count copy.
// Called from OnPostSetup before any guest code runs; all zero, the default, leaves the copy as the game fills it.
void InstallSuitCounts(const costumes::SuitCounts& counts);
// The installed counts, read back from the hook's state for the start-up log.
costumes::SuitCounts InstalledSuitCounts();

}  // namespace doa4::suits

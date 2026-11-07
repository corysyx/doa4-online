// Suit converter (Plan 2): one suit through the converter. ConvertSuit runs convert.main's order of calls for one suit
// and one recipe level (the oracle harness's LEVELS: L0 is geometry only; L1 adds the bust, L2 the waist, L3 the hands,
// M1 the rest of the physics). Levels whose features are not ported yet return a ConvertFailure{"level", ...}.
//
// The work runs under num::RunUnderConvertEnv (the converter's floating-point environment); the cancel token is
// looked at between stages; everything thrown inside (ConvertError, InputChanged, std::bad_alloc, any exception) is
// caught at this boundary and returned as a ConvertFailure naming the stage. With a Trace the stage trace v1 is
// written line by line (doa4_suitconv_trace.h); with a null Trace nothing is formatted. Pure C++ plus the SHA-1 of
// the trace (gamefiles::Sha1Hex) and the input reads of ReadSuitInputs (doa4_suitconv_inputs.h).

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "doa4_suitconv_recipe.h"
#include "doa4_suitconv_trace.h"

namespace doa4::suitconv {

class InputSet;

enum class Level { kL0, kL1, kL2, kL3, kM1 };

using Bytes = std::shared_ptr<const std::vector<uint8_t>>;

// The entries one suit reads. The gen pair is null unless the suit has gen_cloth.
struct SuitInputs {
  Bytes body_tpr, body_cat, suit_tpr, suit_cat, ref_tpr, ref_cat, hand_tpr, hand_cat, waist_tpr, waist_cat,
      gen_donor_cat, gen_bend_cat;
};

struct SuitOutput {
  std::vector<uint8_t> tpr, cat;
};

struct ConvertFailure {
  std::string stage, what;
};

// Converts one suit at `level`. Returns the two files, or nullopt with *failure (when failure is not null) set.
// cancel and trace may be null.
std::optional<SuitOutput> ConvertSuit(const SuitRecipe& suit, const GirlRecipe& girl, const SuitInputs& in, Level level,
                                      const CancelToken* cancel, Trace* trace, ConvertFailure* failure);

// The girl named `girl` ("vks", ...), or null.
const GirlRecipe* FindGirl(std::string_view girl);

// Reads (and re-checks) the entries the suit needs: the body pair from chr.afs at body_index / +1, the suit pair from
// its archive at tpr_index / +1, the reference and hand pairs from the girl, the waist pair from WaistTemplate, and the
// gen pair (GenDonor, GenBend) only when the suit has gen_cloth. Throws InputChanged as InputSet::Read does.
SuitInputs ReadSuitInputs(InputSet& set, const SuitRecipe& suit, const GirlRecipe& girl);

}  // namespace doa4::suitconv

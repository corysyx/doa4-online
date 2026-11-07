// Suit converter (Plan 2): physics_port.attach and _al. See doa4_suitconv_catx.h.
#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_catx.h"

#include "doa4_suitconv_num.h"

namespace doa4::suitconv::catx {

size_t Al(std::vector<uint8_t>& b, size_t a) {
  if (a == 0) throw ConvertError("integer division or modulo by zero");
  b.resize(b.size() + (a - b.size() % a) % a, 0);  // b.extend(bytes(-len(b) % a))
  return b.size();
}

std::vector<uint8_t> Attach(std::span<const uint8_t> cat, const std::vector<uint8_t>* mpv,
                            const std::vector<uint8_t>* cloth, const std::vector<uint8_t>* donor_cat,
                            const std::vector<uint8_t>* head_physics) {
  std::vector<uint8_t> out(cat.begin(), cat.end());
  if (donor_cat != nullptr) throw ConvertError("not ported yet: attach, the hand donor's request table (_add_apa)");
  if (head_physics && !head_physics->empty()) throw ConvertError("not ported yet: attach, the head physics section");
  if (mpv && !mpv->empty()) {
    const size_t at = Al(out, 16);
    out.insert(out.end(), mpv->begin(), mpv->end());
    if (out.size() < 0x10) throw ConvertError("struct.error: pack_into requires a buffer of at least 16 bytes");
    if (at > 0xFFFFFFFFull) throw ConvertError("struct.error: argument out of range");
    for (int i = 0; i < 4; ++i) out[0xC + i] = static_cast<uint8_t>(at >> (8 * (3 - i)));  // struct.pack_into('>I', out, 0xC, at)
  }
  // mpv_at = _u32(out, 0xC) if mpv else 0 is only read by the cloth branch below.
  if (cloth && !cloth->empty())
    throw ConvertError(mpv && !mpv->empty()
                           ? "not ported yet: attach, the MULTI_CLOTH and the relocation of the MPV's pin rows (MPP +0x40)"
                           : "not ported yet: attach, the MULTI_CLOTH");
  Al(out, 16);
  return out;
}

}  // namespace doa4::suitconv::catx

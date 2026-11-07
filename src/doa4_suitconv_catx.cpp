// Suit converter (Plan 2): physics_port.attach and _al. See doa4_suitconv_catx.h.
#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_catx.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <span>
#include <string>

#include "doa4_suitconv_be.h"
#include "doa4_suitconv_num.h"

namespace doa4::suitconv::catx {

size_t Al(std::vector<uint8_t>& b, size_t a) {
  if (a == 0) throw ConvertError("integer division or modulo by zero");
  b.resize(b.size() + (a - b.size() % a) % a, 0);  // b.extend(bytes(-len(b) % a))
  return b.size();
}

namespace {

void PutU32At(std::vector<uint8_t>& b, int64_t at, uint32_t v) {
  if (at < 0 || static_cast<uint64_t>(at) + 4 > b.size()) throw ConvertError("struct.error: pack_into offset out of range");
  for (int i = 0; i < 4; ++i) b[static_cast<size_t>(at) + i] = static_cast<uint8_t>(v >> (8 * (3 - i)));
}

// _add_apa(out, donor_cat) (physics_port.py lines 872-916): copy the donor's type-5 block ('apa', the hand request
// table) to the end of the file, move its absolute offsets, and rewrite the directory with the new entry.
void AddApa(std::vector<uint8_t>& out, const std::vector<uint8_t>& donor) {
  const be::Reader d(std::span<const uint8_t>(donor.data(), donor.size()));
  const int64_t dir_off = d.U32(4);
  std::map<uint8_t, uint32_t> entries;  // entries[d[p + 2]] = _u32(d, p + 4); a later entry of one type wins
  int64_t p = dir_off;
  while (d.U32(p)) {
    entries[d.U8(p + 2)] = d.U32(p + 4);
    p += 8;
  }
  const auto it = entries.find(5);
  if (it == entries.end()) throw ConvertError("KeyError: 5");
  const int64_t apa = it->second;
  if (d.U32(apa) != 0x617061u) {
    char hex[24];
    std::snprintf(hex, sizeof hex, "0x%llx", static_cast<unsigned long long>(apa));
    throw ConvertError(std::string("AssertionError: ('not an apa block', '") + hex + "')");
  }
  const int64_t count = d.U32(apa + 4);
  std::vector<int64_t> entry_at;
  for (int64_t i = 0; i < count; ++i) entry_at.push_back(d.U32(apa + 8 + 4 * i));
  std::vector<int64_t> fields, rule_dirs, lows{apa};
  for (int64_t e : entry_at) {
    const auto h = d.U32s(e, 4);  // nmap, mp, nrules, rp
    const int64_t mp = h[1], nrules = h[2], rp = h[3];
    lows.push_back(e);
    lows.push_back(mp);
    if (rp) {
      lows.push_back(rp);
      for (int64_t k = 0; k < nrules; ++k) {
        lows.push_back(d.U32(rp + 8 * k + 4));
        rule_dirs.push_back(rp + 8 * k + 4);
      }
    }
    fields.push_back(e + 4);
    fields.push_back(e + 12);
  }
  const int64_t start = *std::min_element(lows.begin(), lows.end());
  const int64_t end = apa + 8 + 4 * count;
  const int64_t at = static_cast<int64_t>(Al(out, 16));
  const int64_t delta = at - start;
  const auto sl = d.Slice(static_cast<uint64_t>(start), static_cast<uint64_t>(end));
  std::vector<uint8_t> blob(sl.begin(), sl.end());
  std::vector<int64_t> all;
  for (int64_t i = 0; i < count; ++i) all.push_back(apa + 8 + 4 * i);
  all.insert(all.end(), fields.begin(), fields.end());
  all.insert(all.end(), rule_dirs.begin(), rule_dirs.end());
  for (int64_t f : all) {
    const uint32_t v = d.U32(f);
    if (v) PutU32At(blob, f - start, be::CheckU32S(static_cast<int64_t>(v) + delta));
  }
  out.insert(out.end(), blob.begin(), blob.end());
  std::vector<uint8_t> new_dir;
  const be::Reader o(std::span<const uint8_t>(out.data(), out.size()));
  int64_t q = o.U32(4);
  while (o.U32(q)) {
    const auto e8 = o.Slice(static_cast<uint64_t>(q), static_cast<uint64_t>(q) + 8);
    new_dir.insert(new_dir.end(), e8.begin(), e8.end());
    q += 8;
  }
  const uint32_t apa_new = be::CheckU32S(apa + delta);
  new_dir.insert(new_dir.end(), {1, 0, 5, 0});  // struct.pack('>BBBBI', 1, 0, 5, 0, apa + delta) + bytes(8)
  for (int i = 0; i < 4; ++i) new_dir.push_back(static_cast<uint8_t>(apa_new >> (8 * (3 - i))));
  new_dir.insert(new_dir.end(), 8, 0);
  const uint64_t dir_at = Al(out, 16);
  out.insert(out.end(), new_dir.begin(), new_dir.end());
  PutU32At(out, 4, be::CheckU32(dir_at));
}

}  // namespace

std::vector<uint8_t> Attach(std::span<const uint8_t> cat, const std::vector<uint8_t>* mpv,
                            const std::vector<uint8_t>* cloth, const std::vector<uint8_t>* donor_cat,
                            const std::vector<uint8_t>* head_physics) {
  std::vector<uint8_t> out(cat.begin(), cat.end());
  if (donor_cat != nullptr) AddApa(out, *donor_cat);
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

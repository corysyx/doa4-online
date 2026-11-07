// Suit converter (Plan 2): physics_port.attach, _al, with_spares and prep_head_physics. See doa4_suitconv_catx.h.
#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_catx.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
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

// hex(n) of a non-negative Python int, for assert texts
std::string Hex(int64_t v) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "0x%llx", static_cast<unsigned long long>(v));
  return buf;
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
    throw ConvertError(std::string("AssertionError: ('not an apa block', '") + Hex(apa) + "')");
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
  if (head_physics && !head_physics->empty()) {  // at = _al(out, 16); out += head_physics; +8 = at
    const size_t at = Al(out, 16);
    out.insert(out.end(), head_physics->begin(), head_physics->end());
    be::PackIntoU32(out, 8, at);
  }
  const bool has_mpv = mpv && !mpv->empty();
  if (has_mpv) {
    const size_t at = Al(out, 16);
    out.insert(out.end(), mpv->begin(), mpv->end());
    be::PackIntoU32(out, 0xC, at);  // struct.pack_into('>I', out, 0xC, at)
  }
  // mpv_at = _u32(out, 0xC) if mpv else 0
  const int64_t mpv_at = has_mpv ? static_cast<int64_t>(be::Reader(std::span<const uint8_t>(out)).U32(0xC)) : 0;
  if (cloth && !cloth->empty()) {
    const size_t at = Al(out, 16);
    out.insert(out.end(), cloth->begin(), cloth->end());
    be::PackIntoU32(out, 0x14, at);
    if (has_mpv) {  // the MPV's pin rows hold offsets relative to the file start: move them by the cloth's position
      const int64_t mpp =
          mpv_at + static_cast<int64_t>(be::Reader(std::span<const uint8_t>(out)).U32(mpv_at + 36 + 16));
      const auto bc = be::Reader(std::span<const uint8_t>(out)).U32s(mpp + 0x40, 2);  // block, count
      const int64_t block = bc[0], count = bc[1];
      for (int64_t i = 0; i < count; ++i) {
        const int64_t row = mpp + block + 40 * i;
        const int32_t rel = static_cast<int32_t>(be::Reader(std::span<const uint8_t>(out)).U32(row));
        const int64_t v = static_cast<int64_t>(at) + rel - mpp;
        if (v < INT32_MIN || v > INT32_MAX)
          throw ConvertError("struct.error: 'i' format requires -2147483648 <= number <= 2147483647");
        PutU32At(out, row, static_cast<uint32_t>(static_cast<int32_t>(v)));
      }
    }
  }
  Al(out, 16);
  return out;
}

// ---------------------------------------------------------------- with_spares (physics_port.py 466-494)
std::optional<std::vector<uint8_t>> WithSpares(const std::vector<uint8_t>* cloth, std::span<const uint8_t> ref_cat) {
  const be::Reader d(ref_cat);
  const int64_t mc = d.U32(0x14);
  const int64_t sp = mc ? static_cast<int64_t>(d.U32(mc + 0x20)) : 0;
  if (!sp) return cloth ? std::optional<std::vector<uint8_t>>(*cloth) : std::nullopt;  // 'hair spares: none in the reference'
  auto starts = [&](int64_t at, const char* tag) {  // d[at:at + 11] == tag
    const auto sl = d.Slice(static_cast<uint64_t>(at), static_cast<uint64_t>(at) + 11);
    return sl.size() == 11 && std::memcmp(sl.data(), tag, 11) == 0;
  };
  if (!starts(mc, "MULTI_CLOTH")) throw ConvertError("AssertionError");
  const int64_t s = mc + sp;
  if (!starts(s, "SPARE_CHUNK")) core::Assert(false, "('no SPARE_CHUNK at the offset', '" + Hex(s) + "')");
  const int64_t count = d.U32(s + 0x1C), table = d.U32(s + 0x20);
  int64_t end = 0;
  for (int64_t i = 0; i < count; ++i) {
    const int64_t b = s + static_cast<int64_t>(d.U32(s + table + 4 * i));
    end = std::max(end, b + static_cast<int64_t>(d.U32(b + 0x10)) + static_cast<int64_t>(d.U32(b + 0x24)));
  }
  if (end != static_cast<int64_t>(d.size()))
    core::Assert(false, "('the spare blocks do not end the file', '" + Hex(end) + "', '" +
                            Hex(static_cast<int64_t>(d.size())) + "')");
  std::vector<uint8_t> out;
  if (!cloth) {  // a new empty MULTI_CLOTH: the reference's first 0x50 bytes, +0x18 = 0
    const auto sl = d.Slice(static_cast<uint64_t>(mc), static_cast<uint64_t>(mc) + 0x50);
    out.assign(sl.begin(), sl.end());
    be::PackIntoU32(out, 0x18, 0);
  } else {
    out = *cloth;
  }
  const size_t at = Al(out, 16);
  be::PackIntoU32(out, 0x20, at);
  const auto tail = d.From(static_cast<uint64_t>(s));
  out.insert(out.end(), tail.begin(), tail.end());
  return out;
}

// ---------------------------------------------------------------- prep_head_physics (physics_port.py 917-967)
std::vector<int64_t> SmallSetOrder(const std::vector<int64_t>& ids) {
  // CPython 3.12 Objects/setobject.c set_add_entry for a set of at most four small ints: the table has 8 slots (mask
  // 7), no linear probing fits (LINEAR_PROBES is 9, and i + 9 <= 7 never holds), a collision moves on by
  // `perturb >>= 5; i = (i * 5 + 1 + perturb) & mask`, and the table does not resize while fill * 5 < mask * 3, which
  // holds up to four entries. hash(n) is n (-1 is -2) for the small values a node id takes.
  std::array<int64_t, 8> slot{};
  std::array<bool, 8> used{};
  size_t distinct = 0;
  for (int64_t key : ids) {
    const int64_t h = key == -1 ? -2 : key;
    uint64_t perturb = static_cast<uint64_t>(h);
    size_t i = static_cast<size_t>(perturb & 7);
    while (used[i] && slot[i] != key) {
      perturb >>= 5;
      i = (i * 5 + 1 + static_cast<size_t>(perturb)) & 7;
    }
    if (!used[i]) {
      used[i] = true;
      slot[i] = key;
      if (++distinct > 4) throw ConvertError("SmallSetOrder: more than four distinct head objects");
    }
  }
  std::vector<int64_t> order;
  for (size_t i = 0; i < 8; ++i)
    if (used[i]) order.push_back(slot[i]);
  return order;
}

HeadPhysics PrepHeadPhysics(std::span<const uint8_t> ref_cat, const std::vector<int64_t>& head_ids,
                            std::vector<std::optional<core::Object>>& objects) {
  const be::Reader d(ref_cat);
  const int64_t base = d.U32(8), mpv = d.U32(12);
  if (!(base != 0 && mpv > base))
    core::Assert(false, "('physics section must sit before MPV', '" + Hex(base) + "', '" + Hex(mpv) + "')");
  const std::set<int64_t> head(head_ids.begin(), head_ids.end());
  std::vector<uint8_t> keep;  // the kept 8-byte directory entries, joined
  int64_t o = base;
  while (d.U8(o) != 0xFF) {
    const uint8_t kind = d.U8(o);
    const int64_t off = d.U32(o + 4);
    const int64_t r = base + off;
    bool ok = false;
    if (kind == 1) {
      const int64_t target = d.U32(base + static_cast<int64_t>(d.U32(r + 0x4C)));
      ok = ((target >> 16) == 0xFFFF || (target >> 16) == 0xFFFE) && head.count(target & 0xFFF) != 0;
    } else if (kind == 0xFA) {
      ok = head.count(d.U16(r + 2)) != 0 && head.count(d.U16(r + 4)) != 0;  // the second read only when the first is in
    }
    if (ok) {
      const auto e = d.Slice(static_cast<uint64_t>(o), static_cast<uint64_t>(o) + 8);
      keep.insert(keep.end(), e.begin(), e.end());
    }
    o += 8;
  }
  const auto sec = d.Slice(static_cast<uint64_t>(base), static_cast<uint64_t>(mpv));
  std::vector<uint8_t> section(sec.begin(), sec.end());
  std::vector<uint8_t> repl = keep;  // table = joined entries + b'\xff' + bytes(7)
  repl.push_back(0xFF);
  repl.insert(repl.end(), 7, 0);
  const int64_t n = o + 8 - base;
  if (!(static_cast<int64_t>(repl.size()) <= n)) throw ConvertError("AssertionError");
  repl.insert(repl.end(), static_cast<size_t>(n) - repl.size(), 0);  // table + bytes(n - len(table))
  // section[:n] = repl: a slice assignment, which replaces at most len(section) bytes with all of repl
  const size_t cut = std::min(static_cast<size_t>(n), section.size());
  section.erase(section.begin(), section.begin() + static_cast<std::ptrdiff_t>(cut));
  section.insert(section.begin(), repl.begin(), repl.end());
  // the face channels, copied whole: their offsets are relative to each block
  HeadPhysics out;
  for (int64_t index : {int64_t{2}, int64_t{3}}) {
    const int64_t off = d.U32(mpv + 36 + 4 * index);
    if (!off) core::Assert(false, "('reference has no face channel', " + std::to_string(index) + ")");
    const int64_t p = mpv + off;
    const auto h = d.U32s(p + 12, 4);  // obj, kind, nposes, _mp
    if (!(head.count(h[0]) != 0 && h[1] == 0))
      core::Assert(false, "('face channel', " + std::to_string(index) + ", " + std::to_string(h[0]) + ", " +
                              std::to_string(h[1]) + ")");
    int64_t q = p + h[3];
    int64_t records = 1;
    while (d.U32(q) != 0xFFFFFFFFu) {
      records += (d.U32(q) & 0x80000000u) ? 1 : 0;
      q += 4;
    }
    if (h[2] == 0) core::Fail("ValueError: max() arg is an empty sequence");
    int64_t end = 0;
    for (uint32_t i = 0; i < h[2]; ++i) {
      const int64_t v = p + static_cast<int64_t>(d.U32(p + 28 + 4 * static_cast<int64_t>(i)));
      end = i == 0 ? v : std::max(end, v);
    }
    end += records * 12;
    const auto ch = d.Slice(static_cast<uint64_t>(p), static_cast<uint64_t>(end));
    out.channels[index].assign(ch.begin(), ch.end());
  }
  // for oid in head: for part in objects[oid][0]: assert part.native is not None; part.writable = True. The set
  // iterates in CPython's table order. The order reaches no output byte (writable is set on every part either way);
  // it only decides which assert text a malformed reference raises first.
  for (int64_t oid : SmallSetOrder(head_ids)) {
    if (oid < 0 || oid >= static_cast<int64_t>(objects.size())) core::Fail("IndexError: list index out of range");
    auto& ob = objects[static_cast<size_t>(oid)];
    if (!ob) core::Fail("TypeError: 'NoneType' object is not subscriptable");
    for (core::Part* part : ob->ps) {
      if (!part->native) core::Assert(false, "('head decl was not carried', " + std::to_string(oid) + ")");
      part->writable = true;
    }
  }
  out.section = std::move(section);
  return out;
}

void MergeChannels(std::map<int64_t, std::vector<uint8_t>>& hand,
                   const std::map<int64_t, std::vector<uint8_t>>& face) {
  for (const auto& [index, blob] : face) hand[index] = blob;  // {**hand, **face}: the face value wins
}

}  // namespace doa4::suitconv::catx

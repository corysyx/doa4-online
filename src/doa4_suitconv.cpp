// Suit converter (Plan 2): ConvertSuit, convert.main's order of calls. See doa4_suitconv.h. Line numbers are
// convert.py's (SHA-1 05cb00536712a89d9cd4cb8f79cebe529e6db2fa).
#include "doa4_suitconv_fp.h"

#include "doa4_suitconv.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <set>
#include <stdexcept>

#include "doa4_suitconv_bust.h"
#include "doa4_suitconv_catx.h"
#include "doa4_suitconv_core.h"
#include "doa4_suitconv_inputs.h"
#include "doa4_suitconv_num.h"
#include "doa4_suitconv_waist.h"

namespace doa4::suitconv {

namespace {

namespace c = core;

struct Cancelled {};

std::string Fmt5(uint32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%05u", v);
  return buf;
}

std::string Join(const std::vector<int64_t>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); ++i) s += (i ? "," : "") + std::to_string(v[i]);
  return s.empty() ? "-" : s;
}

std::span<const uint8_t> Need(const Bytes& b, const char* what) {
  if (!b) throw ConvertError(std::string("input missing: ") + what);
  return std::span<const uint8_t>(b->data(), b->size());
}

struct Job {
  const SuitRecipe* suit;
  const GirlRecipe* girl;
  const SuitInputs* in;
  const CancelToken* cancel;
  Trace* trace;
  Level level;
  std::string stage;
  std::optional<SuitOutput> out;
};

// The run of convert.main (lines 1315-1563) with the level's arguments: --neck-weights owner --expand-formats
// --mesh-passes, --leg-midline (Lei Fang), --carry-head (Tina), --cat 2,4,ref4,6, and at L1 and above --bust-down /
// --bust-up (the suit's strengths) and --strap-fade (when non-zero), at L2 and above --waist-fit. No other option is
// ported yet.
void RunConvert(Job& job) {
  const SuitRecipe& sr = *job.suit;
  const GirlRecipe& gr = *job.girl;
  const SuitInputs& in = *job.in;
  const bool bust_on = job.level >= Level::kL1 && sr.bust_down != 0.0;  // `if a.bust_down` (a float: 0.0 is false)
  trace::Tracer tr(job.trace);
  auto step = [&](const char* name) {
    if (job.cancel && job.cancel->cancelled()) throw Cancelled{};
    job.stage = name;
  };
  auto stage = [&](const char* name) {  // a traced stage: the S line goes out before the call
    step(name);
    tr.S(name);
  };

  step("load");
  c::Context ctx;
  ctx.expand = true;     // EXPAND[0] = a.expand_formats
  ctx.neck_mode = true;  // --neck-weights owner
  auto cat_of = [](const Bytes& b, const char* what) {
    Need(b, what);
    return std::shared_ptr<const c::Bytes>(b);
  };
  const c::Model body = c::LoadModel(Fmt5(gr.body_index) + "_" + gr.girl + "1.tpr", Need(in.body_tpr, "body .tpr"),
                                     cat_of(in.body_cat, "body .cat"));
  const c::Model ref = c::LoadModel("ref", Need(in.ref_tpr, "reference .tpr"), cat_of(in.ref_cat, "reference .cat"));
  const c::Model suit = c::LoadModel(Fmt5(sr.tpr_index) + "_" + sr.design + "_m", Need(in.suit_tpr, "suit .tpr"),
                                     cat_of(in.suit_cat, "suit .cat"));

  step("main_chain");
  const c::MainChain mc = c::MainChainOf(ref);

  stage("channels");
  const c::ChannelMap cm = c::ChannelMapOf(ref);
  const auto& chan2id = cm.out;
  if (tr.on()) {
    std::vector<int64_t> codes;
    for (const auto& [code, id] : chan2id.items()) codes.push_back(code);
    std::sort(codes.begin(), codes.end());
    for (int64_t code : codes) {
      char name[24];
      std::snprintf(name, sizeof name, "chan.%04llx", static_cast<unsigned long long>(code));
      tr.K(name, std::to_string(chan2id.At(code)));
    }
    tr.K("main_n", std::to_string(cm.main.size()));
    std::vector<uint8_t> m;
    for (const auto& [code, id] : cm.main) {
      m.push_back(uint8_t(code >> 8));
      m.push_back(uint8_t(code));
      m.push_back(id);
    }
    tr.B("main", m);
  }
  // NECK_ID[0] = chan2id.get(0x9101) (an assignment: None when the reference has no neck)
  const int64_t* neck = chan2id.Find(0x9101);
  ctx.neck_id = neck ? std::optional<int64_t>(*neck) : std::nullopt;
  ctx.allow_owner_slot = false;  // ALLOW_OWNER_SLOT[0] = a.no_rigid_ankle

  const int64_t lower_root = chan2id.At(0x9002);
  stage("skeleton");
  const std::vector<tpr::Node> nodes = c::BuildSkeleton(ref, mc.main_max, mc.orphans, lower_root);
  if (tr.on()) {
    tr.K("main_max", std::to_string(mc.main_max));
    tr.K("orphans", Join(mc.orphans));
    tr.K("lower_root", std::to_string(lower_root));
    tr.K("nodes", std::to_string(nodes.size()));
    tr.B("hie", tpr::BuildHie(nodes));
  }
  step("roles");
  for (int64_t code : c::RoleCodesSorted()) {
    char hex[16];
    std::snprintf(hex, sizeof hex, "0x%llx", static_cast<unsigned long long>(code));
    c::Assert(chan2id.Contains(code), std::string("('reference has no node for channel', '") + hex + "')");
  }
  std::set<int64_t> ref_codes;
  for (const auto& [code, id] : cm.main) ref_codes.insert(code);
  for (const auto& [code, obj] : {std::pair<int64_t, int64_t>{0x9103, 3}, {0x9109, 22}})
    if (!ref_codes.count(code)) ctx.twist_rigid_body.insert(obj);  // rule A2

  stage("maps");
  const c::Maps maps = c::FitMaps(body, ref, chan2id, gr.leg_midline);
  if (tr.on()) {
    tr.K("torso_y_scale", trace::Hx(maps.info.torso_y_scale));
    tr.K("torso_x_scale_hips", trace::Hx(maps.info.torso_x_scale_hips));
    tr.K("torso_x_scale_shoulders", trace::Hx(maps.info.torso_x_scale_shoulders));
    tr.K("hips_y", trace::Hx(maps.info.hips_y.first) + " " + trace::Hx(maps.info.hips_y.second));
    tr.K("shoulders_y", trace::Hx(maps.info.shoulders_y.first) + " " + trace::Hx(maps.info.shoulders_y.second));
    const c::Vec3 probes[2] = {{0.1, 0.2, 0.3}, {-0.4, 1.1, 0.05}};
    for (const auto& [bid, f] : maps.maps.items())
      for (int pi = 0; pi < 2; ++pi) {
        const c::Vec3 v = f(probes[pi]);
        tr.K("map." + std::to_string(bid) + "." + std::to_string(pi),
             trace::Hx(v[0]) + " " + trace::Hx(v[1]) + " " + trace::Hx(v[2]));
      }
  }

  step("dummy_mesh");
  {
    const c::Bytes* loc = nullptr;
    for (const auto& g : body.geo) {
      for (const auto& m : g.meshes)
        if (c::U32(m, 0x18) == 1 && c::U32(m, 0x30) == 7) {
          loc = &m;
          break;
        }
      if (loc) break;
    }
    if (!loc) throw ConvertError("IndexError: list index out of range (no 1-vertex locator mesh in the body)");
    ctx.dummy_mesh = tpr::MeshPatch(*loc, 0, 0, 0, 3);
  }
  c::IdMap body_ids;
  for (const auto& [i, j] : body.joint.items()) body_ids.Set(i, i);
  c::PartArena arena;

  stage("parts_body");
  std::vector<c::Part*> parts =
      c::CollectParts(ctx, arena, body, body_ids, maps, 0, chan2id, c::DropObjectsBody());
  if (tr.on()) {
    tr.K("nparts", std::to_string(parts.size()));
    for (const c::Part* p : parts) tr.PartFull(*p);
  }
  std::vector<c::TexSpan> textures = c::TexSpans(body);

  stage("select_copy");
  const std::optional<std::set<int64_t>> keep = c::SelectCopy(suit, body);
  if (tr.on()) {
    if (!keep) {
      tr.K("keep", "none");
    } else {
      tr.K("keep_n", std::to_string(keep->size()));
      std::vector<uint8_t> b;
      for (int64_t i : *keep)
        for (int k = 0; k < 4; ++k) b.push_back(uint8_t(static_cast<uint32_t>(static_cast<int32_t>(i)) >> (8 * k)));
      tr.B("keep", b);
    }
  }
  stage("accessories");
  const std::vector<int64_t> acc = c::AccessoryIds(suit, body, keep);
  if (tr.on()) tr.K("acc", Join(acc));
  const std::set<int64_t> accset(acc.begin(), acc.end());
  stage("smap");
  const c::IdMap smap = c::SuitToBodyIds(suit, body, keep, accset);
  if (tr.on()) {
    std::string s;
    for (const auto& [a, b] : smap.items()) s += (s.empty() ? "" : ",") + std::to_string(a) + ":" + std::to_string(b);
    tr.K("smap", s.empty() ? "-" : s);
  }
  stage("parts_suit");
  const std::vector<c::Part*> suit_parts = c::CollectParts(ctx, arena, suit, smap, maps,
                                                           static_cast<int64_t>(body.tex.size()), chan2id, {}, accset);
  if (tr.on()) {
    tr.K("nparts", std::to_string(suit_parts.size()));
    for (const c::Part* p : suit_parts) tr.PartFull(*p);
  }
  std::set<int64_t> rigid_owners;
  for (const auto& nd : nodes)
    if (nd.parent < 0) rigid_owners.insert(nd.id);
  stage("snap_s1");
  auto snapped = c::SnapSuitWeights(parts, suit_parts, 0.0005, rigid_owners);
  if (tr.on()) {
    tr.K("snapped", std::to_string(snapped.size()));
    for (const auto& [p, j] : snapped) tr.V(*p, j);
  }
  parts.insert(parts.end(), suit_parts.begin(), suit_parts.end());
  {
    auto st = c::TexSpans(suit);
    textures.insert(textures.end(), st.begin(), st.end());
  }

  if (gr.carry_head) {  // rule H1 (lines 1431-1451)
    step("carry_head");
    std::vector<int64_t> head_ids;
    for (int64_t code : {0x9001, 0x9011, 0x9015})
      if (const int64_t* id = chan2id.Find(code)) head_ids.push_back(*id);
    c::Assert(!head_ids.empty() && chan2id.Contains(0x9001), "--carry-head: the reference has no head node");
    std::vector<int64_t> carry;
    for (int64_t h : head_ids)
      if (std::find(carry.begin(), carry.end(), h) == carry.end()) carry.push_back(h);
    for (int64_t oid : carry) {
      std::vector<c::Part*> kept;
      for (c::Part* p : parts)
        if (p->owner != oid) kept.push_back(p);
      parts = std::move(kept);
      if (oid < 0 || oid >= static_cast<int64_t>(ref.geo.size()))
        throw ConvertError("IndexError: list index out of range");
      const auto& decls = ref.geo[static_cast<size_t>(oid)].decls;
      for (size_t k = 0; k < decls.size(); ++k) {
        if (decls[k].head[5] <= 1) continue;
        auto [rp, used] = c::CarryRefDecl(arena, ref, oid, static_cast<int64_t>(k), static_cast<int64_t>(textures.size()));
        parts.push_back(rp);
        const auto rtex = c::TexSpans(ref);
        for (uint32_t t : used) {
          if (t >= rtex.size()) throw ConvertError("IndexError: list index out of range");
          textures.push_back(rtex[t]);
        }
      }
    }
  }

  if (ctx.neck_mode) {  // rule N2 seam ring (line 1465)
    const int64_t nid = chan2id.At(0x9101);
    std::vector<c::Part*> others, neck;
    for (c::Part* p : parts)
      if (p->owner != nid) others.push_back(p);
    for (c::Part* p : parts)
      if (p->owner == nid && !p->native) neck.push_back(p);
    stage("snap_n2");
    auto ring = c::SnapSuitWeights(others, neck);
    if (tr.on()) {
      tr.K("snapped", std::to_string(ring.size()));
      for (const auto& [p, j] : ring) tr.V(*p, j);
    }
  }
  if (!acc.empty()) {
    stage("accessory_parts");
    const auto ap = c::AttachAccessories(ctx, arena, suit, acc, parts, maps, static_cast<int64_t>(body.tex.size()),
                                         chan2id);
    if (tr.on()) {
      tr.K("nparts", std::to_string(ap.size()));
      for (const c::Part* p : ap) tr.PartFull(*p);
    }
    parts.insert(parts.end(), ap.begin(), ap.end());
  }

  stage("objects");
  c::BuiltObjects bo = c::BuildObjects(ctx, parts, nodes);
  if (tr.on()) {
    tr.K("nparts_total", std::to_string(parts.size()));
    tr.K("owner_slot", Join(std::vector<int64_t>(ctx.owner_slot.begin(), ctx.owner_slot.end())));
    tr.State(bo.objects);
    trace::Ser s;
    s.L(bo.bind4.size());
    for (const auto& [id, v] : bo.bind4.items()) {
      s.L(2);
      s.I(id);
      s.L(3);
      for (double x : v) s.D(x);
    }
    tr.B("bind4", s.bytes());
  }

  int64_t chest = -1;
  if (bust_on) {  // line 1486; cloth_objs is empty (no --port-cloth before M1), cloth_model=suit
    stage("bust");
    chest = bust::PrepBust(bo.objects, parts, chan2id, sr.bust_down, sr.bust_up, {}, &suit, sr.strap_fade);
    if (tr.on()) {
      tr.K("chest", std::to_string(chest));
      for (const c::Part* p : bo.objects[static_cast<size_t>(chest)]->ps) {
        if (!p->bust) continue;
        for (size_t j = 0; j < p->bust->first.size(); ++j) {
          std::string l = "D " + std::to_string(p->index) + " " + std::to_string(j);
          for (const auto* side : {&p->bust->first, &p->bust->second})
            for (double x : (*side)[j]) l += " " + trace::Hx(x);
          tr.Line(l);
        }
      }
      tr.State(bo.objects);
    }
  }

  if (job.level >= Level::kL2) {  // line 1495: `if a.waist_fit`, the shared template hel05's bare waist (bones chest 2, pelvis 3)
    stage("waist");
    const c::Model tmpl = c::LoadModel("00020_hel05", Need(in.waist_tpr, "waist template .tpr"),
                                       cat_of(in.waist_cat, "waist template .cat"));
    const std::optional<int64_t> target = waist::PrepWaistFit(tmpl, 2, 3, bo.objects, parts, nodes, chan2id);
    if (tr.on()) {
      trace::Ser s;
      if (target) {
        s.I(*target);
      } else {
        s.N();
      }
      tr.B("waist_target", s.bytes());
      tr.State(bo.objects);
    }
  }

  ctx.mesh_passes = true;  // MESH_PASSES = bool(a.mesh_passes) (line 1510)
  stage("encode");
  c::Encoded enc = c::EncodeObjects(ctx, bo.objects, bo.bind4);
  if (tr.on()) {
    tr.K("counts", "geos=" + std::to_string(enc.geos.size()) + " vbs=" + std::to_string(enc.vbs.size()) +
                       " ibs=" + std::to_string(enc.ibs.size()));
    tr.Geos(enc.geos, &enc.infos);
    for (size_t i = 0; i < enc.vbs.size(); ++i) tr.B("vb" + std::to_string(i), enc.vbs[i]);
    for (size_t i = 0; i < enc.ibs.size(); ++i) tr.B("ib" + std::to_string(i), enc.ibs[i]);
  }

  std::optional<std::vector<uint8_t>> mpv;
  if (bust_on) {
    stage("peak");  // line 1518
    const auto peak = bust::BustPeak(bo.objects, chest, bo.bind4);
    if (tr.on()) tr.K("peak", peak ? trace::Hx((*peak)[0]) + " " + trace::Hx((*peak)[1]) : "none");
    stage("mpv");  // line 1532: full_down = 1.0 / a.bust_down, full_up = 1.0 / a.bust_up; no hand channels, no pins at L1
    mpv = bust::BuildMpv(bo.objects, enc.geos, bo.bind4, chest, 0.0005, c::PyDiv(1.0, sr.bust_down),
                         c::PyDiv(1.0, sr.bust_up));
    tr.B("mpv", *mpv);
  }

  stage("tpr");
  SuitOutput out;
  out.tpr = c::BuildTpr(nodes, enc.geos, enc.infos, enc.vbs, enc.ibs, textures);
  if (tr.on()) {
    tr.K("counts", "nodes=" + std::to_string(nodes.size()) + " geos=" + std::to_string(enc.geos.size()) +
                       " vbs=" + std::to_string(enc.vbs.size()) + " ibs=" + std::to_string(enc.ibs.size()) +
                       " textures=" + std::to_string(textures.size()));
    tr.B("mdlgeo", tpr::BuildMdlGeo(enc.geos));
    tr.B("mdlinfo", tpr::BuildMdlInfo(enc.infos));
    tr.B("tpr", out.tpr);
  }
  std::vector<int64_t> kept_ids;
  {
    const std::set<int64_t> orph(mc.orphans.begin(), mc.orphans.end());
    for (const auto& nd : nodes)
      if (!orph.count(nd.id)) kept_ids.push_back(nd.id);
  }
  stage("cat");
  const c::Bytes base = c::BuildCat(ref, body, {2, 4, 6}, kept_ids, {"ref4"});
  tr.B("cat_base", base);
  out.cat = catx::Attach(base, mpv ? &*mpv : nullptr, nullptr, nullptr, nullptr);
  tr.B("cat", out.cat);
  job.out = std::move(out);
}

void RunJob(void* p) { RunConvert(*static_cast<Job*>(p)); }

std::optional<SuitOutput> Failed(ConvertFailure* failure, std::string stage, std::string what) {
  if (failure) *failure = ConvertFailure{std::move(stage), std::move(what)};
  return std::nullopt;
}

}  // namespace

std::optional<SuitOutput> ConvertSuit(const SuitRecipe& suit, const GirlRecipe& girl, const SuitInputs& in, Level level,
                                      const CancelToken* cancel, Trace* trace, ConvertFailure* failure) {
  // B1: the levels whose features are not ported yet. Each later task deletes its own gate.
  if (level == Level::kM1) {
    if (suit.port_cloth) return Failed(failure, "level", "not ported yet: port_cloth");
    if (suit.gen_cloth) return Failed(failure, "level", "not ported yet: gen_cloth");
    if (suit.fit_leg_colliders) return Failed(failure, "level", "not ported yet: fit_leg_colliders");
    if (suit.unstick_cloth) return Failed(failure, "level", "not ported yet: unstick_cloth");
    if (suit.back_margin != 0.0) return Failed(failure, "level", "not ported yet: back_margin");
    if (suit.sink_hamstrings != 0.0) return Failed(failure, "level", "not ported yet: sink_hamstrings");
    if (girl.hair_spares) return Failed(failure, "level", "not ported yet: hair_spares");
    if (girl.head_physics) return Failed(failure, "level", "not ported yet: head_physics");
  }
  if (level >= Level::kL3) return Failed(failure, "level", "not ported yet: hands");  // gate: hands (L3)

  Job job{&suit, &girl, &in, cancel, trace, level, "start", std::nullopt};
  try {
    num::RunUnderConvertEnv(&RunJob, &job);
  } catch (const Cancelled&) {
    return Failed(failure, "cancelled", "cancelled at " + job.stage);
  } catch (const std::bad_alloc&) {
    return Failed(failure, job.stage, "out of memory");
  } catch (const ConvertError& e) {
    return Failed(failure, job.stage, e.what());
  } catch (const InputChanged& e) {
    return Failed(failure, job.stage, std::string("input changed: ") + e.what());
  } catch (const std::exception& e) {
    return Failed(failure, job.stage, e.what());
  } catch (...) {
    return Failed(failure, job.stage, "unknown exception");
  }
  if (!job.out) return Failed(failure, job.stage, "no output");
  return std::move(job.out);
}

const GirlRecipe* FindGirl(std::string_view girl) {
  for (const GirlRecipe& g : Girls())
    if (girl == g.girl) return &g;
  return nullptr;
}

namespace {
EntryRef FindDisc(ArcId arc, uint32_t index) {
  for (const EntryRef& e : DiscEntries())
    if (e.arc == arc && e.index == index) return e;
  throw ConvertError("the recipe tables have no disc entry " + std::string(ArcName(arc)) + " " +
                           std::to_string(index));
}
}  // namespace

SuitInputs ReadSuitInputs(InputSet& set, const SuitRecipe& suit, const GirlRecipe& girl) {
  SuitInputs in;
  in.body_tpr = set.Read(FindDisc(ArcId::kChr, girl.body_index));
  in.body_cat = set.Read(FindDisc(ArcId::kChr, girl.body_index + 1));
  in.suit_tpr = set.Read(FindDisc(suit.arc, suit.tpr_index));
  in.suit_cat = set.Read(FindDisc(suit.arc, suit.tpr_index + 1));
  in.ref_tpr = set.Read(girl.ref_tpr);
  in.ref_cat = set.Read(girl.ref_cat);
  in.hand_tpr = set.Read(girl.hand_tpr);
  in.hand_cat = set.Read(girl.hand_cat);
  in.waist_tpr = set.Read(WaistTemplate(true));
  in.waist_cat = set.Read(WaistTemplate(false));
  if (suit.gen_cloth) {
    in.gen_donor_cat = set.Read(GenDonor());
    in.gen_bend_cat = set.Read(GenBend());
  }
  return in;
}

}  // namespace doa4::suitconv

// Suit converter (Plan 2): the stage trace v1. See doa4_suitconv_trace.h; the Python is plan2_oracle/trace_m1.py.
#include "doa4_suitconv_fp.h"

#include "doa4_suitconv_trace.h"

#include <bit>
#include <cstdio>
#include <cstring>

#include "doa4_gamefiles.h"

namespace doa4::suitconv {

Trace::~Trace() = default;

}  // namespace doa4::suitconv

namespace doa4::suitconv::trace {

namespace {
void Le(std::vector<uint8_t>& o, uint64_t v, int n) {
  for (int i = 0; i < n; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
std::string Src(const core::Part& p) {
  if (p.has_model) return p.model_name + ":" + std::to_string(p.obj_id) + ":" + std::to_string(p.decl_index);
  return "native:" + std::to_string(p.obj_id) + ":" + std::to_string(p.decl_index);
}
std::string WeightsText(const std::vector<std::pair<int64_t, double>>& w) {
  std::string s;
  for (size_t i = 0; i < w.size(); ++i) {
    if (i) s += ',';
    s += std::to_string(w[i].first);
    s += ':';
    s += Hx(w[i].second);
  }
  return s;
}
const uint8_t kEmpty[1] = {0};
}  // namespace

std::string Hx(double x) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(std::bit_cast<uint64_t>(x)));
  return buf;
}

std::string Sha1Hex(std::span<const uint8_t> b) {
  return gamefiles::Sha1Hex(b.empty() ? kEmpty : b.data(), b.size());
}

void Ser::U64(uint64_t v) { Le(out_, v, 8); }
void Ser::I(int64_t v) {
  out_.push_back('I');
  U64(static_cast<uint64_t>(v));
}
void Ser::D(double v) {
  out_.push_back('D');
  U64(std::bit_cast<uint64_t>(v));
}
void Ser::Y(std::span<const uint8_t> b) {
  out_.push_back('Y');
  U64(b.size());
  out_.insert(out_.end(), b.begin(), b.end());
}
void Ser::L(uint64_t count) {
  out_.push_back('L');
  U64(count);
}
void Ser::Z(uint64_t count) {
  out_.push_back('Z');
  U64(count);
}

std::vector<uint8_t> VertsBytes(const core::Part& p) {
  std::vector<uint8_t> out;
  for (size_t k = 0; k < p.verts.size(); ++k) {
    const core::Vert& v = p.verts[k];
    for (int c = 0; c < 3; ++c) Le(out, std::bit_cast<uint64_t>(v.pos[c]), 8);
    Le(out, v.w4.size(), 4);
    for (const auto& [b, w] : v.w4.items()) {
      Le(out, static_cast<uint32_t>(static_cast<int32_t>(b)), 4);
      Le(out, std::bit_cast<uint64_t>(w), 8);
    }
    Le(out, p.native ? 0 : p.raw_lens.size(), 4);
    if (!p.native) {
      size_t at = k * p.raw_stride;
      for (int len : p.raw_lens) {
        Le(out, static_cast<uint32_t>(len), 4);
        out.insert(out.end(), p.raws.begin() + at, p.raws.begin() + at + len);
        at += len;
      }
    }
  }
  return out;
}

void Tracer::Line(std::string_view s) {
  if (t_) t_->Line(s);
}

void Tracer::S(std::string_view stage) {
  if (!t_) return;
  if (have_stage_ && stage == stage_) return;
  stage_ = stage;
  have_stage_ = true;
  Line("S " + std::string(stage));
}

void Tracer::K(std::string_view name, std::string_view value) {
  if (!t_) return;
  std::string s = "K ";
  s += name;
  s += ' ';
  s += value;
  Line(s);
}

void Tracer::B(std::string_view name, std::span<const uint8_t> data) {
  if (!t_) return;
  Line("B " + std::string(name) + " " + std::to_string(data.size()) + " " + Sha1Hex(data));
}

void Tracer::P(const core::Part& p) {
  if (!t_) return;
  Line("P " + std::to_string(p.index) + " owner=" + std::to_string(p.owner) + " src=" + Src(p) +
       " n=" + std::to_string(p.verts.size()) + " writable=" + (p.writable ? "1" : "0"));
}

void Tracer::V(const core::Part& p, size_t j) {
  if (!t_) return;
  const core::Vert& v = p.verts[j];
  std::span<const uint8_t> raw;
  if (!p.native && p.raw_stride) raw = p.Raw(j);
  Line("V " + std::to_string(p.index) + " " + std::to_string(j) + " " + Hx(v.pos[0]) + " " + Hx(v.pos[1]) + " " +
       Hx(v.pos[2]) + " w=" + WeightsText(v.w4.items()) + " raw=" + Sha1Hex(raw).substr(0, 16));
}

void Tracer::PartFull(const core::Part& p) {
  if (!t_) return;
  P(p);
  for (size_t j = 0; j < p.verts.size(); ++j) V(p, j);
  PartFixed(p);
}

void Tracer::PartFixed(const core::Part& p) {
  if (!t_) return;
  const std::string i = "p" + std::to_string(p.index);
  K(i + ".icount", std::to_string(p.icount));
  std::string kept;
  for (size_t k = 0; k < p.kept.size(); ++k) {
    if (k) kept += ',';
    kept += std::to_string(p.kept[k].typ) + ":" + std::to_string(p.kept[k].usage) + ":" + std::to_string(p.kept[k].ui);
  }
  K(i + ".kept", kept.empty() ? "-" : kept);
  std::string passes;
  for (size_t k = 0; k < p.passes.size(); ++k) {
    if (k) passes += ',';
    passes += std::to_string(p.passes[k].src) + ":" + std::to_string(p.passes[k].pass) + ":" +
              std::to_string(p.passes[k].d10);
  }
  K(i + ".passes", passes.empty() ? "-" : passes);
  B(i + ".ib", p.ib);
  Ser m;
  m.L(p.meshes.size());
  for (const auto& mesh : p.meshes) m.Y(mesh);
  B(i + ".mesh", m.bytes());
  Ser s;
  s.L(p.srcv.size());  // getattr(p, 'srcv', []): a carried decl has none
  for (const core::SrcV& sv : p.srcv) {
    s.L(2);
    s.L(3);
    for (int c = 0; c < 3; ++c) s.D(sv.pos[c]);
    s.L(sv.blend.size());
    for (const auto& [b, w] : sv.blend) {
      s.L(2);
      s.I(b);
      s.D(w);
    }
  }
  B(i + ".srcv", s.bytes());
  if (p.attach) {
    const core::AttachInfo& a = *p.attach;
    const std::string blend = WeightsText(a.blend), w4 = WeightsText(a.w4.items());
    K(i + ".attach", "owner=" + std::to_string(a.owner) + " shift=" + Hx(a.shift[0]) + "," + Hx(a.shift[1]) + "," +
                         Hx(a.shift[2]) + " blend=" + (blend.empty() ? "-" : blend) + " w4=" + (w4.empty() ? "-" : w4));
  }
}

void Tracer::State(const std::vector<std::optional<core::Object>>& objects) {
  if (!t_) return;
  for (size_t oid = 0; oid < objects.size(); ++oid) {
    if (!objects[oid]) {
      Line("O " + std::to_string(oid) + " none");
      continue;
    }
    const core::Object& ob = *objects[oid];
    std::string pal, parts;
    for (size_t k = 0; k < ob.pal.size(); ++k) pal += (k ? "," : "") + std::to_string(ob.pal[k]);
    for (size_t k = 0; k < ob.ps.size(); ++k) parts += (k ? "," : "") + std::to_string(ob.ps[k]->index);
    Line("O " + std::to_string(oid) + " pal=" + (pal.empty() ? "-" : pal) + " parts=" + (parts.empty() ? "-" : parts));
    for (const core::Part* p : ob.ps) {
      P(*p);
      const std::string i = "p" + std::to_string(p->index);
      B(i + ".v", VertsBytes(*p));
      B(i + ".ib", p->ib);
      if (p->h4) {
        Ser h;
        h.L(p->h4->size());
        for (const auto& t : *p->h4) {
          h.L(4);
          for (double x : t) h.D(x);
        }
        B(i + ".h4", h.bytes());
      }
      // bust, border, band, welded: no part has them before the physics tasks (they are not Part fields yet)
    }
  }
}

void Tracer::Geos(const std::vector<tpr::ObjGeo>& geos, const std::vector<tpr::ObjInfo>* infos) {
  if (!t_) return;
  for (size_t i = 0; i < geos.size(); ++i) B("geo" + std::to_string(i), geos[i].Build());
  if (infos)
    for (size_t i = 0; i < infos->size(); ++i) B("info" + std::to_string(i), (*infos)[i].Build());
}

}  // namespace doa4::suitconv::trace

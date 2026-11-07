// Suit converter (Plan 2): the stage trace, version 1. The C++ side of the oracle harness's trace_m1.py: the same lines,
// in the same order, from the same values, so a C++ conversion can be compared with the Python's line by line. The
// specification is the harness README's "TRACE V1 CONTENTS" (doax2_work/plan2_oracle/README.txt); this file follows
// trace_m1.py's Tracer name for name (S, K, B, P, Vline -> V, part_full, part_fixed, state, geos, ser, verts_bytes).
//
// With a null Trace* nothing is formatted: every Tracer method returns at once, and callers guard any work done only
// for the trace with on(). Pure C++ plus gamefiles::Sha1Hex (Windows CNG).

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "doa4_suitconv_core.h"

namespace doa4::suitconv {

// Receives the trace lines, one call per line, without the line end.
class Trace {
 public:
  virtual ~Trace();
  virtual void Line(std::string_view line) = 0;
};

}  // namespace doa4::suitconv

namespace doa4::suitconv::trace {

// hx(x): the 16 lowercase hex digits of the double's bits, most significant first.
std::string Hx(double x);
// hashlib.sha1(b).hexdigest()
std::string Sha1Hex(std::span<const uint8_t> b);

// ser(value): the canonical serialization (README "Canonical value serialization"), written leaf by leaf.
class Ser {
 public:
  void N() { out_.push_back('N'); }
  void T(bool v) { out_.push_back(v ? 'T' : 'F'); }
  void I(int64_t v);
  void D(double v);
  void Y(std::span<const uint8_t> b);
  void L(uint64_t count);  // a list or tuple of `count` items follows
  void Z(uint64_t count);  // a set of `count` items follows (the caller writes them ascending)
  const std::vector<uint8_t>& bytes() const { return out_; }

 private:
  void U64(uint64_t v);
  std::vector<uint8_t> out_;
};

// verts_bytes(p.verts): per vertex 3 f64 LE, u32 n weights, (i32 id, f64 w) each, u32 n raws, (u32 len + bytes) each.
std::vector<uint8_t> VertsBytes(const core::Part& p);

class Tracer {
 public:
  explicit Tracer(Trace* t) : t_(t) {}
  bool on() const { return t_ != nullptr; }

  // S <stage>, written when the previous S line named another stage.
  void S(std::string_view stage);
  void K(std::string_view name, std::string_view value);
  void B(std::string_view name, std::span<const uint8_t> data);
  void P(const core::Part& p);
  void V(const core::Part& p, size_t j);
  void PartFull(const core::Part& p);
  void PartFixed(const core::Part& p);
  void State(const std::vector<std::optional<core::Object>>& objects);
  void Geos(const std::vector<tpr::ObjGeo>& geos, const std::vector<tpr::ObjInfo>* infos);
  void Line(std::string_view s);

 private:
  Trace* t_;
  std::string stage_;
  bool have_stage_ = false;
};

}  // namespace doa4::suitconv::trace

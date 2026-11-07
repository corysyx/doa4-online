// Suit converter (Plan 2): big-endian readers and writers, the C++ stand-ins for Python's struct.unpack_from('>...') and
// struct.pack('>...') as the recipe's tpr.py and convert.py use them. Header-only, pure C++.
//
// Reader is bounds-checked: any read that does not fit throws ConvertError("read past end"), where Python raises
// struct.error (unpack_from) or IndexError (bytes[i]). Offsets follow Python's rule for unpack_from / pack_into
// (CPython v3.12.1 Modules/_struct.c s_unpack_from and s_pack_into): a negative offset counts from the end of the
// buffer, provided the whole field then lies inside it; every other out-of-range offset is an error. Reader::Slice is
// Python's bytes[a:b] for non-negative a and b: clamped, never an error.
// Writer and the Put helpers range-check every integer as struct.pack does (a value that does not fit is a
// ConvertError, the struct.error); a float goes through num::PackF32 (overflow is a ConvertError).

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "doa4_suitconv_num.h"

namespace doa4::suitconv::be {

// The place a field of `need` bytes at (possibly negative) offset `o` occupies in a buffer of `len` bytes, by Python's
// unpack_from / pack_into rule. Throws ConvertError("read past end") if it does not fit.
inline size_t ResolveOffset(int64_t o, size_t len, size_t need) {
  const int64_t slen = static_cast<int64_t>(len);
  if (o < 0) {
    if (o + static_cast<int64_t>(need) > 0) throw ConvertError("read past end");
    if (o + slen < 0) throw ConvertError("read past end");
    o += slen;
  }
  if (static_cast<uint64_t>(o) > len || len - static_cast<size_t>(o) < need) throw ConvertError("read past end");
  return static_cast<size_t>(o);
}

class Reader {
 public:
  Reader() = default;
  explicit Reader(std::span<const uint8_t> d) : d_(d) {}

  size_t size() const { return d_.size(); }
  std::span<const uint8_t> data() const { return d_; }

  // bytes[o] (Python IndexError when o is outside; negative indexes are not used by the callers and are refused).
  uint8_t U8(int64_t o) const {
    if (o < 0 || static_cast<uint64_t>(o) >= d_.size()) throw ConvertError("read past end");
    return d_[static_cast<size_t>(o)];
  }
  uint16_t U16(int64_t o) const {
    const size_t p = ResolveOffset(o, d_.size(), 2);
    return static_cast<uint16_t>((uint32_t(d_[p]) << 8) | d_[p + 1]);
  }
  int16_t I16(int64_t o) const { return static_cast<int16_t>(U16(o)); }
  uint32_t U32(int64_t o) const {
    const size_t p = ResolveOffset(o, d_.size(), 4);
    return (uint32_t(d_[p]) << 24) | (uint32_t(d_[p + 1]) << 16) | (uint32_t(d_[p + 2]) << 8) | uint32_t(d_[p + 3]);
  }
  // struct.unpack_from('>f'): the float32 widened to double (exact).
  double F32(int64_t o) const {
    return static_cast<double>(std::bit_cast<float>(U32(o)));
  }
  // struct.unpack_from('>nI') / ('>nH') / ('>nh'): n fields, all or none (the size is checked before anything is
  // allocated, so a hostile count is an error, not an allocation).
  std::vector<uint32_t> U32s(int64_t o, uint64_t n) const {
    CheckRun(o, n, 4);
    std::vector<uint32_t> v;
    v.reserve(static_cast<size_t>(n));
    for (uint64_t i = 0; i < n; ++i) v.push_back(U32(o + static_cast<int64_t>(4 * i)));
    return v;
  }
  std::vector<uint16_t> U16s(int64_t o, uint64_t n) const {
    CheckRun(o, n, 2);
    std::vector<uint16_t> v;
    v.reserve(static_cast<size_t>(n));
    for (uint64_t i = 0; i < n; ++i) v.push_back(U16(o + static_cast<int64_t>(2 * i)));
    return v;
  }
  std::vector<int16_t> I16s(int64_t o, uint64_t n) const {
    CheckRun(o, n, 2);
    std::vector<int16_t> v;
    v.reserve(static_cast<size_t>(n));
    for (uint64_t i = 0; i < n; ++i) v.push_back(I16(o + static_cast<int64_t>(2 * i)));
    return v;
  }
  // bytes[a:b] for non-negative a and b: clamped to the buffer, empty when b <= a.
  std::span<const uint8_t> Slice(uint64_t a, uint64_t b) const {
    const uint64_t len = d_.size();
    if (a > len) a = len;
    if (b > len) b = len;
    if (b < a) b = a;
    return d_.subspan(static_cast<size_t>(a), static_cast<size_t>(b - a));
  }
  // bytes[a:]
  std::span<const uint8_t> From(uint64_t a) const { return Slice(a, d_.size()); }

 private:
  void CheckRun(int64_t o, uint64_t n, size_t each) const {
    if (n > d_.size() / each) throw ConvertError("read past end");  // n fields of `each` bytes cannot fit
    if (n) ResolveOffset(o, d_.size(), static_cast<size_t>(n) * each);
  }
  std::span<const uint8_t> d_;
};

// struct.pack range checks.
inline uint32_t CheckU32(uint64_t v) {
  if (v > 0xFFFFFFFFull) throw ConvertError("argument out of range for 'I' format (requires 0 <= number <= 4294967295)");
  return static_cast<uint32_t>(v);
}
inline uint32_t CheckU32S(int64_t v) {
  if (v < 0 || v > 0xFFFFFFFFll) throw ConvertError("argument out of range for 'I' format (requires 0 <= number <= 4294967295)");
  return static_cast<uint32_t>(v);
}
inline uint16_t CheckU16(uint64_t v) {
  if (v > 0xFFFFull) throw ConvertError("ushort format requires 0 <= number <= 65535");
  return static_cast<uint16_t>(v);
}
inline int16_t CheckI16(int64_t v) {
  if (v < -32768 || v > 32767) throw ConvertError("short format requires -32768 <= number <= 32767");
  return static_cast<int16_t>(v);
}
inline uint8_t CheckU8(uint64_t v) {
  if (v > 0xFFull) throw ConvertError("ubyte format requires 0 <= number <= 255");
  return static_cast<uint8_t>(v);
}

class Writer {
 public:
  void U8(uint64_t v) { buf_.push_back(CheckU8(v)); }
  void U16(uint64_t v) { Put(CheckU16(v), 2); }
  void I16(int64_t v) { Put(static_cast<uint16_t>(CheckI16(v)), 2); }
  void U32(uint64_t v) { Put(CheckU32(v), 4); }
  // struct.pack('>f', x)
  void F32(double x) {
    Put(std::bit_cast<uint32_t>(num::PackF32(x)), 4);
  }
  void Bytes(std::span<const uint8_t> b) { buf_.insert(buf_.end(), b.begin(), b.end()); }
  void Zeros(size_t n) { buf_.insert(buf_.end(), n, uint8_t(0)); }
  size_t size() const { return buf_.size(); }
  const std::vector<uint8_t>& bytes() const { return buf_; }
  std::vector<uint8_t> Take() { return std::move(buf_); }

 private:
  void Put(uint32_t v, int n) {
    for (int i = n - 1; i >= 0; --i) buf_.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  std::vector<uint8_t> buf_;
};

// struct.pack_into('>I', buf, o, v)
inline void PackIntoU32(std::vector<uint8_t>& buf, int64_t o, uint64_t v) {
  const uint32_t x = CheckU32(v);
  const size_t p = ResolveOffset(o, buf.size(), 4);
  buf[p] = uint8_t(x >> 24);
  buf[p + 1] = uint8_t(x >> 16);
  buf[p + 2] = uint8_t(x >> 8);
  buf[p + 3] = uint8_t(x);
}

}  // namespace doa4::suitconv::be

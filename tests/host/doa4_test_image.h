// Reads byte ranges of a decrypted image of your own default.xex, for the host tests that check the code against the
// game's real tables. No game byte is in this repository: each range below is named by its guest address, its length and
// the SHA-1 of the bytes expected there, and the bytes come from the file named by the environment variable
// DOA4_TEST_IMAGE (the whole decrypted image, loaded at 0x82000000, so a guest address maps to file offset
// address - 0x82000000). Without the variable a test section prints "SKIPPED (DOA4_TEST_IMAGE unset)" and the synthetic
// checks still run. With the variable set and a file that cannot be read or whose bytes differ from the pins, the section
// FAILS (ReportNoImage).
//
// Header-only, namespace doa4_test. Windows: the variable is read as UTF-16 (_wgetenv), so the path may hold any character.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <cwchar>
#endif

namespace doa4_test {

// `length` bytes taken at address, address + stride, address + 2 * stride ...; `sha1` is the lowercase hex SHA-1 of the
// taken bytes in order.
struct ImageRange {
  uint32_t address, length;
  const char* sha1;
  uint32_t stride = 1;
};

inline constexpr uint32_t kImageBase = 0x82000000;

// doa4_costumes: the tables, the model records and the file table.
inline constexpr ImageRange kCostumeTablesRange{0x82C34700, 672, "a1f910fd7666a53aa8e9a391ff4a53fb038a8034"};
inline constexpr ImageRange kCostumeRecordsRange{0x82C5E000, 2304, "442fcc08e57ca489292558b01f64d8a0c29b4a46"};
inline constexpr ImageRange kCostumeFilesRange{0x82C3B2E8, 6560, "72057f66142d78c4f804acd249f905fd33547cfa"};
// doa4_unlocks: the profile-index-to-charId table, the maximum costume count per charId, and the Story roster (byte 0 of
// each of 18 entries 40 bytes apart).
inline constexpr ImageRange kProfileIndexToCharRange{0x82C5A014, 23, "b7d23708e02371518d802c45ebc8b9dcc52ae997"};
inline constexpr ImageRange kMaxCostumesRange{0x82C34888, 25, "7b4f632306fea3cd425b62e21e4bc87a7108bf31"};
inline constexpr ImageRange kStoryRosterRange{0x82C34400, 18, "8a4a0d6742dda7234d2b3e47758a8e5ab00b8873", 40};

// SHA-1 (FIPS 180-1) as lowercase hex.
inline std::string Sha1Hex(const uint8_t* data, size_t size) {
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  const auto rol = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };
  std::vector<uint8_t> m(data, data + size);
  m.push_back(0x80);
  while (m.size() % 64 != 56) m.push_back(0);
  const uint64_t bits = uint64_t(size) * 8;
  for (int i = 7; i >= 0; --i) m.push_back(uint8_t(bits >> (8 * i)));
  for (size_t block = 0; block < m.size(); block += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      const uint8_t* p = &m[block + 4 * size_t(i)];
      w[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) {
        f = (b & c) | (~b & d);
        k = 0x5A827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      const uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }
  char out[41];
  for (int i = 0; i < 5; ++i) std::snprintf(out + 8 * i, 9, "%08x", h[i]);
  return std::string(out, 40);
}

inline std::string PathText(const std::filesystem::path& p) {
  const auto u8 = p.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

inline std::string AddressText(uint32_t address) {
  char text[16];
  std::snprintf(text, sizeof(text), "0x%08X", address);
  return text;
}

// The range from the file at `path`; nullopt with *why (when given) = "<path>: cannot read" or
// "<path>: 0x<address> has SHA-1 <x>, expected <pin>".
inline std::optional<std::vector<uint8_t>> ReadRangeFromFile(const std::filesystem::path& path, const ImageRange& range,
                                                             std::string* why) {
  const auto fail = [&](const std::string& text) -> std::optional<std::vector<uint8_t>> {
    if (why) *why = text;
    return std::nullopt;
  };
  const std::string name = PathText(path);
  if (range.address < kImageBase || range.length == 0 || range.stride == 0) return fail(name + ": cannot read");
  std::ifstream in(path, std::ios::binary);
  if (!in) return fail(name + ": cannot read");
  const uint64_t span = uint64_t(range.length - 1) * range.stride + 1;
  std::vector<uint8_t> raw(static_cast<size_t>(span), uint8_t(0));
  in.seekg(std::streamoff(range.address - kImageBase));
  in.read(reinterpret_cast<char*>(raw.data()), std::streamsize(raw.size()));
  if (!in || size_t(in.gcount()) != raw.size()) return fail(name + ": cannot read");
  std::vector<uint8_t> out;
  out.reserve(range.length);
  for (uint32_t i = 0; i < range.length; ++i) out.push_back(raw[size_t(i) * range.stride]);
  const std::string got = Sha1Hex(out.data(), out.size());
  if (got != range.sha1) {
    return fail(name + ": " + AddressText(range.address) + " has SHA-1 " + got + ", expected " + range.sha1);
  }
  return out;
}

// The file named by DOA4_TEST_IMAGE, if set and non-empty.
inline std::optional<std::filesystem::path> ImagePath() {
#ifdef _WIN32
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
  const wchar_t* v = _wgetenv(L"DOA4_TEST_IMAGE");
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#else
  const char* v = std::getenv("DOA4_TEST_IMAGE");
#endif
  if (!v || !*v) return std::nullopt;
  return std::filesystem::path(v);
}

// The range from your own image (DOA4_TEST_IMAGE) at offset address - 0x82000000, checked against its SHA-1. nullopt with
// *why = "DOA4_TEST_IMAGE unset", "<path>: cannot read" or "<path>: 0x<address> has SHA-1 <x>, expected <pin>".
inline std::optional<std::vector<uint8_t>> ReadImageRange(const ImageRange& range, std::string* why) {
  const std::optional<std::filesystem::path> path = ImagePath();
  if (!path) {
    if (why) *why = "DOA4_TEST_IMAGE unset";
    return std::nullopt;
  }
  return ReadRangeFromFile(*path, range, why);
}

// Several short pieces of the image (the sites a patch file names), checked by ONE SHA-1 over their bytes in the order
// given. The pieces are `length` bytes at `address`; the result is their bytes concatenated.
struct ImageSite {
  uint32_t address, length;
};

inline std::optional<std::vector<uint8_t>> ReadSitesFromFile(const std::filesystem::path& path,
                                                             const std::vector<ImageSite>& sites, const char* sha1,
                                                             std::string* why) {
  const auto fail = [&](const std::string& text) -> std::optional<std::vector<uint8_t>> {
    if (why) *why = text;
    return std::nullopt;
  };
  const std::string name = PathText(path);
  std::ifstream in(path, std::ios::binary);
  if (!in || sites.empty()) return fail(name + ": cannot read");
  std::vector<uint8_t> out;
  for (const ImageSite& site : sites) {
    if (site.address < kImageBase || site.length == 0) return fail(name + ": cannot read");
    std::vector<uint8_t> piece(site.length, uint8_t(0));
    in.clear();
    in.seekg(std::streamoff(site.address - kImageBase));
    in.read(reinterpret_cast<char*>(piece.data()), std::streamsize(piece.size()));
    if (!in || size_t(in.gcount()) != piece.size()) return fail(name + ": cannot read");
    out.insert(out.end(), piece.begin(), piece.end());
  }
  const std::string got = Sha1Hex(out.data(), out.size());
  if (got != sha1) {
    return fail(name + ": the sites from " + AddressText(sites.front().address) + " have SHA-1 " + got + ", expected " + sha1);
  }
  return out;
}

inline std::optional<std::vector<uint8_t>> ReadImageSites(const std::vector<ImageSite>& sites, const char* sha1,
                                                          std::string* why) {
  const std::optional<std::filesystem::path> path = ImagePath();
  if (!path) {
    if (why) *why = "DOA4_TEST_IMAGE unset";
    return std::nullopt;
  }
  return ReadSitesFromFile(*path, sites, sha1, why);
}

// What a test does when its image tier cannot read the image. Only a variable that is not set at all is a skip; a variable
// that is set and names a missing or short file, or a file whose bytes differ from the pins, is a failure the caller
// counts (the tier cannot be quietly skipped by a wrong path).
inline bool ImageIsUnset(const std::string& why) { return why == "DOA4_TEST_IMAGE unset"; }

// The three ranges of the costume tests, together.
struct CostumeImage {
  std::vector<uint8_t> tables, records, files;
};

inline std::optional<CostumeImage> LoadCostumeImage(std::string* why) {
  CostumeImage image;
  auto tables = ReadImageRange(kCostumeTablesRange, why);
  if (!tables) return std::nullopt;
  auto records = ReadImageRange(kCostumeRecordsRange, why);
  if (!records) return std::nullopt;
  auto files = ReadImageRange(kCostumeFilesRange, why);
  if (!files) return std::nullopt;
  image.tables = std::move(*tables);
  image.records = std::move(*records);
  image.files = std::move(*files);
  return image;
}

// One line for a section that did not run for want of the image.
inline void PrintSkipped(const char* section, const std::string& why) {
  std::printf("%s: SKIPPED (%s)\n", section, why.c_str());
}

// For a section that needs the image and could not read it: an unset variable prints "SKIPPED (DOA4_TEST_IMAGE unset)" and
// returns false; anything else (a missing or short file, a pin that fails) prints a FAIL line with the reason and returns
// true, and the caller counts one failed check.
inline bool ReportNoImage(const char* section, const std::string& why) {
  if (ImageIsUnset(why)) {
    PrintSkipped(section, why);
    return false;
  }
  std::printf("FAIL %s: DOA4_TEST_IMAGE is set but the image tier cannot run: %s\n", section, why.c_str());
  return true;
}

}  // namespace doa4_test

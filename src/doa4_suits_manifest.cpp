// The import manifest. See doa4_suits_manifest.h.

#include "doa4_suits_manifest.h"

#include <charconv>
#include <set>

namespace doa4::suits {

namespace {

std::vector<std::string_view> Words(std::string_view line) {
  std::vector<std::string_view> out;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    const size_t start = i;
    while (i < line.size() && line[i] != ' ' && line[i] != '\t') ++i;
    if (i > start) out.push_back(line.substr(start, i - start));
  }
  return out;
}

// Plain decimal digits with a value in [0, max].
bool Number(std::string_view text, uint64_t max, uint64_t* out) {
  if (text.empty() || text.size() > 20) return false;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
  }
  uint64_t value = 0;
  const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || end != text.data() + text.size() || value > max) return false;
  *out = value;
  return true;
}

bool Sha1Text(std::string_view text) {
  if (text.size() != 40) return false;
  for (char c : text) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

// The converter word: 1-32 of [A-Za-z0-9._-].
bool ConverterName(std::string_view text) {
  if (text.empty() || text.size() > 32) return false;
  for (char c : text) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '.' || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

bool DesignName(std::string_view text) {
  if (text.empty() || text.size() > 32) return false;
  for (char c : text) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

std::string At(size_t line, const std::string& what) {
  return "line " + std::to_string(line) + ": " + what;
}

std::string Quoted(std::string_view word) { return "'" + std::string(word) + "'"; }

}  // namespace

bool SafeFileName(std::string_view name) {
  if (name.empty() || name.size() > 64 || name[0] == '.') return false;
  for (char c : name) {
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '.' || c == '-';
    if (!ok) return false;
  }
  return true;
}

std::string Suit::Label() const { return design + " c" + std::to_string(colour); }

ParseResult ParseManifest(std::string_view text) {
  ParseResult result;
  if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
  Manifest manifest;
  bool header = false;
  bool converter = false;
  std::set<uint32_t> files;
  std::set<std::string> names;
  std::set<std::string> labels;
  size_t line_no = 0;
  auto fail = [&](const std::string& why) {
    result.error = why;
    return result;
  };
  while (!text.empty()) {
    const size_t nl = text.find('\n');
    std::string_view line = text.substr(0, nl);
    text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
    ++line_no;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const std::vector<std::string_view> w = Words(line);
    if (w.empty() || w[0][0] == '#') continue;
    if (!header) {
      if (w.size() != 2 || w[0] != "doax2-suits" || w[1] != "1") {
        return fail(At(line_no, "not a doax2-suits 1 manifest"));
      }
      header = true;
      continue;
    }
    if (w[0] == "converter") {
      if (converter || w.size() != 2) {
        return fail(At(line_no, "converter must appear once, as one word"));
      }
      if (!ConverterName(w[1])) return fail(At(line_no, "bad converter name " + Quoted(w[1])));
      converter = true;
      manifest.converter = std::string(w[1]);
      continue;
    }
    if (w[0] != "suit") return fail(At(line_no, "unknown line " + Quoted(w[0])));
    if (!converter) return fail(At(line_no, "suit before the converter line"));
    if (w.size() != 15) {
      return fail(At(line_no, "a suit line has 15 words, this one has " + std::to_string(w.size())));
    }
    Suit s;
    uint64_t v = 0;
    if (!Number(w[1], kCharCount - 1, &v)) return fail(At(line_no, "bad character " + Quoted(w[1])));
    s.character = uint8_t(v);
    if (w[2] == "append") {
      s.own_model = -1;
    } else if (w[2].substr(0, 4) == "own:" && Number(w[2].substr(4), 255, &v)) {
      s.own_model = int(v);
    } else {
      return fail(At(line_no, "model must be own:N or append, not " + Quoted(w[2])));
    }
    uint64_t body[2] = {};
    for (int k = 0; k < 2; ++k) {
      if (!Number(w[3 + k], kFileCount - 1, &body[k])) {
        return fail(At(line_no, "bad body file number " + Quoted(w[3 + k])));
      }
    }
    s.body_tpr = uint16_t(body[0]);
    s.body_cat = uint16_t(body[1]);
    if (!(w[5] == "-" && w[6] == "-")) {
      uint64_t part[2] = {};
      if (!Number(w[5], kFileCount - 1, &part[0]) || !Number(w[6], kFileCount - 1, &part[1])) {
        return fail(At(line_no, "head part must be two file numbers or - -"));
      }
      s.part_tpr = uint16_t(part[0]);
      s.part_cat = uint16_t(part[1]);
    }
    if (!DesignName(w[7])) return fail(At(line_no, "bad design name " + Quoted(w[7])));
    s.design = std::string(w[7]);
    if (!Number(w[8], 9, &v)) return fail(At(line_no, "bad colour " + Quoted(w[8])));
    s.colour = int(v);
    for (int k = 0; k < 2; ++k) {
      const size_t at = 9 + 3 * size_t(k);
      SuitFile& file = k == 0 ? s.tpr : s.cat;
      if (!SafeFileName(w[at])) return fail(At(line_no, "bad file name " + Quoted(w[at])));
      if (!Number(w[at + 1], 0xFFFFFFFFull, &v) || v == 0) {
        return fail(At(line_no, "bad size " + Quoted(w[at + 1])));
      }
      if (!Sha1Text(w[at + 2])) return fail(At(line_no, "bad sha1 " + Quoted(w[at + 2])));
      file = SuitFile{std::string(w[at]), v, std::string(w[at + 2])};
      if (!names.insert(file.name).second) {
        return fail(At(line_no, "file name " + file.name + " used twice"));
      }
    }
    for (uint32_t f : {uint32_t(s.body_tpr), uint32_t(s.body_cat)}) {
      if (!files.insert(f).second) {
        return fail(At(line_no, "placeholder file " + std::to_string(f) + " used twice"));
      }
    }
    if (!labels.insert(s.Label()).second) {
      return fail(At(line_no, "suit " + s.Label() + " listed twice"));
    }
    manifest.suits.push_back(std::move(s));
  }
  if (!header) return fail("empty manifest");
  if (!converter) return fail("no converter line");
  result.manifest = std::move(manifest);
  return result;
}

std::string FormatManifest(const Manifest& manifest) {
  std::string out = "doax2-suits 1\nconverter " + manifest.converter + "\n";
  for (const Suit& s : manifest.suits) {
    out += "suit " + std::to_string(s.character) + " ";
    out += s.own_model < 0 ? std::string("append") : "own:" + std::to_string(s.own_model);
    out += " " + std::to_string(s.body_tpr) + " " + std::to_string(s.body_cat) + " ";
    out += s.part_tpr == kNoPart ? std::string("- -")
                                 : std::to_string(s.part_tpr) + " " + std::to_string(s.part_cat);
    out += " " + s.design + " " + std::to_string(s.colour);
    for (const SuitFile* f : {&s.tpr, &s.cat}) {
      out += " " + f->name + " " + std::to_string(f->size) + " " + f->sha1;
    }
    out += "\n";
  }
  return out;
}

}  // namespace doa4::suits

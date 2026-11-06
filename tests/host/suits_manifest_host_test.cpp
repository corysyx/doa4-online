// Host test for src/doa4_suits_manifest.cpp. Built and run by run_suits_manifest_test.bat.

#include "../../src/doa4_suits_manifest.h"

#include <cstdio>
#include <string>

static int g_failures = 0;
#define CHECK(cond)                                               \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                               \
    }                                                             \
  } while (0)

using namespace doa4::suits;

static const char kTwoSuits[] =
    "doax2-suits 1\n"
    "converter proto-1d\n"
    "suit 5 own:7 269 270 509 510 vks_ch_d 0 vks_ch_d_0.tpr 5505024 "
    "0123456789abcdef0123456789abcdef01234567 vks_ch_d_0.cat 1264 "
    "89abcdef0123456789abcdef0123456789abcdef\n"
    "suit 0 append 211 212 - - vls_on_b 1 vls_on_b_1.tpr 8000000 "
    "fedcba9876543210fedcba9876543210fedcba98 vls_on_b_1.cat 4096 "
    "00112233445566778899aabbccddeeff00112233\n";

static void TestParsesFields() {
  const ParseResult r = ParseManifest(kTwoSuits);
  CHECK(r.manifest.has_value());
  if (!r.manifest) {
    std::printf("  error: %s\n", r.error.c_str());
    return;
  }
  const Manifest& m = *r.manifest;
  CHECK(m.converter == "proto-1d");
  CHECK(m.suits.size() == 2);
  if (m.suits.size() != 2) return;
  const Suit& a = m.suits[0];
  CHECK(a.character == 5);
  CHECK(a.own_model == 7);
  CHECK(a.body_tpr == 269);
  CHECK(a.body_cat == 270);
  CHECK(a.part_tpr == 509);
  CHECK(a.part_cat == 510);
  CHECK(a.design == "vks_ch_d");
  CHECK(a.colour == 0);
  CHECK(a.tpr.name == "vks_ch_d_0.tpr");
  CHECK(a.tpr.size == 5505024);
  CHECK(a.tpr.sha1 == "0123456789abcdef0123456789abcdef01234567");
  CHECK(a.cat.name == "vks_ch_d_0.cat");
  CHECK(a.cat.size == 1264);
  CHECK(a.cat.sha1 == "89abcdef0123456789abcdef0123456789abcdef");
  CHECK(a.Label() == "vks_ch_d c0");
  const Suit& b = m.suits[1];
  CHECK(b.character == 0);
  CHECK(b.own_model == -1);
  CHECK(b.body_tpr == 211);
  CHECK(b.part_tpr == kNoPart);
  CHECK(b.part_cat == kNoPart);
  CHECK(b.colour == 1);
  CHECK(b.Label() == "vls_on_b c1");
}

static void TestFormatIsCanonical() {
  const ParseResult r = ParseManifest(kTwoSuits);
  CHECK(r.manifest && FormatManifest(*r.manifest) == std::string(kTwoSuits));
}

static void TestCrlfBom() {
  std::string text = "\xEF\xBB\xBF# written by hand\r\n\r\n";
  for (const char* p = kTwoSuits; *p; ++p) {
    if (*p == '\n') {
      text += "\r\n";
    } else {
      text += *p;
    }
  }
  const ParseResult crlf = ParseManifest(text);
  const ParseResult lf = ParseManifest(kTwoSuits);
  CHECK(crlf.manifest && lf.manifest && *crlf.manifest == *lf.manifest);
}

static void TestEmptySuitList() {
  const ParseResult r = ParseManifest("doax2-suits 1\nconverter c\n");
  CHECK(r.manifest && r.manifest->suits.empty());
}

static void ExpectError(const std::string& text, const std::string& want) {
  const ParseResult r = ParseManifest(text);
  const bool ok = !r.manifest && r.error.find(want) != std::string::npos;
  if (!ok) std::printf("  want an error containing '%s', got '%s'\n", want.c_str(), r.error.c_str());
  CHECK(ok);
}

static const std::string kHead = "doax2-suits 1\nconverter c\n";
static const std::string kGood =
    "suit 5 own:7 269 270 509 510 vks_ch_d 0 a.tpr 10 0123456789abcdef0123456789abcdef01234567 "
    "a.cat 10 0123456789abcdef0123456789abcdef01234567";

// kGood with the first occurrence of `from` replaced by `to`.
static std::string Swap(const std::string& from, const std::string& to) {
  std::string line = kGood;
  const size_t at = line.find(from);
  if (at == std::string::npos) {
    std::printf("  test bug: '%s' is not in the line\n", from.c_str());
    ++g_failures;
    return line;
  }
  return line.replace(at, from.size(), to);
}

static void TestRejects() {
  CHECK(ParseManifest(kHead + kGood + "\n").manifest.has_value());  // the base line is valid
  ExpectError("", "empty manifest");
  ExpectError("doax2-suits 2\n", "line 1: not a doax2-suits 1 manifest");
  ExpectError("doax2-suits 1\nconverter a\nconverter b\n", "line 3: converter must appear once");
  ExpectError("doax2-suits 1\nconverter a b\n", "line 2: converter must appear once");
  ExpectError("doax2-suits 1\n" + kGood + "\n", "line 2: suit before the converter line");
  ExpectError("doax2-suits 1\nsuits 1\n", "line 2: unknown line 'suits'");
  ExpectError("doax2-suits 1\n", "no converter line");
  ExpectError(kHead + Swap(" a.cat 10", "") + "\n", "line 3: a suit line has 15 words, this one has 13");
  ExpectError(kHead + Swap("suit 5 ", "suit 25 ") + "\n", "line 3: bad character '25'");
  ExpectError(kHead + Swap("suit 5 ", "suit +5 ") + "\n", "line 3: bad character '+5'");
  ExpectError(kHead + Swap("own:7", "own:x") + "\n", "line 3: model must be own:N or append, not 'own:x'");
  ExpectError(kHead + Swap("own:7", "own:256") + "\n", "not 'own:256'");
  ExpectError(kHead + Swap(" 269 270 ", " 1640 270 ") + "\n", "line 3: bad body file number '1640'");
  ExpectError(kHead + Swap(" 509 510 ", " - 510 ") + "\n", "line 3: head part must be two file numbers or - -");
  ExpectError(kHead + Swap("vks_ch_d", "VKS_CH_D") + "\n", "line 3: bad design name 'VKS_CH_D'");
  ExpectError(kHead + Swap(" vks_ch_d 0 ", " vks_ch_d 10 ") + "\n", "line 3: bad colour '10'");
  ExpectError(kHead + Swap("a.tpr", "../a.tpr") + "\n", "line 3: bad file name '../a.tpr'");
  ExpectError(kHead + Swap("a.tpr 10 ", "a.tpr 0 ") + "\n", "line 3: bad size '0'");
  ExpectError(kHead + Swap("0123456789abcdef0123456789abcdef01234567",
                           "0123456789ABCDEF0123456789abcdef01234567") + "\n",
              "line 3: bad sha1");
  ExpectError(kHead + Swap(" 269 270 ", " 269 269 ") + "\n", "line 3: placeholder file 269 used twice");
  // A second suit reusing a placeholder file, and one reusing a file name.
  const std::string second_files =
      "suit 5 append 271 270 509 510 vks_on_o 0 b.tpr 10 0123456789abcdef0123456789abcdef01234567 "
      "b.cat 10 0123456789abcdef0123456789abcdef01234567";
  ExpectError(kHead + kGood + "\n" + second_files + "\n", "line 4: placeholder file 270 used twice");
  const std::string second_names =
      "suit 5 append 271 272 509 510 vks_on_o 0 a.tpr 10 0123456789abcdef0123456789abcdef01234567 "
      "b.cat 10 0123456789abcdef0123456789abcdef01234567";
  ExpectError(kHead + kGood + "\n" + second_names + "\n", "line 4: file name a.tpr used twice");
}

static void TestSafeFileName() {
  CHECK(SafeFileName("vks_ch_d_0.tpr"));
  CHECK(SafeFileName("A-b.c"));
  CHECK(!SafeFileName(""));
  CHECK(!SafeFileName(".hidden"));
  CHECK(!SafeFileName(".."));
  CHECK(!SafeFileName("a/b"));
  CHECK(!SafeFileName("a\\b"));
  CHECK(!SafeFileName("a:b"));
  CHECK(!SafeFileName("caf\xC3\xA9"));
  CHECK(!SafeFileName(std::string(65, 'a')));
  CHECK(SafeFileName(std::string(64, 'a')));
}

// ---- Fix round 1 ----

// The suit of a result that holds exactly one, else nullptr (so a check cannot index a missing one).
static const Suit* OnlySuit(const ParseResult& r) {
  return r.manifest && r.manifest->suits.size() == 1 ? &r.manifest->suits[0] : nullptr;
}

// kTwoSuits with every space replaced by `sep`.
static std::string WithSeparator(const std::string& sep) {
  std::string out;
  for (const char* p = kTwoSuits; *p; ++p) {
    if (*p == ' ') {
      out += sep;
    } else {
      out += *p;
    }
  }
  return out;
}

// The converter is a word of 1-32 of [A-Za-z0-9._-]; a stray CR must not survive into it.
static void TestConverterName() {
  ExpectError("doax2-suits 1\nconverter foo\r\r\n", "line 2: bad converter name");
  ExpectError("doax2-suits 1\nconverter caf\xC3\xA9\n", "line 2: bad converter name");
  ExpectError("doax2-suits 1\nconverter " + std::string(33, 'a') + "\n", "line 2: bad converter name");
  std::string with_nul = "doax2-suits 1\nconverter a";
  with_nul += '\0';
  with_nul += "b\n";
  ExpectError(with_nul, "line 2: bad converter name");
  const ParseResult longest = ParseManifest("doax2-suits 1\nconverter " + std::string(32, 'a') + "\n");
  CHECK(longest.manifest && longest.manifest->converter == std::string(32, 'a'));
  const ParseResult charset = ParseManifest("doax2-suits 1\nconverter Aa0._-\n");
  CHECK(charset.manifest && charset.manifest->converter == "Aa0._-");
}

// The text format's edges: a missing final newline, comments and blanks between items, line numbers.
static void TestFormatEdges() {
  const std::string lf(kTwoSuits);
  CHECK(lf.back() == '\n');  // fixture precondition: there is a final newline to take away
  const ParseResult base = ParseManifest(lf);
  CHECK(base.manifest && base.manifest->suits.size() == 2);
  const ParseResult no_final_lf = ParseManifest(lf.substr(0, lf.size() - 1));
  CHECK(base.manifest && no_final_lf.manifest && *no_final_lf.manifest == *base.manifest);
  std::string crlf;
  for (char c : lf) {
    if (c == '\n') {
      crlf += "\r\n";
    } else {
      crlf += c;
    }
  }
  const ParseResult no_final_crlf = ParseManifest(crlf.substr(0, crlf.size() - 2));
  CHECK(base.manifest && no_final_crlf.manifest && *no_final_crlf.manifest == *base.manifest);

  // A comment line and a blank line between the two suit lines are skipped.
  const size_t second = lf.find("suit 0 append");
  CHECK(second != std::string::npos);  // fixture precondition
  const ParseResult between =
      ParseManifest(lf.substr(0, second) + "# the second suit\n\n" + lf.substr(second));
  CHECK(base.manifest && between.manifest && *between.manifest == *base.manifest);

  // Line numbers count blank and comment lines: 1 header, 2 converter, 3 comment, 4 blank, 5 the bad suit.
  ExpectError(kHead + "# a comment\n\n" + Swap("suit 5 ", "suit 25 ") + "\n", "line 5: bad character '25'");
}

// own:0 is a model record, not "append" (-1 is the only append value).
static void TestOwnModelZero() {
  const std::string text = kHead + Swap("own:7", "own:0") + "\n";
  const ParseResult r = ParseManifest(text);
  CHECK(OnlySuit(r) && OnlySuit(r)->own_model == 0);
  const std::string again = r.manifest ? FormatManifest(*r.manifest) : std::string();
  CHECK(again == text);  // formatted as own:0, not as append
  const ParseResult back = ParseManifest(again);
  CHECK(r.manifest && back.manifest && *back.manifest == *r.manifest && OnlySuit(back) &&
        OnlySuit(back)->own_model == 0);
}

// The reject side of each bound is in TestRejects; these pin the last accepted value.
static void TestAcceptsLastValueOfBounds() {
  const ParseResult tpr = ParseManifest(kHead + Swap(" 269 270 ", " 1639 270 ") + "\n");
  CHECK(OnlySuit(tpr) && OnlySuit(tpr)->body_tpr == 1639);
  const ParseResult cat = ParseManifest(kHead + Swap(" 269 270 ", " 269 1639 ") + "\n");
  CHECK(OnlySuit(cat) && OnlySuit(cat)->body_cat == 1639);
  const ParseResult character = ParseManifest(kHead + Swap("suit 5 ", "suit 24 ") + "\n");
  CHECK(OnlySuit(character) && OnlySuit(character)->character == 24);
  const ParseResult colour = ParseManifest(kHead + Swap(" vks_ch_d 0 ", " vks_ch_d 9 ") + "\n");
  CHECK(OnlySuit(colour) && OnlySuit(colour)->colour == 9 && OnlySuit(colour)->Label() == "vks_ch_d c9");
}

// A hand-edited manifest may use tabs, alone or mixed with spaces, between words.
static void TestTabsSeparateWords() {
  const ParseResult base = ParseManifest(kTwoSuits);
  const ParseResult tabs = ParseManifest(WithSeparator("\t"));
  const ParseResult mixed = ParseManifest(WithSeparator(" \t  "));
  CHECK(base.manifest && base.manifest->suits.size() == 2);
  CHECK(base.manifest && tabs.manifest && *tabs.manifest == *base.manifest);
  CHECK(base.manifest && mixed.manifest && *mixed.manifest == *base.manifest);
}

// Suits are kept by label (design + colour): a second suit with the same label is refused.
static std::string SecondSuit(const std::string& character, const std::string& design,
                              const std::string& colour) {
  return "suit " + character + " append 271 272 509 510 " + design + " " + colour +
         " b.tpr 10 0123456789abcdef0123456789abcdef01234567 "
         "b.cat 10 0123456789abcdef0123456789abcdef01234567";
}

static void TestDuplicateLabel() {
  // kGood is vks_ch_d c0. The second line differs from it in its files and placeholder numbers only.
  ExpectError(kHead + kGood + "\n" + SecondSuit("5", "vks_ch_d", "0") + "\n",
              "line 4: suit vks_ch_d c0 listed twice");
  // The label has no character in it: another character's suit with the same label is refused too.
  ExpectError(kHead + kGood + "\n" + SecondSuit("0", "vks_ch_d", "0") + "\n",
              "line 4: suit vks_ch_d c0 listed twice");
  // Controls: the same line with another colour, or another design, is a different label and parses
  // (which also shows that two suits may share a head part).
  const ParseResult other_colour = ParseManifest(kHead + kGood + "\n" + SecondSuit("5", "vks_ch_d", "1") + "\n");
  CHECK(other_colour.manifest && other_colour.manifest->suits.size() == 2);
  const ParseResult other_design = ParseManifest(kHead + kGood + "\n" + SecondSuit("5", "vks_on_o", "0") + "\n");
  CHECK(other_design.manifest && other_design.manifest->suits.size() == 2);
}

int main() {
  TestParsesFields();
  TestFormatIsCanonical();
  TestCrlfBom();
  TestEmptySuitList();
  TestRejects();
  TestSafeFileName();
  TestConverterName();
  TestFormatEdges();
  TestOwnModelZero();
  TestAcceptsLastValueOfBounds();
  TestTabsSeparateWords();
  TestDuplicateLabel();
  if (g_failures) {
    std::printf("%d check(s) failed\n", g_failures);
    return 1;
  }
  std::printf("suits_manifest: all checks passed\n");
  return 0;
}

// The word under the title logo. See doa4_title_word.h.
//
// The numbers below come from the approved mock-up (notes/mockups/2026-11-05-online-title/logo_mock3.py): cut lines read off
// zoomed views of the logo, letter advances, the N's shape. Only + - * / sqrt floor on doubles and integers (see the header).

#include "doa4_title_word.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <tuple>

namespace doa4::title_word {

uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t width, uint32_t log_bpp) {
  const uint32_t aligned = (width + 31) & ~31u;
  const uint32_t macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpp + 7);
  const uint32_t micro = ((x & 7) + ((y & 6) << 2)) << log_bpp;
  const uint32_t offset = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4);
  const uint32_t element = (((offset & ~511u) << 3) + ((offset & 448) << 2) + (offset & 63) + ((y & 16) << 7) +
                            (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >>
                           log_bpp;
  return element << log_bpp;
}

namespace {

constexpr int kSs = 4;              // the N is drawn at 4x and averaged down
constexpr double kSlant = 0.27;     // px left per row down, measured on the L and the E
constexpr int kBase = 92, kTop = 41;  // the letters' baseline and top row in the logo
constexpr int kCanvasW = 560, kCanvasH = int(kLogoHeight);
constexpr int kNWidth = 72, kNStem = 24, kNLeft = 8, kNCanvasW = 120;
constexpr double kLStem = 585.0, kIStem = 650.0;  // those letters' left edges at the baseline
constexpr int kWordX0 = 0, kWordY0 = 34, kWordW = 444, kWordH = 76;  // the part of the canvas that holds the word
constexpr uint32_t kLogoFileTextures = 12;                           // six images, each followed by its palette
constexpr uint8_t kSolidAlpha = 235;  // the logo's letters read 240-255 here, its black shadow 190 at most

struct Px {
  double r = 0, g = 0, b = 0, a = 0;
};
using Image = std::vector<Px>;

// Source over destination, straight alpha.
void Over(Px* d, const Px& s) {
  const double sa = s.a / 255.0, da = d->a / 255.0;
  const double out = sa + da * (1 - sa);
  if (out <= 0) {
    *d = Px{};
    return;
  }
  d->r = (s.r * sa + d->r * da * (1 - sa)) / out;
  d->g = (s.g * sa + d->g * da * (1 - sa)) / out;
  d->b = (s.b * sa + d->b * da * (1 - sa)) / out;
  d->a = out * 255.0;
}

double Clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

struct Cut {
  char letter;
  double left_top, left_bottom, right_top, right_bottom;  // x of the two slanted lines at rows 30 and 110
  int origin;                                             // the letter's left edge at the baseline
  int advance;
};
constexpr Cut kCuts[] = {
    {'O', 340, 340, 432.5, 419, 347, 80},
    {'L', 591.6, 583.1, 660, 643, 580, 72},
    {'I', 663, 646, 689.5, 668, 650, 33},
    {'E', 765, 751, 842, 842, 755, 80},
};

const Cut& CutFor(char letter) {
  for (const Cut& c : kCuts) {
    if (c.letter == letter) return c;
  }
  return kCuts[0];
}

// How much of a logo pixel belongs to the letter: 1 between the lines, a short soft ramp at each (as the mock-up's blurred mask).
double Coverage(const Cut& c, int x, int y) {
  if (y < 30 || y >= 110) return 0;
  const double t = (y + 0.5 - 30.0) / 80.0;
  const double left = c.left_top + (c.left_bottom - c.left_top) * t;
  const double right = c.right_top + (c.right_bottom - c.right_top) * t;
  const double px = x + 0.5;
  return Clamp((px - left) / 1.5 + 0.5, 0, 1) * Clamp((right - px) / 1.5 + 0.5, 0, 1);
}

Px LogoAt(const std::vector<Rgba>& logo, int x, int y) {
  const Rgba& p = logo[size_t(y) * kLogoWidth + x];
  return Px{double(p.r), double(p.g), double(p.b), double(p.a)};
}

std::array<double, 3> SampleRgb(const std::vector<Rgba>& logo, double left_at_base, double y, double d) {
  const int sy = int(Clamp(std::floor(y + 0.5), 42, 91));
  const double sx = left_at_base + kSlant * (kBase - sy) + d;
  const int x0 = int(sx);
  const double f = sx - x0;
  const Px a = LogoAt(logo, x0, sy), b = LogoAt(logo, x0 + 1, sy);
  return {a.r * (1 - f) + b.r * f, a.g * (1 - f) + b.g * f, a.b * (1 - f) + b.b * f};
}

// Three or more passes of a box filter stand in for a Gaussian: integers in, the same numbers on every PC.
void BoxBlur(std::vector<double>* v, int w, int h, int radius, int passes) {
  std::vector<double> tmp(v->size());
  const double n = 2.0 * radius + 1.0;
  for (int pass = 0; pass < passes; ++pass) {
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        double sum = 0;
        for (int k = -radius; k <= radius; ++k) sum += (*v)[size_t(y) * w + std::clamp(x + k, 0, w - 1)];
        tmp[size_t(y) * w + x] = sum / n;
      }
    }
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        double sum = 0;
        for (int k = -radius; k <= radius; ++k) sum += tmp[size_t(std::clamp(y + k, 0, h - 1)) * w + x];
        (*v)[size_t(y) * w + x] = sum / n;
      }
    }
  }
}

// One line of the exact distance transform (Felzenszwalb and Huttenlocher): out[q] = min over p of (q - p)^2 + in[p].
void Distance1d(const std::vector<double>& in, std::vector<double>* out) {
  const int n = int(in.size());
  std::vector<int> v(n);
  std::vector<double> z(n + 1);
  int k = 0;
  v[0] = 0;
  z[0] = -std::numeric_limits<double>::infinity();
  z[1] = std::numeric_limits<double>::infinity();
  for (int q = 1; q < n; ++q) {
    double s;
    for (;;) {
      const int p = v[k];
      s = ((in[q] + double(q) * q) - (in[p] + double(p) * p)) / (2.0 * q - 2.0 * p);
      if (s > z[k]) break;
      --k;
    }
    ++k;
    v[k] = q;
    z[k] = s;
    z[k + 1] = std::numeric_limits<double>::infinity();
  }
  k = 0;
  for (int q = 0; q < n; ++q) {
    while (z[k + 1] < q) ++k;
    const double d = double(q) - v[k];
    (*out)[q] = d * d + in[v[k]];
  }
}

// A letter over one soft shadow, down and to the right: the same for all six, so the word reads as one piece.
Image WithSoftShadow(const Image& letter, int w, int h) {
  std::vector<double> soft(letter.size());
  for (size_t i = 0; i < letter.size(); ++i) soft[i] = letter[i].a;
  BoxBlur(&soft, w, h, 2, 4);
  Image full(letter.size());
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (x >= 3 && y >= 3) {
        const double a = soft[size_t(y - 3) * w + x - 3] * 0.45;
        full[size_t(y) * w + x] = Px{10, 24, 44, a};
      }
      Over(&full[size_t(y) * w + x], letter[size_t(y) * w + x]);
    }
  }
  return full;
}

// The N, 1x, its shadow under it, as wide as the O and the E. On left-facing edges the chrome is `scuff_stem`'s real scuffed
// band at the same row and depth; other edges have the clean rim; the inside is the L's dark centre, row by row.
Image BuildN(const std::vector<Rgba>& logo, double scuff_stem) {
  const int w4 = kNCanvasW * kSs, h4 = kCanvasH * kSs;
  const int top = kTop, bottom = kBase + 2;
  const std::array<std::array<double, 2>, 10> shape = {{{0.0, double(top)},
                                                        {double(kNStem + 5), double(top)},
                                                        {double(kNWidth - kNStem), double(bottom - 24)},
                                                        {double(kNWidth - kNStem), double(top)},
                                                        {double(kNWidth), double(top)},
                                                        {double(kNWidth), double(bottom)},
                                                        {double(kNWidth - kNStem - 5), double(bottom)},
                                                        {double(kNStem), double(top + 24)},
                                                        {double(kNStem), double(bottom)},
                                                        {0.0, double(bottom)}}};
  std::array<std::array<double, 2>, 10> at4;
  for (size_t i = 0; i < shape.size(); ++i) {
    at4[i] = {(shape[i][0] + kNLeft + kSlant * (kBase - shape[i][1])) * kSs, shape[i][1] * kSs};
  }
  std::vector<double> mask(size_t(w4) * h4, 0.0);
  for (int y = 0; y < h4; ++y) {
    const double cy = y + 0.5;
    std::vector<double> crossings;
    for (size_t i = 0; i < at4.size(); ++i) {
      const auto& a = at4[i];
      const auto& b = at4[(i + 1) % at4.size()];
      if ((a[1] <= cy) != (b[1] <= cy)) crossings.push_back(a[0] + (cy - a[1]) * (b[0] - a[0]) / (b[1] - a[1]));
    }
    std::sort(crossings.begin(), crossings.end());
    for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
      for (int x = std::max(0, int(crossings[i])); x < w4 && x + 0.5 < crossings[i + 1]; ++x) {
        if (x + 0.5 >= crossings[i]) mask[size_t(y) * w4 + x] = 255.0;
      }
    }
  }
  BoxBlur(&mask, w4, h4, 4, 3);  // rounds the corners, as the logo's are
  std::vector<uint8_t> inside(mask.size());
  for (size_t i = 0; i < mask.size(); ++i) inside[i] = mask[i] > 127.0 ? 1 : 0;

  const double inf = 1e12;
  std::vector<double> dist(mask.size());
  for (size_t i = 0; i < mask.size(); ++i) dist[i] = inside[i] ? inf : 0.0;
  {
    std::vector<double> in(h4), out(h4);
    for (int x = 0; x < w4; ++x) {
      for (int y = 0; y < h4; ++y) in[y] = dist[size_t(y) * w4 + x];
      Distance1d(in, &out);
      for (int y = 0; y < h4; ++y) dist[size_t(y) * w4 + x] = out[y];
    }
    in.resize(w4);
    out.resize(w4);
    for (int y = 0; y < h4; ++y) {
      for (int x = 0; x < w4; ++x) in[x] = dist[size_t(y) * w4 + x];
      Distance1d(in, &out);
      for (int x = 0; x < w4; ++x) dist[size_t(y) * w4 + x] = out[x];
    }
  }
  for (double& d : dist) d = std::sqrt(d) / kSs;
  std::vector<double> smooth = dist;
  BoxBlur(&smooth, w4, h4, 2, 3);

  Image n1(size_t(kNCanvasW) * kCanvasH);
  std::vector<std::array<double, 4>> sums(n1.size());  // premultiplied by coverage; [3] counts subpixels
  for (int y = 1; y + 1 < h4; ++y) {
    for (int x = 1; x + 1 < w4; ++x) {
      const size_t at = size_t(y) * w4 + x;
      if (!inside[at]) continue;
      const double row = double(y) / kSs, d = dist[at];
      const double gx = (smooth[at + 1] - smooth[at - 1]) / 2.0, gy = (smooth[at + w4] - smooth[at - w4]) / 2.0;
      const double norm = std::max(std::sqrt(gx * gx + gy * gy), 1e-6);
      const double facing = gx / norm;
      const auto fill = SampleRgb(logo, kLStem, Clamp(row, 47, 86), 17);
      const double t = (row - kTop) / double(kBase - kTop);
      const std::array<double, 3> white = {206 * (1 - t) + 246 * t, 226 * (1 - t) + 250 * t, 238 * (1 - t) + 252 * t};
      const std::array<double, 3> line = {96, 128, 146};
      std::array<double, 3> clean;
      for (int c = 0; c < 3; ++c) {
        if (d < 3.0) {
          clean[c] = white[c];
        } else if (d < 4.2) {
          const double k = (d - 3.0) / 1.2;
          clean[c] = white[c] * (1 - k) + line[c] * k;
        } else if (d < 5.4) {
          const double k = (d - 4.2) / 1.2;
          clean[c] = line[c] * (1 - k) + fill[c] * k;
        } else {
          clean[c] = fill[c];
        }
      }
      double weight = Clamp((facing - 0.35) / 0.35, 0, 1);
      if (d >= 11.0) {
        weight = 0;
      } else if (d > 8.5) {
        weight *= (11.0 - d) / 2.5;
      }
      const double depth = std::min(d, 11.0);
      const int sy = int(Clamp(std::floor(row + 0.5), 42, 91));
      // Where the source pixel is see-through (just outside the real letter's edge) its colour means nothing: keep the rim.
      const double source_alpha = LogoAt(logo, int(scuff_stem + kSlant * (kBase - sy) + depth), sy).a / 255.0;
      weight *= Clamp((source_alpha - 0.6) / 0.35, 0, 1);
      const auto scuff = SampleRgb(logo, scuff_stem, row, depth);
      auto& s = sums[size_t(y / kSs) * kNCanvasW + x / kSs];
      for (int c = 0; c < 3; ++c) s[c] += clean[c] * (1 - weight) + scuff[c] * weight;
      s[3] += 1.0;
    }
  }
  for (size_t i = 0; i < n1.size(); ++i) {
    if (sums[i][3] <= 0) continue;
    n1[i] = Px{sums[i][0] / sums[i][3], sums[i][1] / sums[i][3], sums[i][2] / sums[i][3], sums[i][3] / (kSs * kSs) * 255.0};
  }
  return WithSoftShadow(n1, kNCanvasW, kCanvasH);
}

// One letter of the logo, without what surrounds it: the largest connected solid shape inside its cut, plus that shape's own
// soft edge. The logo's letters sit in a black see-through shadow that overlaps their neighbours, so cutting pixels by position
// alone keeps chopped shadow and slivers of the next letter; here neither survives. Its shadow comes from WithSoftShadow.
Image LetterBody(const std::vector<Rgba>& logo, const Cut& cut) {
  const int w = int(kLogoWidth), h = int(kLogoHeight);
  std::vector<int> label(size_t(w) * h, 0);
  std::vector<uint8_t> solid(label.size(), 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if (logo[size_t(y) * w + x].a >= kSolidAlpha && Coverage(cut, x, y) >= 0.5) solid[size_t(y) * w + x] = 1;
    }
  }
  int best = 0, best_size = 0, next = 0;
  std::vector<size_t> stack;
  for (size_t start = 0; start < solid.size(); ++start) {  // in a fixed order: ties go to the first shape found
    if (!solid[start] || label[start]) continue;
    ++next;
    int size = 0;
    label[start] = next;
    stack.push_back(start);
    while (!stack.empty()) {
      const size_t at = stack.back();
      stack.pop_back();
      ++size;
      const int x = int(at % size_t(w)), y = int(at / size_t(w));
      for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
          const int nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
          const size_t n = size_t(ny) * w + nx;
          if (solid[n] && !label[n]) {
            label[n] = next;
            stack.push_back(n);
          }
        }
      }
    }
    if (size > best_size) {
      best_size = size;
      best = next;
    }
  }
  Image body(label.size());
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const size_t at = size_t(y) * w + x;
      const double coverage = Coverage(cut, x, y);
      if (coverage <= 0) continue;
      Px p = LogoAt(logo, x, y);
      if (label[at] == best && best != 0) {
        p.a *= coverage;
        body[at] = p;
        continue;
      }
      if (p.a >= kSolidAlpha) continue;  // solid, but another shape: a piece of the next letter
      // The soft edge: within two pixels of the letter and not shadow, which is black. Dark edge pixels fade out with it.
      bool near = false;
      for (int dy = -2; dy <= 2 && !near; ++dy) {
        for (int dx = -2; dx <= 2 && !near; ++dx) {
          const int nx = x + dx, ny = y + dy;
          near = nx >= 0 && ny >= 0 && nx < w && ny < h && best != 0 && label[size_t(ny) * w + nx] == best;
        }
      }
      if (!near) continue;
      const double brightness = (p.r + p.g + p.b) / 3.0;
      p.a *= coverage * Clamp(brightness / 80.0, 0, 1);
      body[at] = p;
    }
  }
  return WithSoftShadow(body, w, h);
}

// Area-weighted scale down, in premultiplied alpha.
Word Scale(const Image& image, int x0, int y0, int w, int h, int stride, int out_w, int out_h) {
  Word out;
  out.width = uint32_t(out_w);
  out.height = uint32_t(out_h);
  out.pixels.resize(size_t(out_w) * out_h);
  const double fx = double(w) / out_w, fy = double(h) / out_h;
  for (int oy = 0; oy < out_h; ++oy) {
    const double sy0 = oy * fy, sy1 = (oy + 1) * fy;
    for (int ox = 0; ox < out_w; ++ox) {
      const double sx0 = ox * fx, sx1 = (ox + 1) * fx;
      double r = 0, g = 0, b = 0, a = 0, area = 0;
      for (int sy = int(sy0); double(sy) < sy1 && sy < h; ++sy) {
        const double wy = std::min(sy1, double(sy + 1)) - std::max(sy0, double(sy));
        for (int sx = int(sx0); double(sx) < sx1 && sx < w; ++sx) {
          const double wx = std::min(sx1, double(sx + 1)) - std::max(sx0, double(sx));
          const double weight = wx * wy;
          const Px& p = image[size_t(y0 + sy) * stride + x0 + sx];
          const double pa = p.a * weight;
          r += p.r * pa;
          g += p.g * pa;
          b += p.b * pa;
          a += pa;
          area += weight;
        }
      }
      Rgba& o = out.pixels[size_t(oy) * out_w + ox];
      if (a > 0) {
        o.r = uint8_t(Clamp(r / a + 0.5, 0, 255));
        o.g = uint8_t(Clamp(g / a + 0.5, 0, 255));
        o.b = uint8_t(Clamp(b / a + 0.5, 0, 255));
        o.a = uint8_t(Clamp(a / area + 0.5, 0, 255));
      }
    }
  }
  return out;
}

uint32_t Be32(const std::vector<uint8_t>& b, size_t at) {
  return uint32_t(b[at]) << 24 | uint32_t(b[at + 1]) << 16 | uint32_t(b[at + 2]) << 8 | b[at + 3];
}

struct Texture {
  size_t texels = 0;  // file offset
};

// The i-th TX2D, checked to be the format and size the design measured.
std::optional<Texture> FindTexture(const std::vector<uint8_t>& b, uint32_t index, uint32_t format, uint32_t swap,
                                   uint32_t width, uint32_t height, uint32_t log_bpp, std::string* why) {
  const uint64_t header = Be32(b, 4), data = Be32(b, 8), count = Be32(b, 12), header_end = 12 + header;
  uint32_t seen = 0;
  for (uint64_t i = 0; i < count && 16 + 16 * i + 16 <= header_end; ++i) {
    const size_t entry = size_t(16 + 16 * i);
    if (std::memcmp(b.data() + entry, "TX2D", 4) != 0) continue;
    if (seen++ != index) continue;
    const uint64_t at = 12 + uint64_t(Be32(b, entry + 4)), size = Be32(b, entry + 8);
    const std::string name = "texture " + std::to_string(index);
    if (size < 24 || at + size > header_end) {
      *why = name + "'s descriptor runs past the header";
      return std::nullopt;
    }
    uint32_t fc[6];
    for (int w = 0; w < 6; ++w) fc[w] = Be32(b, size_t(at + size - 24 + 4 * w));
    const uint32_t tex_w = (fc[2] & 0x1FFF) + 1, tex_h = ((fc[2] >> 13) & 0x1FFF) + 1;
    if ((fc[1] & 0x3F) != format) {
      *why = name + " is format " + std::to_string(fc[1] & 0x3F) + ", not " + std::to_string(format);
    } else if (((fc[1] >> 6) & 3) != swap) {
      *why = name + " has byte swap " + std::to_string((fc[1] >> 6) & 3) + ", not " + std::to_string(swap);
    } else if (tex_w != width || tex_h != height) {
      *why = name + " is " + std::to_string(tex_w) + "x" + std::to_string(tex_h);
    } else if ((fc[0] >> 31) == 0) {
      *why = name + " is not tiled";
    } else if ((fc[5] & 0xFFFFF000u) != 0) {
      *why = name + " has mip levels stored";
    } else {
      uint64_t extent = 0;
      for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) extent = std::max<uint64_t>(extent, TiledOffset(x, y, width, log_bpp));
      }
      extent += uint64_t(1) << log_bpp;
      if (uint64_t(fc[1] & 0xFFFFF000u) + extent > data) {
        *why = name + "'s texels run past the data section";
      } else {
        return Texture{size_t(header_end + (fc[1] & 0xFFFFF000u))};
      }
    }
    return std::nullopt;
  }
  *why = "it has no texture " + std::to_string(index);
  return std::nullopt;
}

// Median cut to 255 colours, in a fixed order, sorted by alpha then brightness; index 0 is fully transparent.
std::vector<Rgba> Quantize(const Word& word, std::vector<uint8_t>* indices) {
  std::map<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>, uint64_t> census;
  for (const Rgba& p : word.pixels) {
    if (p.a != 0) ++census[{p.r, p.g, p.b, p.a}];
  }
  struct Entry {
    std::array<int, 4> c;
    uint64_t n;
  };
  std::vector<std::vector<Entry>> boxes(1);
  for (const auto& [key, n] : census) {
    boxes[0].push_back({{std::get<0>(key), std::get<1>(key), std::get<2>(key), std::get<3>(key)}, n});
  }
  const auto widest = [](const std::vector<Entry>& box, int* channel) {
    int best = -1;
    *channel = 0;
    for (int c = 0; c < 4; ++c) {
      int lo = 255, hi = 0;
      for (const Entry& e : box) {
        lo = std::min(lo, e.c[c]);
        hi = std::max(hi, e.c[c]);
      }
      if (hi - lo > best) {
        best = hi - lo;
        *channel = c;
      }
    }
    return best;
  };
  while (boxes.size() < 255) {
    size_t pick = boxes.size();
    uint64_t pick_score = 0;
    for (size_t i = 0; i < boxes.size(); ++i) {
      if (boxes[i].size() < 2) continue;
      int channel;
      const uint64_t range = uint64_t(widest(boxes[i], &channel));
      uint64_t pixels = 0;
      for (const Entry& e : boxes[i]) pixels += e.n;
      const uint64_t score = range * range * pixels;
      if (score > pick_score) {
        pick_score = score;
        pick = i;
      }
    }
    if (pick == boxes.size()) break;
    int channel;
    widest(boxes[pick], &channel);
    std::stable_sort(boxes[pick].begin(), boxes[pick].end(),
                     [channel](const Entry& a, const Entry& b) { return a.c[channel] < b.c[channel]; });
    uint64_t total = 0, run = 0;
    for (const Entry& e : boxes[pick]) total += e.n;
    size_t cut = 1;
    for (size_t i = 0; i + 1 < boxes[pick].size(); ++i) {
      run += boxes[pick][i].n;
      cut = i + 1;
      if (run * 2 >= total) break;
    }
    std::vector<Entry> tail(boxes[pick].begin() + long(cut), boxes[pick].end());
    boxes[pick].resize(cut);
    boxes.push_back(std::move(tail));
  }
  std::vector<Rgba> palette(1);  // index 0: transparent
  for (const auto& box : boxes) {
    if (box.empty()) continue;
    uint64_t n = 0, sum[4] = {};
    for (const Entry& e : box) {
      n += e.n;
      for (int c = 0; c < 4; ++c) sum[c] += uint64_t(e.c[c]) * e.n;
    }
    palette.push_back(Rgba{uint8_t((sum[0] + n / 2) / n), uint8_t((sum[1] + n / 2) / n), uint8_t((sum[2] + n / 2) / n),
                           uint8_t((sum[3] + n / 2) / n)});
  }
  std::stable_sort(palette.begin() + 1, palette.end(), [](const Rgba& a, const Rgba& b) {
    return std::make_tuple(a.a, 299 * a.r + 587 * a.g + 114 * a.b, a.r, a.g, a.b) <
           std::make_tuple(b.a, 299 * b.r + 587 * b.g + 114 * b.b, b.r, b.g, b.b);
  });
  palette.resize(256);
  const size_t used = 1 + boxes.size();
  std::map<std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>, uint8_t> nearest;
  indices->assign(word.pixels.size(), 0);
  for (size_t i = 0; i < word.pixels.size(); ++i) {
    const Rgba& p = word.pixels[i];
    if (p.a == 0) continue;
    const auto key = std::make_tuple(p.r, p.g, p.b, p.a);
    auto it = nearest.find(key);
    if (it == nearest.end()) {
      int64_t best = std::numeric_limits<int64_t>::max();
      uint8_t pick = 1;
      for (size_t k = 1; k < used && k < palette.size(); ++k) {
        const Rgba& q = palette[k];
        const int64_t dr = int(p.r) - q.r, dg = int(p.g) - q.g, db = int(p.b) - q.b, da = int(p.a) - q.a;
        const int64_t score = (dr * dr + dg * dg + db * db) * p.a + da * da * 512;
        if (score < best) {
          best = score;
          pick = uint8_t(k);
        }
      }
      it = nearest.emplace(key, pick).first;
    }
    (*indices)[i] = it->second;
  }
  return palette;
}

}  // namespace

Word ComposeWord(const std::vector<Rgba>& logo) {
  if (logo.size() != size_t(kLogoWidth) * kLogoHeight) return {};
  Image canvas(size_t(kCanvasW) * kCanvasH);
  int pen = 6;
  double scuff_stem = kIStem;  // two different scuffs, so the two Ns are not copies
  for (const char letter : std::string_view("ONLINE")) {
    if (letter == 'N') {
      const Image n = BuildN(logo, scuff_stem);
      scuff_stem = kLStem;
      for (int y = 0; y < kCanvasH; ++y) {
        for (int x = 0; x < kNCanvasW; ++x) {
          const int to = x + pen - kNLeft;
          if (to >= 0 && to < kCanvasW) Over(&canvas[size_t(y) * kCanvasW + to], n[size_t(y) * kNCanvasW + x]);
        }
      }
      pen += kNWidth + 11;
      continue;
    }
    const Cut& c = CutFor(letter);
    const Image body = LetterBody(logo, c);
    for (int y = 0; y < kCanvasH; ++y) {
      for (int x = 0; x < int(kLogoWidth); ++x) {
        const int to = x + pen - c.origin;
        if (to >= 0 && to < kCanvasW) Over(&canvas[size_t(y) * kCanvasW + to], body[size_t(y) * kLogoWidth + x]);
      }
    }
    pen += c.advance;
  }
  // A fixed window, not the picture's edges: the two logo files' faint shadows differ by a pixel, and one sprite record
  // serves both. Scaled about 0.985 wide and 0.84 tall, as in the approved mock-up; the game draws the sprite 1:1.
  return Scale(canvas, kWordX0, kWordY0, kWordW, kWordH, kCanvasW, kWordW * 985 / 1000, kWordH * 84 / 100);
}

std::string PutWordInLogoFile(std::vector<uint8_t>* xpr, uint32_t* width, uint32_t* height) {
  std::vector<uint8_t>& b = *xpr;
  if (b.size() < 16 || std::memcmp(b.data(), "XPR2", 4) != 0) return "not an XPR2 file";
  if (12 + uint64_t(Be32(b, 4)) + Be32(b, 8) != b.size()) return "its header and data sizes do not add up to the file's size";
  uint32_t textures = 0;
  const uint64_t resources = Be32(b, 12), table_end = 12 + uint64_t(Be32(b, 4));
  for (uint64_t i = 0; i < resources && 16 + 16 * i + 16 <= table_end; ++i) {
    if (std::memcmp(b.data() + size_t(16 + 16 * i), "TX2D", 4) == 0) ++textures;
  }
  if (textures != kLogoFileTextures) return "it has " + std::to_string(textures) + " textures, not 12";
  std::string why;
  const auto logo_tex = FindTexture(b, 0, 2, 0, kLogoWidth, kLogoHeight, 0, &why);
  if (!logo_tex) return why;
  const auto logo_pal = FindTexture(b, 1, 6, 2, 256, 1, 2, &why);
  if (!logo_pal) return why;
  const auto slot_tex = FindTexture(b, 8, 2, 0, kSlotWidth, kSlotHeight, 0, &why);
  if (!slot_tex) return why;
  const auto slot_pal = FindTexture(b, 9, 6, 2, 256, 1, 2, &why);
  if (!slot_pal) return why;

  std::array<Rgba, 256> palette;
  for (uint32_t i = 0; i < 256; ++i) {
    const size_t at = logo_pal->texels + TiledOffset(i, 0, 256, 2);
    palette[i] = Rgba{b[at + 1], b[at + 2], b[at + 3], b[at]};  // ARGB in the file
  }
  std::vector<Rgba> logo(size_t(kLogoWidth) * kLogoHeight);
  for (uint32_t y = 0; y < kLogoHeight; ++y) {
    for (uint32_t x = 0; x < kLogoWidth; ++x) {
      logo[size_t(y) * kLogoWidth + x] = palette[b[logo_tex->texels + TiledOffset(x, y, kLogoWidth, 0)]];
    }
  }
  // Every letter to be cut has to be there: a logo with no picture has nothing to give.
  for (const Cut& c : kCuts) {
    int solid = 0;
    for (int y = 30; y < 110; ++y) {
      for (int x = 0; x < int(kLogoWidth); ++x) {
        if (Coverage(c, x, y) > 0.99 && logo[size_t(y) * kLogoWidth + x].a > 200) ++solid;
      }
    }
    if (solid < 200) return std::string("the logo has no letter ") + c.letter + " where it is expected";
  }
  const Word word = ComposeWord(logo);
  if (word.width == 0 || word.width > kSlotWidth || word.height == 0 || word.height > kSlotHeight) {
    return "the word came out " + std::to_string(word.width) + "x" + std::to_string(word.height) +
           ", which does not fit the image";
  }
  std::vector<uint8_t> indices;
  const std::vector<Rgba> out_palette = Quantize(word, &indices);
  for (uint32_t y = 0; y < kSlotHeight; ++y) {
    for (uint32_t x = 0; x < kSlotWidth; ++x) {
      const uint8_t v = x < word.width && y < word.height ? indices[size_t(y) * word.width + x] : uint8_t(0);
      b[slot_tex->texels + TiledOffset(x, y, kSlotWidth, 0)] = v;
    }
  }
  for (uint32_t i = 0; i < 256; ++i) {
    const size_t at = slot_pal->texels + TiledOffset(i, 0, 256, 2);
    const Rgba& c = out_palette[i];
    b[at] = c.a;
    b[at + 1] = c.r;
    b[at + 2] = c.g;
    b[at + 3] = c.b;
  }
  *width = word.width;
  *height = word.height;
  return {};
}

std::shared_ptr<const afs::Overlay> BuildOverlay(const std::filesystem::path& game_root, uint32_t* width,
                                                 uint32_t* height, std::string* error) {
  const std::string archive = kSprArchive;
  std::shared_ptr<afs::FileSource> original = afs::FileSource::Open(game_root / archive);
  if (!original) {
    *error = "cannot open " + archive + " in the game folder";
    return nullptr;
  }
  std::string why;
  const std::optional<afs::Toc> toc = afs::ReadToc(*original, &why);
  if (!toc) {
    *error = archive + ": " + why;
    return nullptr;
  }
  if (toc->count <= kSprLogoW) {
    *error = archive + " has only " + std::to_string(toc->count) + " entries";
    return nullptr;
  }
  std::vector<afs::Replacement> replacements;
  uint32_t word_w = 0, word_h = 0;
  for (const uint32_t entry : {kSprLogoN, kSprLogoW}) {
    const std::string name = archive + " entry " + std::to_string(entry);
    std::vector<uint8_t> bytes(toc->sizes[entry]);
    if (!original->Read(toc->offsets[entry], bytes.data(), bytes.size())) {
      *error = name + ": cannot be read";
      return nullptr;
    }
    uint32_t w = 0, h = 0;
    why = PutWordInLogoFile(&bytes, &w, &h);
    if (!why.empty()) {
      *error = name + ": " + why;
      return nullptr;
    }
    // One sprite record serves both files, so the word has to be the same size in both.
    if (entry != kSprLogoN && (w != word_w || h != word_h)) {
      *error = name + ": the word is " + std::to_string(w) + "x" + std::to_string(h) + " here and " +
               std::to_string(word_w) + "x" + std::to_string(word_h) + " in the other logo file";
      return nullptr;
    }
    word_w = w;
    word_h = h;
    replacements.push_back({entry, std::make_shared<afs::MemorySource>(std::move(bytes))});
  }
  std::shared_ptr<const afs::Overlay> overlay = afs::Overlay::Build(original, std::move(replacements), &why);
  if (!overlay) {
    *error = archive + ": " + why;
    return nullptr;
  }
  bool same = overlay->size() == original->size();
  for (uint32_t i = 0; same && i < toc->count; ++i) same = overlay->entry_offset(i) == toc->offsets[i];
  if (!same) {
    *error = archive + ": the combined archive would not keep the original layout";
    return nullptr;
  }
  *width = word_w;
  *height = word_h;
  return overlay;
}

std::string FilesLine(const afs::Overlay* overlay) {
  const std::string none = "title-word=none\n";
  if (!overlay) return none;
  uint64_t hash = 0xCBF29CE484222325ull;  // FNV-1a 64 over both logo files as served
  for (const uint32_t entry : {kSprLogoN, kSprLogoW}) {
    std::vector<uint8_t> bytes(size_t(overlay->entry_size(entry)));
    if (!overlay->Read(overlay->entry_offset(entry), bytes.data(), bytes.size())) return none;
    for (const uint8_t b : bytes) {
      hash ^= b;
      hash *= 0x100000001B3ull;
    }
  }
  static constexpr char kHex[] = "0123456789abcdef";
  std::string line = "title-word=";
  for (int shift = 60; shift >= 0; shift -= 4) line += kHex[(hash >> shift) & 15];
  return line + "\n";
}

std::string CheckRecord(const GuestBytes& guest) {
  const uint32_t record = kSheet3Records + 12 * (kWordSprite & 0xFFFF);
  const uint16_t expected[6] = {170, 8, 854, 56, 3, 4};  // PRE-RELEASE VERSION in image 4 of sheet 3
  for (uint32_t i = 0; i < 6; ++i) {
    const uint16_t have = uint16_t(uint16_t(guest.Read(record + 2 * i)) << 8 | guest.Read(record + 2 * i + 1));
    if (have != expected[i]) {
      return "sprite record 4's word " + std::to_string(i) + " is " + std::to_string(have) + ", not " +
             std::to_string(expected[i]);
    }
  }
  return {};
}

std::string InstallRecord(const GuestBytes& guest, uint32_t width, uint32_t height) {
  if (width == 0 || width > kSlotWidth || height == 0 || height > kSlotHeight) return "the word does not fit its image";
  if (std::string why = CheckRecord(guest); !why.empty()) return why;
  const uint32_t record = kSheet3Records + 12 * (kWordSprite & 0xFFFF);
  const uint16_t rect[4] = {0, 0, uint16_t(width), uint16_t(height)};
  for (uint32_t i = 0; i < 4; ++i) {
    guest.Write(record + 2 * i, uint8_t(rect[i] >> 8));
    guest.Write(record + 2 * i + 1, uint8_t(rect[i]));
  }
  return {};
}

WordDraw WordDrawFor(float logo_x, float logo_y, float scale_x, float scale_y) {
  WordDraw d;
  d.x = logo_x + kWordOffsetX * scale_x;
  d.y = logo_y + kWordOffsetY * scale_y;
  return d;
}

}  // namespace doa4::title_word

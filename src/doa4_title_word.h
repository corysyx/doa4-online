// "ONLINE" under the title screen's logo (Dead or Alive 4 Online), drawn by the game itself so that it scales with the
// resolution and fades with the logo.
//
// What the game does (measured 2026-11-05, build with a logging hook on the sprite resolver sub_823BF210): the title draws
// sheet 3's sprites every frame: 0x30000 the logo at (640,300), 0x30003 the copyright at (640,635), 0x30001 and 0x30002
// PRESS START at (640,546). Each draw struct holds the id at +4, the centre at +12/+16 (floats), the scale at +24/+28 and
// the fade in the top byte of the colour at +32. Sheet 3 is spLogoN.xpr or spLogoW.xpr (spr.afs entries 18 and 19), six k_8
// images each followed by its 256x1 palette. Sprite 0x30004 is "PRE-RELEASE VERSION" (image 4, 1024x64), which this game
// never draws, so its image, palette and sprite record are free.
//
// What this does: the word is composed once at start-up from the player's own logo, so no game art enters the repository:
// the logo's O, L, I and E, cut along their slant, and an N built in the same slant and colours, whose scuffed left edges
// are the real stems' pixels (notes/mockups/2026-11-05-online-title, approved by the user as mock_title_v4). It replaces
// image 4 and its palette in memory, spr.afs is served with those two entries replaced (afs::Overlay; nothing is written to
// disk), record 4 is pointed at the word, and a hook on the resolver draws 0x30004 right after each logo draw with the
// logo's own colour and scale.
//
// Two players must get identical bytes (the texture sits in guest memory), so the composition uses integer and IEEE
// add/multiply/divide/sqrt arithmetic only, never exp, pow or sin, whose results depend on the C library.
// If anything does not look as measured, nothing changes: the game's own title, with a line in the log.
//
// Pure C++ over GuestBytes and afs::Source (no rex includes), so host tests build it.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "doa4_afs_overlay.h"
#include "doa4_guest.h"

namespace doa4::title_word {

inline constexpr const char* kSprArchive = "spr.afs";
inline constexpr uint32_t kSprLogoN = 18;
inline constexpr uint32_t kSprLogoW = 19;

inline constexpr uint32_t kLogoSprite = 0x30000;
inline constexpr uint32_t kWordSprite = 0x30004;
inline constexpr uint32_t kSheet3Records = 0x82C700A0;  // *(0x82C77D08 + 4 * 3) in the image: 6 x 12 bytes

// The word's centre relative to the logo's, in the game's 1280x720 canvas at scale 1 (the approved mock-up).
inline constexpr float kWordOffsetX = 2.0f;
inline constexpr float kWordOffsetY = 81.5f;

inline constexpr uint32_t kLogoWidth = 1024, kLogoHeight = 128;  // image 0
inline constexpr uint32_t kSlotWidth = 1024, kSlotHeight = 64;   // image 4

struct Rgba {
  uint8_t r = 0, g = 0, b = 0, a = 0;
};

// The word as an image, composed from the logo's pixels (kLogoWidth x kLogoHeight, row by row).
struct Word {
  uint32_t width = 0, height = 0;
  std::vector<Rgba> pixels;
};
Word ComposeWord(const std::vector<Rgba>& logo);

// Writes the word into an XPR2 file's image 4 and palette, at the image's top left corner, the rest of the image
// transparent; nothing else in the file changes. "" and *size holds the word's size, or why not and the file is untouched.
std::string PutWordInLogoFile(std::vector<uint8_t>* xpr, uint32_t* width, uint32_t* height);

// spr.afs with both logo files replaced, same sizes and layout, or null and *error. *width, *height: the word's size.
std::shared_ptr<const afs::Overlay> BuildOverlay(const std::filesystem::path& game_root, uint32_t* width,
                                                 uint32_t* height, std::string* error);

// Points sprite record 4 at the word, if it is still the game's PRE-RELEASE VERSION record; "" or why not (nothing written).
std::string InstallRecord(const GuestBytes& guest, uint32_t width, uint32_t height);

// The draw struct of the word's extra draw: from the logo's fields, what to change. Follows the logo's scale.
struct WordDraw {
  uint32_t id = kWordSprite;
  float x = 0, y = 0;
};
WordDraw WordDrawFor(float logo_x, float logo_y, float scale_x, float scale_y);

// Turns the extra draw on (defined with the hook in doa4_app_hooks.cpp; only after the record and the file are in place).
void EnableWordDraw();

// Between an XPR2 file's 8-bit palette image and plain pixels; exposed for the tests. Xenos tiling formula as in
// tools and the game's own textures. log_bpp: 0 for k_8 texels, 2 for 8888 palette entries.
uint32_t TiledOffset(uint32_t x, uint32_t y, uint32_t width, uint32_t log_bpp);

}  // namespace doa4::title_word

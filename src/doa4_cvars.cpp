// Cvars the port defines. The Escape menu saves them to dead_or_alive_4.toml.

#include <rex/cvar.h>

// Extras tab: game tweaks (doa4_tweaks.h). Applied at load (OnPostLoadXexImage).
REXCVAR_DEFINE_BOOL(doa4_tag_all_stages, false, "DOA4",
                    "All stages in Tag battle (Xenia patch by ryu_highabusa)");
REXCVAR_DEFINE_BOOL(doa4_mp_hidden_characters, false, "DOA4",
                    "Hidden characters in multiplayer select (Xenia patch by ryu_highabusa)");
REXCVAR_DEFINE_BOOL(doa4_helena_opera, false, "DOA4", "Helena's opera dress as an extra costume");
REXCVAR_DEFINE_BOOL(doa4_kokoro_kimonos, false, "DOA4", "Kokoro's pink and blue kimonos as extra costumes");
REXCVAR_DEFINE_BOOL(doa4_christie_costume, false, "DOA4", "Christie's cutscene costume as an extra costume");

// Game tab and F7: frames per second over the game. Host-only: not carried to the other player.
REXCVAR_DEFINE_BOOL(doa4_show_fps, false, "DOA4", "Show frames per second over the game");

// Game tab: skip the boot intro (doa4_intro_skip.h), read once at start-up: the game starts at the title screen. Host-only: not
// carried to the other player (a session skips every movie on both PCs anyway).
REXCVAR_DEFINE_BOOL(doa4_skip_intro, false, "DOA4", "Skip the intro at start-up: start at the title screen");

// First-run setup (doa4_gamefiles.h): the folder the setup chose. The command
// line's --game_data_root overrides it.
REXCVAR_DEFINE_STRING(doa4_game_files, "", "DOA4",
                      "Folder holding the game files (default.xex), chosen by the first-run setup");

// DOAX2 swimsuit import (doa4_suits_import_flow.h): set when an import starts, cleared when it ends well; while set, the
// next start offers the import again.
REXCVAR_DEFINE_BOOL(doa4_suits_import_pending, false, "DOA4",
                    "A DOAX2 swimsuit import was started and has not finished; the next start offers it again");

// The save and shader-cache folder (doa4_gamefiles.h ChooseUserData); the
// command line's --user_data_root overrides it. Empty: Documents\dead_or_alive_4.
REXCVAR_DEFINE_STRING(doa4_user_data, "", "DOA4",
                      "Folder for saves and the shader cache (empty: Documents/dead_or_alive_4)");

// Photo tab: full-quality photos (doa4_photo_service.h).
REXCVAR_DEFINE_STRING(doa4_photo_format, "png_monitor", "DOA4",
                      "Photo files: png_monitor, png_full or jpg_full")
    .allowed({"png_monitor", "png_full", "jpg_full"});
REXCVAR_DEFINE_STRING(doa4_photo_dir, "", "DOA4",
                      "Folder for photos (empty: the Pictures folder's DOA4 folder)");
REXCVAR_DEFINE_BOOL(doa4_photo_auto, false, "DOA4",
                    "Also save a full-quality copy when an in-game snapshot is saved to the Album");

// Online tab (netplay phase 1; np_online.h). Saved through doa4::SaveTomlKey by the Online tab (Task 14). Host-only:
// none of them is carried to the other player (np_handoff's census lists them as not carried).
REXCVAR_DEFINE_UINT32(netplay_port, 7200, "DOA4",
                      "Online play: preferred UDP port (7200-7215 keep the join code short)");
REXCVAR_DEFINE_BOOL(netplay_delay_auto, true, "DOA4",
                    "Online play: input delay from the measured ping");
REXCVAR_DEFINE_INT32(netplay_delay, 2, "DOA4",
                     "Online play: input delay in frames when not automatic (1-8)")
    .range(1, 8);
REXCVAR_DEFINE_STRING(netplay_last_join, "", "DOA4",
                      "Online play: the last code joined");
REXCVAR_DEFINE_BOOL(netplay_show_stats, false, "DOA4",
                    "Online play: show the connection stats line");
REXCVAR_DEFINE_BOOL(netplay_upnp, true, "DOA4",
                    "Online play: open the port on the router (UPnP) when hosting");
// The game's text and subtitles language (doa4_language.h), saved when the player changes them on the game's own options
// screen and applied at boot (doa4_language_hooks.cpp). -1: never saved, the game's own. Carried into a session (np_handoff
// kPortSettings): both PCs boot with the host's, and nothing in a session saves them.
REXCVAR_DEFINE_INT32(doa4_language, -1, "DOA4",
                     "Game text language: -1 the game's own, 0 Japanese, 1 English, 2 French, 3 German, 4 Italian, "
                     "5 Spanish, 6 Traditional Chinese, 7 Korean");
REXCVAR_DEFINE_INT32(doa4_subtitles, -1, "DOA4",
                     "Game subtitles language (Language > Subtitles in the game's options): -1 the game's own, 0-7 as the "
                     "text language, 8 off/default");
// Extras tab: DOAX2 swimsuits (doa4_suits_bust.h).
REXCVAR_DEFINE_BOOL(doa4_suits_full_bust, false, "DOA4",
                    "DOAX2 swimsuits: full DOAX2 up and down breast movement");

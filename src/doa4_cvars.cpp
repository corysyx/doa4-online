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

// Issue #4 (CPU heat): the loader and GPU fence waits sleep instead of spinning a core each (doa4_idle_waits.cpp). Read once
// at start-up; not in the menu (a fallback and the A/B switch). Host-only: not carried to the other player (it changes no
// guest state, only the host time between the guest's own checks).
REXCVAR_DEFINE_BOOL(doa4_idle_waits, true, "DOA4", "Let the game's loader and GPU waits sleep instead of spinning (lower CPU use)");

// Game tab: skip the boot intro (doa4_intro_skip.h), read once at start-up: the game starts at the title screen. Host-only: not
// carried to the other player (a session skips every movie on both PCs anyway).
REXCVAR_DEFINE_BOOL(doa4_skip_intro, true, "DOA4", "Skip the intro at start-up: start at the title screen");

// Game tab: the Discord status "Playing Dead or Alive 4 Online" (doa4_discord.h). Host-only: not carried to the other player
// (it talks to the Discord app on this PC and touches no guest memory).
REXCVAR_DEFINE_BOOL(doa4_discord_status, true, "DOA4", "Show 'Playing Dead or Alive 4 Online' as my Discord status");

// Online tab: the start-up update notice (doa4_update_notice.h), read once at start-up. Host-only: not carried to the other
// player (a session relaunch never checks and never shows it).
REXCVAR_DEFINE_BOOL(doa4_update_notice, true, "DOA4", "Tell me about new versions at start-up");

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

// Shader tab: the picture effects (doa4_picture_fx.h), "on=1;split=0;sharpen=0.500;..."; empty: off.
REXCVAR_DEFINE_STRING(doa4_picture_fx, "", "DOA4", "Picture effects of the Esc menu's Shader tab");

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
// Lobby launcher (DOA4 Lobby): set on the command line for one start, never saved. They press Host / Join once the
// Online tab would allow it, and the host presses Start online session once the other player is in; only in the
// process the player started (a session process and the lobby it returns to are relaunches: no rematch loop).
REXCVAR_DEFINE_BOOL(netplay_autohost, false, "DOA4",
                    "Online play: host as soon as the game allows it, then start the session once the other player is in");
REXCVAR_DEFINE_STRING(netplay_autojoin, "", "DOA4",
                      "Online play: join this code as soon as the game allows it (retried for 2 minutes)");
// Rollback, milestone 1 (src/net/rb_wmap.h): measure which guest pages the game writes per frame (offline only).
REXCVAR_DEFINE_BOOL(doa4_rb_wmap, false, "DOA4",
                    "Rollback research: log which memory pages the game writes each frame (offline play only)");
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

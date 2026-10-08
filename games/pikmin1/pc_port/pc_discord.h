#ifndef PC_DISCORD_H
#define PC_DISCORD_H

// Discord Rich Presence over Discord's local IPC (socket on Linux/macOS,
// named pipe on Windows). No SDK: it only speaks the handshake and
// SET_ACTIVITY. Everything runs on its own thread, so a missing or slow
// Discord never stalls the game; without Discord it simply does nothing.

#ifdef __cplusplus
extern "C" {
#endif

/// Starts the presence thread. @p gameName is the first line ("Pikmin 1").
void pc_discord_init(const char* gameName);
void pc_discord_shutdown(void);

/// Call every gameplay frame (cheap). Without calls for a few seconds the
/// presence falls back to "In the menus" with the app icon.
/// @p zoneKey is the art asset name uploaded to the Discord app
/// (e.g. "forest_of_hope"); @p stateLine is the second line ("Day 5 · 12/30 parts").
void pc_discord_set_playing(const char* zoneName, const char* zoneKey, const char* stateLine);

#ifdef __cplusplus
}
#endif

#endif

/**
 * @file audio_stubs.cpp
 * @brief Stub implementations for audio-related Dolphin functions.
 * Signatures match: Dolphin/ai.h, Dolphin/dsp.h
 */
#include "Dolphin/ai.h"
#include "Dolphin/dsp.h"
#include "audio/pc_audio.h"
#include "audio/pc_event_commands.h"
#include <cstdlib>
#include <chrono>
#include <set>
#include "jaudio/app_inter.h"
#include "jaudio/interface.h"
#include "jaudio/piki_bgm.h"
#include "jaudio/piki_scene.h"
#include "jaudio/piki_player.h"
#include "jaudio/pikidemo.h"
#include "jaudio/pikiinter.h"
#include "jaudio/verysimple.h"
#include "MoviePlayer.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <iterator>
#include <string>

// Volume preferences in this game are 0..10 sliders (see ogTitle.cpp).
static constexpr u8 PIKI_JAC_VOLUME_STEPS = 10;

#if !PIKI_USE_JAUDIO
namespace {
const char* const kDemoStreams[] = {
    "piki.stx", "o_dead.stx", "d_end1.stx", "gyoku.stx", "d_end3.stx", "fanf5.stx", "badend0.stx",
    "badend1.stx", "opening.stx", "happyend1.stx", "compend1.stx", "compend0.stx", "badend2.stx", "onion.stx"
};

// Movie audio is stateful. In particular, the opening consists of movie IDs
// 1 and 2: ID 1 has JAudio's 0x20 "keep playing" flag so opening.stx spans the
// cut between both CIN files. The old stub stopped every stream at the end of
// each individual movie and therefore cut the opening in half.
bool sKeepDemoStreamOnFinish = false;
u32 sNativeBgm = BGM_PikiSE;
u8 sNativeBgmMode = 0;
float sNativeBgmVolume = 1.0f;
float sNativeBgmPreviousVolume = 1.0f;
u32 sCountdownSounds = 0;
u32 sPikiGayaLevel = 0;
bool sPartsFindDemoActive = false;
bool sTextDemoActive = false;
u32 sNativeScene = SCENE_NULL;
u32 sNativeStage = 0;
bool sChallengeMode = false;
bool sDemoJamActive = false;
bool sDemoEventPaused = false;
int sCurrentDemo = -1;
constexpr size_t kNoDemoTimedEvent = static_cast<size_t>(-1);
size_t sDemoTimedEvent = kNoDemoTimedEvent;
u8 sDemoPartsId = 0;
u8 sDemoOnyonCount = 0;
u8 sDemoPartsCount = 0;
bool sMenuOrPauseActive = false;
bool sMenuActive = false;
bool sPauseActive = false;
bool sDVDPauseActive = false;
u8 sEventResumeFrames = 0;
bool sPikiFlyReady = false;
u16 sPulledVoiceHistory[3] = { 4, 5, 6 };
u32 sVoiceRandom = 0x6D2B79F5u;
u32 sPikiGayaTimer = 0;
float sPikiGayaVolume = 0.0f;

void apply_gameplay_audio_pause() {
    const bool paused = sMenuActive || sPauseActive || sDVDPauseActive
                     || sPartsFindDemoActive || sTextDemoActive;
    sMenuOrPauseActive = paused;
    pc_audio_set_se_track_paused(10, paused);
    pc_audio_set_events_paused(paused || sDemoEventPaused
                              || sEventResumeFrames != 0);
}

constexpr u8 kDemoBgmFadeMode[] = {
    2,2,2,5,2,2,2,5,1,2,2,2,5,5,1,5,2,4,2,2,2,2,2,2,2,2,4,5,4,4,4,4,
    1,1,1,1,1,1,1,1,5,5,5,5,5,5,4,1,1,1,1,4,4,4,4,4,2,2,2,2,5,5,5,5,
    5,5,5,5,5,4,4,4,4,2,2,2,4,1,2,2,2,2,2,2,2,2,2,4,1,5,1,4,5,5,4,2,
    2,2,2,2,5,2,4,2,2,2,2,2,2,2,2,2,2,2,4
};
constexpr u8 kDemoGameplayFlags[] = {
    1,1,1,1,1,1,1,0,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,
    2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,2,2,2,2,1,2,2,2,2,1,1,1,1,0,0,0,0,
    0,0,0,0,1,2,2,2,2,1,1,1,2,1,1,1,1,1,1,1,1,1,1,2,2,1,2,2,1,0,2,1,
    1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,2,2
};

bool audio_demo_active() {
    return sCurrentDemo >= 0 || sPartsFindDemoActive || sTextDemoActive;
}

bool landing_demo_active() {
    return (sCurrentDemo >= 40 && sCurrentDemo <= 43)
        || sCurrentDemo == 89 || sCurrentDemo == 100;
}

u32 next_voice_variation(u32 limit) {
    sVoiceRandom = sVoiceRandom * 1664525u + 1013904223u;
    return limit ? (sVoiceRandom >> 16) % limit : 0;
}

constexpr u8 kDemoAudioConfig[] = {
    131,168,192,0,141,141,141,0,0,2,2,2,0,0,0,0,7,9,1,1,6,6,6,6,6,1,10,0,
    162,162,162,162,192,192,192,192,37,37,37,37,0,0,0,0,0,0,129,132,132,132,
    132,129,132,132,132,132,0,0,0,0,0,0,0,0,0,0,0,0,0,134,134,134,134,135,
    140,171,170,192,6,6,1,0,0,0,0,0,0,162,192,0,132,132,0,0,134,0,0,0,0,0,
    0,6,0,0,0,0,0,0,0,0,0,0,0,192,169
};

// Exact timed sound sequences from JAudio's DEMO_STATUS table. Offsets point
// into frame/event pairs; -1 means that the cinematic has no timed sequence.
constexpr s16 kDemoTimedOffsets[] = {
    -1,0,4,6,10,32,32,-1,-1,52,52,52,58,-1,-1,-1,62,80,122,130,152,172,172,172,
    192,212,228,254,258,320,320,320,382,382,382,382,-1,-1,-1,-1,388,388,388,388,
    -1,-1,-1,398,398,398,398,-1,420,420,420,420,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,446,446,446,446,456,482,510,530,-1,172,172,122,-1,-1,-1,-1,-1,-1,
    320,382,388,398,420,-1,-1,446,-1,-1,-1,-1,-1,540,552,568,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,572,608
};
constexpr s16 kDemoTimedData[] = {
    4,-6,2000,-1,2000,-1,380,1,444,-1,0,0,2,-6,5,1,15,2,25,1,30,3,60,4,154,5,176,6,225,7,280,-1,
    0,0,2,-6,5,1,15,2,21,3,60,4,128,5,150,6,200,7,280,-1,4,-6,200,-5,201,-3,4,0,600,-1,
    4,-6,10,1,19,2,31,1,42,2,52,2,64,3,120,6,300,-1,8,-6,56,10,64,11,80,12,98,13,128,14,140,8,
    163,1,172,2,183,1,192,2,200,0,203,1,213,2,224,1,232,2,250,3,270,4,294,5,310,9,365,-2,
    8,-6,140,8,200,0,365,-2,8,-6,10,10,45,11,65,14,84,15,98,16,102,12,108,13,140,8,200,0,365,-2,
    4,-6,10,0,48,1,65,2,80,3,92,4,96,5,120,7,185,6,300,-1,4,-6,10,0,48,1,65,2,80,3,92,4,96,5,
    100,7,148,6,300,-1,4,-6,10,0,48,1,65,2,80,3,92,4,96,5,100,7,148,6,300,-1,8,-6,14,10,18,11,
    90,12,123,13,140,8,200,0,365,-2,8,-6,240,0,250,9,308,1,320,2,330,1,337,2,348,1,355,3,392,4,
    421,5,440,8,500,-2,4,0,600,-1,4,-6,30,0,41,1,53,0,65,1,77,0,89,1,101,0,111,1,122,0,131,1,
    143,0,150,2,215,3,310,4,320,5,330,4,342,5,350,4,362,5,372,4,381,5,391,4,403,5,413,4,423,5,
    433,4,443,6,490,7,561,8,600,-2,4,-6,30,0,41,1,53,0,65,1,77,0,89,1,101,0,111,1,122,0,131,1,
    143,0,150,2,215,3,310,4,320,5,330,4,342,5,350,4,362,5,372,4,381,5,391,4,403,5,413,4,423,5,
    433,4,443,6,490,7,561,8,600,-2,0,0,160,1,600,-1,3,2,4,3,150,0,190,1,268,-1,4,-6,21,0,43,1,
    62,0,78,1,103,2,220,0,240,1,300,3,358,4,380,-1,4,-6,104,0,122,1,205,0,224,1,244,0,262,1,
    282,0,306,1,326,2,360,3,420,4,450,-1,4,-6,145,0,225,1,280,3,1100,-1,2,-6,3,0,43,1,72,2,135,1,
    148,2,172,1,210,2,270,3,320,4,350,5,411,6,1100,-1,2,-6,3,0,215,1,395,2,458,3,500,4,534,5,
    535,6,585,7,673,8,685,9,736,10,750,11,1100,-1,4,-6,150,3,160,4,170,3,180,4,190,5,900,0,945,1,
    993,2,1100,-1,4,-6,3,0,240,1,382,2,1100,-1,3,2,4,3,150,0,190,1,204,1,268,-1,4,-6,10,0,48,1,
    65,2,80,3,92,4,96,5,300,-1,160,1,600,-1,34,1,41,2,50,0,53,1,83,2,85,1,286,3,400,4,462,5,
    474,6,482,5,512,6,527,11,539,9,540,7,568,10,570,8,1200,-1,3,0,4,-6,1100,-1
};

static_assert(std::size(kDemoBgmFadeMode) == std::size(kDemoGameplayFlags));
static_assert(std::size(kDemoAudioConfig) == std::size(kDemoGameplayFlags));
static_assert(std::size(kDemoTimedOffsets) == std::size(kDemoGameplayFlags));

u8 demo_audio_config(u32 cinemaId) {
    return cinemaId < std::size(kDemoAudioConfig)
        ? kDemoAudioConfig[cinemaId] : 0;
}

constexpr u16 kBgmMuteSets[20][4] = {
    {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF},
    {0x103C, 0x17D8, 0x8003, 0x19C3}, {0x0033, 0x00F3, 0x003C, 0x00FC},
    {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF},
    {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, {0xC00F, 0xF78D, 0xC07C, 0xFE2C},
    {0x0047, 0x0297, 0x002D, 0x023D}, {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF},
    {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF},
    {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF},
    {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, {0x057D, 0x05FB, 0x0A78, 0x0AFA},
    {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF},
    {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF},
};

void apply_native_bgm_layers(u32 fadeFrames) {
    if (sNativeBgm < BGM_Dummy2 || sNativeBgm > BGM_FinalResult) return;
    const u32 song = sNativeBgm - BGM_Dummy2;
    const u16 mask = kBgmMuteSets[song][sNativeBgmMode & 3];
    float volume = song == 15 ? 0.55f : (song == 18 ? 0.35f : 0.5f);
    if (sNativeBgmMode & 4) volume = 0.15f;
    if (sNativeBgmMode & 8) volume = 0.0f;
    pc_audio_set_sequence_layers(mask, volume, fadeFrames);
}
int sSystemSounds[64];
int sOrimaSounds[64];
int sWhistleVoice = -1;

struct NativeEvent {
    bool active = false;
    u32 type = 0;
    u8 frameTimer = 0;
    float pan = 0.0f;
    float volume = 1.0f;
    int voices[16];
    int actions[16];
    u8 groups[16] = {};
    u8 priorities[16] = {};
    u32 timestamps[16] = {};
    // Wall-clock milliseconds at which each slot was taken, so the trace can
    // say how long a voice really held the group's only slot.
    u64 startedMs[16] = {};
};
static u32 sEventClock = 0;
NativeEvent sEvents[16];
constexpr float kEventDistanceScale[] = { 1.0f, 1.0f, 2.0f, 0.8f, 1.2f, 1.0f, 1.2f, 2.0f };

void update_event_mix(int index, const SVector_* position) {
    if (index < 0 || index >= 16 || !position) return;
    NativeEvent& event = sEvents[index];
    const float scale = event.type < std::size(kEventDistanceScale)
                      ? kEventDistanceScale[event.type] : 1.0f;
    const float distance = std::sqrt(position->x * position->x
                                   + position->y * position->y
                                   + position->z * position->z);
    event.volume = 1.0f - std::min(1.0f, distance * scale);
    float angle = std::atan2(position->x, position->z);
    float pan = 0.5f - angle / 3.14159265358979323846f;
    if (pan > 1.0f) pan = 2.0f - pan;
    if (pan < 0.0f) pan = -pan;
    event.pan = std::clamp(pan * 2.0f - 1.0f, -1.0f, 1.0f);
    pc_audio_set_event_mix(static_cast<u8>(index), event.volume, event.pan);
}

void initialize_sound_handles() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    std::fill(std::begin(sSystemSounds), std::end(sSystemSounds), -1);
    std::fill(std::begin(sOrimaSounds), std::end(sOrimaSounds), -1);
    for (NativeEvent& event : sEvents)
        std::fill(std::begin(event.voices), std::end(event.voices), -1);
    for (NativeEvent& event : sEvents)
        std::fill(std::begin(event.actions), std::end(event.actions), -1);
}

void play_demo_stream(unsigned id) {
    if (id >= sizeof(kDemoStreams) / sizeof(kDemoStreams[0])) return;
    const std::string path = std::string("assets/dataDir/SndData/") + kDemoStreams[id];
    pc_audio_play_stx(path.c_str());
}

void start_demo_audio(u32 cinemaId) {
    const u8 config = demo_audio_config(cinemaId);
    const u8 audioId = config & 0x0F;
    if (audioId == 0 || (config & 0x40) != 0) return;
    if ((config & 0x80) != 0) {
        play_demo_stream(audioId);
    } else if (pc_audio_play_sequence_track(1, BGM_DemoBgm)) {
        pc_audio_write_sequence_port(1, 0, audioId);
        pc_audio_fade_sequence_track(1, 1.0f, 8);
        sDemoJamActive = true;
    }
}
}

#endif // !PIKI_USE_JAUDIO

extern "C" {

/* ── AI (from Dolphin/ai.h) ── */
AIDCallback AIRegisterDMACallback(AIDCallback callback)  { return pc_audio_register_dma_callback(callback); }
void AIInitDMA(u32 start_addr, u32 length)               { pc_audio_start_dma(start_addr, length); }
BOOL AIGetDMAEnableFlag(void)                             { return TRUE; }
void AIStartDMA(void)                                     { }
void AIStopDMA(void)                                      { pc_audio_stop_dma(); }
u32  AIGetDMABytesLeft(void)                              { return pc_audio_get_dma_bytes_left(); }
u32  AIGetDMAStartAddr(void)                              { return 0; }
u32  AIGetDMALength(void)                                 { return 0; }
u32  AIGetDSPSampleRate(void)                             { return 32000; }
void AISetDSPSampleRate(u32 rate)                         { (void)rate; }
AISCallback AIRegisterStreamCallback(AISCallback callback){ (void)callback; return nullptr; }
#if !PIKI_USE_JAUDIO
u32  AIGetStreamSampleCount(void)                         { return 0; }
void AIResetStreamSampleCount(void)                       { }
void AISetStreamTrigger(u32 trigger)                      { (void)trigger; }
u32  AIGetStreamTrigger(void)                             { return 0; }
void AISetStreamPlayState(u32 state)                      { (void)state; }
u32  AIGetStreamPlayState(void)                           { return 0; }
void AISetStreamSampleRate(u32 rate)                      { (void)rate; }
u32  AIGetStreamSampleRate(void)                          { return 32000; }
void AISetStreamVolLeft(u8 vol)                           { (void)vol; }
void AISetStreamVolRight(u8 vol)                          { (void)vol; }
u8   AIGetStreamVolLeft(void)                             { return 255; }
u8   AIGetStreamVolRight(void)                            { return 255; }
#endif
void AIInit(u8* stack) {
    (void)stack;
#if !PIKI_USE_JAUDIO
    pc_audio_init();
#endif
}
BOOL AICheckInit(void)                                    { return TRUE; }
void AIReset(void)                                        { }

/* ── DSP (from Dolphin/dsp.h) ── */
/* DSPCheckMail returns u32, not BOOL! */
#if !PIKI_USE_JAUDIO
// With JAudio active this comes from src/jaudio/dspboot.c. The mailbox
// stubs below stay in both builds: src/dsp/ is never compiled, and the
// host renderer deliberately produces no mailbox traffic.
void         DSPInit(void)                                { }
#endif
u32          DSPCheckMailToDSP(void)                      { return 0; }
u32          DSPCheckMailFromDSP(void)                    { return 0; }
u32          DSPReadMailFromDSP(void)                     { return 0; }
void         DSPSendMailToDSP(u32 mail)                   { (void)mail; }
void         DSPAssertInt(void)                           { }
DSPTaskInfo* DSPAddTask(DSPTaskInfo* task)                 { (void)task; return nullptr; }

} // extern "C"

#if !PIKI_USE_JAUDIO
// Everything below re-implements the JAudio public API on top of the
// native PC mixer in pc_port/audio. With PIKI_USE_JAUDIO=1 the real
// engine in src/jaudio provides these instead, driving DSPchannel_ voice
// parameter blocks that pc_dsp_host renders. The two cannot coexist:
// Jac_Start alone owns pc_audio_init and pc_audio_load_wave_bank.
/* ── JAudio high-level stubs ── */
extern "C" {
void Jac_Start(void* heap, u32 heapSize, u32 aramBase, const char* dataPath) {
    (void)heap; (void)heapSize; (void)aramBase; (void)dataPath;
    if (pc_audio_init()) {
        printf("[PC Port] Jac_Start() - Native SDL audio backend active\n");
        pc_audio_load_wave_bank("assets/dataDir/SndData/Banks/pikibank.bx");
    } else {
        printf("[PC Port Warning] Jac_Start() - continuing without an audio device\n");
    }
}
void Jac_Gsync(void) {
    if (sEventResumeFrames != 0 && --sEventResumeFrames == 0)
        apply_gameplay_audio_pause();
    if (!audio_demo_active() && !sMenuOrPauseActive) {
        for (u8 index = 0; index < std::size(sEvents); ++index) {
            NativeEvent& event = sEvents[index];
            if (!event.active || event.frameTimer == 0 || --event.frameTimer != 0) continue;
            for (u8 slot = 0; slot < std::size(event.actions); ++slot) {
                if (event.actions[slot] < 0) continue;
                pc_audio_send_event_action(index, slot, 0, true);
                event.actions[slot] = -1;
            }
        }
    }
}
void Jac_AddDVDBuffer(u8* buffer, u32 size) { (void)buffer; (void)size; }
void Jac_BackDVDBuffer() { }
void Jac_SceneSetup(u32 sceneID, u32 stageID) {
    static const u8 stageBgm[] = { BGM_Tutorial, BGM_Play3, BGM_Cave,
                                   BGM_Yakushima, BGM_Flow };
    if (sceneID == SCENE_ChalSelect) sChallengeMode = true;
    else if (sceneID == SCENE_WorldMap || sceneID == SCENE_Title) sChallengeMode = false;
    if (sceneID == SCENE_Course) sNativeStage = stageID;
    sNativeScene = sceneID;
    // Las tres banderas de pausa son cerrojos: las levanta un SE de sistema
    // (JACSYS_MenuOn/Pause/DVDPause) y solo las baja su pareja exacta. El menú
    // de pausa en juego emite SYSSE_PAUSE al abrirse pero solo emite
    // SYSSE_UNPAUSE por la salida "continuar": cualquier otra salida —ir a
    // guardar o cargar, por ejemplo— dejaba `sPauseActive` puesto, y con él
    // TODOS los eventos de SE en pausa el resto de la sesión. El juego seguía
    // corriendo y no había ni aviso ni caída: simplemente no volvía a sonar un
    // silbato ni un Pikmin. Entrar en una escena es, por definición, un punto
    // sin menús abiertos, así que aquí se reponen.
    //
    // Y son CINCO, no tres: `paused` en apply_gameplay_audio_pause() suma
    // además sPartsFindDemoActive y sTextDemoActive. Esos dos alimentan también
    // audio_demo_active(), que es la puerta con la que Jac_PlayOrimaSe silencia
    // todas las voces de Pikmin. Un "parts find" o un mensaje de texto que no
    // llegue a su Finish deja ambas puertas cerradas para el resto de la
    // partida, y entrar en una escena descarta por definición cualquier
    // cinemática de la anterior.
    sMenuActive = false;
    sPauseActive = false;
    sDVDPauseActive = false;
    sPartsFindDemoActive = false;
    sTextDemoActive = false;
    // sCurrentDemo NO se toca aqui. Se probo y no recupero ninguna voz, asi que
    // era riesgo sin beneficio: Jac_FinishDemo lo necesita para saber el modo de
    // fundido y las banderas de la cinematica que termina, y perderlo a mitad
    // dejaria la musica sin restaurar. Lo limpia Jac_SceneExit, que es donde
    // corresponde.
    sDemoEventPaused = false;
    sEventResumeFrames = 0;
    apply_gameplay_audio_pause();
    int bgm = -1;
    switch (sceneID) {
    case SCENE_Title: bgm = BGM_Jungle; break;
    case SCENE_FileSelect: bgm = BGM_Select; break;
    case SCENE_WorldMap: bgm = BGM_Map; break;
    case SCENE_Course:
        if (stageID < sizeof(stageBgm)) bgm = stageBgm[stageID];
        break;
    case SCENE_Results:
        if (stageID == JACRES_FinalResult) bgm = BGM_FinalResult;
        else if (sChallengeMode) bgm = BGM_ChalResult;
        break;
    case SCENE_ChalSelect: bgm = BGM_Char; break;
    default: break;
    }
    if (bgm >= 0 && !pc_audio_play_sequence(static_cast<u32>(bgm))) {
        printf("[PC Port Warning] Could not start native BGM sequence %d\n", bgm);
    } else if (bgm >= 0) {
        sNativeBgm = static_cast<u32>(bgm);
        sNativeBgmMode = 0;
        pc_audio_fade_sequence(sNativeBgmVolume, 0);
        apply_native_bgm_layers(0);
        if (sceneID == SCENE_Course && stageID != 0) {
            const u32 bossBgm = stageID == 4 ? BGM_Boss3 : BGM_Boss2;
            if (pc_audio_play_sequence_track(1, bossBgm))
                pc_audio_fade_sequence_track(1, 0.0f, 0);
        } else {
            pc_audio_stop_sequence_track(1);
        }
    } else if (bgm < 0) {
        pc_audio_stop_sequence();
    }
}
void Jac_SceneExit(u32 sceneID, u32 stageID) {
    (void)sceneID; (void)stageID;
    pc_audio_stop_sequence();
    pc_audio_stop_sequence_track(1);
    pc_audio_stop_stream();
    sKeepDemoStreamOnFinish = false;
    sCurrentDemo = -1;
    sDemoTimedEvent = kNoDemoTimedEvent;
    sDemoEventPaused = false;
    sEventResumeFrames = 0;
    // Mismo motivo que en Jac_SceneSetup: estos cerrojos sobreviven al cambio
    // de sección, así que un menú que no emitió su "off" envenenaba la escena
    // siguiente. Lo mismo vale para los dos cerrojos de cinemática: cierran
    // audio_demo_active() y con él todas las voces de Pikmin.
    sMenuActive = false;
    sPauseActive = false;
    sDVDPauseActive = false;
    sPartsFindDemoActive = false;
    sTextDemoActive = false;
    apply_gameplay_audio_pause();
}
u32 Jac_GetCurrentScene() { return sNativeScene; }
BOOL Jac_TellChgMode() { return sChallengeMode ? TRUE : FALSE; }
void Jac_PlaySystemSe(s32 seID) {
    if (seID < 0) return;
    if (seID == JACSYS_ContainerOK) {
        Jac_PlayOrimaSe(JACORIMA_Unk14);
        return;
    }
    if ((seID == JACSYS_OrimaLifeDim || seID == JACSYS_ViewChange)
        && (audio_demo_active() || sMenuOrPauseActive || sNativeScene != SCENE_Course))
        return;
    switch (seID) {
    case JACSYS_EveningAlert: Jac_SetBgmModeFlag(0, 2, 1); break;
    case JACSYS_MenuOn:
        Jac_SetBgmModeFlag(0, 4, 1);
        sMenuActive = true;
        apply_gameplay_audio_pause();
        break;
    case JACSYS_Pause:
        Jac_SetBgmModeFlag(0, 4, 1);
        sPauseActive = true;
        apply_gameplay_audio_pause();
        break;
    case JACSYS_DVDPause:
        Jac_SetBgmModeFlag(0, 4, 1);
        sDVDPauseActive = true;
        apply_gameplay_audio_pause();
        break;
    case JACSYS_MenuOff:
        Jac_SetBgmModeFlag(0, 4, 0);
        sMenuActive = false;
        apply_gameplay_audio_pause();
        break;
    case JACSYS_Unpause:
        Jac_SetBgmModeFlag(0, 4, 0);
        sPauseActive = false;
        apply_gameplay_audio_pause();
        if (seID == JACSYS_Unpause) sCountdownSounds = 0;
        break;
    case JACSYS_DVDUnpause:
        Jac_SetBgmModeFlag(0, 4, 0);
        sDVDPauseActive = false;
        apply_gameplay_audio_pause();
        break;
    case JACSYS_Countdown:
        if (++sCountdownSounds == 10) Jac_FadeOutBgm(0, 60);
        break;
    default: break;
    }
    pc_audio_send_system_se(static_cast<u16>(seID), false);
}
static int sFreeEvents = 16;
int Jac_CheckFreeEvents() { return sFreeEvents; }
BOOL Jac_DestroyEvent(s32 idx) {
    initialize_sound_handles();
    if (idx < 0 || idx >= 16 || !sEvents[idx].active) return FALSE;
    for (int& voice : sEvents[idx].voices) {
        if (voice >= 0) pc_audio_stop_wave(voice);
        voice = -1;
    }
    for (int slot = 0; slot < 16; ++slot) {
        if (sEvents[idx].actions[slot] >= 0)
            pc_audio_send_event_action(static_cast<u8>(idx), static_cast<u8>(slot), 0, true);
        sEvents[idx].actions[slot] = -1;
    }
    sEvents[idx].active = false;
    pc_audio_set_event_mix(static_cast<u8>(idx), 0.0f, 0.0f);
    if (sFreeEvents < 16) ++sFreeEvents;
    return TRUE;
}
void Jac_UpdateCamera(SVector_*, SVector_*) {
    bool closeBattle = false;
    for (int i = 0; i < 16; ++i) {
        if (!sEvents[i].active) continue;
        // Context positions are already camera-relative when they reach JAudio.
        update_event_mix(i, nullptr);
        if (sEvents[i].type == JACEVENT_Battle && sEvents[i].volume >= 0.8f)
            closeBattle = true;
    }
    Jac_SetBgmModeFlag(0, 1, closeBattle);
}
void Jac_InitBgm() {
    sNativeBgm = BGM_PikiSE;
    sNativeBgmMode = 0;
    sNativeBgmVolume = sNativeBgmPreviousVolume = 1.0f;
}
void Jac_FadeOutBgm(u32 trackIndex, u32 fadeFrames) {
    if (trackIndex == 0) {
        sNativeBgmMode |= 8;
        apply_native_bgm_layers(fadeFrames);
        pc_audio_fade_sequence(0.0f, fadeFrames);
    }
}
void Jac_StopBgm(u32 trackIndex) { pc_audio_stop_sequence_track(static_cast<u8>(trackIndex)); }
void Jac_ReadyBgm(u32) {}
void Jac_PlayBgm(u32 trackIndex, u32 bgmID) {
    if (trackIndex == 1) {
        if (pc_audio_play_sequence_track(1, bgmID))
            pc_audio_fade_sequence_track(1, 0.0f, 0);
        return;
    }
    if (trackIndex != 0 || !pc_audio_play_sequence(bgmID)) return;
    sNativeBgm = bgmID;
    sNativeBgmMode = 0;
    pc_audio_fade_sequence(sNativeBgmVolume, 0);
    apply_native_bgm_layers(0);
}
BOOL Jac_ChangeBgmMode(u32 trackIndex, u8 modeFlags) {
    if (trackIndex != 0 || modeFlags == sNativeBgmMode) return FALSE;
    sNativeBgmMode = modeFlags;
    apply_native_bgm_layers((modeFlags & 8) ? 30 : 60);
    return TRUE;
}
void Jac_SetBgmModeFlag(u32 trackIndex, u8 flagMask, u8 enabled) {
    if (trackIndex != 0) return;
    const u8 next = enabled ? static_cast<u8>(sNativeBgmMode | flagMask)
                            : static_cast<u8>(sNativeBgmMode & ~flagMask);
    Jac_ChangeBgmMode(trackIndex, next);
}
void Jac_BgmFrameWork() {}
void Jac_MoveBgmTrackVol(BgmControl_*) {}
void Jac_ChangeBgmTrackVol(BgmControl_*) {}
void Jac_GameVolume(u8 bgmLevel, u8 seLevel) {
    static constexpr u16 seVolumes[] = {
        0, 1000, 2000, 4000, 7000, 10000, 13000, 16000, 20000, 25000, 0x7FFF
    };
    static constexpr u16 demoVolumes[] = {
        0, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 10000, 12000
    };
    static constexpr u16 streamVolumes[] = {
        0, 600, 1000, 2000, 3000, 4000, 5000, 6000, 8000, 10000, 12000
    };
    bgmLevel = std::min<u8>(bgmLevel, PIKI_JAC_VOLUME_STEPS);
    seLevel = std::min<u8>(seLevel, PIKI_JAC_VOLUME_STEPS);
    sNativeBgmVolume = bgmLevel / 10.0f;
    pc_audio_fade_sequence(sNativeBgmVolume, 10);
    pc_audio_write_sequence_port(1, 2, demoVolumes[bgmLevel]);
    pc_audio_set_bus_volume(PC_AUDIO_BUS_STREAM, streamVolumes[bgmLevel] / 12000.0f);
    pc_audio_set_bus_volume(PC_AUDIO_BUS_SE, seVolumes[seLevel] / 32767.0f);
}
void Jac_EasyCrossFade(u8 crossfadeMode, u32 fadeFrames) {
    pc_audio_fade_sequence_track(0, crossfadeMode == 1 ? 0.0f : sNativeBgmVolume,
                                 fadeFrames);
    pc_audio_fade_sequence_track(1, crossfadeMode == 1 ? sNativeBgmVolume : 0.0f,
                                 fadeFrames);
}
void Jac_DemoFade(u8 fadeType, u32 fadeFrames, f32 volumeScale) {
    switch (fadeType) {
    case 0: sNativeBgmVolume = sNativeBgmPreviousVolume; break;
    case 1:
        sNativeBgmPreviousVolume = sNativeBgmVolume;
        sNativeBgmVolume *= volumeScale;
        break;
    case 2: sNativeBgmVolume = sNativeBgmPreviousVolume * volumeScale; break;
    case 3: sNativeBgmVolume = volumeScale; break;
    }
    pc_audio_fade_sequence(sNativeBgmVolume, fadeFrames);
}
void Jac_ExitBossMode() { Jac_EasyCrossFade(0, 100); }
void Jac_EnterBossMode() {
    // Normally track 1 has been advancing silently since scene setup. Recover
    // it lazily if a demo/transition or earlier sequence error removed it.
    if (!pc_audio_sequence_track_active(1)) {
        const u32 bossBgm = sNativeStage == 4 ? BGM_Boss3 : BGM_Boss2;
        if (pc_audio_play_sequence_track(1, bossBgm))
            pc_audio_fade_sequence_track(1, 0.0f, 0);
    }
    Jac_EasyCrossFade(1, 100);
}
void Jac_StopSystemSe(s32 id) {
    if (id == JACSYS_ContainerOK) {
        Jac_StopOrimaSe(JACORIMA_Unk14);
    } else if (id >= 0) {
        pc_audio_send_system_se(static_cast<u16>(id), true);
    }
}
void Jac_PlayOrimaSe(u32 id) {
    if ((id & JACORIMA_PIKISOUND) == 0 && id == JACORIMA_Gather) {
        if (sMenuOrPauseActive || audio_demo_active()) return;
        if (sWhistleVoice >= 0) pc_audio_stop_wave(sWhistleVoice);
        // pikise.jam resolves Gather to this sustained note. Playing it
        // directly removes the frame-order race between its queued play/stop
        // ports while retaining the original IBNK/WSYS sample and envelope.
        sWhistleVoice = pc_audio_play_note(0, 0, 17, 127, 0, 1.0f, 0.0f,
                                           PC_AUDIO_BUS_SE, 112, 1.0f);
        return;
    }
    if (id & JACORIMA_PIKISOUND) {
        if (audio_demo_active()
            || (sPikiFlyReady && id == JACORIMA_PikiFlyReady))
            return;
        pc_audio_send_orima_se(static_cast<u16>(id & 0x7FFF), false, true);
        if (id == JACORIMA_PikiFly) sPikiFlyReady = false;
        else if (id == JACORIMA_PikiFlyReady) sPikiFlyReady = true;
        return;
    }
    pc_audio_send_orima_se(static_cast<u16>(id), false, false);
    if (id == JACORIMA_PikiPulled2) {
        const u16 variation = static_cast<u16>(next_voice_variation(4) & 3);
        u16 sound = variation == 3 ? 0x000B : static_cast<u16>(0x000D + variation);
        if (sPulledVoiceHistory[2] == sPulledVoiceHistory[1]
            && sPulledVoiceHistory[1] == sPulledVoiceHistory[0]) {
            sound = sPulledVoiceHistory[0] != variation
                ? static_cast<u16>(0x0010 + variation) : 0x0014;
            sPulledVoiceHistory[0] = 5;
        } else {
            sPulledVoiceHistory[2] = sPulledVoiceHistory[1];
            sPulledVoiceHistory[1] = sPulledVoiceHistory[0];
            sPulledVoiceHistory[0] = variation;
        }
        pc_audio_send_orima_se(sound, false, true);
    }
    if (id == JACORIMA_PlayerDown) {
        pc_audio_fade_sequence_track(0, 0.0f, 100);
        pc_audio_fade_sequence_track(1, 0.0f, 100);
        Jac_PlaySystemSe(JACSYS_Unk40);
    }
}
void Jac_StopOrimaSe(s32 id) {
    if ((id & JACORIMA_PIKISOUND) == 0 && id == JACORIMA_Gather) {
        if (sWhistleVoice >= 0) pc_audio_release_wave(sWhistleVoice, 1600, 30);
        sWhistleVoice = -1;
        return;
    }
    if ((id & JACORIMA_PIKISOUND) == 0) {
        pc_audio_send_orima_se(static_cast<u16>(id), true, false);
    }
}
// The volume preferences are 0..10 sliders, not 0..127: ogTitle steps them with
// `if (mBgmVol < 10) mBgmVol++`, and the scene-volume path above clamps the same
// level to 10 before indexing its tables. Dividing by 127 left both buses at
// 10/127 = 0.079 at maximum, so sequenced gameplay music peaked around 1000 of
// 32767 while streamed cinematic audio, which only passes through the STREAM
// bus, reached 32426 -- the ~13x imbalance between in-game and cutscene music.
static float jac_slider_gain(u8 level)
{
	return std::min<u8>(level, PIKI_JAC_VOLUME_STEPS) / float(PIKI_JAC_VOLUME_STEPS);
}
void Jac_SetBGMVolume(u8 volume) { pc_audio_set_bus_volume(PC_AUDIO_BUS_BGM, jac_slider_gain(volume)); }
void Jac_SetSEVolume(u8 volume) { pc_audio_set_bus_volume(PC_AUDIO_BUS_SE, jac_slider_gain(volume)); }
void Jac_OutputMode(int mode) { pc_audio_set_stereo(mode != 0); }
void Jac_SetDemoPartsID(int id) { sDemoPartsId = static_cast<u8>(std::clamp(id, 0, 31)); }
void Jac_SetDemoOnyons(int count) { sDemoOnyonCount = static_cast<u8>(std::clamp(count, 0, 3)); }
void Jac_SetDemoPartsCount(int count) { sDemoPartsCount = static_cast<u8>(std::clamp(count, 0, 30)); }
void Jac_StartDemo(u32 cinemaId) {
    // Audio configurations with bit 7 set in JAudio's DEMO_STATUS table are
    // streamed STX tracks. Route the known cinematics directly to SDL while
    // the original sequence/DSP engine remains disabled.
    (void)sDemoPartsId;
    (void)sDemoOnyonCount;
    (void)sDemoPartsCount;
    printf("[DEBUG] Jac_StartDemo(%u) called\n", cinemaId);
    const u8 fadeMode = cinemaId < std::size(kDemoBgmFadeMode)
        ? kDemoBgmFadeMode[cinemaId] : 1;
    printf("[DEBUG] fadeMode=%u (0=stop all BGM immediately)\n", fadeMode);
    switch (fadeMode) {
    case 0:
        printf("[DEBUG] Stopping BGM tracks 0 and 1\n");
        pc_audio_stop_sequence();
        pc_audio_stop_sequence_track(1);
        printf("[DEBUG] BGM tracks stopped\n");
        break;
    case 2: Jac_DemoFade(1, 15, 0.01f); break;
    case 3: Jac_DemoFade(1, 8, 0.0f); break;
    case 4:
        pc_audio_fade_sequence_track(0, 0.0f, 15);
        pc_audio_fade_sequence_track(1, 0.0f, 15);
        break;
    case 5: Jac_DemoFade(1, 30, 0.5f); break;
    default: break;
    }
    const u8 gameplayFlags = cinemaId < std::size(kDemoGameplayFlags)
        ? kDemoGameplayFlags[cinemaId] : 0;
    sDemoEventPaused = gameplayFlags == 1;
    sEventResumeFrames = 0;
    if (gameplayFlags == 2) {
        Jac_InitAllEvent();
        Jac_Orima_Formation(0, 0);
    }
    apply_gameplay_audio_pause();
    sCurrentDemo = static_cast<int>(cinemaId);
    const s16 timedOffset = cinemaId < std::size(kDemoTimedOffsets)
        ? kDemoTimedOffsets[cinemaId] : -1;
    sDemoTimedEvent = timedOffset >= 0 ? static_cast<size_t>(timedOffset)
                                      : kNoDemoTimedEvent;
    sKeepDemoStreamOnFinish = cinemaId == 1;
    pc_audio_write_se_port(15, 0, static_cast<u16>(cinemaId));
    pc_audio_write_se_port(15, 1, static_cast<u16>(cinemaId));
    if (sDemoTimedEvent == kNoDemoTimedEvent) start_demo_audio(cinemaId);
}
void Jac_DemoSound(int id) { if (id >= 0) pc_audio_write_se_port(15, 2, static_cast<u16>(id)); }
BOOL Jac_DemoFrame(int frame) {
    if (sCurrentDemo < 0) return FALSE;
    while (sDemoTimedEvent != kNoDemoTimedEvent
           && sDemoTimedEvent + 1 < std::size(kDemoTimedData)
           && frame >= kDemoTimedData[sDemoTimedEvent]) {
        const s16 event = kDemoTimedData[sDemoTimedEvent + 1];
        sDemoTimedEvent += 2;
        if (event >= 0) {
            Jac_DemoSound(event);
        } else if (event == -1) {
            Jac_FinishDemo();
            return TRUE;
        } else if (event == -2 || event == -3) {
            sDemoTimedEvent = kNoDemoTimedEvent;
        } else if (event == -4) {
            const u8 config = demo_audio_config(static_cast<u32>(sCurrentDemo));
            if (config != 0 && (config & 0x20) == 0) {
                if ((config & 0x80) != 0) pc_audio_stop_stream();
                else pc_audio_stop_sequence_track(1);
            }
        } else if (event == -5) {
            Jac_DemoFade(2, 70, 0.5f);
        } else if (event == -6) {
            start_demo_audio(static_cast<u32>(sCurrentDemo));
        }
        // -7 only preloaded an asynchronously loaded bank on GameCube. Native
        // asset loading is synchronous, so it intentionally has no action.
    }
    return TRUE;
}
void Jac_FinishDemo() {
    const u32 finishedDemo = sCurrentDemo >= 0
        ? static_cast<u32>(sCurrentDemo) : UINT32_MAX;
    const u8 fadeMode = finishedDemo < std::size(kDemoBgmFadeMode)
        ? kDemoBgmFadeMode[finishedDemo] : 1;
    if (fadeMode == 2 || fadeMode == 3 || fadeMode == 5)
        Jac_DemoFade(0, 70, 1.0f);
    const u8 gameplayFlags = finishedDemo < std::size(kDemoGameplayFlags)
        ? kDemoGameplayFlags[finishedDemo] : 0;
    if (gameplayFlags == 1) sEventResumeFrames = 6;
    else if (gameplayFlags == 2) Jac_Orima_Formation(0, 0);
    sDemoEventPaused = false;
    apply_gameplay_audio_pause();
    if (!sKeepDemoStreamOnFinish) {
        // La musica de las cinematicas es un stream (.stx), y hasta ahora se
        // cortaba en seco: Jac_DemoFade solo funde la secuencia, nunca el
        // stream. Medio segundo basta para quitar el corte sin que la pista se
        // solape con la musica de la escena siguiente. Medio segundo se probo
        // en juego y seguia sonando seco.
        constexpr u32 kDemoStreamFadeFrames = 90; // 1,5 s a 60 Hz
        pc_audio_fade_stream(kDemoStreamFadeFrames);
    }
    sKeepDemoStreamOnFinish = false;
    sCurrentDemo = -1;
    sDemoTimedEvent = kNoDemoTimedEvent;
    if (sDemoJamActive) {
        pc_audio_stop_sequence_track(1);
        if (sNativeScene == SCENE_Course && sNativeStage != 0) {
            const u32 bossBgm = sNativeStage == 4 ? BGM_Boss3 : BGM_Boss2;
            if (pc_audio_play_sequence_track(1, bossBgm))
                pc_audio_fade_sequence_track(1, 0.0f, 0);
        }
        sDemoJamActive = false;
    }
}
void Jac_PrepareDemo(u32) {}
void Jac_StartPartsFindDemo(u32 jingleType, BOOL hasAudio) {
    if (sPartsFindDemoActive) {
        if (hasAudio) Jac_PlaySystemSe(JACSYS_Unk30);
        return;
    }
    Jac_Orima_Formation(0, 0);
    if (hasAudio) {
        Jac_DemoFade(1, 15, 0.1f);
        Jac_PlaySystemSe(jingleType == 0 ? JACSYS_Unk36 : JACSYS_Unk30);
    } else {
        Jac_DemoFade(1, 30, 0.5f);
        Jac_PlaySystemSe(JACSYS_Unk31);
    }
    sPartsFindDemoActive = true;
    sEventResumeFrames = 0;
    apply_gameplay_audio_pause();
}
void Jac_StartTextDemo(int) {
    // pikidemo.c exige tres condiciones, no dos: text_demo_state != 1,
    // parts_find_demo_state == 0 y **current_demo_no == DEMOID_FINISHED**, es
    // decir, que no haya ninguna cinemática en curso.
    //
    // Sin esa tercera, un mensaje de texto que aparezca durante una cinemática
    // ejecuta su Jac_DemoFade(1, ..) y guarda como «volumen anterior» el que la
    // cinemática ya había atenuado —a veces cero—. Al terminar la cinemática,
    // el Jac_DemoFade(0, ..) de Jac_FinishDemo restaura ese valor y la música
    // se queda baja o muda de forma permanente.
    if (sTextDemoActive || sPartsFindDemoActive || sCurrentDemo >= 0) return;
    Jac_Orima_Formation(0, 0);
    Jac_DemoFade(1, 30, 0.5f);
    sTextDemoActive = true;
    sEventResumeFrames = 0;
    apply_gameplay_audio_pause();
}
void Jac_FinishPartsFindDemo() {
    if (!sPartsFindDemoActive) return;
    Jac_DemoFade(0, 70, 1.0f);
    sPartsFindDemoActive = false;
    sEventResumeFrames = 3;
    apply_gameplay_audio_pause();
}
void Jac_FinishTextDemo() {
    // Mismo guardado que en el arranque: si una cinemática empezó mientras el
    // mensaje seguía activo, la restauración del volumen es suya, no nuestra.
    // Restaurarlo aquí pisaría el volumen que la cinemática tiene guardado.
    if (!sTextDemoActive || sPartsFindDemoActive || sCurrentDemo >= 0) return;
    Jac_DemoFade(0, 70, 1.0f);
    sTextDemoActive = false;
    sEventResumeFrames = 3;
    apply_gameplay_audio_pause();
}
void Jac_Freeze_Precall() {
    // Mirrors AllStop_1Shot + FlushRelease_1Shot without destroying the
    // persistent BGM/stream state before the reset path has committed.
    pc_audio_stop_bus(PC_AUDIO_BUS_SE);
}
void Jac_Freeze() { pc_audio_stop_dma(); }
void Jac_Orima_Walk(s32 groundSoundID, u32) {
    // Original handle 0x10008: alternate writes are intentional; the JAM
    // track selects the correct left/right foot variation itself.
    sPikiGayaTimer = 0;
    if (sNativeScene == SCENE_Course
        && (!audio_demo_active() || landing_demo_active()))
        pc_audio_write_se_port(8, 0, static_cast<u16>(groundSoundID));
}
void Jac_Orima_Formation(s32 stickX, s32 stickY) {
    static bool active = false;
    if (audio_demo_active() || sMenuOrPauseActive) stickX = stickY = 0;
    stickX = std::clamp(stickX, -127, 127);
    stickY = std::clamp(stickY, -127, 127);
    const int magnitude = static_cast<int>(
        std::sqrt(static_cast<float>(stickX * stickX + stickY * stickY)));
    pc_audio_write_se_port(7, 2, static_cast<u16>(stickX));
    pc_audio_write_se_port(7, 3, static_cast<u16>(magnitude));
    if (!active && magnitude != 0) {
        pc_audio_write_se_port(7, 0, 1);
        active = true;
        sPikiGayaTimer = 0;
    } else if (active && magnitude == 0) {
        pc_audio_write_se_port(7, 0, 0);
        active = false;
    }
}
void Jac_StopSe(s32) {}
void Jac_Piki_Number(u32 pikiNum) {
    if (pikiNum >= 100) sPikiGayaLevel = 29;
    else if (pikiNum >= 50) sPikiGayaLevel = (pikiNum - 50) / 10 + 25;
    else if (pikiNum >= 25) sPikiGayaLevel = (pikiNum - 25) / 5 + 20;
    else if (pikiNum >= 15) sPikiGayaLevel = (pikiNum - 15) / 2 + 15;
    else sPikiGayaLevel = pikiNum;
}
// An action that has run its course frees its slot; without this the sixteen
// slots fill up and later sounds start displacing live ones.
// Traces one gameplay sound from the game's request to the sequencer, so a
// sound that is not heard can be told apart at the point it goes missing:
// never asked for, refused for want of an event, or sent and still silent.
static u64 audio_now_ms();

static void trace_event_action(int index, int action, const char* what,
                               u32 type, u16 command, int slot) {
    static const bool enabled = [] {
        const char* value = getenv("PIKMIN_AUDIO_STATS");
        return value != nullptr && value[0] == '1';
    }();
    if (!enabled) return;
    // One line per (type, action, outcome), not a flat cap: a sound the ship
    // repeats every second would otherwise fill the log before the one being
    // investigated ever happens.
    static const bool traceAll = [] {
        const char* value = getenv("PIKMIN_AUDIO_TRACE_ALL");
        return value != nullptr && value[0] == '1';
    }();
    // The ship's ambience alone is four lines a frame at volume zero, which
    // buries the handful of lines an investigation is actually about.
    // PIKMIN_AUDIO_TRACE_TYPE takes a comma-separated list of JACEVENT types
    // (6 = piki voices); unset means every type, as before.
    static const u32 typeMask = [] {
        const char* value = getenv("PIKMIN_AUDIO_TRACE_TYPE");
        if (!value || !value[0]) return 0xFFFFFFFFu;
        u32 mask = 0;
        for (const char* cursor = value; *cursor;) {
            if (*cursor < '0' || *cursor > '9') { ++cursor; continue; }
            u32 parsed = 0;
            while (*cursor >= '0' && *cursor <= '9') parsed = parsed * 10 + u32(*cursor++ - '0');
            if (parsed < 32) mask |= 1u << parsed;
        }
        return mask ? mask : 0xFFFFFFFFu;
    }();
    // Type 0 is what the failure paths report when the event is not even
    // active; a filter must never hide the case where a sound never got out.
    if (type != 0 && type < 32 && !(typeMask & (1u << type))) return;
    static std::set<u32> seen;
    const u32 key = (type << 20) | ((action & 0x3FF) << 10)
                  | static_cast<u32>(what[0] & 0x3F);
    if (!traceAll && !seen.insert(key).second) return;
    const float volume = (index >= 0 && index < 16) ? sEvents[index].volume : -1.0f;
    const float pan = (index >= 0 && index < 16) ? sEvents[index].pan : 0.0f;
    // Ordering alone cannot say whether a dropped sound arrived 30 ms or 600 ms
    // into the sound that displaced it, and that difference is the whole
    // question when a voice group only allows one at a time. The stamp is the
    // raw steady clock so it lines up with probes outside the audio code.
    printf("[PC Audio] reloj=%llu  evento %d tipo %u accion %d -> %s (comando 0x%03X, ranura %d, "
           "volumen %.3f, paneo %+.2f, eventos libres %d)\n",
           static_cast<unsigned long long>(audio_now_ms()),
           index, type, action, what, command, slot, volume, pan, sFreeEvents);
}

static u64 audio_now_ms() {
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

static void event_action_finished(u8 event, u8 slot) {
    if (event >= 16 || slot >= 16) return;
    const u64 held = sEvents[event].startedMs[slot]
                   ? audio_now_ms() - sEvents[event].startedMs[slot] : 0;
    char label[64];
    std::snprintf(label, sizeof label, "TERMINADA tras %llu ms",
                  static_cast<unsigned long long>(held));
    trace_event_action(event, sEvents[event].actions[slot], label,
                       sEvents[event].type, 0, slot);
    sEvents[event].actions[slot] = -1;
}

BOOL Jac_PlayEventAction(int index, int action) {
    if (index < 0 || index >= 16 || !sEvents[index].active) {
        trace_event_action(index, action, "SIN EVENTO ACTIVO", 0, 0, -1);
        return FALSE;
    }
    NativeEvent& event = sEvents[index];
    u16 command;
    PCEventStatus status;
    size_t statusIndex;
    if (!pc_event_command(event.type, action, command)
        || !pc_event_status(event.type, action, status, statusIndex)) {
        trace_event_action(index, action, "SIN COMANDO EN LA TABLA", event.type, 0, -1);
        return FALSE;
    }

    // Jac_PlayEventAction, src/jaudio/pikiinter.c. Slot choice is not "first
    // free": it is governed by the action's group and priority, and a request
    // that cannot win a slot is dropped.
    ++sEventClock;
    int usedSlots = 0;
    while (usedSlots < 16 && event.actions[usedSlots] >= 0) ++usedSlots;
    int targetSlot = usedSlots;

    if (status.flags & 0x10) {
        u32 maximum = status.flags & 0x0F;
        if (maximum == 0) maximum = 16;
        u32 concurrent = 0;
        u8 lowestPriority = status.priority;
        int oldestSlot = 0;
        u32 oldestAge = 0;
        for (int i = 0; i < 16; ++i) {
            if (event.actions[i] < 0) continue;
            if (event.groups[i] == status.group) ++concurrent;
            const u8 slotPriority = event.priorities[i];
            if (lowestPriority > slotPriority) {
                oldestAge = sEventClock - event.timestamps[i];
                oldestSlot = i;
                lowestPriority = slotPriority;
            } else if (lowestPriority == slotPriority) {
                const u32 age = sEventClock - event.timestamps[i];
                if (age > oldestAge) { oldestAge = age; oldestSlot = i; }
            }
        }
        if (concurrent >= maximum || targetSlot == 16) {
            if (status.flags & 0x20) {
                trace_event_action(index, action, "DESCARTADA (grupo lleno)",
                                   event.type, command, -1);
                return FALSE;
            }
            targetSlot = oldestSlot;
        }
    } else {
        int firstFree = 16;
        int i = 0;
        for (; i < 16; ++i) {
            if (event.actions[i] < 0) { firstFree = i; continue; }
            if (event.groups[i] != status.group) continue;
            if (status.flags & 0x20) {
                if (status.priority > event.priorities[i]) {
                    targetSlot = i;
                } else {
                    // Name the holder and its age: "dropped on priority" is the
                    // rule working as designed, so what matters is whether the
                    // request was late by 20 ms or by half a second.
                    const u64 heldFor = event.startedMs[i]
                                      ? audio_now_ms() - event.startedMs[i] : 0;
                    char label[96];
                    std::snprintf(label, sizeof label,
                                  "DESCARTADA (prioridad; ranura %d la tiene la accion %d "
                                  "desde hace %llu ms)",
                                  i, event.actions[i],
                                  static_cast<unsigned long long>(heldFor));
                    trace_event_action(index, action, label, event.type, command, -1);
                    return FALSE;
                }
            } else {
                targetSlot = i;
            }
            break;
        }
        if (i == 16) targetSlot = firstFree;
    }

    if (targetSlot == 16) {
        u8 lowest = static_cast<u8>(status.priority + 1);
        for (int i = 0; i < 16; ++i) {
            if (event.actions[i] < 0) continue;
            if (event.priorities[i] < lowest) {
                lowest = event.priorities[i];
                targetSlot = i;
            }
        }
        if (targetSlot == 16) {
            trace_event_action(index, action, "DESCARTADA (sin ranura)",
                               event.type, command, -1);
            return FALSE;
        }
    }

    if (!pc_audio_send_event_action(static_cast<u8>(index),
                                    static_cast<u8>(targetSlot), command, false)) {
        trace_event_action(index, action, "EL SECUENCIADOR LO RECHAZA",
                           event.type, command, targetSlot);
        return FALSE;
    }
    trace_event_action(index, action, "enviado", event.type, command, targetSlot);
    event.actions[targetSlot] = action;
    event.groups[targetSlot] = status.group;
    event.priorities[targetSlot] = status.priority;
    event.timestamps[targetSlot] = sEventClock;
    event.startedMs[targetSlot] = audio_now_ms();
    return TRUE;
}
BOOL Jac_StopEventAction(int index, int action) {
    if (index < 0 || index >= 16 || action < 0 || !sEvents[index].active) return FALSE;
    for (int slot = 0; slot < 16; ++slot) {
        if (sEvents[index].actions[slot] != action) continue;
        pc_audio_send_event_action(static_cast<u8>(index), static_cast<u8>(slot), 0, true);
        sEvents[index].actions[slot] = -1;
    }
    return TRUE;
}
BOOL Jac_UpdateEventPosition(int index, SVector_* position) {
    if (index < 0 || index >= 16 || !sEvents[index].active || !position) return FALSE;
    sEvents[index].frameTimer = 100;
    update_event_mix(index, position);
    return TRUE;
}
void Jac_InitAllEvent() {
    initialize_sound_handles();
    pc_audio_set_event_action_finished_hook(event_action_finished);
    for (int index = 0; index < 16; ++index) {
        NativeEvent& event = sEvents[index];
        for (int& voice : event.voices) {
            if (voice >= 0) pc_audio_stop_wave(voice);
            voice = -1;
        }
        for (int slot = 0; slot < 16; ++slot) {
            if (event.actions[slot] >= 0)
                pc_audio_send_event_action(static_cast<u8>(index), static_cast<u8>(slot), 0, true);
        }
        event.active = false;
        std::fill(std::begin(event.actions), std::end(event.actions), -1);
        pc_audio_set_event_mix(static_cast<u8>(index), 0.0f, 0.0f);
    }
    sFreeEvents = 16;
}
int Jac_CreateEvent(u32 type, SVector_* position) {
    initialize_sound_handles();
    if (type == JACEVENT_NULL || type >= std::size(kEventDistanceScale)) {
        trace_event_action(-1, -1, "TIPO DE EVENTO FUERA DE RANGO", type, 0, -1);
        return -1;
    }
    if (sCurrentDemo == DEMOID_GenericDayEnd
        || sCurrentDemo == DEMOID_DayEndPractice
        || sCurrentDemo == DEMOID_DayEndForest
        || sCurrentDemo == DEMOID_DayEndCaveLast
        || sCurrentDemo == DEMOID_DayEndYakushima) return -1;
    if (sCurrentDemo >= 0
        && static_cast<size_t>(sCurrentDemo) < std::size(kDemoGameplayFlags)
        && kDemoGameplayFlags[sCurrentDemo] == 2) return -1;
    for (int i = 0; i < 16; ++i) {
        if (sEvents[i].active) continue;
        sEvents[i].active = true;
        sEvents[i].type = type;
        sEvents[i].frameTimer = 100;
        std::fill(std::begin(sEvents[i].actions), std::end(sEvents[i].actions), -1);
        update_event_mix(i, position);
        --sFreeEvents;
        if (type == JACEVENT_Ufo) Jac_PlayEventAction(i, 4);
        return i;
    }
    trace_event_action(-1, -1, "SIN RANURAS DE EVENTO LIBRES", type, 0, -1);
    return -1;
}
int Jac_GetActiveEvents(u32* eventIDs) {
    int count = 0;
    if (!eventIDs) return 0;
    for (u32 i = 0; i < 16; ++i)
        if (sEvents[i].active) eventIDs[count++] = i;
    return count;
}
} // extern "C"

void Jac_UpdatePikiGaya() {
    if (sNativeScene != SCENE_Course || audio_demo_active()) {
        sPikiGayaVolume = 0.0f;
        pc_audio_set_se_track_volume(3, 0.0f);
        return;
    }
    if (sPikiGayaTimer < 150 || sPikiGayaLevel < 1) {
        pc_audio_set_se_track_volume(3, sPikiGayaVolume);
        sPikiGayaVolume = std::max(0.0f, sPikiGayaVolume - 0.05f);
        if (sPikiGayaLevel == 0) pc_audio_write_se_port(3, 0, 0);
    } else if (sPikiGayaTimer >= 240) {
        pc_audio_write_se_port(3, 0, static_cast<u16>(sPikiGayaLevel));
        sPikiGayaVolume = std::min(1.0f, sPikiGayaVolume + 0.05f);
        pc_audio_set_se_track_volume(3, sPikiGayaVolume);
    }
    ++sPikiGayaTimer;
}

void Jac_PauseOrimaSe() {
    Jac_Orima_Formation(0, 0);
    if (sWhistleVoice >= 0) pc_audio_release_wave(sWhistleVoice, 1600, 30);
    sWhistleVoice = -1;
    pc_audio_set_se_track_paused(10, true);
}
void Jac_UnPauseOrimaSe() { apply_gameplay_audio_pause(); }

#endif // !PIKI_USE_JAUDIO

#if !PIKI_USE_JAUDIO
// H4M video needs the original audio engine underneath it: the file carries
// video and audio interleaved, and the audio half goes out through jaudio's
// stream path. With the old mixer there is nothing to hand it to, so the
// player is told there are no pictures and gives up cleanly. The real
// implementation is src/jaudio/app_inter.c, built with PIKMIN_NATIVE_JAUDIO.
extern "C" {
void Jac_StreamMovieUpdate() {}
void Jac_StreamMovieInit(const char*, u8*, int) {}
int Jac_StreamMovieGetPicture(void* pictureBuffer, int* widthOut, int* heightOut) {
    if (pictureBuffer) *static_cast<void**>(pictureBuffer) = nullptr;
    if (widthOut) *widthOut = 0;
    if (heightOut) *heightOut = 0;
    return -1;
}
void Jac_StreamMovieStop() {}
}
#endif

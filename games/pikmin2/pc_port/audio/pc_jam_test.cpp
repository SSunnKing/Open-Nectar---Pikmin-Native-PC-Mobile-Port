#include "audio/pc_jam.h"
#include "audio/pc_instrument_bank.h"
#include "audio/pc_event_commands.h"
#include "audio/pc_sequence_archive.h"
#include "audio/pc_wave_bank.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

extern "C" u8 HEAD_pikiseq[];

static bool writeWaveFile(const char* path, const std::vector<s16>& samples,
                          u32 sampleRate) {
    FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    const u32 dataSize = static_cast<u32>(samples.size() * sizeof(s16));
    const auto write16 = [file](u16 value) {
        const u8 bytes[] = { static_cast<u8>(value), static_cast<u8>(value >> 8) };
        return std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    };
    const auto write32 = [file](u32 value) {
        const u8 bytes[] = { static_cast<u8>(value), static_cast<u8>(value >> 8),
                             static_cast<u8>(value >> 16), static_cast<u8>(value >> 24) };
        return std::fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    };
    bool ok = std::fwrite("RIFF", 1, 4, file) == 4 && write32(36 + dataSize)
        && std::fwrite("WAVEfmt ", 1, 8, file) == 8 && write32(16)
        && write16(1) && write16(1) && write32(sampleRate)
        && write32(sampleRate * sizeof(s16)) && write16(sizeof(s16))
        && write16(16) && std::fwrite("data", 1, 4, file) == 4
        && write32(dataSize)
        && std::fwrite(samples.data(), sizeof(s16), samples.size(), file) == samples.size();
    ok = std::fclose(file) == 0 && ok;
    return ok;
}

int main(int argc, char** argv) {
    const bool renderSelectLong = argc == 2
        && std::string(argv[1]) == "--render-select-long";
    int failures = 0;
    {
        // bank/program 2/5, note 60 on voice 0 for two ticks, then off/end.
        const std::vector<u8> data = {
            0xAC, 0x06, 0x02, 0x05,
            60, 0x08, 100, 100, 0x02,
            0x81, 0xFF,
        };
        PCJamPlayer player;
        std::vector<PCJamEvent> events;
        if (!player.start(data) || player.tick(events) != PCJamResult::Ok
            || events.empty() || events[0].type != PCJamEventType::NoteOn
            || events[0].bank != 2 || events[0].program != 5
            || events[0].key != 60 || events[0].velocity != 100) ++failures;
        player.tick(events);
        for (const PCJamEvent& event : events)
            if (event.type != PCJamEventType::VoiceUpdate) ++failures;
        player.tick(events);
        bool sawNoteOff = false;
        for (const PCJamEvent& event : events)
            sawNoteOff |= event.type == PCJamEventType::NoteOff;
        if (!sawNoteOff || player.result() != PCJamResult::Finished) ++failures;
    }
    {
        // Closing a child track must release its sustained mixer voices.  The
        // original Jaq_CloseTrack always calls __AllNoteOff; omitting that is
        // what made looped FileSelect instruments survive as long beeps.
        const std::vector<u8> data = {
            0xC1, 0x00, 0x00, 0x00, 0x0A, // open child 0 at offset 10
            0x80, 0x02,                   // let the child play for two ticks
            0xDA, 0x00,                   // close child 0
            0xFF,                         // finish root
            60, 0x01, 100,                // sustained note on child voice 1
            0x80, 0x7F,                   // keep the child alive
        };
        PCJamPlayer player;
        std::vector<PCJamEvent> events;
        if (!player.start(data) || player.tick(events) != PCJamResult::Ok) {
            ++failures;
        }
        bool sawChildNote = false;
        for (const PCJamEvent& event : events)
            sawChildNote |= event.type == PCJamEventType::NoteOn
                         && event.track == 1 && event.voice == 1;
        player.tick(events);
        player.tick(events);
        bool releasedChildNote = false;
        for (const PCJamEvent& event : events)
            releasedChildNote |= event.type == PCJamEventType::NoteOff
                              && event.track == 1 && event.voice == 1;
        if (!sawChildNote || !releasedChildNote
            || player.result() != PCJamResult::Finished) ++failures;
    }
    {
        // Pausing a persistent child freezes its musical wait/note timers,
        // while other children and the root remain independently runnable.
        const std::vector<u8> data = {
            0xC1, 0x0A, 0x00, 0x00, 0x0A, // open player child 10
            0x80, 0x20,                   // keep root alive
            0xFF, 0xFF, 0xFF,             // padding to child offset
            60, 0x08, 100, 100, 0x02,     // two-tick note
            0x81, 0xFF,
        };
        PCJamPlayer player;
        std::vector<PCJamEvent> events;
        if (!player.start(data) || player.tick(events) != PCJamResult::Ok
            || !player.setChildPaused(10, true)) {
            ++failures;
        }
        for (int i = 0; i < 8; ++i) {
            player.tick(events);
            for (const PCJamEvent& event : events)
                if (event.type == PCJamEventType::NoteOff) ++failures;
        }
        if (!player.setChildPaused(10, false)) ++failures;
        bool released = false;
        for (int i = 0; i < 3; ++i) {
            player.tick(events);
            for (const PCJamEvent& event : events)
                released |= event.type == PCJamEventType::NoteOff;
        }
        if (!released) ++failures;
    }
    PCSequenceArchive archive;
    PCInstrumentBank instruments;
    PCWaveBank waves;
    if (!instruments.load("assets/dataDir/SndData/Banks/pikibank.bx")
        || !waves.load("assets/dataDir/SndData/Banks/pikibank.bx")) {
        std::printf("bank load failure\n");
        return 1;
    }
    if (!archive.load("assets/dataDir/SndData/Seqs/pikiseq.arc",
                      HEAD_pikiseq, 0x20 + 22 * 0x20)) {
        std::printf("archive failure: %s\n", archive.error().c_str());
        return 1;
    }
    {
        std::vector<u8> data;
        archive.read(1, data);
        PCJamPlayer eventPlayer;
        std::vector<PCJamEvent> eventEvents;
        eventPlayer.start(data, 0);
        eventPlayer.tick(eventEvents, 4096);
        u16 command = 0;
        size_t eventNotes = 0;
        if (!eventPlayer.childPortReady(0, 0) || !pc_event_command(1, 2, command)
            || !eventPlayer.writeChildPort(0, 0, static_cast<u16>(0x1000 | command))) {
            ++failures;
        }
        for (int tick = 0; tick < 2048 && eventPlayer.result() == PCJamResult::Ok; ++tick) {
            eventPlayer.tick(eventEvents, 4096);
            for (const PCJamEvent& event : eventEvents)
                if (event.type == PCJamEventType::NoteOn) ++eventNotes;
        }
        std::printf("gameplay event test: command=%03X notes=%zu result=%u opcode=%02X pc=%X\n",
                    command, eventNotes, static_cast<unsigned>(eventPlayer.result()),
                    eventPlayer.unsupportedOpcode(), eventPlayer.lastOpcodeAddress());
        if (eventNotes == 0 || eventPlayer.result() != PCJamResult::Ok) ++failures;
    }
    {
        std::vector<u8> data;
        archive.read(0, data);
        PCJamPlayer soundPlayer;
        std::vector<PCJamEvent> soundEvents;
        soundPlayer.start(data, 0);
        soundPlayer.tick(soundEvents, 4096);
        if (!soundPlayer.writeChildPort(9, 0, 1)) {
            std::printf("system SE track/port setup failure\n");
            ++failures;
        }
        size_t soundNotes = 0;
        for (int tick = 0; tick < 4096 && soundPlayer.result() == PCJamResult::Ok; ++tick) {
            soundPlayer.tick(soundEvents, 4096);
            for (const PCJamEvent& event : soundEvents)
                if (event.type == PCJamEventType::NoteOn) ++soundNotes;
        }
        std::printf("system SE port test: notes=%zu result=%u opcode=%02X\n",
                    soundNotes, static_cast<unsigned>(soundPlayer.result()),
                    soundPlayer.unsupportedOpcode());
        if (soundNotes == 0 || soundPlayer.result() != PCJamResult::Ok) ++failures;
    }
    {
        // A child track inherits its parent's bank and program at creation,
        // and nothing in the sequence restates them, so anything that reads
        // the bank from somewhere other than the inherited value silently
        // plays every instrument from bank 0. tutorial.jam is authored in
        // bank 4, which makes it the cheap witness for that whole class.
        std::vector<u8> data;
        archive.read(4, data);
        PCJamPlayer player;
        std::vector<PCJamEvent> events;
        player.start(data, 4);
        size_t notes = 0, wrongBank = 0;
        for (int tick = 0; tick < 256 && player.result() == PCJamResult::Ok; ++tick) {
            player.tick(events, 4096);
            for (const PCJamEvent& event : events) {
                if (event.type != PCJamEventType::NoteOn) continue;
                ++notes;
                if (event.bank != 4) ++wrongBank;
            }
        }
        std::printf("inherited bank test: %zu notes, %zu outside bank 4\n", notes, wrongBank);
        if (notes == 0 || wrongBank != 0) ++failures;
    }
    {
        // Every sound the effect sequencer can be asked for, on every track
        // that dispatches one. A sound id must never stop the sequence: when
        // it does, every later effect dies with it until the sequencer is
        // restarted. The id ranges are the dispatch table sizes read out of
        // pikise.jam; past those the sequence indexes unrelated bytes, which
        // the original would have done too.
        const std::pair<int, u16> sweeps[] = {
            { 0, 16 }, { 2, 16 }, { 7, 16 }, { 8, 16 }, { 9, 38 }, { 10, 15 },
        };
        size_t deaths = 0, sounded = 0;
        for (const auto& sweep : sweeps) {
            for (u16 id = 0; id < sweep.second; ++id) {
                std::vector<u8> data;
                archive.read(0, data);
                PCJamPlayer player;
                std::vector<PCJamEvent> events;
                player.start(data, 0);
                player.tick(events, 4096);
                if (!player.writeChildPort(static_cast<u8>(sweep.first), 0, id)) continue;
                size_t notes = 0;
                for (int tick = 0; tick < 256 && player.result() == PCJamResult::Ok; ++tick) {
                    player.tick(events, 4096);
                    for (const PCJamEvent& event : events)
                        if (event.type == PCJamEventType::NoteOn) ++notes;
                }
                if (player.result() != PCJamResult::Ok
                    && player.result() != PCJamResult::Finished) {
                    if (deaths == 0)
                        std::printf("SE sweep: track %d id %u stopped, result=%u at byte %u\n",
                                    sweep.first, id, static_cast<unsigned>(player.result()),
                                    player.lastOpcodeAddress());
                    ++deaths;
                } else if (notes != 0) {
                    ++sounded;
                }
            }
        }
        std::printf("SE sweep: %zu sounds played, %zu sequence deaths\n", sounded, deaths);
        if (deaths != 0 || sounded < 90) ++failures;
    }
    {
        // Player sound 8 is the sustained gather/whistle command. Verify the
        // original play(port 0), stop(port 2), replay(port 0) protocol.
        std::vector<u8> data;
        archive.read(0, data);
        PCJamPlayer soundPlayer;
        std::vector<PCJamEvent> soundEvents;
        soundPlayer.start(data, 0);
        soundPlayer.tick(soundEvents, 4096);
        size_t notes[3] = {};
        size_t noteOffs[3] = {};
        PCJamEvent firstWhistle {};
        const u8 ports[3] = { 0, 2, 0 };
        for (int phase = 0; phase < 3; ++phase) {
            if (!soundPlayer.writeChildPort(10, ports[phase], 8)) ++failures;
            for (int tick = 0; tick < 1024 && soundPlayer.result() == PCJamResult::Ok; ++tick) {
                soundPlayer.tick(soundEvents, 4096);
                for (const PCJamEvent& event : soundEvents) {
                    if (event.type == PCJamEventType::NoteOn) {
                        if (phase == 0 && notes[phase] == 0) firstWhistle = event;
                        ++notes[phase];
                    }
                    else if (event.type == PCJamEventType::NoteOff) ++noteOffs[phase];
                }
            }
        }
        std::printf("whistle play/stop/replay: on=%zu/%zu/%zu off=%zu/%zu/%zu result=%u opcode=%02X\n",
                    notes[0], notes[1], notes[2],
                    noteOffs[0], noteOffs[1], noteOffs[2],
                    static_cast<unsigned>(soundPlayer.result()),
                    soundPlayer.unsupportedOpcode());
        PCInstrumentSelection whistleSelection;
        const bool whistleResolved = instruments.select(
            firstWhistle.bank, firstWhistle.program, firstWhistle.key,
            firstWhistle.velocity, whistleSelection);
        std::printf("whistle note: track=%u voice=%u bank=%u program=%u key=%u velocity=%u vol=%.3f pan=%.3f pitch=%.3f resolved=%d\n",
                    firstWhistle.track, firstWhistle.voice, firstWhistle.bank,
                    firstWhistle.program, firstWhistle.key, firstWhistle.velocity,
                    firstWhistle.volume, firstWhistle.pan, firstWhistle.pitch,
                    whistleResolved ? 1 : 0);
        if (notes[0] == 0 || notes[2] == 0 || soundPlayer.result() != PCJamResult::Ok)
            ++failures;
    }
    {
        size_t orimaCommands = 0;
        size_t orimaNotes = 0;
        size_t orimaResolved = 0;
        for (u16 command = 0; command <= 0x17; ++command) {
            for (u8 port : { static_cast<u8>(0), static_cast<u8>(1) }) {
                std::vector<u8> data;
                archive.read(0, data);
                PCJamPlayer player;
                std::vector<PCJamEvent> events;
                player.start(data, 0);
                player.tick(events, 4096);
                if (!player.writeChildPort(10, port, command)) continue;
                ++orimaCommands;
                size_t commandNotes = 0;
                size_t commandResolved = 0;
                // Igual que en el barrido de eventos: un programa >= 0xF0 se
                // cuenta como "resuelto" pero pc_audio_play_note lo SILENCIA,
                // asi que hay que separarlo para saber que se oye de verdad.
                size_t commandOsc = 0;
                size_t commandUnresolved = 0;
                for (int tick = 0; tick < 1024 && player.result() == PCJamResult::Ok; ++tick) {
                    player.tick(events, 4096);
                    for (const PCJamEvent& event : events) {
                        if (event.type != PCJamEventType::NoteOn) continue;
                        ++orimaNotes;
                        ++commandNotes;
                        PCInstrumentSelection selection;
                        if (event.program >= 0xF0) ++commandOsc;
                        if (event.program >= 0xF0
                            || instruments.select(event.bank, event.program, event.key,
                                                  event.velocity, selection)) {
                            ++orimaResolved;
                            ++commandResolved;
                        } else {
                            ++commandUnresolved;
                        }
                    }
                }
                const char* verdict = commandNotes == 0 ? "  <== MUDO (sin notas)"
                    : commandNotes == commandOsc ? "  <== MUDO (solo osciladores)"
                    : commandNotes == commandUnresolved ? "  <== MUDO (no resuelve)"
                    : "";
                std::printf("  player port=%u command=%2u notes=%zu resolved=%zu osc=%zu%s\n",
                            port, command, commandNotes, commandResolved, commandOsc, verdict);
            }
        }
        std::printf("Olimar/Pikmin command coverage: commands=%zu notes=%zu resolved=%zu\n",
                    orimaCommands, orimaNotes, orimaResolved);
        if (orimaNotes == 0 || orimaResolved == 0) ++failures;
    }
    {
        // Exercise every system command independently.  Besides checking that
        // menu sounds resolve, report commands which still own a voice after
        // a long idle period; those are the likely source of audible "stuck"
        // tones in FileSelect.
        size_t totalNotes = 0;
        for (u16 command = 0; command <= 40; ++command) {
            std::vector<u8> data;
            archive.read(0, data);
            PCJamPlayer player;
            std::vector<PCJamEvent> events;
            player.start(data, 0);
            player.tick(events, 4096);
            if (!player.writeChildPort(9, 0, command)) continue;
            bool active[kPCJamTrackCount][8] = {};
            size_t notes = 0;
            size_t offs = 0;
            size_t oscillators = 0;
            size_t unresolved = 0;
            for (int tick = 0; tick < 4096 && player.result() == PCJamResult::Ok; ++tick) {
                player.tick(events, 4096);
                for (const PCJamEvent& event : events) {
                    if (event.type == PCJamEventType::NoteOff) {
                        ++offs;
                        active[event.track % kPCJamTrackCount][event.voice & 7] = false;
                        continue;
                    }
                    if (event.type != PCJamEventType::NoteOn) continue;
                    ++notes;
                    ++totalNotes;
                    active[event.track % kPCJamTrackCount][event.voice & 7] = true;
                    if (event.program >= 0xF0) {
                        ++oscillators;
                        continue;
                    }
                    PCInstrumentSelection selection;
                    if (!instruments.select(event.bank, event.program, event.key,
                                            event.velocity, selection))
                        ++unresolved;
                }
            }
            size_t activeCount = 0;
            for (const auto& track : active)
                for (bool voice : track) activeCount += voice ? 1 : 0;
            std::printf("  system command=%u on/off=%zu/%zu active=%zu osc=%zu unresolved=%zu result=%u\n",
                        command, notes, offs, activeCount, oscillators, unresolved,
                        static_cast<unsigned>(player.result()));
        }
        std::printf("system command coverage: notes=%zu\n", totalNotes);
        if (totalNotes == 0) ++failures;
    }
    {
        size_t eventCommands = 0;
        size_t eventNotes = 0;
        size_t eventResolved = 0;
        size_t eventFailures = 0;
        u8 firstEventFailureOpcode = 0;
        u32 firstFailureType = 0;
        size_t firstFailureAction = 0;
        unsigned firstFailureResult = 0;
        u8 firstUnsupportedEventOpcode = 0;
        size_t eventResults[8] = {};
        size_t totalZeroActions = 0;
        // Una accion es MUDA en juego si no produce notas, o si todas las que
        // produce son inaudibles: programa >= 0xF0 (oscilador del DSP, que
        // pc_audio_play_note silencia salvo con PIKMIN_OSC=1) o instrumento que
        // no resuelve. El recuento "resolved" de arriba trata los osciladores
        // como validos, asi que por si solo no dice que se oye.
        struct SilentAction { u32 type; size_t action; u16 command; const char* reason; };
        std::vector<SilentAction> silent;
        for (u32 type = 1; type <= 7; ++type) {
            size_t typeNotes = 0;
            size_t typeResolved = 0;
            size_t zeroNoteActions = 0;
            bool printedUnresolved = false;
            const size_t begin = kPCEventOffsets[type];
            const size_t end = type + 1 < std::size(kPCEventOffsets)
                ? kPCEventOffsets[type + 1] : std::size(kPCEventCommands);
            for (size_t action = 0; begin + action < end; ++action) {
                u16 command;
                if (!pc_event_command(type, static_cast<int>(action), command)) continue;
                // sysevent.jam, on a child other than 0: the game spreads its
                // sixteen concurrent events across all sixteen, so the sweep
                // must not silently depend on one of them being special.
                std::vector<u8> data;
                archive.read(1, data);
                PCJamPlayer player;
                std::vector<PCJamEvent> events;
                player.start(data, 1);
                player.tick(events, 4096);
                if (!player.writeChildPort(3, 0, static_cast<u16>(0x1000 | command))) continue;
                ++eventCommands;
                size_t actionNotes = 0;
                size_t actionOsc = 0;
                size_t actionUnresolved = 0;
                for (int tick = 0; tick < 1024 && player.result() == PCJamResult::Ok; ++tick) {
                    // Sustained actions (carrying, digging) loop until the game
                    // stops them; leaving them running is the sweep's artefact,
                    // not the sequence's fault.
                    if (tick == 64) player.writeChildPort(3, 0, 0x1000);
                    player.tick(events, 4096);
                    for (const PCJamEvent& event : events) {
                        if (event.type != PCJamEventType::NoteOn) continue;
                        ++eventNotes;
                        ++typeNotes;
                        ++actionNotes;
                        PCInstrumentSelection selection;
                        if (event.program >= 0xF0) ++actionOsc;
                        if (event.program >= 0xF0
                            || instruments.select(event.bank, event.program, event.key,
                                                  event.velocity, selection)) {
                            ++eventResolved;
                            ++typeResolved;
                        } else if (++actionUnresolved, !printedUnresolved) {
                            std::printf("  first unresolved event type=%u action=%zu command=%03X bank=%u program=%u key=%u\n",
                                        type, action, command, event.bank,
                                        event.program, event.key);
                            printedUnresolved = true;
                        }
                    }
                }
                if (actionNotes == 0) ++zeroNoteActions;
                if (actionNotes == 0) {
                    silent.push_back({ type, action, command, "sin notas" });
                } else if (actionNotes == actionOsc) {
                    silent.push_back({ type, action, command, "solo osciladores" });
                } else if (actionNotes == actionUnresolved) {
                    silent.push_back({ type, action, command, "instrumento no resuelve" });
                } else if (actionOsc + actionUnresolved > 0) {
                    silent.push_back({ type, action, command, "parcial" });
                }
                if ((type == 6 && action == 28)
                    || (type == 4 && (action == 6 || action == 7)))
                    std::printf("  targeted event type=%u action=%zu command=%03X notes=%zu\n",
                                type, action, command, actionNotes);
                if (((type == 6 && action == 28)
                     || (type == 4 && (action == 6 || action == 7)))
                    && actionNotes == 0)
                    ++failures;
                if (player.result() != PCJamResult::Ok) {
                    const unsigned resultValue = static_cast<unsigned>(player.result());
                    if (resultValue < std::size(eventResults)) ++eventResults[resultValue];
                    if (player.result() != PCJamResult::Finished && eventFailures++ == 0) {
                        firstEventFailureOpcode = player.unsupportedOpcode();
                        firstFailureType = type;
                        firstFailureAction = action;
                        firstFailureResult = resultValue;
                    }
                    if (player.result() == PCJamResult::UnsupportedOpcode
                        && firstUnsupportedEventOpcode == 0)
                        firstUnsupportedEventOpcode = player.unsupportedOpcode();
                }
            }
            totalZeroActions += zeroNoteActions;
            std::printf("  event type=%u notes=%zu resolved=%zu zero-actions=%zu\n",
                        type, typeNotes, typeResolved, zeroNoteActions);
        }
        std::printf("--- acciones de evento sin sonido audible ---\n");
        for (const SilentAction& a : silent) {
            std::printf("  tipo %u accion %-3zu comando %03X  %s\n",
                        a.type, a.action, a.command, a.reason);
        }
        std::printf("--- total mudas o parciales: %zu de %zu ---\n", silent.size(), eventCommands);
        std::printf("positional event coverage: commands=%zu notes=%zu resolved=%zu failures=%zu firstOpcode=%02X\n",
                    eventCommands, eventNotes, eventResolved, eventFailures,
                    firstEventFailureOpcode);
        std::printf("  first event failure: type=%u action=%zu result=%u unsupportedOpcode=%02X; results 1..7=%zu/%zu/%zu/%zu/%zu/%zu/%zu\n",
                    firstFailureType, firstFailureAction, firstFailureResult,
                    firstUnsupportedEventOpcode,
                    eventResults[1], eventResults[2], eventResults[3], eventResults[4],
                    eventResults[5], eventResults[6], eventResults[7]);
        // Every gameplay action must reach a sound and none may stop the
        // event sequence: a single stopped action takes every later sound with
        // it until the sequencer is restarted.
        if (eventNotes == 0 || eventResolved == 0 || eventFailures != 0
            || totalZeroActions > 12) ++failures;
    }
    {
        std::vector<u8> data;
        archive.read(13, data);
        size_t demoNotes = 0;
        const u16 demoBgmIds[] = { 1, 5, 6, 7, 8, 9, 10 };
        for (u16 id : demoBgmIds) {
            PCJamPlayer demoPlayer;
            std::vector<PCJamEvent> demoEvents;
            demoPlayer.start(data, 13);
            demoPlayer.writeRootPort(0, id);
            size_t idNotes = 0;
            for (int tick = 0; tick < 2048 && demoPlayer.result() == PCJamResult::Ok; ++tick) {
                demoPlayer.tick(demoEvents, 4096);
                for (const PCJamEvent& event : demoEvents)
                    if (event.type == PCJamEventType::NoteOn) {
                        ++demoNotes;
                        ++idNotes;
                    }
            }
            if (idNotes == 0) {
                std::printf("  demo id=%u result=%u opcode=%02X pc=%X\n", id,
                            static_cast<unsigned>(demoPlayer.result()),
                            demoPlayer.unsupportedOpcode(), demoPlayer.lastOpcodeAddress());
                ++failures;
            }
        }
        std::printf("demo BGM port test: notes=%zu\n", demoNotes);
    }
    std::set<const PCWaveInfo*> referencedAfc2Waves;
    for (u32 i = 0; i < archive.size(); ++i) {
        std::vector<u8> data;
        if (!archive.read(i, data)) continue;
        PCJamPlayer player;
        std::vector<PCJamEvent> events;
        player.start(data, static_cast<u8>(i));
        size_t ticks = 0;
        size_t noteOns = 0;
        size_t noteOffs = 0;
        size_t resolved = 0;
        size_t oscillatorNotes = 0;
        using SelectVoice = std::tuple<u8, u8, u8, s16, s16, const PCWaveInfo*>;
        std::map<SelectVoice, size_t> selectVoices;
        struct SelectLongVoice {
            bool active = false;
            u8 bank = 0;
            u8 program = 0;
            u8 key = 0;
            size_t onTick = 0;
            double onSeconds = 0.0;
            size_t gates = 0;
        };
        SelectLongVoice selectLongVoices[kPCJamTrackCount][8] {};
        double sequenceSeconds = 0.0;
        bool activeNotes[kPCJamTrackCount][8] = {};
        size_t activeNoteCount = 0;
        size_t maxActiveNotes = 0;
        while (player.result() == PCJamResult::Ok && ticks++ < 20000) {
            player.tick(events, 4096);
            for (const PCJamEvent& event : events) {
                if (event.type == PCJamEventType::NoteOff) {
                    ++noteOffs;
                    SelectLongVoice& longVoice =
                        selectLongVoices[event.track % kPCJamTrackCount][event.voice & 7];
                    if (i == 18 && longVoice.active) {
                        std::printf("  SELECT long off %u:%u track=%u slot=%u key=%u tick=%zu duration=%zu ticks/%.3f s gates=%zu release=%u\n",
                                    longVoice.bank, longVoice.program, event.track,
                                    event.voice, longVoice.key, ticks,
                                    ticks - longVoice.onTick,
                                    sequenceSeconds - longVoice.onSeconds,
                                    longVoice.gates, event.release);
                        longVoice = {};
                    }
                    if (activeNotes[event.track % kPCJamTrackCount][event.voice & 7]) {
                        activeNotes[event.track % kPCJamTrackCount][event.voice & 7] = false;
                        --activeNoteCount;
                    }
                    continue;
                }
                if (event.type != PCJamEventType::NoteOn
                    && event.type != PCJamEventType::GateUpdate) continue;
                ++noteOns;
                if (i == 18 && event.bank == 18
                    && (event.program == 4 || event.program == 5)) {
                    SelectLongVoice& longVoice =
                        selectLongVoices[event.track % kPCJamTrackCount][event.voice & 7];
                    if (event.type == PCJamEventType::GateUpdate && longVoice.active) {
                        ++longVoice.gates;
                        std::printf("  SELECT long gate %u:%u track=%u slot=%u key=%u->%u tick=%zu after=%zu ticks/%.3f s\n",
                                    longVoice.bank, longVoice.program, event.track,
                                    event.voice, longVoice.key, event.key, ticks,
                                    ticks - longVoice.onTick,
                                    sequenceSeconds - longVoice.onSeconds);
                        longVoice.key = event.key;
                    } else {
                        longVoice = { true, event.bank, event.program, event.key,
                                      ticks, sequenceSeconds, 0 };
                        std::printf("  SELECT long on  %u:%u track=%u slot=%u key=%u tick=%zu time=%.3f s\n",
                                    event.bank, event.program, event.track,
                                    event.voice, event.key, ticks, sequenceSeconds);
                    }
                }
                if (!activeNotes[event.track % kPCJamTrackCount][event.voice & 7]) {
                    activeNotes[event.track % kPCJamTrackCount][event.voice & 7] = true;
                    ++activeNoteCount;
                    maxActiveNotes = std::max(maxActiveNotes, activeNoteCount);
                }
                if (event.program >= 0xF0) ++oscillatorNotes;
                PCInstrumentSelection selection;
                if (event.program < 0xF0
                    && !instruments.select(event.bank, event.program, event.key,
                                           event.velocity, selection)) {
                    if (noteOns == 1) std::printf("  first unresolved %u:%u key=%u vel=%u\n",
                        event.bank, event.program, event.key, event.velocity);
                    continue;
                }
                const int waveSystem = selection.region.waveSystem == -1
                    ? static_cast<int>(selection.physicalBank)
                    : waves.physicalWaveSystem(
                        static_cast<u16>(selection.region.waveSystem));
                const PCWaveInfo* wave = waveSystem < 0 ? nullptr
                    : waves.waveByIdPhysical(static_cast<u32>(waveSystem),
                        static_cast<u16>(selection.region.waveId), 0);
                if (wave) {
                    ++resolved;
                    if (wave->format == 1) referencedAfc2Waves.insert(wave);
                    if (i == 18) {
                        ++selectVoices[{ event.bank, event.program, event.key,
                                        selection.region.waveSystem,
                                        selection.region.waveId, wave }];
                    }
                }
            }
            sequenceSeconds += 60.0
                / (static_cast<double>(player.tempo()) * player.timeBase());
        }
        std::printf("JAM[%u] %-14s result=%u opcode=%02X ticks=%zu on/off=%zu/%zu max=%zu resolved=%zu osc=%zu natural-waits=%zu gate=%zu bend=%zu duration-reg=%zu\n",
                    i, archive.entry(i)->name.c_str(),
                    static_cast<unsigned>(player.result()),
                    player.unsupportedOpcode(), ticks, noteOns, noteOffs,
                    maxActiveNotes, resolved,
                    oscillatorNotes, player.naturalWaitCount(), player.gateNoteCount(),
                    player.pitchBendCount(), player.durationRegisterCount());
        if (player.opcodeCount(0xD6) || player.opcodeCount(0xD7)
            || player.opcodeCount(0xD8) || player.opcodeCount(0xEF)
            || player.opcodeCount(0xF0) || player.opcodeCount(0xF1)) {
            std::printf("  track DSP commands: D6=%zu D7=%zu D8=%zu EF=%zu F0=%zu F1=%zu\n",
                        player.opcodeCount(0xD6), player.opcodeCount(0xD7),
                        player.opcodeCount(0xD8), player.opcodeCount(0xEF),
                        player.opcodeCount(0xF0), player.opcodeCount(0xF1));
        }
        if (i == 18) {
            for (size_t track = 0; track < kPCJamTrackCount; ++track) {
                for (size_t voiceSlot = 0; voiceSlot < 8; ++voiceSlot) {
                    const SelectLongVoice& longVoice = selectLongVoices[track][voiceSlot];
                    if (!longVoice.active) continue;
                    std::printf("  SELECT long open %u:%u track=%zu slot=%zu key=%u tick=%zu age=%zu ticks/%.3f s gates=%zu\n",
                                longVoice.bank, longVoice.program, track,
                                voiceSlot, longVoice.key, longVoice.onTick,
                                ticks - longVoice.onTick,
                                sequenceSeconds - longVoice.onSeconds,
                                longVoice.gates);
                }
            }
            for (const auto& [voice, count] : selectVoices) {
                const auto& [bank, program, key, waveSystem, waveId, wave] = voice;
                std::printf("  SELECT voice %u:%u key=%u wsys=%d wave=%d count=%zu fmt=%u rate=%.2f samples=%u loop=%u dataoff=%u start=%u end=%u data=%u\n",
                            bank, program, key, waveSystem, waveId, count,
                            wave->format, wave->sampleRate, wave->sampleCount,
                            wave->looping ? 1u : 0u, wave->dataOffset,
                            wave->loopSample,
                            wave->loopEndSample,
                            wave->dataLength);
            }
            if (renderSelectLong) {
                for (u8 program : { u8(4), u8(5) }) {
                    PCInstrumentSelection selection;
                    if (!instruments.select(18, program, 60, 127, selection)) {
                        ++failures;
                        continue;
                    }
                    const int waveSystem = selection.region.waveSystem == -1
                        ? static_cast<int>(selection.physicalBank)
                        : waves.physicalWaveSystem(
                            static_cast<u16>(selection.region.waveSystem));
                    const PCWaveInfo* wave = waveSystem < 0 ? nullptr
                        : waves.waveByIdPhysical(static_cast<u32>(waveSystem),
                            static_cast<u16>(selection.region.waveId), 0);
                    std::vector<s16> pcm;
                    char path[64];
                    std::snprintf(path, sizeof(path),
                                  "/tmp/pikmin-select-18-%u-wave%u.wav", program,
                                  selection.region.waveId);
                    if (!wave || !pc_decode_wave(*wave, pcm)
                        || !writeWaveFile(path, pcm,
                            static_cast<u32>(wave->sampleRate))) {
                        std::printf("  SELECT render failed %u:%u\n", 18, program);
                        ++failures;
                    } else {
                        std::printf("  SELECT rendered %u:%u wave=%u samples=%zu path=%s\n",
                                    18, program, selection.region.waveId,
                                    pcm.size(), path);
                    }
                }
            }
        }
    }
    size_t decodedAfc2Waves = 0;
    for (const PCWaveInfo* wave : referencedAfc2Waves) {
        std::vector<s16> pcm;
        if (pc_decode_wave(*wave, pcm) && pcm.size() == wave->sampleCount)
            ++decodedAfc2Waves;
    }
    std::printf("referenced AFC2 waves: decoded=%zu/%zu\n",
                decodedAfc2Waves, referencedAfc2Waves.size());
    if (decodedAfc2Waves != referencedAfc2Waves.size()) ++failures;
    std::printf("pc_jam_test: %d synthetic failures\n", failures);
    return failures == 0 ? 0 : 1;
}

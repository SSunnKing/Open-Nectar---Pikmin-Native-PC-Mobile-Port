#include "pc_day_history.h"

#include "pc_speedrun.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

std::string pc_card_root_dir(); // card_stubs.cpp: la tarjeta en uso (normal, Randomizer...)

namespace {

constexpr int kSlots = 3;
int sChoice[kSlots] = {};

bool validSlot(int slot) { return slot >= 0 && slot < kSlots; }

fs::path slotDir(int slot) { return fs::path(pc_card_root_dir()) / "days" / ("slot" + std::to_string(slot)); }

fs::path dayPath(int slot, int day)
{
    char name[16];
    std::snprintf(name, sizeof(name), "day%02d.bin", day);
    return slotDir(slot) / name;
}

// "day07.bin" → 7; 0 si no es un archivo del historial.
int dayOf(const fs::path& p)
{
    int day = 0;
    if (p.extension() != ".bin" || std::sscanf(p.stem().string().c_str(), "day%d", &day) != 1) return 0;
    return day;
}

} // namespace

bool pc_days_enabled(void) { return !pc_speedrun_active(); }

void pc_days_store(int slot, int day, const void* data, int size)
{
    if (!validSlot(slot) || day < 1 || !data || size <= 0) return;
    std::error_code error;
    fs::create_directories(slotDir(slot), error);
    // Los días posteriores son de la línea de partida que se ha dejado atrás.
    for (const auto& entry : fs::directory_iterator(slotDir(slot), error))
        if (dayOf(entry.path()) > day) fs::remove(entry.path(), error);
    std::ofstream out(dayPath(slot, day), std::ios::binary | std::ios::trunc);
    out.write(static_cast<const char*>(data), size);
}

int pc_days_list(int slot, int* days, int max)
{
    if (!validSlot(slot)) return 0;
    int count = 0;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(slotDir(slot), error)) {
        const int day = dayOf(entry.path());
        if (day > 0 && count < max) days[count++] = day;
    }
    std::sort(days, days + count);
    return count;
}

bool pc_days_read(int slot, int day, void* data, int size)
{
    if (!validSlot(slot)) return false;
    std::ifstream in(dayPath(slot, day), std::ios::binary);
    if (!in) return false;
    in.read(static_cast<char*>(data), size);
    return in.gcount() == size;
}

void pc_days_delete_slot(int slot)
{
    if (!validSlot(slot)) return;
    std::error_code error;
    fs::remove_all(slotDir(slot), error);
    sChoice[slot] = 0;
}

void pc_days_copy_slot(int from, int to)
{
    if (!validSlot(from) || !validSlot(to) || from == to) return;
    std::error_code error;
    fs::remove_all(slotDir(to), error);
    if (fs::is_directory(slotDir(from), error)) fs::copy(slotDir(from), slotDir(to), fs::copy_options::recursive, error);
}

void pc_days_set_choice(int slot, int day)
{
    if (validSlot(slot)) sChoice[slot] = day;
}

int pc_days_take_choice(int slot)
{
    if (!validSlot(slot)) return 0;
    const int day = sChoice[slot];
    sChoice[slot] = 0;
    return day;
}

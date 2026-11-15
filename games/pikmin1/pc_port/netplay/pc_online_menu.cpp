#include "netplay/pc_online_menu.h"
#include "netplay/pc_online_menu_rows.h"
#include "netplay/pc_online_switch.h"
#include "netplay/pc_online_relaunch.h"

#include "netplay/pc_netplay_session.h"
#include "gl/pc_gfx.h"
#include "pc_menu_click.h"
#include "settings/pc_settings_p2d.h"

#include "Colour.h"
#include "Controller.h"
#include "Geometry.h"
#include "Graphics.h"
#include "Matrix4f.h"
#include "SoundID.h"
#include "SoundMgr.h"
#include "system.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

enum Page { kClosed, kMode, kOnline, kHostSave, kJoin, kNotice };

Page sPage    = kClosed;
bool sVs      = false;
int sCursor   = 0;
std::string sMessage; // error or hint under the options
// Panel from the last draw, so a click lands on the row that was painted.
int sHitX = 0, sHitY = 0, sHitW = 0, sHitRows = 0;

constexpr int kScreenW = 640, kScreenH = 480;

// One user path: rooms are NECTAR1 (host-authoritative) when this build has
// the online layer. The lockstep ICE room (NPIX3) is the only one without it,
// and a development/diagnostic option with it (PIKMIN_ONLINE_LEGACY_ICE=1).
// Joining follows the code that was pasted, whichever room made it.
bool legacy_ice()
{
#if PIKI_NECTAR_ONLINE
	const char* v = std::getenv("PIKMIN_ONLINE_LEGACY_ICE");
	return v != nullptr && v[0] == '1' && v[1] == '\0';
#else
	return true;
#endif
}

std::vector<const char*> options()
{
	switch (sPage) {
	case kMode: return { "Local", "Online" };
	case kOnline: return { "Create room", "Join with a code", "Back" };
	case kHostSave: return { "New game", "Continue file 1", "Continue file 2", "Continue file 3", "Back" };
	case kJoin: return { "Paste code and join", "Back" };
	case kNotice: return { "OK" };
	default: return {};
	}
}

void go(Page page)
{
	sPage   = page;
	sCursor = 0;
	sMessage.clear();
	sHitRows = 0;
}

// Starts this game again with `extra` and leaves (Android: Java ends this
// process a moment later). False on failure.
bool relaunch(const std::vector<std::string>& extra)
{
	if (!pc_online_relaunch(extra, &sMessage)) return false;
#if defined(__ANDROID__)
	sMessage = "Restarting the game..."; // Java ends this process
#else
	std::_Exit(0);
#endif
	return true;
}

std::vector<std::string> mode_args()
{
	return { "--netplay-mode", sVs ? "vs" : "coop" };
}

#if PIKI_NECTAR_ONLINE
void host(int saveSlot)
{
	// A fixed port, so one firewall rule or router forward serves every session.
	std::vector<std::string> extra = { "--online-host", "39911", "--online-code-clipboard" };
	if (saveSlot != 0) extra.insert(extra.end(), { "--online-save", std::to_string(saveSlot) });
	const std::vector<std::string> mode = mode_args();
	extra.insert(extra.end(), mode.begin(), mode.end());
	relaunch(extra);
}
#endif

// The netplay run folder goes beside the game data: the default, beside the
// executable, is read-only on Android (/system/bin).
void add_run_root(std::vector<std::string>& extra)
{
	std::error_code ec;
	std::filesystem::create_directories("online-sessions", ec);
	const std::filesystem::path runRoot = std::filesystem::absolute("online-sessions", ec);
	if (!ec) extra.insert(extra.end(), { "--netplay-run-root", runRoot.string() });
}

// Lockstep host over ICE: the game opens the room, copies its code and asks
// for the friend's answer in a dialog (--netplay-dialogs).
void ice_host()
{
	std::vector<std::string> extra = { "--netplay-host-ice", "--netplay-dialogs", "--netplay-mode", sVs ? "vs" : "coop" };
	add_run_root(extra);
	relaunch(extra);
}

// The code the guest was sent, from the clipboard: "NECTAR1-" and Crockford
// Base32 groups (an online room), or an ICE offer "NPIX3-"/"NPIX2-"
// (pc_netplay_ice, the lockstep room). Spaces and line breaks are dropped.
constexpr const char* kCodePrefixes[] = { "NECTAR1-", "NPIX3-", "NPIX2-" };
constexpr size_t kMaxCode = 16384;

bool clipboard_code(std::string& code)
{
	char* text = SDL_GetClipboardText();
	std::string raw = text != nullptr ? text : "";
	SDL_free(text);
	code.clear();
	for (char c : raw) {
		if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
		if (c < 0x21 || c > 0x7e || code.size() >= kMaxCode) return false;
		code += c;
	}
	bool prefixed = false;
	for (const char* prefix : kCodePrefixes) prefixed = prefixed || code.rfind(prefix, 0) == 0;
	return prefixed && code.size() > 16;
}

void join()
{
	std::string code;
	if (!clipboard_code(code)) {
		sMessage = std::string("No code in the clipboard: copy the code your friend sent you (")
		         + (legacy_ice() ? "NPIX3" : "NECTAR1") + "-...), then try again.";
		return;
	}
	const bool nectar = code.rfind("NECTAR1-", 0) == 0;
#if !PIKI_NECTAR_ONLINE
	if (nectar) {
		sMessage = "This build cannot join NECTAR1 rooms: it was built without the online layer.";
		return;
	}
#endif
	// A file only this user can read, removed by the game once read.
	std::error_code ec;
	std::filesystem::create_directories("online-sessions", ec);
	const std::string path = nectar ? "online-sessions/join-code.txt" : "online-sessions/join-offer.txt";
#ifdef _WIN32
	FILE* f = fopen(path.c_str(), "wb");
#else
	const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	FILE* f      = fd >= 0 ? fdopen(fd, "wb") : nullptr;
#endif
	const bool ok = f != nullptr && fwrite(code.data(), 1, code.size(), f) == code.size();
	if (f != nullptr) fclose(f);
	if (!ok) {
		sMessage = "Cannot write the code file in online-sessions/.";
		return;
	}
	std::vector<std::string> extra;
	if (nectar) {
		extra = { "--online-join", "@" + path, "--online-code-once" };
	} else {
		// Lockstep over ICE: the game answers the offer (the answer goes to the
		// clipboard) and starts once the host pastes it.
		extra = { "--netplay-join-ice", "@" + path, "--online-code-once", "--netplay-dialogs" };
		add_run_root(extra);
	}
	const std::vector<std::string> mode = mode_args();
	extra.insert(extra.end(), mode.begin(), mode.end());
	// The new game reads the file and removes it; only a failed restart does here.
	if (!relaunch(extra)) std::remove(path.c_str());
}

void sound(int id)
{
	if (seSystem != nullptr) seSystem->playSysSe(id);
}

// Word wrap for the message line.
std::vector<std::string> wrap(const std::string& text, int maxW, int fw)
{
	std::vector<std::string> out;
	std::string line;
	size_t i = 0;
	while (i < text.size()) {
		size_t j = text.find(' ', i);
		if (j == std::string::npos) j = text.size();
		const std::string word = text.substr(i, j - i);
		const std::string cand = line.empty() ? word : line + " " + word;
		if (!line.empty() && pc_settings_p2d_text_width(cand.c_str(), fw) > maxW) {
			out.push_back(line);
			line = word;
		} else {
			line = cand;
		}
		i = j + 1;
	}
	if (!line.empty()) out.push_back(line);
	return out;
}

} // namespace

bool pc_online_menu_available(void)
{
#if !PC_ONLINE_ENABLED
	return false; // online features are off project-wide (pc_online_switch.h)
#else
	return !pc_netplay_session_active();
#endif
}

void pc_online_menu_open(bool vs)
{
	sVs = vs;
	go(kMode);
}

bool pc_online_menu_active(void)
{
	// A failed join came back to the title: say why first.
	if (sPage == kClosed) {
		if (const char* why = pc_netplay_online_take_error()) {
			go(kNotice);
			sMessage = why;
		}
	}
	return sPage != kClosed;
}

int pc_online_menu_update(Controller* input)
{
	if (sPage == kClosed || input == nullptr) return PC_ONLINE_MENU_CANCEL;
	const int count = (int)options().size();
	// The title treats a left click as A. This menu wants the click for the
	// row under the pointer instead, and a right click as Back.
	pc_menu_click_claim_pointer_menu();
	bool pointerChoose = false;
	float nx = 0, ny = 0, gx = 0, gy = 0;
	PcMenuClickRect rows[8];
	const int hitCount = sHitRows < count ? sHitRows : count;
	if (hitCount > 0) pc_online_menu_row_rects(sHitX, sHitY, sHitW, hitCount, rows);
	if (hitCount > 0 && pc_menu_click_read_pointer_normalized(&nx, &ny)
	    && pc_gfx_menu_pointer_to_graph(nx, ny, &gx, &gy)) {
		const int hover = pc_online_menu_row_hit(gx, gy, rows, hitCount);
		if (hover >= 0 && hover != sCursor) {
			sCursor = hover;
			sound(SYSSE_MOVE1);
		}
	}
	if (hitCount > 0 && pc_menu_click_pending(&nx, &ny) && pc_gfx_menu_pointer_to_graph(nx, ny, &gx, &gy)) {
		const int hit = pc_online_menu_row_hit(gx, gy, rows, hitCount);
		if (hit >= 0) {
			pc_menu_click_take(&nx, &ny);
			sCursor       = hit;
			pointerChoose = true;
		}
	}
	const bool pointerBack = pc_menu_click_take_back();
	if (input->keyClick(KBBTN_DPAD_UP | KBBTN_MSTICK_UP)) {
		sCursor = (sCursor + count - 1) % count;
		sound(SYSSE_MOVE1);
	}
	if (input->keyClick(KBBTN_DPAD_DOWN | KBBTN_MSTICK_DOWN)) {
		sCursor = (sCursor + 1) % count;
		sound(SYSSE_MOVE1);
	}
	const bool choose = input->keyClick(KBBTN_A | KBBTN_START) || pointerChoose;
	const bool back   = input->keyClick(KBBTN_B) || pointerBack || (choose && sCursor == count - 1 && sPage != kMode);
	if (back) {
		sound(SYSSE_CANCEL);
		if (sPage == kMode || sPage == kNotice) {
			sPage = kClosed;
			return PC_ONLINE_MENU_CANCEL;
		}
		go(sPage == kOnline ? kMode : kOnline);
		return PC_ONLINE_MENU_OPEN;
	}
	if (!choose) return PC_ONLINE_MENU_OPEN;
	sound(SYSSE_DECIDE1);
	switch (sPage) {
	case kMode:
		if (sCursor == 0) {
			sPage = kClosed;
			return PC_ONLINE_MENU_LOCAL;
		}
		go(kOnline);
		break;
	case kOnline:
		if (sCursor != 0) go(kJoin);
		else if (legacy_ice()) ice_host();
#if PIKI_NECTAR_ONLINE
		else if (sVs) host(0);
		else go(kHostSave);
#endif
		break;
#if PIKI_NECTAR_ONLINE
	case kHostSave: host(sCursor); break;
#endif
	case kJoin: join(); break;
	default: break;
	}
	return PC_ONLINE_MENU_OPEN;
}

void pc_online_menu_draw(void)
{
	if (sPage == kClosed || !gsys || !gsys->mDGXGfx) return;
	DGXGraphics* gfx = static_cast<DGXGraphics*>(gsys->mDGXGfx);
	pc_gfx_set_ui_43_no_bars(0);
	struct Unbind {
		~Unbind() { pc_gfx_set_ui_43_no_bars(0); }
	} unbind;
	PcSettingsP2DFrame frame(kScreenW, kScreenH);
	if (!pc_settings_p2d_active()) return;
	pc_gfx_set_ui_43_no_bars(1);

	const std::vector<const char*> rows = options();
	const int w = 400, pad = 28, rowH = 30;
	std::vector<std::string> msg;
	std::string hint;
	if (sPage == kOnline && !legacy_ice())
		hint = "Create a room and send the code to your friend, or join with the code you were sent.";
	if (sPage == kOnline && legacy_ice())
		hint = "Create a room and send its code to your friend, or join with the code you were sent. Both need the "
		       "same disc and build.";
	if (sPage == kHostSave) hint = "Continuing uses a file of your memory card; only your card keeps the progress.";
	if (sPage == kJoin && !legacy_ice()) hint = "Copy the code your friend sent you (NECTAR1-...) and choose Paste.";
	if (sPage == kJoin && legacy_ice())
		hint = "Copy the code your friend sent you (NPIX3-...) and choose Paste. Your answer code is then copied: "
		       "send it back to your friend.";
	for (const std::string& l : wrap(sMessage.empty() ? hint : sMessage, w - 2 * pad, 10)) msg.push_back(l);
	const int h = 70 + (int)rows.size() * rowH + (msg.empty() ? 0 : 12 + (int)msg.size() * 17) + 40;
	const int x = (kScreenW - w) / 2, y = (kScreenH - h) / 2;
	pc_gfx_blur_gx_rect(x + 6, y + 6, w - 12, h - 12, 3);
	Matrix4f ortho;
	gfx->setOrthogonal(ortho.mMtx, RectArea(0, 0, kScreenW, kScreenH));
	pc_settings_p2d_plate(x, y, w, h, 3);
	sHitX = x;
	sHitY = y;
	sHitW = w;
	sHitRows = (int)rows.size();
	pc_gfx_note_menu_tap_space(kScreenW, kScreenH);

	const char* title = sPage == kMode     ? (sVs ? "VS" : "Co-op")
	                  : sPage == kOnline   ? (sVs ? "VS online" : "Co-op online")
	                  : sPage == kHostSave ? "Create room"
	                  : sPage == kNotice   ? "Could not join"
	                                       : "Join a room";
	pc_settings_p2d_text_styled(x + pad, y + 22, title, Colour(255, 236, 140, 255), Colour(255, 190, 60, 255), 16, 22);
	int ry = y + 62;
	for (int i = 0; i < (int)rows.size(); ++i) {
		if (i == sCursor) pc_settings_p2d_plate(x + pad - 10, ry - 4, w - 2 * pad + 20, rowH - 2, 2);
		pc_settings_p2d_text(x + pad + 4, ry + 2, rows[i], i == sCursor ? Colour(255, 255, 255, 255) : Colour(200, 210, 225, 255),
		                     12, 18);
		ry += rowH;
	}
	if (!msg.empty()) {
		ry += 12;
		const Colour tone = sMessage.empty() ? Colour(180, 200, 220, 255) : Colour(255, 150, 120, 255);
		for (const std::string& l : msg) {
			pc_settings_p2d_text(x + pad, ry, l.c_str(), tone, 10, 15);
			ry += 17;
		}
	}
	pc_settings_p2d_text(x + pad, y + h - 30, "A: choose    B: back", Colour(160, 170, 190, 255), 10, 15);
}

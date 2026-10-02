// Discord Rich Presence (see pc_discord.h).
//
// Protocol: frames of { u32 opcode, u32 length, JSON } in little endian.
// Opcode 0 is the handshake ({"v":1,"client_id":...}), 1 a command frame.
// Discord answers with READY and with a reply per command; replies are read
// and thrown away so the pipe never fills up.

#include "pc_discord.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#if PIKI_P2_HOST
// Pikmin 2: el operator new global va al heap JKR del juego, que no es seguro
// entre hilos y cuyas secciones se destruyen. Todo lo de este módulo (el hilo
// y los textos que guarda) usa malloc.
bool pc_host_alloc_active();
void pc_host_alloc_set(bool active);
namespace {
struct PcDiscordHostAlloc {
	bool prev = pc_host_alloc_active();
	PcDiscordHostAlloc() { pc_host_alloc_set(true); }
	~PcDiscordHostAlloc() { pc_host_alloc_set(prev); }
};
} // namespace
#define PC_DISCORD_HOST_ALLOC() PcDiscordHostAlloc pcDiscordHostAlloc
#else
#define PC_DISCORD_HOST_ALLOC() ((void)0)
#endif

namespace {

// Application "Open Nectar" in the Discord Developer Portal: its name is what
// Discord shows as "Playing Open Nectar", and its art assets are the icons.
const char* const kClientId = "1555634321266581625";

// Updates are rate limited by Discord (about 5 every 20 s).
constexpr double kMinSendInterval = 5.0;
// No gameplay frame for this long means the player is in a menu.
constexpr double kMenuTimeout = 3.0;

double nowSeconds()
{
	using namespace std::chrono;
	return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string jsonEscape(const std::string& in)
{
	std::string out;
	for (unsigned char c : in) {
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		default:
			if (c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			} else {
				out += (char)c;
			}
		}
	}
	return out;
}

class Connection {
public:
	~Connection() { close(); }

	bool open()
	{
#ifdef _WIN32
		for (int i = 0; i < 10; i++) {
			char name[64];
			snprintf(name, sizeof(name), "\\\\.\\pipe\\discord-ipc-%d", i);
			HANDLE h = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
			if (h != INVALID_HANDLE_VALUE) {
				mPipe = h;
				return true;
			}
		}
		return false;
#else
		// Discord, Flatpak and Snap each put the socket somewhere else.
		const char* dirs[4] = { getenv("XDG_RUNTIME_DIR"), getenv("TMPDIR"), getenv("TMP"), "/tmp" };
		const char* subdirs[3] = { "", "app/com.discordapp.Discord/", "snap.discord/" };
		for (const char* dir : dirs) {
			if (!dir || !dir[0]) continue;
			for (const char* sub : subdirs) {
				for (int i = 0; i < 10; i++) {
					sockaddr_un addr {};
					addr.sun_family = AF_UNIX;
					snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%sdiscord-ipc-%d", dir, sub, i);
					int fd = socket(AF_UNIX, SOCK_STREAM, 0);
					if (fd < 0) return false;
					if (connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0) {
						fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
						mFd = fd;
						return true;
					}
					::close(fd);
				}
			}
		}
		return false;
#endif
	}

	bool isOpen() const
	{
#ifdef _WIN32
		return mPipe != INVALID_HANDLE_VALUE;
#else
		return mFd >= 0;
#endif
	}

	void close()
	{
#ifdef _WIN32
		if (mPipe != INVALID_HANDLE_VALUE) CloseHandle(mPipe);
		mPipe = INVALID_HANDLE_VALUE;
#else
		if (mFd >= 0) ::close(mFd);
		mFd = -1;
#endif
	}

	bool send(uint32_t opcode, const std::string& json)
	{
		std::string frame(8 + json.size(), '\0');
		const uint32_t len = (uint32_t)json.size();
		for (int i = 0; i < 4; i++) {
			frame[i]     = (char)((opcode >> (8 * i)) & 0xFF);
			frame[4 + i] = (char)((len >> (8 * i)) & 0xFF);
		}
		memcpy(&frame[8], json.data(), json.size());
#ifdef _WIN32
		DWORD written = 0;
		if (!WriteFile(mPipe, frame.data(), (DWORD)frame.size(), &written, nullptr) || written != frame.size()) {
			close();
			return false;
		}
#else
		size_t sent = 0;
		const double deadline = nowSeconds() + 1.0;
		while (sent < frame.size()) {
			ssize_t n = ::send(mFd, frame.data() + sent, frame.size() - sent, MSG_NOSIGNAL);
			if (n > 0) {
				sent += (size_t)n;
			} else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && nowSeconds() < deadline) {
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			} else {
				close();
				return false;
			}
		}
#endif
		return true;
	}

	/// Reads and discards whatever Discord sent. False if it hung up.
	bool drain()
	{
		char buf[4096];
#ifdef _WIN32
		DWORD avail = 0;
		while (true) {
			if (!PeekNamedPipe(mPipe, nullptr, 0, nullptr, &avail, nullptr)) {
				close();
				return false;
			}
			if (avail == 0) return true;
			DWORD got = 0;
			if (!ReadFile(mPipe, buf, avail < sizeof(buf) ? avail : (DWORD)sizeof(buf), &got, nullptr)) {
				close();
				return false;
			}
		}
#else
		while (true) {
			ssize_t n = recv(mFd, buf, sizeof(buf), 0);
			if (n > 0) continue;
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return true;
			close();
			return false;
		}
#endif
	}

private:
#ifdef _WIN32
	HANDLE mPipe = INVALID_HANDLE_VALUE;
#else
	int mFd = -1;
#endif
};

struct State {
	std::string zoneName;
	std::string zoneKey;
	std::string stateLine;
	double lastGameplay = -1000.0;
};

std::mutex sMutex;
State sState;
std::string sGameName = "Open Nectar";
std::atomic<bool> sRunning { false };

std::string buildActivity(const State& st, bool inGame, long long startEpoch)
{
	std::string details, state, largeKey, largeText;
	if (inGame) {
		details   = sGameName + " - " + st.zoneName;
		state     = st.stateLine;
		largeKey  = st.zoneKey;
		largeText = st.zoneName;
	} else {
		details   = sGameName;
		state     = "In the menus";
		largeKey  = "app";
		largeText = "Open Nectar";
	}
	std::string a = "{\"details\":\"" + jsonEscape(details) + "\"";
	if (!state.empty()) a += ",\"state\":\"" + jsonEscape(state) + "\"";
	a += ",\"timestamps\":{\"start\":" + std::to_string(startEpoch) + "}";
	a += ",\"assets\":{\"large_image\":\"" + jsonEscape(largeKey) + "\",\"large_text\":\"" + jsonEscape(largeText) + "\"";
	if (inGame) a += ",\"small_image\":\"app\",\"small_text\":\"Open Nectar\"";
	a += "}}";
	return a;
}

void threadMain()
{
	PC_DISCORD_HOST_ALLOC();
	Connection conn;
	const long long startEpoch = (long long)time(nullptr);
	double nextConnectTry = 0.0;
	double lastSend       = -1000.0;
	std::string lastActivity;
	int nonce = 0;
#ifdef _WIN32
	const unsigned long pid = GetCurrentProcessId();
#else
	const unsigned long pid = (unsigned long)getpid();
#endif

	while (sRunning) {
		const double now = nowSeconds();
		if (!conn.isOpen() && now >= nextConnectTry) {
			nextConnectTry = now + 15.0;
			if (conn.open()) {
				const std::string hello = std::string("{\"v\":1,\"client_id\":\"") + kClientId + "\"}";
				if (!conn.send(0, hello)) {
					conn.close();
				}
				lastActivity.clear();
			}
		}

		if (conn.isOpen() && conn.drain()) {
			State st;
			{
				std::lock_guard<std::mutex> lock(sMutex);
				st = sState;
			}
			const bool inGame        = now - st.lastGameplay < kMenuTimeout;
			const std::string activity = buildActivity(st, inGame, startEpoch);
			if (activity != lastActivity && now - lastSend >= kMinSendInterval) {
				const std::string cmd = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(pid)
				                      + ",\"activity\":" + activity + "},\"nonce\":\"" + std::to_string(++nonce) + "\"}";
				if (conn.send(1, cmd)) {
					lastActivity = activity;
					lastSend     = now;
				}
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
}

} // namespace

extern "C" void pc_discord_init(const char* gameName)
{
	PC_DISCORD_HOST_ALLOC();
	if (sRunning || getenv("OPEN_NECTAR_NO_DISCORD")) return;
	if (gameName) sGameName = gameName;
	sRunning = true;
	// Detached: the game may leave through exit() anywhere, and a joinable
	// std::thread destroyed at exit would call std::terminate.
	std::thread(threadMain).detach();
}

extern "C" void pc_discord_shutdown(void)
{
	sRunning = false;
}

extern "C" void pc_discord_set_playing(const char* zoneName, const char* zoneKey, const char* stateLine)
{
	PC_DISCORD_HOST_ALLOC();
	std::lock_guard<std::mutex> lock(sMutex);
	sState.zoneName     = zoneName ? zoneName : "";
	sState.zoneKey      = zoneKey ? zoneKey : "app";
	sState.stateLine    = stateLine ? stateLine : "";
	sState.lastGameplay = nowSeconds();
}

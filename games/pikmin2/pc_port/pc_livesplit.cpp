#include "pc_livesplit.h"

#include "settings/pc_settings.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;
#else
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
constexpr Socket kNoSocket = -1;
#endif

// Pikmin 2: el operator new global va al heap JKR del juego, que no es seguro
// entre hilos y cuyas secciones se destruyen. El hilo reserva siempre con
// malloc, y las funciones que le pasan comandos también (la marca es por hilo).
bool pc_host_alloc_active();
void pc_host_alloc_set(bool active);

namespace {

struct HostAlloc {
    bool prev = pc_host_alloc_active();
    HostAlloc() { pc_host_alloc_set(true); }
    ~HostAlloc() { pc_host_alloc_set(prev); }
};

using Clock = std::chrono::steady_clock;
constexpr auto kRetry = std::chrono::seconds(3);

// Se crea una vez y no se destruye: el hilo puede seguir vivo al salir.
struct Client {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> queue;
    std::string host;
    int port = 16834;
    std::atomic<bool> enabled { false };
    std::atomic<bool> connected { false };
    std::atomic<bool> needSync { false }; // recién conectado: el juego manda el estado
};
Client* sClient = nullptr;

void closeSocket(Socket& s)
{
    if (s == kNoSocket) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    s = kNoSocket;
}

Socket connectTo(const std::string& host, int port)
{
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(host.c_str(), service.c_str(), &hints, &list) != 0) return kNoSocket;
    Socket s = kNoSocket;
    for (addrinfo* a = list; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == kNoSocket) continue;
        if (connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) break;
        closeSocket(s);
    }
    freeaddrinfo(list);
    if (s == kNoSocket) return s;
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
#ifdef __APPLE__
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    return s;
}

bool sendAll(Socket s, const std::string& data)
{
#if defined(_WIN32) || defined(__APPLE__)
    const int flags = 0;
#else
    const int flags = MSG_NOSIGNAL;
#endif
    size_t off = 0;
    while (off < data.size()) {
        const int n = (int)send(s, data.data() + off, (int)(data.size() - off), flags);
        if (n <= 0) return false;
        off += size_t(n);
    }
    return true;
}

// LiveSplit no contesta a los comandos que se mandan; si cierra, recv da 0.
bool peerClosed(Socket s)
{
#ifdef _WIN32
    WSAPOLLFD p = { s, POLLRDNORM, 0 };
    if (WSAPoll(&p, 1, 0) <= 0) return false;
#else
    pollfd p = { s, POLLIN, 0 };
    if (poll(&p, 1, 0) <= 0) return false;
#endif
    char buf[256];
    return recv(s, buf, sizeof(buf), 0) <= 0;
}

void worker(Client* c)
{
    pc_host_alloc_set(true);
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    Socket s = kNoSocket;
    Clock::time_point nextTry = Clock::now();
    for (;;) {
        if (!c->enabled) {
            closeSocket(s);
            c->connected = false;
            std::unique_lock<std::mutex> lock(c->mutex);
            c->queue.clear();
            c->wake.wait_for(lock, std::chrono::milliseconds(500));
            continue;
        }
        if (s == kNoSocket) {
            if (Clock::now() >= nextTry) {
                std::string host;
                int port;
                {
                    std::lock_guard<std::mutex> lock(c->mutex);
                    host = c->host;
                    port = c->port;
                }
                nextTry = Clock::now() + kRetry;
                s = connectTo(host, port);
                if (s != kNoSocket) {
                    {
                        std::lock_guard<std::mutex> lock(c->mutex);
                        c->queue.clear(); // lo de antes de conectar ya no vale
                    }
                    c->connected = true;
                    c->needSync = true;
                }
            }
            if (s == kNoSocket) {
                std::unique_lock<std::mutex> lock(c->mutex);
                c->wake.wait_for(lock, std::chrono::milliseconds(250));
                continue;
            }
        }
        std::string batch;
        {
            std::unique_lock<std::mutex> lock(c->mutex);
            if (c->queue.empty()) c->wake.wait_for(lock, std::chrono::milliseconds(250));
            while (!c->queue.empty()) {
                batch += c->queue.front();
                c->queue.pop_front();
            }
        }
        if ((!batch.empty() && !sendAll(s, batch)) || peerClosed(s)) {
            closeSocket(s);
            c->connected = false;
            nextTry = Clock::now() + kRetry;
        }
    }
}

void push(std::initializer_list<std::string> commands)
{
    if (!sClient || !sClient->connected) return;
    {
        std::lock_guard<std::mutex> lock(sClient->mutex);
        for (const std::string& cmd : commands) sClient->queue.push_back(cmd + "\r\n");
    }
    sClient->wake.notify_one();
}

std::string gameTime(uint64_t ms)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u:%02u:%02u.%03u", unsigned(ms / 3600000), unsigned(ms / 60000 % 60),
                  unsigned(ms / 1000 % 60), unsigned(ms % 1000));
    return buf;
}

} // namespace

void pc_livesplit_update(bool enabled, bool running, uint64_t elapsedMs)
{
    HostAlloc hostAlloc;
    if (!sClient) {
        if (!enabled) return;
        sClient = new Client();
        std::thread(worker, sClient).detach();
    }
    {
        std::lock_guard<std::mutex> lock(sClient->mutex);
        sClient->host = pc_settings_get_speedrun_ls_host();
        sClient->port = pc_settings_get_speedrun_ls_port();
    }
    if (sClient->enabled != enabled) {
        sClient->enabled = enabled;
        sClient->wake.notify_one();
    }
    // Conectado a mitad de run: LiveSplit empieza con el tiempo del juego.
    if (sClient->connected && sClient->needSync.exchange(false)) {
        if (running) push({ "reset", "starttimer", "initgametime", "setgametime " + gameTime(elapsedMs) });
        else push({ "reset" });
    }
}

bool pc_livesplit_connected(void) { return sClient && sClient->enabled && sClient->connected; }

void pc_livesplit_start(void) { HostAlloc hostAlloc; push({ "reset", "starttimer", "initgametime", "setgametime 0:00:00.000" }); }

void pc_livesplit_split(uint64_t elapsedMs) { HostAlloc hostAlloc; push({ "setgametime " + gameTime(elapsedMs), "split" }); }

void pc_livesplit_reset(void) { HostAlloc hostAlloc; push({ "reset" }); }

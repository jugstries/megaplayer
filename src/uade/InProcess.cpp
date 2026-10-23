// Fa girare uadecore, l'emulatore di UADE, in un thread del player invece
// che in un processo separato: su Windows, iOS e nel browser non si possono
// lanciare processi.
//
// Sostituisce le due funzioni di unixsupport.c che avviano e fermano
// uadecore (escluse dalla patch patches/uade-in-process.patch) e le funzioni
// di unixatomic.c su cui libuade e uadecore leggono e scrivono: al posto
// delle pipe ci sono canali in memoria. Il protocollo resta quello di UADE.
//
// L'emulatore usa molte variabili globali, quindi può girarne una sola
// istanza per volta. Il thread parte alla prima lettura di libuade e non
// alla creazione dello stato: così gli stati usati solo per riconoscere i
// formati non occupano l'emulatore.

#include <pthread.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>

extern "C" {
struct uade_ipc;
void uade_set_peer(struct uade_ipc* ipc, int peer_is_client, int in_fd, int out_fd);
int uadecore_main(int argc, char** argv);
}

namespace {

// I canali hanno numeri lontani da quelli dei file descriptor veri, che
// libuade usa ancora per il database delle durate.
constexpr int kFirstHandle = 1 << 20;

// L'emulatore è nato per girare come programma a sé: meglio dargli lo stack
// di un thread principale (su macOS i thread secondari hanno 512 KB).
constexpr size_t kCoreStackBytes = 8u << 20;

class Channel {
public:
    // Come uade_atomic_read(): legge esattamente `count` byte e restituisce
    // 0 se il canale viene chiuso prima.
    ssize_t read(void* buf, size_t count)
    {
        std::unique_lock lock(mutex_);
        changed_.wait(lock, [&] { return data_.size() >= count || closed_; });
        if (data_.size() < count)
            return 0;
        std::copy_n(data_.begin(), count, static_cast<char*>(buf));
        data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(count));
        return static_cast<ssize_t>(count);
    }

    ssize_t write(const void* buf, size_t count)
    {
        {
            std::lock_guard lock(mutex_);
            if (closed_) {
                errno = EPIPE;
                return -1;
            }
            const auto* bytes = static_cast<const char*>(buf);
            data_.insert(data_.end(), bytes, bytes + count);
        }
        changed_.notify_all();
        return static_cast<ssize_t>(count);
    }

    void close()
    {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<char> data_;
    bool closed_ = false;
};

struct Session {
    int id = 0;
    int toCoreHandle = 0; // libuade scrive, uadecore legge
    int toHostHandle = 0; // uadecore scrive, libuade legge
    std::shared_ptr<Channel> toCore = std::make_shared<Channel>();
    std::shared_ptr<Channel> toHost = std::make_shared<Channel>();
    pthread_t thread{};
    bool started = false;
    std::jmp_buf exitJump{};

    void closeChannels()
    {
        toCore->close();
        toHost->close();
    }
};

std::mutex gMutex;
std::map<int, std::shared_ptr<Channel>> gChannels;
std::map<int, std::unique_ptr<Session>> gSessions;
int gNextHandle = kFirstHandle;
int gNextSession = 1;
Session* gRunning = nullptr;

thread_local Session* tCoreSession = nullptr;

void* runCore(void* arg)
{
    auto* session = static_cast<Session*>(arg);
    tCoreSession = session;

    std::string in = std::to_string(session->toCoreHandle);
    std::string out = std::to_string(session->toHostHandle);
    char name[] = "uadecore";
    char inFlag[] = "-i";
    char outFlag[] = "-o";
    char* argv[] = {name, inFlag, in.data(), outFlag, out.data(), nullptr};

    // uade_inprocess_exit() torna qui. Tra setjmp e longjmp ci sono solo
    // funzioni C dell'emulatore, senza distruttori da saltare.
    if (setjmp(session->exitJump) == 0)
        uadecore_main(5, argv);

    // Finito, normalmente o per errore: libuade se ne accorge alla prossima
    // lettura.
    session->closeChannels();
    {
        std::lock_guard lock(gMutex);
        gRunning = nullptr;
    }
    return nullptr;
}

// Avvia il thread di uadecore se `handle` è il canale da cui libuade legge
// le risposte di una sessione non ancora partita. Va chiamata con gMutex.
void startCoreIfNeeded(int handle)
{
    for (auto& [id, session] : gSessions) {
        if (session->toHostHandle != handle || session->started)
            continue;

        if (gRunning) {
            std::fprintf(stderr, "uade: un altro brano UADE sta già suonando in questo processo\n");
            session->closeChannels();
            return;
        }

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, kCoreStackBytes);
        const int error = pthread_create(&session->thread, &attr, runCore, session.get());
        pthread_attr_destroy(&attr);
        if (error != 0) {
            std::fprintf(stderr, "uade: impossibile avviare il thread di uadecore\n");
            session->closeChannels();
            return;
        }
        session->started = true;
        gRunning = session.get();
        return;
    }
}

std::shared_ptr<Channel> findChannel(int fd)
{
    std::lock_guard lock(gMutex);
    const auto it = gChannels.find(fd);
    return it == gChannels.end() ? nullptr : it->second;
}

} // namespace

extern "C" [[noreturn]] void uade_inprocess_exit(int status)
{
    if (Session* session = tCoreSession)
        std::longjmp(session->exitJump, 1);
    std::exit(status);
}

extern "C" int uade_arch_spawn(struct uade_ipc* ipc, pid_t* uadepid, const char* /*uadename*/,
                               const int* /*keep_fds*/)
{
    auto session = std::make_unique<Session>();

    std::lock_guard lock(gMutex);
    session->id = gNextSession++;
    session->toCoreHandle = gNextHandle++;
    session->toHostHandle = gNextHandle++;
    gChannels[session->toCoreHandle] = session->toCore;
    gChannels[session->toHostHandle] = session->toHost;

    uade_set_peer(ipc, 1, session->toHostHandle, session->toCoreHandle);
    *uadepid = session->id;
    gSessions[session->id] = std::move(session);
    return 0;
}

extern "C" void uade_arch_kill_and_wait_uadecore(struct uade_ipc* /*ipc*/, pid_t* uadepid)
{
    if (*uadepid == 0)
        return;

    std::unique_ptr<Session> session;
    {
        std::lock_guard lock(gMutex);
        const auto it = gSessions.find(*uadepid);
        if (it != gSessions.end()) {
            session = std::move(it->second);
            gSessions.erase(it);
            gChannels.erase(session->toCoreHandle);
            gChannels.erase(session->toHostHandle);
        }
    }
    *uadepid = 0;
    if (!session)
        return;

    // Con i canali chiusi uadecore fallisce alla prossima lettura o
    // scrittura, chiama exit() e il thread termina.
    session->closeChannels();
    if (session->started)
        pthread_join(session->thread, nullptr);
}

extern "C" ssize_t uade_atomic_read(int fd, const void* buf, size_t count)
{
    std::shared_ptr<Channel> channel;
    {
        std::lock_guard lock(gMutex);
        const auto it = gChannels.find(fd);
        if (it != gChannels.end()) {
            channel = it->second;
            startCoreIfNeeded(fd);
        }
    }
    if (channel)
        return channel->read(const_cast<void*>(buf), count);

    auto* bytes = static_cast<char*>(const_cast<void*>(buf));
    size_t done = 0;
    while (done < count) {
        const ssize_t got = ::read(fd, bytes + done, count - done);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            return got;
        done += static_cast<size_t>(got);
    }
    return static_cast<ssize_t>(done);
}

extern "C" ssize_t uade_atomic_write(int fd, const void* buf, size_t count)
{
    if (const auto channel = findChannel(fd))
        return channel->write(buf, count);

    const auto* bytes = static_cast<const char*>(buf);
    size_t done = 0;
    while (done < count) {
        const ssize_t wrote = ::write(fd, bytes + done, count - done);
        if (wrote < 0 && errno == EINTR)
            continue;
        if (wrote < 0)
            return -1;
        done += static_cast<size_t>(wrote);
    }
    return static_cast<ssize_t>(done);
}

extern "C" int uade_atomic_close(int fd)
{
    if (const auto channel = findChannel(fd)) {
        channel->close();
        return 0;
    }
    while (::close(fd) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return 0;
}

extern "C" int uade_atomic_dup2(int oldfd, int newfd)
{
    while (::dup2(oldfd, newfd) < 0) {
        if (errno != EINTR)
            return -1;
    }
    return newfd;
}

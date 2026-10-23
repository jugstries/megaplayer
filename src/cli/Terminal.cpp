#include "Terminal.h"

#include <csignal>

#ifndef _WIN32
#include <string>

#include <termios.h>
#include <unistd.h>
#endif

namespace mp::cli {

namespace {

volatile std::sig_atomic_t gInterrupted = 0;

void onInterrupt(int)
{
    gInterrupted = 1;
}

} // namespace

void installInterruptHandler()
{
    std::signal(SIGINT, onInterrupt);
#ifdef SIGTERM
    std::signal(SIGTERM, onInterrupt);
#endif
}

bool interrupted()
{
    return gInterrupted != 0;
}

#ifndef _WIN32

struct TerminalInput::Impl {
    termios saved{};
    // Byte letti ma non ancora interpretati: se il ciclo del player rallenta,
    // una sola read() può restituire più tasti.
    std::string pending;
};

TerminalInput::TerminalInput()
{
    if (!isatty(STDIN_FILENO))
        return;

    auto impl = std::make_unique<Impl>();
    if (tcgetattr(STDIN_FILENO, &impl->saved) != 0)
        return;

    termios raw = impl->saved;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;  // read() non blocca
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0)
        impl_ = std::move(impl);
}

TerminalInput::~TerminalInput()
{
    if (impl_)
        tcsetattr(STDIN_FILENO, TCSANOW, &impl_->saved);
}

Key TerminalInput::poll()
{
    if (!impl_)
        return Key::None;

    char buf[32];
    const ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
    if (n > 0)
        impl_->pending.append(buf, static_cast<size_t>(n));
    if (impl_->pending.empty())
        return Key::None;

    std::string& pending = impl_->pending;

    // Frecce: ESC [ C (destra) e ESC [ D (sinistra).
    if (pending.size() >= 3 && pending[0] == '\x1b' && pending[1] == '[') {
        const char code = pending[2];
        pending.erase(0, 3);
        if (code == 'C')
            return Key::SeekForward;
        if (code == 'D')
            return Key::SeekBack;
        return Key::None;
    }

    const char c = pending[0];
    pending.erase(0, 1);
    switch (c) {
    case ' ': return Key::Pause;
    case 'n': return Key::NextSubsong;
    case 'p': return Key::PrevSubsong;
    case '\n':
    case '\r': return Key::NextFile;
    case 'q': return Key::Quit;
    default: return Key::None;
    }
}

#else

struct TerminalInput::Impl {};

TerminalInput::TerminalInput() = default;
TerminalInput::~TerminalInput() = default;

Key TerminalInput::poll()
{
    return Key::None;
}

#endif

} // namespace mp::cli

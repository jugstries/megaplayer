#pragma once

#include <memory>

namespace mp::cli {

enum class Key {
    None,
    Pause,
    SeekBack,
    SeekForward,
    NextSubsong,
    PrevSubsong,
    NextFile,
    MegaSoundUp,
    MegaSoundDown,
    MegaSoundCompare,
    Quit,
};

// Legge i tasti senza attendere l'invio. Se stdin non è un terminale (o su
// Windows, per ora) poll() restituisce sempre Key::None.
class TerminalInput {
public:
    TerminalInput();
    ~TerminalInput();

    TerminalInput(const TerminalInput&) = delete;
    TerminalInput& operator=(const TerminalInput&) = delete;

    Key poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Finché vive, quello che le librerie scrivono su stderr si perde: libuade
// avvisa per molti file (formati che riconosce ma non suona, moduli
// troncati), e durante la scansione della libreria sarebbe solo rumore.
class QuietStderr {
public:
    QuietStderr();
    ~QuietStderr();

    QuietStderr(const QuietStderr&) = delete;
    QuietStderr& operator=(const QuietStderr&) = delete;

private:
    int saved_ = -1;
};

// Dopo Ctrl+C interrupted() diventa true: il player chiude in modo ordinato
// e il terminale torna allo stato originale.
void installInterruptHandler();
bool interrupted();

} // namespace mp::cli

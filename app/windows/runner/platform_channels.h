#ifndef RUNNER_PLATFORM_CHANNELS_H_
#define RUNNER_PLATFORM_CHANNELS_H_

#include <flutter/binary_messenger.h>
#include <flutter/encodable_value.h>
#include <flutter/method_channel.h>
#include <windows.h>

#include <memory>

// I canali dell'app con Windows, gli stessi di macOS
// (macos/Runner/MainFlutterWindow.swift).

using Channel = flutter::MethodChannel<flutter::EncodableValue>;

// "megaplayer/files": le finestre per aprire file e scegliere cartelle, e i
// file passati da riga di comando.
std::unique_ptr<Channel> CreateFilesChannel(flutter::BinaryMessenger* messenger,
                                            HWND window);

// "megaplayer/icon": l'icona nella barra delle applicazioni segue la skin
// finché l'app è aperta; l'eseguibile porta la moderna.
std::unique_ptr<Channel> CreateIconChannel(flutter::BinaryMessenger* messenger,
                                           HWND window);

// "megaplayer/drop": i file trascinati da Esplora risorse sulla finestra,
// con "enter" ed "exit" mentre passano e "drop" con i percorsi. Prima di
// distruggere la finestra va chiamato RevokeDropTarget.
std::unique_ptr<Channel> CreateDropChannel(flutter::BinaryMessenger* messenger,
                                           HWND window);
void RevokeDropTarget(HWND window);

// "megaplayer/tray": il miniplayer nell'area di notifica, con l'icona della
// skin e un menu con il brano, play e pausa, precedente e successivo, il
// volume, la finestra e l'uscita. L'app manda lo stato con "update"; i
// comandi tornano come "togglePause", "previous", "next" e "setVolume". La
// finestra passa i suoi messaggi a HandleTrayMessage; finché l'icona c'è,
// chiudere la finestra la nasconde (TrayIconShown) e la musica continua.
// Prima di distruggere la finestra va chiamato RemoveTrayIcon.
std::unique_ptr<Channel> CreateTrayChannel(flutter::BinaryMessenger* messenger,
                                           HWND window);
bool HandleTrayMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
bool TrayIconShown();
void RemoveTrayIcon();

#endif  // RUNNER_PLATFORM_CHANNELS_H_

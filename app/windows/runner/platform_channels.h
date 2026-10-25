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

#endif  // RUNNER_PLATFORM_CHANNELS_H_

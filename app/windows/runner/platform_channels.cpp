#include "platform_channels.h"

#include <flutter/standard_method_codec.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <cctype>
#include <string>
#include <variant>
#include <vector>

#include "resource.h"
#include "utils.h"

namespace {

using flutter::EncodableList;
using flutter::EncodableValue;
using flutter::MethodCall;
using flutter::MethodResult;
using Microsoft::WRL::ComPtr;

std::wstring Utf16FromUtf8(const std::string& text) {
  const int size = static_cast<int>(text.size());
  const int length = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                           text.data(), size, nullptr, 0);
  if (length <= 0) {
    return std::wstring();
  }
  std::wstring result(length, L'\0');
  ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), size,
                        result.data(), length);
  return result;
}

std::string PathOf(IShellItem* item) {
  PWSTR path = nullptr;
  if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
    return std::string();
  }
  std::string result = Utf8FromUtf16(path);
  ::CoTaskMemFree(path);
  return result;
}

// La finestra di Windows per scegliere file (anche più d'uno) o una
// cartella, con il titolo `title` e a partire da `start` se c'è. Vuoto se si
// annulla.
std::vector<std::string> Choose(HWND owner, bool folder,
                                const std::string& start,
                                const std::string& title) {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return {};
  }
  FILEOPENDIALOGOPTIONS options = 0;
  dialog->GetOptions(&options);
  options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
  options |= folder ? FOS_PICKFOLDERS : (FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST);
  dialog->SetOptions(options);
  if (folder) {
    dialog->SetTitle(title.empty() ? L"Scegli una cartella"
                                   : Utf16FromUtf8(title).c_str());
    dialog->SetOkButtonLabel(L"Scegli");
    ComPtr<IShellItem> initial;
    if (!start.empty() &&
        SUCCEEDED(::SHCreateItemFromParsingName(Utf16FromUtf8(start).c_str(),
                                                nullptr,
                                                IID_PPV_ARGS(&initial)))) {
      dialog->SetFolder(initial.Get());
    }
  } else {
    dialog->SetTitle(L"Scegli moduli, brani o archivi");
  }
  // Show fallisce anche quando si annulla.
  ComPtr<IShellItemArray> items;
  if (FAILED(dialog->Show(owner)) || FAILED(dialog->GetResults(&items))) {
    return {};
  }
  std::vector<std::string> paths;
  DWORD count = 0;
  items->GetCount(&count);
  for (DWORD i = 0; i < count; ++i) {
    ComPtr<IShellItem> item;
    if (SUCCEEDED(items->GetItemAt(i, &item))) {
      std::string path = PathOf(item.Get());
      if (!path.empty()) {
        paths.push_back(std::move(path));
      }
    }
  }
  return paths;
}

// Solo percorsi assoluti, come su macOS: "C:\..." o "\\server\...".
bool IsAbsolute(const std::string& path) {
  if (path.size() >= 3 && std::isalpha(static_cast<unsigned char>(path[0])) &&
      path[1] == ':' && (path[2] == '\\' || path[2] == '/')) {
    return true;
  }
  return path.rfind("\\\\", 0) == 0;
}

EncodableValue List(const std::vector<std::string>& paths) {
  EncodableList list;
  for (const auto& path : paths) {
    list.emplace_back(path);
  }
  return EncodableValue(list);
}

const std::string* TextArgument(const MethodCall<EncodableValue>& call) {
  return call.arguments() ? std::get_if<std::string>(call.arguments())
                          : nullptr;
}

// Un campo di testo degli argomenti, se sono una mappa; "" se manca.
std::string MapArgument(const MethodCall<EncodableValue>& call,
                        const char* key) {
  const auto* map = call.arguments()
                        ? std::get_if<flutter::EncodableMap>(call.arguments())
                        : nullptr;
  if (!map) {
    return std::string();
  }
  const auto found = map->find(EncodableValue(key));
  if (found == map->end()) {
    return std::string();
  }
  const auto* text = std::get_if<std::string>(&found->second);
  return text ? *text : std::string();
}

std::unique_ptr<Channel> NewChannel(flutter::BinaryMessenger* messenger,
                                    const char* name) {
  return std::make_unique<Channel>(
      messenger, name, &flutter::StandardMethodCodec::GetInstance());
}

}  // namespace

std::unique_ptr<Channel> CreateFilesChannel(flutter::BinaryMessenger* messenger,
                                            HWND window) {
  auto channel = NewChannel(messenger, "megaplayer/files");
  channel->SetMethodCallHandler(
      [window](const MethodCall<EncodableValue>& call,
               std::unique_ptr<MethodResult<EncodableValue>> result) {
        const std::string& method = call.method_name();
        if (method == "openFiles") {
          result->Success(
              List(Choose(window, false, std::string(), std::string())));
        } else if (method == "openFolder") {
          // Una cartella (della libreria, delle ROM): si parte da quella di
          // prima, se c'è, con il messaggio che dà l'app.
          const auto paths = Choose(window, true, MapArgument(call, "start"),
                                    MapArgument(call, "message"));
          result->Success(paths.empty() ? EncodableValue()
                                        : EncodableValue(paths.front()));
        } else if (method == "initialFiles") {
          std::vector<std::string> paths;
          for (const auto& argument : GetCommandLineArguments()) {
            if (IsAbsolute(argument)) {
              paths.push_back(argument);
            }
          }
          result->Success(List(paths));
        } else {
          result->NotImplemented();
        }
      });
  return channel;
}

std::unique_ptr<Channel> CreateIconChannel(flutter::BinaryMessenger* messenger,
                                           HWND window) {
  auto channel = NewChannel(messenger, "megaplayer/icon");
  channel->SetMethodCallHandler(
      [window](const MethodCall<EncodableValue>& call,
               std::unique_ptr<MethodResult<EncodableValue>> result) {
        const std::string* skin = TextArgument(call);
        if (call.method_name() != "setSkin" || !skin) {
          result->NotImplemented();
          return;
        }
        const int id = *skin == "retro" ? IDI_RETRO_ICON : IDI_APP_ICON;
        HINSTANCE instance = ::GetModuleHandle(nullptr);
        // La grande per la barra delle applicazioni e Alt+Tab, la piccola
        // per il titolo della finestra.
        const auto load = [&](int width_metric, int height_metric) {
          return reinterpret_cast<LPARAM>(::LoadImage(
              instance, MAKEINTRESOURCE(id), IMAGE_ICON,
              ::GetSystemMetrics(width_metric),
              ::GetSystemMetrics(height_metric), LR_SHARED));
        };
        ::SendMessage(window, WM_SETICON, ICON_BIG, load(SM_CXICON, SM_CYICON));
        ::SendMessage(window, WM_SETICON, ICON_SMALL,
                      load(SM_CXSMICON, SM_CYSMICON));
        result->Success();
      });
  return channel;
}

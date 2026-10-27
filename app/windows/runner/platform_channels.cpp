#include "platform_channels.h"

#include <flutter/standard_method_codec.h>
#include <ole2.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
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

// La finestra di Windows per scegliere file (anche più d'uno, se non è
// `single`, e solo delle estensioni `extensions`, se ci sono) o una cartella,
// con il titolo `title` e a partire da `start` se c'è. Vuoto se si annulla.
std::vector<std::string> Choose(HWND owner, bool folder,
                                const std::string& start,
                                const std::string& title, bool single = false,
                                const std::vector<std::string>& extensions = {}) {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return {};
  }
  FILEOPENDIALOGOPTIONS options = 0;
  dialog->GetOptions(&options);
  options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
  options |= folder ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST;
  if (!folder && !single) {
    options |= FOS_ALLOWMULTISELECT;
  }
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
    dialog->SetTitle(title.empty() ? L"Scegli moduli, brani o archivi"
                                   : Utf16FromUtf8(title).c_str());
  }
  std::wstring pattern;
  for (const auto& extension : extensions) {
    pattern += (pattern.empty() ? L"*." : L";*.") + Utf16FromUtf8(extension);
  }
  if (!pattern.empty()) {
    const COMDLG_FILTERSPEC filter = {pattern.c_str(), pattern.c_str()};
    dialog->SetFileTypes(1, &filter);
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

// La finestra di Windows per scegliere dove salvare un file, con il nome
// proposto e l'estensione che deve avere. "" se si annulla.
std::string ChooseSaveFile(HWND owner, const std::string& name,
                           const std::string& title,
                           const std::vector<std::string>& extensions) {
  ComPtr<IFileSaveDialog> dialog;
  if (FAILED(::CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
    return std::string();
  }
  FILEOPENDIALOGOPTIONS options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
  if (!title.empty()) {
    dialog->SetTitle(Utf16FromUtf8(title).c_str());
  }
  if (!name.empty()) {
    dialog->SetFileName(Utf16FromUtf8(name).c_str());
  }
  if (!extensions.empty()) {
    const std::wstring extension = Utf16FromUtf8(extensions.front());
    const std::wstring pattern = L"*." + extension;
    const COMDLG_FILTERSPEC filter = {pattern.c_str(), pattern.c_str()};
    dialog->SetFileTypes(1, &filter);
    dialog->SetDefaultExtension(extension.c_str());
  }
  ComPtr<IShellItem> item;
  if (FAILED(dialog->Show(owner)) || FAILED(dialog->GetResult(&item))) {
    return std::string();
  }
  return PathOf(item.Get());
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

const flutter::EncodableMap* MapArguments(
    const MethodCall<EncodableValue>& call) {
  return call.arguments()
             ? std::get_if<flutter::EncodableMap>(call.arguments())
             : nullptr;
}

// Un elenco di testi degli argomenti, se sono una mappa; vuoto se manca.
std::vector<std::string> ListArgument(const MethodCall<EncodableValue>& call,
                                      const char* key) {
  std::vector<std::string> values;
  const auto* map = MapArguments(call);
  if (!map) {
    return values;
  }
  const auto found = map->find(EncodableValue(key));
  if (found == map->end()) {
    return values;
  }
  if (const auto* list = std::get_if<EncodableList>(&found->second)) {
    for (const auto& item : *list) {
      if (const auto* text = std::get_if<std::string>(&item)) {
        values.push_back(*text);
      }
    }
  }
  return values;
}

// Un sì o no degli argomenti, se sono una mappa; no se manca.
bool FlagArgument(const MethodCall<EncodableValue>& call, const char* key) {
  const auto* map = MapArguments(call);
  if (!map) {
    return false;
  }
  const auto found = map->find(EncodableValue(key));
  const bool* flag =
      found == map->end() ? nullptr : std::get_if<bool>(&found->second);
  return flag && *flag;
}

// Un campo di testo degli argomenti, se sono una mappa; "" se manca.
std::string MapArgument(const MethodCall<EncodableValue>& call,
                        const char* key) {
  const auto* map = MapArguments(call);
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

// Un numero degli argomenti, se sono una mappa; [fallback] se manca.
double NumberArgument(const MethodCall<EncodableValue>& call, const char* key,
                      double fallback) {
  const auto* map = MapArguments(call);
  if (!map) {
    return fallback;
  }
  const auto found = map->find(EncodableValue(key));
  const double* number =
      found == map->end() ? nullptr : std::get_if<double>(&found->second);
  return number ? *number : fallback;
}

// Una mappa di testi degli argomenti, se sono una mappa; vuota se manca.
std::map<std::string, std::string> TextMapArgument(
    const MethodCall<EncodableValue>& call, const char* key) {
  std::map<std::string, std::string> texts;
  const auto* map = MapArguments(call);
  if (!map) {
    return texts;
  }
  const auto found = map->find(EncodableValue(key));
  const auto* inner = found == map->end()
                          ? nullptr
                          : std::get_if<flutter::EncodableMap>(&found->second);
  if (!inner) {
    return texts;
  }
  for (const auto& [name, value] : *inner) {
    const auto* text_key = std::get_if<std::string>(&name);
    const auto* text = std::get_if<std::string>(&value);
    if (text_key && text) {
      texts[*text_key] = *text;
    }
  }
  return texts;
}

std::unique_ptr<Channel> NewChannel(flutter::BinaryMessenger* messenger,
                                    const char* name) {
  return std::make_unique<Channel>(
      messenger, name, &flutter::StandardMethodCodec::GetInstance());
}

// Riceve da OLE i file trascinati sulla finestra e li passa all'app.
class FileDropTarget : public IDropTarget {
 public:
  explicit FileDropTarget(Channel* channel) : channel_(channel) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override {
    if (id == IID_IUnknown || id == IID_IDropTarget) {
      *object = static_cast<IDropTarget*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG references = --references_;
    if (references == 0) {
      delete this;
    }
    return references;
  }

  HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL,
                                      DWORD* effect) override {
    accepting_ = !Paths(data).empty();
    if (accepting_) {
      channel_->InvokeMethod("enter", nullptr);
    }
    *effect = accepting_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
    *effect = accepting_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE DragLeave() override {
    if (accepting_) {
      channel_->InvokeMethod("exit", nullptr);
    }
    accepting_ = false;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL,
                                 DWORD* effect) override {
    const std::vector<std::string> paths = Paths(data);
    *effect = paths.empty() ? DROPEFFECT_NONE : DROPEFFECT_COPY;
    channel_->InvokeMethod("drop", std::make_unique<EncodableValue>(List(paths)));
    accepting_ = false;
    return S_OK;
  }

 private:
  // I file e le cartelle trascinati, se sono file (CF_HDROP).
  static std::vector<std::string> Paths(IDataObject* data) {
    FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    std::vector<std::string> paths;
    if (FAILED(data->GetData(&format, &medium))) {
      return paths;
    }
    if (auto drop = static_cast<HDROP>(::GlobalLock(medium.hGlobal))) {
      const UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
      for (UINT i = 0; i < count; ++i) {
        const UINT length = ::DragQueryFileW(drop, i, nullptr, 0);
        std::wstring path(length, L'\0');
        ::DragQueryFileW(drop, i, path.data(), length + 1);
        paths.push_back(Utf8FromUtf16(path.c_str()));
      }
      ::GlobalUnlock(medium.hGlobal);
    }
    ::ReleaseStgMedium(&medium);
    return paths;
  }

  Channel* channel_;
  ULONG references_ = 1;
  bool accepting_ = false;
};

// Il miniplayer nell'area di notifica: l'icona, una sola, e lo stato che ha
// mandato l'app per ultimo.
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayIconId = 1;

// Le voci del menu; quelle del volume vanno da kTrayVolume (muto) a
// kTrayVolume + 4 (100%), a scatti del 25%.
enum TrayCommand : UINT {
  kTrayPlay = 1,
  kTrayPrevious,
  kTrayNext,
  kTrayOpen,
  kTrayQuit,
  kTrayVolume = 100,
};

struct Tray {
  Channel* channel = nullptr;
  HWND window = nullptr;
  UINT taskbar_created = 0;
  bool shown = false;
  std::string title;
  std::string subtitle;
  std::string skin;
  bool playing = false;
  bool can_play = false;
  bool can_previous = false;
  bool can_next = false;
  double volume = 0.8;
  std::map<std::string, std::string> labels;

  std::wstring Label(const char* key) const {
    const auto found = labels.find(key);
    return found == labels.end() ? std::wstring()
                                 : Utf16FromUtf8(found->second);
  }

  // Il titolo, o che non suona niente.
  std::wstring Heading() const {
    return title.empty() ? Label("idle") : Utf16FromUtf8(title);
  }
};

Tray tray;

NOTIFYICONDATAW TrayIconData() {
  NOTIFYICONDATAW data = {};
  data.cbSize = sizeof(data);
  data.hWnd = tray.window;
  data.uID = kTrayIconId;
  return data;
}

// Aggiunge l'icona, o la aggiorna se c'è già: quella della skin, alla misura
// piccola del monitor della finestra, e sotto il mouse il brano.
void ShowTrayIcon() {
  NOTIFYICONDATAW data = TrayIconData();
  data.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
  data.uCallbackMessage = kTrayMessage;
  const UINT dpi = ::GetDpiForWindow(tray.window);
  data.hIcon = static_cast<HICON>(::LoadImage(
      ::GetModuleHandle(nullptr),
      MAKEINTRESOURCE(tray.skin == "retro" ? IDI_RETRO_ICON : IDI_APP_ICON),
      IMAGE_ICON, ::GetSystemMetricsForDpi(SM_CXSMICON, dpi),
      ::GetSystemMetricsForDpi(SM_CYSMICON, dpi), LR_SHARED));
  std::wstring tip = tray.Heading();
  if (!tray.subtitle.empty()) {
    tip += L" \u2014 " + Utf16FromUtf8(tray.subtitle);
  }
  tip = tip.substr(0, std::size(data.szTip) - 1);
  std::copy(tip.begin(), tip.end(), data.szTip);
  if (tray.shown) {
    ::Shell_NotifyIconW(NIM_MODIFY, &data);
    return;
  }
  tray.shown = ::Shell_NotifyIconW(NIM_ADD, &data);
  if (tray.shown) {
    data.uVersion = NOTIFYICON_VERSION_4;
    ::Shell_NotifyIconW(NIM_SETVERSION, &data);
  }
}

void ShowMainWindow() {
  ::ShowWindow(tray.window, ::IsIconic(tray.window) ? SW_RESTORE : SW_SHOW);
  ::SetForegroundWindow(tray.window);
}

// Nei menu "&" sottolinea la lettera dopo: nei titoli va raddoppiata.
std::wstring MenuText(std::wstring text) {
  for (size_t at = text.find(L'&'); at != std::wstring::npos;
       at = text.find(L'&', at + 2)) {
    text.insert(at, 1, L'&');
  }
  return text;
}

void RunTrayCommand(UINT command) {
  if (command >= kTrayVolume && command <= kTrayVolume + 4) {
    tray.channel->InvokeMethod(
        "setVolume", std::make_unique<EncodableValue>(
                         (command - kTrayVolume) * 0.25));
    return;
  }
  switch (command) {
    case kTrayPlay:
      tray.channel->InvokeMethod("togglePause", nullptr);
      break;
    case kTrayPrevious:
      tray.channel->InvokeMethod("previous", nullptr);
      break;
    case kTrayNext:
      tray.channel->InvokeMethod("next", nullptr);
      break;
    case kTrayOpen:
      ShowMainWindow();
      break;
    case kTrayQuit:
      // Senza l'icona la finestra si chiude davvero, e l'app esce.
      RemoveTrayIcon();
      ::PostMessage(tray.window, WM_CLOSE, 0, 0);
      break;
  }
}

// Il menu dell'icona, nel punto [at]: il brano, che non si sceglie, i
// comandi, il volume a scatti con il segno su quello più vicino, la
// finestra (la voce in neretto) e l'uscita.
void ShowTrayMenu(POINT at) {
  HMENU menu = ::CreatePopupMenu();
  ::AppendMenuW(menu, MF_STRING | MF_DISABLED, 0,
                MenuText(tray.Heading()).c_str());
  if (!tray.subtitle.empty()) {
    ::AppendMenuW(menu, MF_STRING | MF_DISABLED, 0,
                  MenuText(Utf16FromUtf8(tray.subtitle)).c_str());
  }
  ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  const auto add = [menu](UINT id, const std::wstring& label, bool enabled) {
    ::AppendMenuW(menu, MF_STRING | (enabled ? MF_ENABLED : MF_GRAYED), id,
                  label.c_str());
  };
  add(kTrayPlay, tray.Label(tray.playing ? "pause" : "play"), tray.can_play);
  add(kTrayPrevious, tray.Label("previous"), tray.can_previous);
  add(kTrayNext, tray.Label("next"), tray.can_next);
  HMENU volume = ::CreatePopupMenu();
  for (UINT step = 0; step <= 4; ++step) {
    const std::wstring label =
        step == 0 ? tray.Label("mute") : std::to_wstring(step * 25) + L"%";
    ::AppendMenuW(volume, MF_STRING, kTrayVolume + step, label.c_str());
  }
  const UINT nearest = static_cast<UINT>(
      std::clamp(std::lround(tray.volume * 4), 0L, 4L));
  ::CheckMenuRadioItem(volume, kTrayVolume, kTrayVolume + 4,
                       kTrayVolume + nearest, MF_BYCOMMAND);
  ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(volume),
                tray.Label("volume").c_str());
  ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  add(kTrayOpen, tray.Label("open"), true);
  add(kTrayQuit, tray.Label("quit"), true);
  ::SetMenuDefaultItem(menu, kTrayOpen, FALSE);

  // Senza la finestra in primo piano il menu non si chiude cliccando fuori.
  ::SetForegroundWindow(tray.window);
  const UINT align = ::GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN
                                                              : TPM_LEFTALIGN;
  const UINT command = static_cast<UINT>(::TrackPopupMenuEx(
      menu, align | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD |
                TPM_NONOTIFY,
      at.x, at.y, tray.window, nullptr));
  ::PostMessage(tray.window, WM_NULL, 0, 0);
  ::DestroyMenu(menu);  // anche il sottomenu del volume
  RunTrayCommand(command);
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
          // Di solito brani e archivi, anche più d'uno; l'app può chiedere
          // un file solo, di certe estensioni (un SoundFont).
          result->Success(List(Choose(
              window, false, std::string(), MapArgument(call, "message"),
              FlagArgument(call, "single"), ListArgument(call, "extensions"))));
        } else if (method == "openFolder") {
          // Una cartella (della libreria, delle ROM): si parte da quella di
          // prima, se c'è, con il messaggio che dà l'app.
          const auto paths = Choose(window, true, MapArgument(call, "start"),
                                    MapArgument(call, "message"));
          result->Success(paths.empty() ? EncodableValue()
                                        : EncodableValue(paths.front()));
        } else if (method == "saveFile") {
          const std::string path = ChooseSaveFile(
              window, MapArgument(call, "name"), MapArgument(call, "message"),
              ListArgument(call, "extensions"));
          result->Success(path.empty() ? EncodableValue() : EncodableValue(path));
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

std::unique_ptr<Channel> CreateDropChannel(flutter::BinaryMessenger* messenger,
                                           HWND window) {
  auto channel = NewChannel(messenger, "megaplayer/drop");
  // Il trascinamento passa da OLE, che va avviato sul thread della finestra.
  if (SUCCEEDED(::OleInitialize(nullptr))) {
    auto* target = new FileDropTarget(channel.get());
    ::RegisterDragDrop(window, target);
    target->Release();  // RegisterDragDrop tiene il suo riferimento
  }
  return channel;
}

void RevokeDropTarget(HWND window) {
  if (SUCCEEDED(::RevokeDragDrop(window))) {
    ::OleUninitialize();
  }
}

std::unique_ptr<Channel> CreateTrayChannel(flutter::BinaryMessenger* messenger,
                                           HWND window) {
  auto channel = NewChannel(messenger, "megaplayer/tray");
  tray.channel = channel.get();
  tray.window = window;
  // Se Esplora risorse riparte, le icone vanno rimesse.
  tray.taskbar_created = ::RegisterWindowMessageW(L"TaskbarCreated");
  channel->SetMethodCallHandler(
      [](const MethodCall<EncodableValue>& call,
         std::unique_ptr<MethodResult<EncodableValue>> result) {
        if (call.method_name() != "update") {
          result->NotImplemented();
          return;
        }
        if (!FlagArgument(call, "visible")) {
          RemoveTrayIcon();
          result->Success();
          return;
        }
        tray.title = MapArgument(call, "title");
        tray.subtitle = MapArgument(call, "subtitle");
        tray.skin = MapArgument(call, "skin");
        tray.playing = FlagArgument(call, "playing");
        tray.can_play = FlagArgument(call, "canPlay");
        tray.can_previous = FlagArgument(call, "canPrevious");
        tray.can_next = FlagArgument(call, "canNext");
        tray.volume = NumberArgument(call, "volume", tray.volume);
        tray.labels = TextMapArgument(call, "labels");
        ShowTrayIcon();
        result->Success();
      });
  return channel;
}

bool HandleTrayMessage(HWND window, UINT message, WPARAM wparam,
                       LPARAM lparam) {
  if (window != tray.window) {
    return false;
  }
  if (message == tray.taskbar_created && tray.taskbar_created != 0) {
    if (tray.shown) {
      tray.shown = false;
      ShowTrayIcon();
    }
    return false;
  }
  if (message != kTrayMessage) {
    return false;
  }
  // Con NOTIFYICON_VERSION_4 l'evento è in lparam e il punto in wparam.
  switch (LOWORD(lparam)) {
    case NIN_SELECT:
    case NIN_KEYSELECT:
    case WM_CONTEXTMENU:
      ShowTrayMenu({GET_X_LPARAM(wparam), GET_Y_LPARAM(wparam)});
      break;
  }
  return true;
}

bool TrayIconShown() { return tray.shown; }

void RemoveTrayIcon() {
  if (tray.shown) {
    NOTIFYICONDATAW data = TrayIconData();
    ::Shell_NotifyIconW(NIM_DELETE, &data);
  }
  tray.shown = false;
}

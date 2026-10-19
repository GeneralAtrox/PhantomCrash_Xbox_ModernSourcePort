#include "desktop_slai.hpp"
#include "desktop_settings_menu.hpp"
#include "phantomcrash/slai_content.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <optional>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace phantomcrash {
namespace {
std::optional<std::filesystem::path> choose_iso() {
  struct Answer {
    std::atomic<bool> done{};
    std::optional<std::filesystem::path> path;
  } answer;
  const SDL_DialogFileFilter filter{"S.L.A.I. European PS2 disc image", "iso"};
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    throw std::runtime_error(SDL_GetError());
  }
  SDL_ShowOpenFileDialog(
      [](void *user, const char *const *files, int) {
        auto &answer = *static_cast<Answer *>(user);
        if (files && files[0]) {
          answer.path = std::filesystem::path(reinterpret_cast<const char8_t *>(files[0]));
        }
        answer.done = true;
      },
      &answer, nullptr, &filter, 1, nullptr, false);
  while (!answer.done) {
    SDL_PumpEvents();
    SDL_Delay(15);
  }
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  return answer.path;
}

#ifdef _WIN32
std::wstring quoted(const std::filesystem::path &path) {
  auto value = path.wstring();
  if (value.ends_with(L'\\')) {
    value += L'\\';
  }
  return L"\"" + value + L"\"";
}
bool import_iso(const std::filesystem::path &root, const std::filesystem::path &disc) {
  const auto directory =
      std::filesystem::path(reinterpret_cast<const char8_t *>(SDL_GetBasePath()));
  const auto program = directory / "phantomcrash_slai_install.exe";
  const auto converter = directory / "phantomcrash_slai_audio.exe";
  if (!std::filesystem::is_regular_file(program) || !std::filesystem::is_regular_file(converter)) {
    throw std::runtime_error(
        "The S.L.A.I. installer is missing. Extract the complete release ZIP.");
  }
  auto command = quoted(program) + L" --root " + quoted(root) + L" --disc " + quoted(disc) +
                 L" --converter " + quoted(converter);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE,
                      CREATE_NEW_CONSOLE | CREATE_SUSPENDED, nullptr, nullptr, &startup,
                      &process)) {
    throw std::runtime_error("Cannot start the S.L.A.I. installer");
  }
  const HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job ||
      !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
      !AssignProcessToJobObject(job, process.hProcess)) {
    TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (job) {
      CloseHandle(job);
    }
    throw std::runtime_error("Cannot supervise the S.L.A.I. installer");
  }
  ResumeThread(process.hThread);
  CloseHandle(process.hThread);
  const auto started = SDL_GetTicks();
  while (WaitForSingleObject(process.hProcess, 30) == WAIT_TIMEOUT &&
         SDL_GetTicks() - started < 30 * 60 * 1000) {
    SDL_PumpEvents();
  }
  DWORD code = 1;
  GetExitCodeProcess(process.hProcess, &code);
  CloseHandle(job); // Also ends a hung importer and its owned audio-converter children.
  WaitForSingleObject(process.hProcess, 5000);
  CloseHandle(process.hProcess);
  return code == 0;
}
#endif
} // namespace

bool prepare_slai_data(const std::filesystem::path &root, bool interactive) {
  if (slai_installed(root)) {
    return true;
  }
  if (!interactive) {
    return false;
  }
  try {
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION, "Install S.L.A.I. content",
        "Select your European S.L.A.I. PlayStation 2 ISO (SLES-52940).\n"
        "Conversion may take several minutes. The ISO is needed only for installation.",
        nullptr);
    const auto disc = choose_iso();
    if (!disc) {
      return false;
    }
#ifdef _WIN32
    if (import_iso(root, *disc) && slai_installed(root)) {
      return true;
    }
#endif
    throw std::runtime_error(
        "Installation did not complete. Check that the ISO is the European "
        "S.L.A.I. disc and that there is enough disk space. S.L.A.I. stays off.");
  } catch (const std::exception &error) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "S.L.A.I. installation", error.what(), nullptr);
    return false;
  }
}

void update_slai_menu(SettingsMenu &menu, const std::filesystem::path &root,
                      const std::filesystem::path &configuration) {
  if (!menu.take_slai_toggle()) {
    return;
  }
  const bool enabled = !menu.settings().slai_enabled;
  if (enabled && !prepare_slai_data(root, true)) {
    return;
  }
  auto settings = load_settings(configuration);
  if (settings.save_slots_unreadable) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "S.L.A.I.",
                             "Settings are unreadable; the content setting was not changed.",
                             nullptr);
    return;
  }
  settings.slai_enabled = enabled;
  if (!save_settings(configuration, settings)) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "S.L.A.I.", "Cannot save this setting.",
                             nullptr);
    return;
  }
  menu.set_slai_enabled(enabled);
  SDL_ShowSimpleMessageBox(
      SDL_MESSAGEBOX_INFORMATION, "S.L.A.I.",
      "Restart the game to apply this setting.\n"
      "Base-game and S.L.A.I. campaigns use separate saves. Existing saves are kept.",
      nullptr);
}
} // namespace phantomcrash

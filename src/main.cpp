#include "web_ui.hpp"
#include "i18n.hpp"
#include "startup_options.hpp"
#include <Windows.h>
#include <Ole2.h>
#include <shellapi.h>
#include <TlHelp32.h>
#include <clocale>

namespace
{
	// 保留旧入口的同路径单实例规则，只查找当前 Web 宿主窗口。
	HWND existing_web_window(const std::filesystem::path &executable)
	{
		const auto snapshot {CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
		if(snapshot == INVALID_HANDLE_VALUE) return nullptr;
		PROCESSENTRY32W process {}; process.dwSize = sizeof(process);
		HWND found {nullptr};
		if(Process32FirstW(snapshot, &process))
			do
			{
				if(process.th32ProcessID == GetCurrentProcessId() || executable.filename().wstring() != process.szExeFile) continue;
				const auto handle {OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, process.th32ProcessID)};
				if(!handle) continue;
				wchar_t path[32768]; DWORD size {32768};
				const bool matches {QueryFullProcessImageNameW(handle, 0, path, &size) && executable.wstring() == std::wstring {path, size}};
				CloseHandle(handle);
				if(!matches) continue;
				struct window_search { DWORD pid; HWND hwnd; } search {process.th32ProcessID, nullptr};
				EnumWindows([](HWND hwnd, LPARAM parameter) -> BOOL
				{
					auto &search {*reinterpret_cast<window_search *>(parameter)};
					DWORD pid {0}; GetWindowThreadProcessId(hwnd, &pid);
					wchar_t name[256] {}; GetClassNameW(hwnd, name, 256);
					if(pid == search.pid && IsWindowVisible(hwnd) && std::wstring {name} == L"YtdlpWebUI") { search.hwnd = hwnd; return FALSE; }
					return TRUE;
				}, reinterpret_cast<LPARAM>(&search));
				if(search.hwnd) { found = search.hwnd; break; }
			} while(Process32NextW(snapshot, &process));
		CloseHandle(snapshot);
		return found;
	}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	std::setlocale(LC_ALL, "en_US.UTF-8");
	wchar_t path[32768]; GetModuleFileNameW(nullptr, path, 32768);
	const std::filesystem::path executable {path}, appdir {executable.parent_path()};
	std::filesystem::current_path(appdir);
	i18n::load(appdir / "locales", "zh-CN");
	i18n::load_embedded("zh-CN");
	int count {0}; const auto arguments {CommandLineToArgvW(GetCommandLineW(), &count)};
	const auto startup {read_startup_options(std::vector<std::wstring> {arguments + 1, arguments + count})};
	LocalFree(arguments);
	if(!startup.error.empty()) { MessageBoxW(nullptr, i18n::wide(startup.error).c_str(), i18n::wide("web.title").c_str(), MB_OK | MB_ICONERROR); return 1; }
	if(const auto window {existing_web_window(executable)})
	{
		if(IsIconic(window)) ShowWindow(window, SW_RESTORE);
		SetForegroundWindow(window);
		BringWindowToTop(window);
		if(!startup.url.empty()) MessageBoxW(window, i18n::wide("web.startup.running_url").c_str(), i18n::wide("web.title").c_str(), MB_OK | MB_ICONINFORMATION);
		return 0;
	}
	if(FAILED(OleInitialize(nullptr))) { MessageBoxW(nullptr, i18n::wide("web.startup.window_failed").c_str(), i18n::wide("web.title").c_str(), MB_OK | MB_ICONERROR); return 1; }
	const auto result {run_web_ui(appdir, "yt-dlp.exe", startup.url)};
	OleUninitialize();
	return result;
}

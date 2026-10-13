#include "windows_desktop.hpp"
#include "core_text.hpp"
#include "i18n.hpp"
#include "components.hpp"
#include "about.hpp"
#include <WebView2.h>
#include <Shlobj.h>
#include <Exdisp.h>
#include <Shldisp.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <powrprof.h>
#include <fstream>
#include <regex>

namespace fs = std::filesystem;
namespace
{
	bool regular_file(const fs::path &file) { std::error_code error; return !file.empty() && fs::is_regular_file(file, error); }
	bool directory(const fs::path &folder) { std::error_code error; return fs::is_directory(folder, error); }
	unsigned monitor_dpi(HMONITOR monitor)
	{
		UINT x {96}, y {96}; GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &x, &y); return x;
	}
	MONITORINFOEXW monitor_info(HMONITOR monitor)
	{
		MONITORINFOEXW info {}; info.cbSize = sizeof(info); GetMonitorInfoW(monitor, &info); return info;
	}
	void flatten(const nlohmann::json &value, const std::string &path, std::string &text)
	{
		if(value.is_object() || value.is_array())
			for(const auto &entry : value.items()) flatten(entry.value(), path.empty() ? entry.key() : path + "." + entry.key(), text);
		else text += path + ": " + (value.is_string() ? value.get<std::string>() : value.dump()) + "\r\n";
	}
}

fs::path windows_desktop::environment_path(const wchar_t *name)
{
	const auto size {GetEnvironmentVariableW(name, nullptr, 0)}; if(!size) return {};
	std::wstring value(size, L'\0'); value.resize(GetEnvironmentVariableW(name, value.data(), size)); return value;
}

windows_desktop::windows_desktop(HWND owner, completion_executor executor) : owner {owner}, completion_service {std::move(executor)}
{
	// 仅显式隔离测试路径启用 C++ 模拟器；配置损坏时失败，不回退到真实电源操作。
	if(const auto test_file {environment_path(L"YTDLP_COMPLETION_TEST_FILE")}; !test_file.empty() && !completion_service.execute)
	{
		const auto read = [test_file]
		{
			if(!test_file.is_absolute()) throw desktop_error {"completionTestInvalid", "web.completion_failed", ERROR_INVALID_DATA};
			std::ifstream file {test_file}; return json::parse(file);
		};
		read(); completion_service.simulated = true;
		completion_service.capabilities = [read] { auto caps {read().at("capabilities")}; caps["none"] = caps["exit"] = true; return caps; };
		completion_service.execute = [read, test_file, owner](const auto &action, auto token)
		{
			const auto config {read()}; const auto code {config.value("errorCode", 0ul)};
			std::ofstream log {test_file.wstring() + L".requests.jsonl", std::ios::app | std::ios::binary};
			log << json {{"action", action}, {"token", token}, {"simulated", true}, {"systemError", code}, {"timestamp", core::timestamp_now()}}.dump() << '\n';
			log.flush(); if(!log) throw desktop_error {"completionTestLogFailed", "web.completion_failed", ERROR_WRITE_FAULT};
			if(code) throw desktop_error {"completionFailed", "web.completion_failed", code};
			if(action == "exit" && config.value("closeOnExit", false) && !PostMessageW(owner, WM_CLOSE, 0, 0)) throw desktop_error {"completionFailed", "web.completion_failed", GetLastError()};
		};
	}
	state_file = environment_path(L"YTDLP_HOST_FILE");
	if(state_file.empty()) state_file = environment_path(L"LOCALAPPDATA") / "ytdlp-interface" / "WebHost" / "window-v1.json";
	try
	{
		if(!state_file.is_absolute()) throw std::runtime_error {"path"};
		fs::create_directories(state_file.parent_path());
		state_lock = CreateFileW((state_file.wstring() + L".lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if(state_lock == INVALID_HANDLE_VALUE) throw std::runtime_error {"lock"};
		writable = true;
	}
	catch(const std::exception &) { state_error = "hostSettingsUnavailable"; }
	system_theme();
	progress_result = taskbar.CoCreateInstance(CLSID_TaskbarList);
	if(SUCCEEDED(progress_result)) progress_result = taskbar->HrInit();
	if(FAILED(progress_result)) taskbar.Release();
}

windows_desktop::~windows_desktop() { if(state_lock != INVALID_HANDLE_VALUE) CloseHandle(state_lock); }

void windows_desktop::system_theme()
{
	DWORD value {1}, size {sizeof(value)};
	RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
	light = value != 0;
	const BOOL dark {settings.theme == "dark" || settings.theme == "system" && !light};
	DwmSetWindowAttribute(owner, 20, &dark, sizeof(dark));
}

void windows_desktop::apply(const core::appearance_settings &value) { settings = value; system_theme(); }

windows_desktop::json windows_desktop::choose(const std::string &kind, const fs::path &folder, const std::string &name)
{
	CComPtr<IFileDialog> dialog;
	const bool save {kind == "output" || kind == "txt" || kind == "json" || kind == "pretty"};
	const auto created {dialog.CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog)};
	if(FAILED(created)) throw desktop_error {"dialogFailed", "web.folder_failed"};
	DWORD flags {0}; dialog->GetOptions(&flags);
	dialog->SetOptions(flags | FOS_FORCEFILESYSTEM | (save ? FOS_OVERWRITEPROMPT | FOS_NOTESTFILECREATE : kind == "folder" ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST));
	dialog->SetTitle(i18n::wide(kind == "folder" ? "web.choose_folder" : kind == "component" ? "web.component.choose_executable" : kind == "cookie" ? "web.choose_cookie_file" : kind == "output" ? "web.choose_output_file" : "web.export_metadata").c_str());
	CComPtr<IShellItem> initial;
	if(directory(folder) && SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&initial)))) dialog->SetFolder(initial);
	if(!name.empty()) dialog->SetFileName(core::to_wide(name).c_str());
	if(kind == "component")
	{
		const auto description {i18n::wide("web.component.executable_files")}; const COMDLG_FILTERSPEC filter {description.c_str(), L"*.exe"}; dialog->SetFileTypes(1, &filter);
	}
	if(kind == "txt" || kind == "json" || kind == "pretty")
	{
		const auto description {i18n::wide(kind == "txt" ? "web.text_files" : "web.json_files")};
		const COMDLG_FILTERSPEC filter {description.c_str(), kind == "txt" ? L"*.txt" : L"*.json"};
		dialog->SetFileTypes(1, &filter); dialog->SetDefaultExtension(kind == "txt" ? L"txt" : L"json");
	}
	const auto shown {dialog->Show(owner)};
	if(shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {{"cancelled", true}};
	if(FAILED(shown)) throw desktop_error {"dialogFailed", "web.folder_failed"};
	CComPtr<IShellItem> selected; PWSTR text {nullptr};
	if(FAILED(dialog->GetResult(&selected)) || FAILED(selected->GetDisplayName(SIGDN_FILESYSPATH, &text))) throw desktop_error {"dialogFailed", "web.folder_failed"};
	const fs::path path {text}; CoTaskMemFree(text);
	if(!path.is_absolute() || kind == "folder" && !directory(path) || (kind == "cookie" || kind == "component") && !regular_file(path) || save && !directory(path.parent_path())) throw desktop_error {"invalidPath", "web.file_missing"};
	return {{"cancelled", false}, {kind == "folder" ? "folder" : kind == "cookie" || kind == "component" ? "file" : "filename", core::to_utf8(path.wstring())}};
}

windows_desktop::json windows_desktop::export_metadata(const json &metadata, const std::string &format, const fs::path &folder)
{
	if(format != "txt" && format != "json" && format != "pretty") throw desktop_error {"invalidPayload", "web.invalid_payload"};
	const auto selected {choose(format, folder, "metadata." + std::string(format == "txt" ? "txt" : "json"))};
	if(selected.at("cancelled")) return selected;
	std::string text;
	if(format == "txt") flatten(metadata, "", text);
	else text = metadata.dump(format == "pretty" ? 2 : -1) + '\n';
	const auto file {CreateFileW(fs::u8path(selected.at("filename").get<std::string>()).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
	DWORD written {0};
	const bool success {file != INVALID_HANDLE_VALUE && WriteFile(file, text.data(), DWORD(text.size()), &written, nullptr) && written == text.size() && FlushFileBuffers(file)};
	if(file != INVALID_HANDLE_VALUE) CloseHandle(file);
	if(!success) throw desktop_error {"exportFailed", "web.export_failed"};
	return selected;
}

void windows_desktop::open_file(const fs::path &file)
{
	if(!regular_file(file)) throw desktop_error {"fileNotFound", "web.file_missing"};
	if(reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", file.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) throw desktop_error {"openFailed", "web.open_failed"};
}

void windows_desktop::open_component_release(const std::string &component, const std::string &channel, const std::string &tag)
{
	if(component == "gui" || component != "yt-dlp" && component != "ffmpeg" && component != "deno" || component == "yt-dlp" && channel != "stable" && channel != "nightly") throw desktop_error {"invalidPayload", "web.invalid_payload"};
	if(component == "ffmpeg" ? tag != "latest" && !std::regex_match(tag, std::regex {R"(autobuild-\d{4}-\d{2}-\d{2}-\d{2}-\d{2})"}) : core::compare_component_versions(component, tag, tag) != "equal") throw desktop_error {"invalidPayload", "web.invalid_payload"};
	const auto repository {component == "yt-dlp" ? channel == "stable" ? "yt-dlp/yt-dlp" : "yt-dlp/yt-dlp-nightly-builds" : component == "ffmpeg" ? "yt-dlp/FFmpeg-Builds" : "denoland/deno"};
	const auto url {core::to_wide("https://github.com/" + std::string {repository} + "/releases/tag/" + tag)};
	if(reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) throw desktop_error {"openFailed", "web.open_failed"};
}

windows_desktop::json windows_desktop::load_about(const json &components, const fs::path &appdir) const
{
	PWSTR version {nullptr}; std::string runtime;
	if(SUCCEEDED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &version)) && version) runtime = core::to_utf8(version);
	CoTaskMemFree(version);
	auto value {core::about_information(runtime)};
	value["programVersion"] = ""; value["components"] = components;
	for(const auto &component : components) if(component.at("component") == "gui") value["programVersion"] = component.at("currentVersion");
	value["bundledLicensesAvailable"] = directory(appdir / "licenses");
	return value;
}

void windows_desktop::open_about_link(const std::string &id, const fs::path &appdir)
{
	std::wstring target;
	if(id == "bundledLicenses")
	{
		if(!directory(appdir / "licenses")) throw desktop_error {"fileNotFound", "web.file_missing"};
		target = (appdir / "licenses").wstring();
	}
	else target = core::to_wide(core::about_link(id));
	if(reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) throw desktop_error {"openFailed", "web.open_failed"};
}

void windows_desktop::reveal_file(const fs::path &file, const fs::path &folder)
{
	if(!file.empty())
	{
		if(!regular_file(file)) throw desktop_error {"fileNotFound", "web.file_missing"};
		PIDLIST_ABSOLUTE item {ILCreateFromPathW(file.c_str())};
		const auto result {item ? SHOpenFolderAndSelectItems(item, 0, nullptr, 0) : E_FAIL}; if(item) ILFree(item);
		if(FAILED(result)) throw desktop_error {"openFailed", "web.open_failed"};
		// 新开的 Explorer 视图可能尚未装载文件，等待该目录视图后明确选择目标。
		CComPtr<IShellWindows> windows;
		if(SUCCEEDED(windows.CoCreateInstance(CLSID_ShellWindows)))
			for(unsigned attempt {0}; attempt < 50; ++attempt)
			{
				long count {0}; windows->get_Count(&count);
				for(long index {0}; index < count; ++index)
				{
					CComPtr<IDispatch> dispatch; windows->Item(CComVariant(index), &dispatch);
					CComQIPtr<IWebBrowser2> browser {dispatch}; if(!browser) continue;
					CComPtr<IDispatch> document; browser->get_Document(&document);
					CComQIPtr<IShellFolderViewDual> view {document}; if(!view) continue;
					CComPtr<Folder> folder; view->get_Folder(&folder);
					CComQIPtr<Folder2> current {folder}; if(!current) continue;
					CComPtr<FolderItem> self; current->get_Self(&self); if(!self) continue;
					CComBSTR path; self->get_Path(&path); if(!path) continue;
					std::error_code error; if(!fs::equivalent(fs::path {path.m_str}, file.parent_path(), error)) continue;
					CComPtr<FolderItem> target; folder->ParseName(CComBSTR(file.filename().c_str()), &target); if(!target) continue;
					CComVariant selection {target.p};
					if(SUCCEEDED(view->SelectItem(&selection, SVSI_SELECT | SVSI_DESELECTOTHERS | SVSI_FOCUSED | SVSI_ENSUREVISIBLE)))
					{
						CComPtr<FolderItems> selected; view->SelectedItems(&selected); if(!selected) continue;
						long selected_count {0}; selected->get_Count(&selected_count); if(selected_count != 1) continue;
						CComPtr<FolderItem> selected_item; selected->Item(CComVariant(0), &selected_item); if(!selected_item) continue;
						CComBSTR selected_path; selected_item->get_Path(&selected_path);
						if(selected_path && fs::equivalent(fs::path {selected_path.m_str}, file, error)) return;
					}
				}
				Sleep(100);
			}
		throw desktop_error {"openFailed", "web.open_failed"};
	}
	else if(!directory(folder) || reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) throw desktop_error {"fileNotFound", "web.file_missing"};
}

void windows_desktop::copy_text(const std::string &text)
{
	const auto wide {core::to_wide(text)};
	if(!OpenClipboard(owner)) throw desktop_error {"clipboardUnavailable", "web.clipboard_unavailable"};
	const auto memory {GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t))};
	const auto data {memory ? GlobalLock(memory) : nullptr};
	bool copied {false};
	if(data)
	{
		memcpy(data, wide.c_str(), (wide.size() + 1) * sizeof(wchar_t)); GlobalUnlock(memory);
		copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
	}
	if(!copied && memory) GlobalFree(memory);
	CloseClipboard();
	if(!copied) throw desktop_error {"clipboardFailed", "web.clipboard_failed"};
}

std::string windows_desktop::clipboard_text(bool changed_only)
{
	const auto sequence {GetClipboardSequenceNumber()}; if(changed_only && sequence == clipboard_sequence) return {};
	if(!OpenClipboard(owner)) throw desktop_error {"clipboardUnavailable", "web.clipboard_unavailable"};
	std::string text;
	if(const auto memory {GetClipboardData(CF_UNICODETEXT)}; memory)
		if(const auto value {static_cast<const wchar_t *>(GlobalLock(memory))}; value) { text = core::to_utf8(value); GlobalUnlock(memory); }
	CloseClipboard(); clipboard_sequence = sequence; return text;
}

void windows_desktop::restore()
{
	const auto initial_monitor {MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST)};
	const auto initial_work {monitor_info(initial_monitor).rcWork}; const auto initial_dpi {monitor_dpi(initial_monitor)};
	const auto initial_width {std::min<int>(MulDiv(default_width, initial_dpi, 96), initial_work.right - initial_work.left)};
	const auto initial_height {std::min<int>(MulDiv(default_height, initial_dpi, 96), initial_work.bottom - initial_work.top)};
	SetWindowPos(owner, nullptr, initial_work.left + (initial_work.right - initial_work.left - initial_width) / 2,
		initial_work.top + (initial_work.bottom - initial_work.top - initial_height) / 2, initial_width, initial_height, SWP_NOZORDER | SWP_NOACTIVATE);
	GetWindowRect(owner, &normal_bounds);
	if(!writable || !fs::exists(state_file)) return;
	try
	{
		std::ifstream input {state_file}; const auto value {json::parse(input)};
		if(value.at("schemaVersion") != 1) throw std::runtime_error {"version"};
		const RECT saved {value.at("left"), value.at("top"), value.at("right"), value.at("bottom")};
		HMONITOR monitor {nullptr};
		struct search {std::wstring device; HMONITOR monitor {nullptr};} find {core::to_wide(value.at("monitor").get<std::string>())};
		EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR current, HDC, LPRECT, LPARAM data) -> BOOL
		{
			auto &find {*reinterpret_cast<search *>(data)};
			if(find.device == monitor_info(current).szDevice) find.monitor = current;
			return TRUE;
		}, reinterpret_cast<LPARAM>(&find));
		monitor = find.monitor ? find.monitor : MonitorFromRect(&saved, MONITOR_DEFAULTTONEAREST);
		const auto info {monitor_info(monitor)}; const auto &work {info.rcWork};
		const auto dpi {monitor_dpi(monitor)};
		const auto old_dpi {value.at("dpi").get<unsigned>()}; if(!old_dpi || saved.right <= saved.left || saved.bottom <= saved.top) throw std::runtime_error {"rect"};
		const auto scale = [&](int size) { return MulDiv(size, dpi, old_dpi); };
		const auto minimum {settings.no_min_width ? GetSystemMetricsForDpi(SM_CXMINTRACK, dpi) : MulDiv(minimum_width, dpi, 96)};
		const auto width {std::clamp<int>(scale(saved.right - saved.left), std::min<int>(minimum, work.right - work.left), work.right - work.left)};
		const auto height {std::clamp<int>(scale(saved.bottom - saved.top), std::min<int>(MulDiv(minimum_height, dpi, 96), work.bottom - work.top), work.bottom - work.top)};
		const auto x {std::clamp(work.left + scale(saved.left - value.at("workLeft").get<int>()), work.left, work.right - width)};
		const auto y {std::clamp(work.top + scale(saved.top - value.at("workTop").get<int>()), work.top, work.bottom - height)};
		SetWindowPos(owner, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE); normal_bounds = {x, y, x + width, y + height};
		maximized = value.at("maximized").get<bool>();
	}
	catch(const std::exception &) { writable = false; state_error = "hostSettingsRestoreFailed"; }
	const auto show {maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL};
	// 首次调用可能采用启动器标志，随后明确应用保存的窗口状态。
	ShowWindow(owner, show); ShowWindow(owner, show);
}

void windows_desktop::save()
{
	if(!writable) return;
	const auto info {monitor_info(MonitorFromRect(&normal_bounds, MONITOR_DEFAULTTONEAREST))};
	const auto bytes {json {{"schemaVersion", 1}, {"left", normal_bounds.left}, {"top", normal_bounds.top}, {"right", normal_bounds.right}, {"bottom", normal_bounds.bottom},
		{"maximized", IsIconic(owner) ? maximized : IsZoomed(owner) != FALSE}, {"dpi", GetDpiForWindow(owner)}, {"monitor", core::to_utf8(info.szDevice)},
		{"workLeft", info.rcWork.left}, {"workTop", info.rcWork.top}}.dump(2) + '\n'};
	const auto temporary {state_file.wstring() + L".tmp"};
	const auto file {CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
	DWORD written {0};
	const bool success {file != INVALID_HANDLE_VALUE && WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) && written == bytes.size() && FlushFileBuffers(file)};
	if(file != INVALID_HANDLE_VALUE) CloseHandle(file);
	if(!success || !MoveFileExW(temporary.c_str(), state_file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) state_error = "hostSettingsWriteFailed";
}

bool windows_desktop::message(UINT message, WPARAM wparam, LPARAM lparam, LRESULT &result)
{
	if(message == WM_GETMINMAXINFO)
	{
		auto &info {*reinterpret_cast<MINMAXINFO *>(lparam)}; const auto dpi {GetDpiForWindow(owner)};
		const auto work {monitor_info(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST)).rcWork};
		info.ptMinTrackSize.x = std::min<int>(settings.no_min_width ? GetSystemMetricsForDpi(SM_CXMINTRACK, dpi) : MulDiv(minimum_width, dpi, 96), work.right - work.left);
		info.ptMinTrackSize.y = std::min<int>(MulDiv(minimum_height, dpi, 96), work.bottom - work.top); result = 0; return true;
	}
	if(message == WM_MOVING && settings.snap)
	{
		auto &rect {*reinterpret_cast<RECT *>(lparam)}; const auto work {monitor_info(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST)).rcWork};
		const auto margin {MulDiv(12, GetDpiForWindow(owner), 96)};
		const auto x {abs(rect.left - work.left) <= margin ? work.left - rect.left : abs(rect.right - work.right) <= margin ? work.right - rect.right : 0};
		const auto y {abs(rect.top - work.top) <= margin ? work.top - rect.top : abs(rect.bottom - work.bottom) <= margin ? work.bottom - rect.bottom : 0};
		OffsetRect(&rect, x, y); result = TRUE; return true;
	}
	if(message == WM_DPICHANGED)
	{
		const auto &rect {*reinterpret_cast<RECT *>(lparam)}; const auto dpi {LOWORD(wparam)};
		const auto work {monitor_info(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST)).rcWork};
		const auto minimum {settings.no_min_width ? GetSystemMetricsForDpi(SM_CXMINTRACK, dpi) : MulDiv(minimum_width, dpi, 96)};
		const auto width {std::clamp<int>(rect.right - rect.left, std::min<int>(minimum, work.right - work.left), work.right - work.left)};
		const auto height {std::clamp<int>(rect.bottom - rect.top, std::min<int>(MulDiv(minimum_height, dpi, 96), work.bottom - work.top), work.bottom - work.top)};
		SetWindowPos(owner, nullptr, std::clamp(rect.left, work.left, work.right - width), std::clamp(rect.top, work.top, work.bottom - height), width, height, SWP_NOZORDER | SWP_NOACTIVATE); result = 0; return true;
	}
	if(message == WM_SETTINGCHANGE || message == WM_THEMECHANGED) system_theme();
	if(message == WM_WINDOWPOSCHANGED && !IsIconic(owner))
	{
		maximized = IsZoomed(owner) != FALSE; if(!maximized) GetWindowRect(owner, &normal_bounds);
	}
	if(message == RegisterWindowMessageW(L"TaskbarButtonCreated") && taskbar)
	{
		progress_result = taskbar->SetProgressState(owner, progress_state);
		if(SUCCEEDED(progress_result) && progress_state != TBPF_NOPROGRESS && progress_state != TBPF_INDETERMINATE) progress_result = taskbar->SetProgressValue(owner, progress_value, 10000);
	}
	return false;
}

void windows_desktop::update_progress(const core::queue_snapshot &queue)
{
	size_t running {0}, pending {0}, stopped {0}, failed {0}, count {0}; double completed {0};
	for(const auto &task : queue.tasks)
	{
		if(task.status == core::task_status::skipped) continue;
		++count;
		if(core::active(task.status)) ++running;
		if(task.status == core::task_status::waiting) ++pending;
		if(task.status == core::task_status::stopped) ++stopped;
		if(task.status == core::task_status::failed) ++failed;
		completed += task.status == core::task_status::done ? 100 : std::clamp(task.percent, 0.0, 100.0);
	}
	progress_value = count ? ULONGLONG(completed * 100 / count) : 0;
	progress_state = running ? (progress_value ? TBPF_NORMAL : TBPF_INDETERMINATE) : failed ? TBPF_ERROR : stopped || pending ? TBPF_PAUSED : TBPF_NOPROGRESS;
	if(taskbar)
	{
		progress_result = taskbar->SetProgressState(owner, progress_state);
		if(SUCCEEDED(progress_result) && progress_state != TBPF_NOPROGRESS && progress_state != TBPF_INDETERMINATE) progress_result = taskbar->SetProgressValue(owner, progress_value, 10000);
	}
}

windows_desktop::json windows_desktop::snapshot() const
{
	const char *progress {progress_state == TBPF_NORMAL ? "normal" : progress_state == TBPF_PAUSED ? "paused" : progress_state == TBPF_ERROR ? "error" : progress_state == TBPF_INDETERMINATE ? "indeterminate" : "none"};
	return {{"systemTheme", light ? "light" : "dark"}, {"taskbar", {{"available", taskbar != nullptr && SUCCEEDED(progress_result)}, {"state", progress}, {"value", progress_value}, {"total", 10000}, {"hresult", progress_result}}},
		{"windowPersistence", {{"writable", writable}, {"error", state_error}}}, {"completion", completion_capabilities()}};
}

bool windows_desktop::completion_observable() const { return IsWindowVisible(owner) && !IsIconic(owner) && GetForegroundWindow() == owner; }

windows_desktop::json windows_desktop::completion_capabilities() const
{
	json result {{"none", true}, {"exit", true}, {"sleep", false}, {"hibernate", false}, {"simulated", completion_service.simulated}};
	if(completion_service.capabilities)
	{
		try { result.update(completion_service.capabilities()); }
		catch(const std::exception &) { result["systemError"] = ERROR_INVALID_DATA; }
	}
	else
	{
		SYSTEM_POWER_CAPABILITIES capabilities {};
		if(GetPwrCapabilities(&capabilities))
		{
			result["sleep"] = capabilities.SystemS1 || capabilities.SystemS2 || capabilities.SystemS3 || capabilities.AoAc;
			result["hibernate"] = capabilities.SystemS4 && capabilities.HiberFilePresent;
		}
		else result["systemError"] = GetLastError();
	}
	return result;
}

void windows_desktop::execute_completion(const std::string &action, std::uint64_t token)
{
	if(action == "none") return;
	if(action != "exit" && action != "sleep" && action != "hibernate") throw desktop_error {"invalidFinishAction", "web.invalid_payload", ERROR_INVALID_PARAMETER};
	if(!completion_observable()) throw desktop_error {"completionBackground", "web.completion_background", ERROR_CANCELLED};
	if(!completion_capabilities().value(action, false)) throw desktop_error {"completionUnavailable", "web.completion_unavailable", ERROR_NOT_SUPPORTED};
	if(completion_service.execute) { completion_service.execute(action, token); return; }
	if(action == "exit")
	{
		if(!PostMessageW(owner, WM_CLOSE, 0, 0)) throw desktop_error {"completionFailed", "web.completion_failed", GetLastError()};
		return;
	}
	HANDLE token_handle {nullptr};
	if(!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token_handle)) throw desktop_error {"completionDenied", "web.completion_denied", GetLastError()};
	TOKEN_PRIVILEGES privilege {}; privilege.PrivilegeCount = 1; privilege.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	const auto found {LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privilege.Privileges[0].Luid)};
	DWORD error {found ? ERROR_SUCCESS : GetLastError()};
	if(found)
	{
		SetLastError(ERROR_SUCCESS);
		const auto adjusted {AdjustTokenPrivileges(token_handle, FALSE, &privilege, 0, nullptr, nullptr)};
		error = GetLastError(); if(!adjusted && !error) error = ERROR_ACCESS_DENIED;
	}
	CloseHandle(token_handle);
	if(error) throw desktop_error {"completionDenied", "web.completion_denied", error};
	if(!SetSuspendState(action == "hibernate", FALSE, FALSE)) throw desktop_error {"completionFailed", "web.completion_failed", GetLastError()};
}

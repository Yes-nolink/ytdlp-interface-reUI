#include "components.hpp"
#include "core_text.hpp"
#include <Windows.h>
#include <Shlobj.h>
#include <algorithm>
#include <chrono>
#include <regex>

namespace
{
	struct handle
	{
		HANDLE value {nullptr};
		~handle() { if(value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
	};
	struct version_result { std::string output; unsigned long exit_code {0}, system_error {0}; std::string error; };
	version_result run_version(const core::fs::path &path, const wchar_t *arguments, std::atomic_bool &working)
	{
		version_result result;
		SECURITY_ATTRIBUTES security {sizeof(security), nullptr, TRUE}; handle read, write, input, job;
		if(!CreatePipe(&read.value, &write.value, &security, 0) || !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0)) { result.error = "processFailed"; result.system_error = GetLastError(); return result; }
		input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
		job.value = CreateJobObjectW(nullptr, nullptr);
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {}; limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		if(input.value == INVALID_HANDLE_VALUE || !job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) { result.error = "processFailed"; result.system_error = GetLastError(); return result; }
		STARTUPINFOW startup {sizeof(startup)}; startup.dwFlags = STARTF_USESTDHANDLES;
		startup.hStdInput = input.value; startup.hStdOutput = startup.hStdError = write.value;
		PROCESS_INFORMATION process {};
		auto command {core::quote_argument(path.wstring()) + L' ' + arguments};
		if(!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, path.parent_path().c_str(), &startup, &process))
		{ result.error = "processFailed"; result.system_error = GetLastError(); return result; }
		handle child {process.hProcess}, thread {process.hThread};
		if(!AssignProcessToJobObject(job.value, child.value) || ResumeThread(thread.value) == DWORD(-1))
		{ result.error = "processFailed"; result.system_error = GetLastError(); TerminateProcess(child.value, 1); WaitForSingleObject(child.value, 1000); return result; }
		CloseHandle(write.value); write.value = nullptr;
		const auto deadline {std::chrono::steady_clock::now() + std::chrono::seconds {5}};
		for(;;)
		{
			const bool done {WaitForSingleObject(child.value, 20) == WAIT_OBJECT_0}; DWORD available {0};
			while(PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr) && available)
			{
				char buffer[4096]; DWORD count {0};
				if(!ReadFile(read.value, buffer, std::min<DWORD>(sizeof(buffer), available), &count, nullptr) || !count) break;
				result.output.append(buffer, count);
				if(result.output.size() > 65536) { result.error = "invalidVersion"; break; }
			}
			if(!working) result.error = "cancelled";
			else if(!done && std::chrono::steady_clock::now() >= deadline) result.error = "processTimeout";
			if(!result.error.empty()) { TerminateJobObject(job.value, 1); WaitForSingleObject(child.value, 1000); break; }
			if(done) { GetExitCodeProcess(child.value, &result.exit_code); if(result.exit_code) result.error = "processExit"; break; }
		}
		return result;
	}
	core::fs::path executable_in_path(const wchar_t *name)
	{
		std::wstring found(32768, L'\0'); const auto count {SearchPathW(nullptr, name, nullptr, DWORD(found.size()), found.data(), nullptr)};
		if(!count || count >= found.size()) return {};
		found.resize(count); return found;
	}
	bool regular(const core::fs::path &path) { std::error_code error; return core::fs::is_regular_file(path, error); }
	std::string module_version(const core::fs::path &path)
	{
		const auto size {GetFileVersionInfoSizeW(path.c_str(), nullptr)};
		if(!size) return {};
		std::vector<char> data(size); VS_FIXEDFILEINFO *info {nullptr}; UINT count {0};
		if(!GetFileVersionInfoW(path.c_str(), 0, size, data.data()) || !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void **>(&info), &count) || count < sizeof(*info) || info->dwSignature != 0xfeef04bd) return {};
		return std::to_string(HIWORD(info->dwFileVersionMS)) + "." + std::to_string(LOWORD(info->dwFileVersionMS)) + "." + std::to_string(HIWORD(info->dwFileVersionLS)) + "." + std::to_string(LOWORD(info->dwFileVersionLS));
	}
	core::fs::path module_path()
	{
		std::wstring path(32768, L'\0'); const auto size {GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()))}; path.resize(size); return path;
	}
}

core::component_environment core::native_component_environment(fs::path appdir)
{
	component_environment result; result.appdir = appdir.empty() ? module_path().parent_path() : std::move(appdir);
	const auto size {GetEnvironmentVariableW(L"YTDLP_COMPONENT_TEST_ORIGIN", nullptr, 0)};
	if(size)
	{
		std::wstring value(size, L'\0'); value.resize(GetEnvironmentVariableW(L"YTDLP_COMPONENT_TEST_ORIGIN", value.data(), size));
		result.test_origin = to_utf8(value);
		if(!result.test_origin.empty() && !std::regex_match(result.test_origin, std::regex {R"(http://127\.0\.0\.1:[1-9]\d{0,4})"})) throw application_error {"invalidTestOrigin", "web.invalid_payload"};
	}
		const auto variable = [](const wchar_t *name) { const auto size {GetEnvironmentVariableW(name, nullptr, 0)}; if(!size) return std::wstring {}; std::wstring value(size, L'\0'); value.resize(GetEnvironmentVariableW(name, value.data(), size)); return value; };
		result.test_root = variable(L"YTDLP_COMPONENT_TEST_ROOT");
		if(!result.test_root.empty()) result.update_root = result.test_root / "updates";
		if(!result.test_origin.empty()) result.test_fault = to_utf8(variable(L"YTDLP_COMPONENT_TEST_FAULT"));
		if(result.update_root.empty()) { PWSTR path {nullptr}; if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) { result.update_root = fs::path {path} / "Temp" / "ytdlp-component-updates-v1"; CoTaskMemFree(path); } }
	return result;
}

core::component_paths core::resolve_component_paths(const fs::path &appdir, const component_settings &settings)
{
	component_paths paths;
	paths.ytdlp = settings.ytdlp_path.empty() ? appdir / (regular(appdir / "ytdl-patched-red.exe") ? "ytdl-patched-red.exe" : "yt-dlp.exe") : fs::u8path(settings.ytdlp_path);
	if(!settings.ffmpeg_directory.empty()) paths.ffmpeg = fs::u8path(settings.ffmpeg_directory) / "ffmpeg.exe";
	else if(regular(appdir / "ffmpeg.exe")) paths.ffmpeg = appdir / "ffmpeg.exe";
	else if(regular(paths.ytdlp.parent_path() / "ffmpeg.exe")) paths.ffmpeg = paths.ytdlp.parent_path() / "ffmpeg.exe";
	else paths.ffmpeg = executable_in_path(L"ffmpeg.exe");
	if(paths.ffmpeg.empty()) paths.ffmpeg = appdir / "ffmpeg.exe";
	if(regular(paths.ytdlp.parent_path() / "deno.exe")) paths.deno = paths.ytdlp.parent_path() / "deno.exe";
	else if(regular(appdir / "deno.exe")) paths.deno = appdir / "deno.exe";
	else paths.deno = executable_in_path(L"deno.exe");
	return paths;
}

core::component_snapshot core::probe_component(component_snapshot snapshot, std::atomic_bool &working)
{
	if(snapshot.id == "gui")
	{
		snapshot.path = to_utf8(module_path().wstring()); snapshot.current_version = module_version(module_path());
		snapshot.installation = snapshot.current_version.empty() ? "unknown" : "known"; return snapshot;
	}
	const auto path {fs::u8path(snapshot.path)};
	if(!regular(path)) { snapshot.installation = "missing"; return snapshot; }
	const auto result {run_version(path, snapshot.id == "ffmpeg" ? L"-version" : snapshot.id == "yt-dlp" ? L"--ignore-config --version" : L"--version", working)};
	if(!result.error.empty()) { snapshot.installation = "unknown"; snapshot.local_error = failure {result.error, "web.component." + result.error, result.system_error ? result.system_error : result.exit_code}; return snapshot; }
	const auto first {result.output.substr(0, result.output.find_first_of("\r\n"))}; std::smatch match;
	const std::regex pattern {snapshot.id == "yt-dlp" ? R"(([0-9][A-Za-z0-9.+_-]{0,127}))" : snapshot.id == "ffmpeg" ? R"(ffmpeg version ([A-Za-z0-9.+_-]{1,128})(?: .*)?)" : R"(deno ([0-9][A-Za-z0-9.+_-]{0,127})(?: .*)?)"};
	if(std::regex_match(first, match, pattern)) { snapshot.current_version = match[1].str(); snapshot.installation = "known"; }
	else { snapshot.installation = "unknown"; snapshot.local_error = failure {"invalidVersion", "web.component.invalidVersion"}; }
	return snapshot;
}

void core::validate_component_path(const std::string &id, const fs::path &path, std::atomic_bool &working)
{
	std::error_code error;
	if(id != "yt-dlp" && id != "ffmpeg") throw application_error {"invalidComponent", "web.invalid_payload"};
	if(!path.is_absolute() || (id == "ffmpeg" ? !fs::is_directory(path, error) : !regular(path))) throw application_error {"invalidComponentPath", "web.component.invalid_path"};
	component_snapshot snapshot; snapshot.id = id; snapshot.path = to_utf8((id == "ffmpeg" ? path / "ffmpeg.exe" : path).wstring());
	const auto probed {probe_component(snapshot, working)};
	if(probed.local_error) { application_error failure {probed.local_error->code, probed.local_error->message_key}; failure.detail = *probed.local_error; throw failure; }
	if(probed.installation != "known") throw application_error {"invalidComponentPath", "web.component.invalid_path"};
	if(id == "ffmpeg")
	{
		if(!regular(path / "ffprobe.exe")) throw application_error {"ffprobeMissing", "web.component.ffprobeMissing"};
		const auto probe {run_version(path / "ffprobe.exe", L"-version", working)};
		if(!probe.error.empty()) { application_error failure {probe.error, "web.component." + probe.error}; failure.detail.exit_code = probe.system_error ? probe.system_error : probe.exit_code; throw failure; }
		if(!probe.output.starts_with("ffprobe version ")) throw application_error {"invalidVersion", "web.component.invalidVersion"};
	}
}

std::string core::component_tool_version(const fs::path &path, const std::string &tool, std::atomic_bool &working, bool require_static)
{
	const auto result {run_version(path, tool == "yt-dlp" ? L"--ignore-config --version" : tool == "deno" ? L"--version" : L"-version", working)};
	if(!result.error.empty()) { application_error error {result.error, "web.component." + result.error}; error.detail.exit_code = result.system_error ? result.system_error : result.exit_code; throw error; }
	const auto first {result.output.substr(0, result.output.find_first_of("\r\n"))}; std::smatch match;
	const std::regex pattern {tool == "yt-dlp" ? R"(([0-9][A-Za-z0-9.+_-]{0,127}))" : tool == "deno" ? R"(deno ([0-9][A-Za-z0-9.+_-]{0,127})(?: .*)?)" : tool + R"( version ([A-Za-z0-9.+_-]{1,128})(?: .*)?)"};
	if(!std::regex_match(first, match, pattern)) throw application_error {"invalidVersion", "web.component.invalidVersion"};
	if(require_static && (!result.output.contains("configuration:") || result.output.contains("--enable-shared"))) throw application_error {"sharedBuild", "web.update.sharedBuild"};
	return match[1].str();
}

core::release_response core::query_component_release(const std::string &source, const std::string &proxy, const component_environment &environment)
{
	std::atomic_bool working {true}; return component_http_get(source, proxy, environment, working, 1024 * 1024);
}

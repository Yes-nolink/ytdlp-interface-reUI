#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "application_settings.hpp"
#include "task_queue.hpp"
#include <Windows.h>
#include <Shobjidl.h>
#include <atlbase.h>

struct desktop_error : std::runtime_error
{
	std::string code, key;
	DWORD system_error {0};
	desktop_error(std::string code, std::string key, DWORD system_error = 0) : std::runtime_error {code}, code {std::move(code)}, key {std::move(key)}, system_error {system_error} {}
};

// Web 宿主的原生桌面行为；只消费 Core 数据，不创建或调度下载。
class windows_desktop
{
public:
	using json = nlohmann::json;
	struct completion_executor
	{
		std::function<json()> capabilities;
		std::function<void(const std::string &, std::uint64_t)> execute;
		bool simulated {false};
	};
	static constexpr int default_width {1280}, default_height {800}, minimum_width {800}, minimum_height {500};
	windows_desktop(HWND owner, completion_executor executor = {});
	~windows_desktop();
	json choose(const std::string &kind, const std::filesystem::path &folder, const std::string &name = "");
	json export_metadata(const json &metadata, const std::string &format, const std::filesystem::path &folder);
	void open_file(const std::filesystem::path &file);
	void open_component_release(const std::string &component, const std::string &channel, const std::string &tag);
	json load_about(const json &components, const std::filesystem::path &appdir) const;
	void open_about_link(const std::string &id, const std::filesystem::path &appdir);
	void reveal_file(const std::filesystem::path &file, const std::filesystem::path &folder);
	void copy_text(const std::string &text);
	std::string clipboard_text(bool changed_only = false);
	void apply(const core::appearance_settings &settings);
	void update_progress(const core::queue_snapshot &queue);
	json snapshot() const;
	json completion_capabilities() const;
	bool completion_observable() const;
	void execute_completion(const std::string &action, std::uint64_t token);
	void restore();
	void save();
	bool message(UINT message, WPARAM wparam, LPARAM lparam, LRESULT &result);

private:
	HWND owner;
	completion_executor completion_service;
	std::filesystem::path state_file;
	core::appearance_settings settings;
	RECT normal_bounds {};
	bool light {true}, maximized {false}, writable {false};
	std::string state_error;
	HANDLE state_lock {INVALID_HANDLE_VALUE};
	CComPtr<ITaskbarList3> taskbar;
	TBPFLAG progress_state {TBPF_NOPROGRESS};
	ULONGLONG progress_value {0};
	HRESULT progress_result {S_OK};
	DWORD clipboard_sequence {0};
	void system_theme();
	static std::filesystem::path environment_path(const wchar_t *name);
};

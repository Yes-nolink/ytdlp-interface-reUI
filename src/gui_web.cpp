#include "web_ui.hpp"
#include "web_bridge.hpp"
#include "webview_host.hpp"
#include "windows_desktop.hpp"
#include "core_text.hpp"
#include "i18n.hpp"
#include <Shlobj.h>
#include <fstream>

namespace fs = std::filesystem;
namespace
{
	fs::path system_folder(REFKNOWNFOLDERID id)
	{
		PWSTR path {nullptr};
		SHGetKnownFolderPath(id, 0, nullptr, &path);
		const fs::path result {path ? path : L"."};
		CoTaskMemFree(path);
		return result;
	}

	// 仅读取第一阶段已有的执行配置；不恢复旧队列或写入配置。
	core::download_settings execution_settings(const nlohmann::json &config, const fs::path &appdir, const std::string &filename)
	{
		core::download_settings settings;
		settings.ytdlp_path = fs::u8path(config.value("ytdlp_path", ""));
		if(settings.ytdlp_path.empty() || !fs::exists(settings.ytdlp_path))
			settings.ytdlp_path = appdir / (fs::exists(appdir / "ytdl-patched-red.exe") ? "ytdl-patched-red.exe" : filename);
		settings.ffmpeg_path = fs::u8path(config.value("ffmpeg_path", ""));
		if(!fs::exists(settings.ffmpeg_path))
		{
			if(fs::exists(appdir / "ffmpeg.exe")) settings.ffmpeg_path = ".\\";
			else if(fs::exists(settings.ytdlp_path.parent_path() / "ffmpeg.exe")) settings.ffmpeg_path = settings.ytdlp_path.parent_path();
		}
		settings.pref_res = config.value("pref_res", 0u) + (config.contains("pref_res") && !config.contains("json_hide_null") ? 2u : 0u);
		settings.pref_video = config.value("pref_video", 0u);
		settings.pref_audio = config.value("pref_audio", 0u);
		settings.pref_vcodec = config.value("pref_vcodec", 0u);
		settings.pref_acodec = config.value("pref_acodec", 0u);
		settings.pref_fps = config.value("pref_fps", false);
		settings.premium = config.value("cb_premium", true);
		settings.aria = config.value("cb_aria", false);
		settings.aria_options = config.value("aria_options", "-x 6");
		settings.cookies = config.value("cb_cookies", false);
		settings.cookies_path = fs::u8path(config.value("cookies_path", ""));
		settings.cookie_browser = config.value("com_cookies", 0u);
		settings.cookie_options = config.value("cookie_options", "");
		settings.output_template = core::to_wide(config.value("output_template", "%(title)s.%(ext)s"));
		settings.sub_format = config.value("sub_format", "");
		settings.sub_langs = config.value("sub_langs", "");
		if(config.contains("sblock"))
		{
			const auto &block {config.at("sblock")};
			settings.sponsor_mark = block.value("cb_mark", false);
			settings.sponsor_remove = block.value("cb_remove", false);
			settings.sponsor_mark_categories = block.value("mark", std::vector<int> {});
			settings.sponsor_remove_categories = block.value("remove", std::vector<int> {});
		}
		if(config.contains("proxy"))
		{
			settings.use_proxy = config.at("proxy").value("enabled", false);
			settings.proxy = core::to_wide(config.at("proxy").value("URL", ""));
		}
		settings.use_ffmpeg_location = !core::ffmpeg_in_config(settings.ytdlp_path);
		return settings;
	}

	struct web_window
	{
		HWND hwnd {nullptr};
		bool started {false};
		int exit_code {0};
		std::unique_ptr<webview_host> view;
		std::unique_ptr<web_bridge> bridge;
		std::unique_ptr<core::download_application> application;
		std::unique_ptr<windows_desktop> desktop;
		void failed(const std::string &key, HRESULT result, const fs::path &path = {})
		{
			if(exit_code) return;
			exit_code = 1;
			if(hwnd) KillTimer(hwnd, 20);
			wchar_t code[32] {}; swprintf_s(code, L"\nHRESULT 0x%08lX", static_cast<unsigned long>(result));
			const auto text {i18n::wide(key) + code + (path.empty() ? L"" : L"\n" + path.wstring())};
			MessageBoxW(hwnd, text.c_str(), i18n::wide("web.title").c_str(), MB_OK | MB_ICONERROR);
			if(hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
		}
		void close()
		{
			// 先取消并等待业务线程，再释放事件出口和主线程 COM 对象。
			application.reset();
			bridge.reset();
			view.reset();
			desktop.reset();
		}
	};

	LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
	{
		auto window {reinterpret_cast<web_window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))};
		if(message == WM_NCCREATE)
		{
			window = static_cast<web_window *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
			window->hwnd = hwnd;
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
		}
		if(window)
		{
			LRESULT result {0};
			if(window->desktop && window->desktop->message(message, wparam, lparam, result)) return result;
			if((message == WM_SETTINGCHANGE || message == WM_THEMECHANGED) && window->bridge) window->bridge->desktop_changed();
			if(message == webview_host::message_id) { if(window->view) window->view->drain(); return 0; }
				if(message == WM_SIZE) { if(window->view) window->view->resize(); if(window->bridge) window->bridge->completion_tick(); return 0; }
				if(message == WM_ACTIVATE && window->bridge)
				{
					if(LOWORD(wparam) == WA_INACTIVE) window->bridge->cancel_background_completion();
					else { window->bridge->window_activated(); window->bridge->completion_tick(); }
				}
			if(message == WM_TIMER && wparam == 25 && window->bridge) { window->bridge->completion_tick(); return 0; }
			if(message == WM_TIMER && wparam == 20 && !window->started) { window->failed("web.startup.bridge_failed", HRESULT_FROM_WIN32(ERROR_TIMEOUT)); return 0; }
			if((message == WM_QUERYENDSESSION || message == WM_ENDSESSION || message == WM_POWERBROADCAST && wparam == PBT_APMSUSPEND) && window->bridge) window->bridge->cancel_background_completion();
			if(message == WM_CLOSE) { KillTimer(hwnd, 20); KillTimer(hwnd, 25); if(window->started && window->desktop) window->desktop->save(); window->close(); DestroyWindow(hwnd); return 0; }
			if(message == WM_DESTROY) { PostQuitMessage(0); return 0; }
		}
		return DefWindowProcW(hwnd, message, wparam, lparam);
	}
}

int run_web_ui(const fs::path &appdir, const std::string &ytdlp_filename, const std::wstring &initial_url)
{
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	web_window window;
	for(const auto filename : {"index.html", "app.js", "bridge.js", "lucide.min.js", "v1-text-keys.json"})
	{
		const auto file {appdir / "ui" / filename};
		std::ifstream input {file, std::ios::binary};
		if(!input || input.peek() == std::char_traits<char>::eof()) { window.failed("web.startup.resources_failed", HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), file); return 1; }
		if(std::string {filename} == "v1-text-keys.json")
		{
			const auto keys {nlohmann::json::parse(input, nullptr, false)};
			if(!keys.is_object() || keys.empty() || std::any_of(keys.begin(), keys.end(), [](const auto &value) { return !value.is_string(); })) { window.failed("web.startup.resources_failed", HRESULT_FROM_WIN32(ERROR_INVALID_DATA), file); return 1; }
		}
	}
	for(const auto language : {"zh-CN", "en-US"})
	{
		if(!i18n::load(appdir / "locales", language).empty() || !i18n::dictionary().contains("web.title"))
		{
			i18n::load_embedded(language);
			window.failed("web.startup.locales_failed", HRESULT_FROM_WIN32(ERROR_INVALID_DATA), appdir / "locales" / (std::string {language} + ".json")); return 1;
		}
	}
	i18n::load(appdir / "locales", "zh-CN");
	wchar_t config_override[32768] {};
	GetEnvironmentVariableW(L"YTDLP_WEB_CONFIG_FILE", config_override, 32768);
	const auto config_path {config_override[0] ? fs::path {config_override} : system_folder(FOLDERID_LocalAppData) / "ytdlp-interface" / "WebUI-v1" / "ytdlp-interface.json"};
	nlohmann::json config = nlohmann::json::object();
	try
	{
		if(fs::exists(config_path))
		{
			std::ifstream file {config_path}; if(!file) throw std::runtime_error {"read"};
			file >> config; if(!config.is_object()) throw std::runtime_error {"object"};
		}
	const auto output_folder {config.contains("outpath") ? fs::u8path(config.at("outpath").get<std::string>()) : system_folder(FOLDERID_Downloads)};
	const auto execution {execution_settings(config, appdir, ytdlp_filename)};
	auto defaults {core::default_output_options(output_folder)};
	defaults.processing.video_codec = core::to_utf8(core::video_codec_options.at(execution.pref_vcodec));
	defaults.processing.audio_codec = core::to_utf8(core::audio_codec_options.at(execution.pref_acodec));
	defaults.processing.higher_fps = execution.pref_fps; defaults.processing.premium = execution.premium;
	defaults.processing.sub_langs = execution.sub_langs; defaults.processing.sub_format = execution.sub_format;
	auto components {core::native_component_environment(appdir)}; components.channel = config.value("ytdlp_nightly", true) ? "nightly" : "stable";
	const auto instance {GetModuleHandleW(nullptr)};
	WNDCLASSW window_class {};
	window_class.lpfnWndProc = window_proc;
	window_class.hInstance = instance;
	window_class.lpszClassName = L"YtdlpWebUI";
	window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
	if(!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { window.failed("web.startup.window_failed", HRESULT_FROM_WIN32(GetLastError())); return 1; }
	const auto title {i18n::wide("ui.ytdlp_interface") + L"v2.19.1"};
	const auto hwnd {CreateWindowExW(0, window_class.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, windows_desktop::default_width, windows_desktop::default_height, nullptr, nullptr, instance, &window)};
	if(!hwnd) { window.failed("web.startup.window_failed", HRESULT_FROM_WIN32(GetLastError())); return 1; }
	window.desktop = std::make_unique<windows_desktop>(hwnd);
	window.view = std::make_unique<webview_host>(hwnd,
		[&](const auto &message)
		{
			if(window.started) { window.bridge->handle(message); return; }
			try
			{
				const auto command {message.at("command").template get<std::string>()};
				if(message.value("version", 0) != 1 || message.at("id").template get<std::string>().empty()) throw std::runtime_error {"protocol"};
				if(!window.bridge)
				{
					if(command != "initialize") throw std::runtime_error {"initialize"};
					window.application = std::make_unique<core::download_application>(execution,
						[&window](const auto &event) { if(window.view) window.view->post(web_bridge::event_json(event)); }, core::scheduler_options {}, core::default_queue_file(), core::default_settings_file(), defaults, config.value("language", "zh-CN"), std::chrono::steady_clock::now, components, true);
					const auto language {window.application->get_settings().application.application_behavior.language};
					if(!i18n::load(appdir / "locales", language).empty()) throw std::runtime_error {"locale"};
					SetWindowTextW(hwnd, (i18n::wide("ui.ytdlp_interface") + L"v2.19.1").c_str());
					window.desktop->apply(window.application->get_settings().application.appearance);
						window.desktop->restore();
					window.desktop->update_progress(window.application->get_queue());
					const auto initialization_id {message.at("id").template get<std::string>()};
					window.bridge = std::make_unique<web_bridge>(*window.application, *window.desktop, web_context {appdir, output_folder, language},
						[&, initialization_id](auto response)
						{
							if(!window.started && response.value("id", "") == initialization_id && !response.value("ok", true)) { window.failed("web.startup.bridge_failed", E_FAIL); return; }
							if(!initial_url.empty() && response.contains("payload") && response.at("payload").contains("capabilities")) response["payload"]["initialUrl"] = core::to_utf8(initial_url);
							window.view->send(response);
						});
				}
				if(command == "uiReady" && !window.started) { window.application->activate(); window.started = true; KillTimer(hwnd, 20); }
				window.bridge->handle(message);
			}
			catch(const std::exception &) { window.failed("web.startup.bridge_failed", E_FAIL); }
		},
		[&window](const auto &event) { if(window.bridge) window.bridge->backend_event(event); },
		[&window](HRESULT result) { window.failed("web.runtime_error", result); });
	ShowWindow(hwnd, SW_SHOW);
	SetTimer(hwnd, 20, 30000, nullptr);
	window.view->start(appdir / "ui");
	SetTimer(hwnd, 25, 250, nullptr);
	MSG message;
	while(GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
	window.close();
	return window.exit_code;
	}
	catch(const std::exception &)
	{
		window.failed("web.startup.settings_failed", HRESULT_FROM_WIN32(ERROR_INVALID_DATA), config_path);
		if(window.hwnd) { window.close(); DestroyWindow(window.hwnd); }
		return 1;
	}
}

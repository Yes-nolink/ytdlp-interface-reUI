#pragma once

#include <Windows.h>
#include <WebView2.h>
#include <wrl.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include "json.hpp"

// WebView2 只负责页面和 JSON 传输，不读取下载任务或 Nana 控件。
class webview_host
{
public:
	using json = nlohmann::json;
	static constexpr UINT message_id {WM_APP + 0x0060};
	webview_host(HWND owner, std::function<void(const json &)> receive,
		std::function<void(const json &)> backend_event, std::function<void(HRESULT)> startup_failed);
	~webview_host();
	void start(const std::filesystem::path &ui_dir);
	void resize();
	void post(json event);
	void drain();
	void send(const json &message);

private:
	HWND hwnd;
	std::function<void(const json &)> receive, backend_event;
	std::function<void(HRESULT)> startup_failed;
	bool failure_reported {false};
	Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
	Microsoft::WRL::ComPtr<ICoreWebView2> view;
	std::mutex mutex;
	std::vector<json> pending;
	std::shared_ptr<bool> alive {std::make_shared<bool>(true)};
	void failed(HRESULT result);
};

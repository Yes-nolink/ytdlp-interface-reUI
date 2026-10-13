#include "webview_host.hpp"
#include "i18n.hpp"
#include "core_text.hpp"
#include <Shlobj.h>

using Microsoft::WRL::Callback;
namespace fs = std::filesystem;
namespace { constexpr auto origin = L"https://ytdlp-ui.local/"; }

webview_host::webview_host(HWND owner, std::function<void(const json &)> receive,
	std::function<void(const json &)> backend_event, std::function<void(HRESULT)> startup_failed) :
	hwnd {owner}, receive {std::move(receive)}, backend_event {std::move(backend_event)}, startup_failed {std::move(startup_failed)} {}

webview_host::~webview_host()
{
	*alive = false;
	if(controller) controller->Close();
}

void webview_host::failed(HRESULT result)
{
	if(failure_reported) return;
	failure_reported = true;
	startup_failed(result);
}

void webview_host::start(const fs::path &ui_dir)
{
	if(!fs::exists(ui_dir / "index.html"))
	{
		failed(HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
		return;
	}
	PWSTR local {nullptr};
	SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local);
	wchar_t profile_override[32768] {};
	GetEnvironmentVariableW(L"WEBVIEW2_USER_DATA_FOLDER", profile_override, 32768);
	const auto profile {profile_override[0] ? fs::path {profile_override} : fs::path {local ? local : L"."} / "ytdlp-interface" / "WebView2"};
	CoTaskMemFree(local);
	std::error_code error;
	const bool profile_exists {fs::exists(profile, error)};
	if(error || profile_exists && !fs::is_directory(profile, error))
	{
		failed(HRESULT_FROM_WIN32(error ? error.value() : ERROR_DIRECTORY));
		return;
	}
	const auto result {CreateCoreWebView2EnvironmentWithOptions(nullptr, profile.c_str(), nullptr,
		Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
		[this, ui_dir, lifetime = alive](HRESULT result, ICoreWebView2Environment *environment) -> HRESULT
		{
			if(!*lifetime) return S_OK;
			if(FAILED(result)) { failed(result); return S_OK; }
			return environment->CreateCoreWebView2Controller(hwnd,
				Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
				[this, ui_dir, lifetime](HRESULT result, ICoreWebView2Controller *created) -> HRESULT
				{
					if(!*lifetime) { if(created) created->Close(); return S_OK; }
					if(FAILED(result)) { failed(result); return S_OK; }
					controller = created;
					Microsoft::WRL::ComPtr<ICoreWebView2Controller4> drop_controller;
					if(SUCCEEDED(controller.As(&drop_controller))) drop_controller->put_AllowExternalDrop(TRUE);
						result = controller->get_CoreWebView2(&view);
						if(FAILED(result) || !view) { failed(FAILED(result) ? result : E_POINTER); return S_OK; }
					Microsoft::WRL::ComPtr<ICoreWebView2_3> local_view;
					result = view.As(&local_view);
					if(FAILED(result)) { failed(result); return S_OK; }
					result = local_view->SetVirtualHostNameToFolderMapping(L"ytdlp-ui.local", ui_dir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
					if(FAILED(result)) { failed(result); return S_OK; }
					Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
					view->get_Settings(&settings);
					settings->put_AreDefaultContextMenusEnabled(FALSE);
					Microsoft::WRL::ComPtr<ICoreWebView2Settings3> accelerator_settings;
					if(SUCCEEDED(settings.As(&accelerator_settings))) accelerator_settings->put_AreBrowserAcceleratorKeysEnabled(FALSE);
					EventRegistrationToken token;
						view->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
							[this, lifetime](ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT
							{
								if(!*lifetime) return S_OK;
								BOOL success {FALSE}; args->get_IsSuccess(&success);
								if(!success) failed(E_FAIL);
								return S_OK;
								}).Get(), &token);
					view->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
						[](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT
						{
							LPWSTR uri {nullptr}; args->get_Uri(&uri);
							if(!uri || !std::wstring {uri}.starts_with(origin)) args->put_Cancel(TRUE);
							CoTaskMemFree(uri); return S_OK;
						}).Get(), &token);
					view->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
						[](ICoreWebView2 *, ICoreWebView2NewWindowRequestedEventArgs *args) -> HRESULT
						{ args->put_Handled(TRUE); return S_OK; }).Get(), &token);
					view->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>(
						[this](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT
						{
							LPWSTR source {nullptr}, data {nullptr}; args->get_Source(&source);
							const bool trusted {source && std::wstring {source} == std::wstring {origin} + L"index.html"};
							CoTaskMemFree(source);
							if(!trusted) return S_OK;
							args->get_WebMessageAsJson(&data);
							const auto message {json::parse(core::to_utf8(data ? data : L""), nullptr, false)};
							CoTaskMemFree(data);
							if(!message.is_discarded()) receive(message);
							return S_OK;
						}).Get(), &token);
					resize();
					controller->put_IsVisible(TRUE);
						result = view->Navigate((std::wstring {origin} + L"index.html").c_str());
						if(FAILED(result)) failed(result);
						return S_OK;
				}).Get());
		}).Get())};
	if(FAILED(result)) failed(result);
}

void webview_host::resize()
{
	if(controller)
	{
		RECT bounds; GetClientRect(hwnd, &bounds);
		controller->put_Bounds(bounds);
		controller->NotifyParentWindowPositionChanged();
	}
}

void webview_host::post(json event)
{
	std::lock_guard lock {mutex};
	pending.push_back(std::move(event));
	PostMessageW(hwnd, message_id, 0, 0);
}

void webview_host::drain()
{
	std::vector<json> events;
	{ std::lock_guard lock {mutex}; events.swap(pending); }
	for(const auto &event : events) backend_event(event);
}

void webview_host::send(const json &message)
{
	if(view) view->PostWebMessageAsJson(core::to_wide(message.dump(-1, ' ', false, json::error_handler_t::replace)).c_str());
}

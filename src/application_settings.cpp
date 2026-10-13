#include "application_settings.hpp"
#include "core_text.hpp"
#include "download_options.hpp"
#include <Windows.h>
#include <algorithm>
#include <set>
#include <thread>

namespace
{
	void put_text(nlohmann::json &object, const std::string &key, const std::string &value, bool disk)
	{
		object[disk ? key + "Protected" : key] = disk ? core::protect_text(value) : value;
	}
	std::string text(const nlohmann::json &object, const std::string &key, bool disk)
	{
		const auto value {object.at(disk ? key + "Protected" : key).get<std::string>()};
		return disk ? core::unprotect_text(value) : value;
	}
	std::vector<int> category_indices(const std::vector<std::string> &values, const std::vector<std::wstring> &catalog)
	{
		if(std::find(values.begin(), values.end(), "all") != values.end()) return {0};
		std::vector<int> result;
		for(const auto &value : values) result.push_back(int(std::find(catalog.begin(), catalog.end(), core::to_wide(value)) - catalog.begin()));
		return result;
	}
}

unsigned core::metadata_cpu_limit() { return std::max(1u, std::thread::hardware_concurrency()); }

void core::validate_application_settings(const application_settings &settings)
{
	const auto invalid = [] { throw application_error {"invalidSettings", "web.invalid_settings"}; };
	if(settings.network.proxy_enabled && settings.network.proxy.empty()) invalid();
	if(settings.authentication.mode != "none" && settings.authentication.mode != "browser" && settings.authentication.mode != "file") invalid();
	if(std::find(cookie_browser_options.begin() + 1, cookie_browser_options.end(), to_wide(settings.authentication.browser)) == cookie_browser_options.end()) invalid();
	if(settings.authentication.mode == "file" && (settings.authentication.cookie_file.empty() || !fs::u8path(settings.authentication.cookie_file).is_absolute())) invalid();
	if(!settings.scheduler.max_downloads || settings.scheduler.max_downloads > 10 || !settings.scheduler.max_metadata || settings.scheduler.max_metadata > metadata_cpu_limit()) invalid();
	if(settings.application_behavior.language != "zh-CN" && settings.application_behavior.language != "en-US") invalid();
	if(settings.application_behavior.folder_origin != "current" && settings.application_behavior.folder_origin != "program") invalid();
	if(settings.appearance.theme != "system" && settings.appearance.theme != "light" && settings.appearance.theme != "dark") invalid();
	if(settings.appearance.contrast > 30) invalid();
	for(const auto &value : {settings.network.proxy, settings.authentication.browser_options, settings.authentication.cookie_file, settings.external_downloader.arguments})
		if(value.find_first_of("\r\n") != std::string::npos || value.find('\0') != std::string::npos) invalid();
	const auto categories = [&](const auto &values, const auto &catalog)
	{
		std::set<std::string> unique;
		for(const auto &value : values)
			if(!unique.insert(value).second || std::find(catalog.begin(), catalog.end(), to_wide(value)) == catalog.end()) invalid();
	};
	categories(settings.content_processing.mark_categories, sponsor_mark_options);
	categories(settings.content_processing.remove_categories, sponsor_remove_options);
}

nlohmann::json core::application_settings_json(const application_settings &settings, bool disk)
{
	nlohmann::json result {
		{"network", {{"proxyEnabled", settings.network.proxy_enabled}}},
		{"authentication", {{"mode", settings.authentication.mode}, {"browser", settings.authentication.browser}}},
		{"externalDownloader", {{"enabled", settings.external_downloader.enabled}}},
		{"contentProcessing", {{"sponsorMark", settings.content_processing.mark}, {"sponsorRemove", settings.content_processing.remove},
			{"markCategories", settings.content_processing.mark_categories}, {"removeCategories", settings.content_processing.remove_categories}}},
		{"scheduler", {{"maxConcurrentDownloads", settings.scheduler.max_downloads}, {"maxConcurrentMetadata", settings.scheduler.max_metadata},
			{"startNextDuringProcessing", settings.scheduler.next_during_processing}, {"continueAfterStop", settings.scheduler.next_after_stop}}},
		{"queueBehavior", {{"autoStartRecoveredQueue", settings.queue_behavior.auto_start_recovered}, {"restoreFailed", settings.queue_behavior.restore_failed}, {"autoRemoveCompleted", settings.queue_behavior.remove_completed}}},
		{"applicationBehavior", {{"clipboardOnFocus", settings.application_behavior.clipboard_on_focus}, {"displayCustomFilenames", settings.application_behavior.custom_filenames},
			{"language", settings.application_behavior.language}, {"folderOrigin", settings.application_behavior.folder_origin}}},
		{"appearance", {{"theme", settings.appearance.theme}, {"contrast", settings.appearance.contrast}, {"snap", settings.appearance.snap},
			{"noMinWidth", settings.appearance.no_min_width}, {"exactBytes", settings.appearance.exact_bytes},
			{"queueDisplay", {{"format", settings.appearance.queue_display.format}, {"note", settings.appearance.queue_display.note},
				{"ext", settings.appearance.queue_display.ext}, {"size", settings.appearance.queue_display.size},
				{"siteIcon", settings.appearance.queue_display.site_icon}, {"siteText", settings.appearance.queue_display.site_text}}}}}};
	put_text(result["network"], "proxy", settings.network.proxy, disk);
	put_text(result["authentication"], "browserOptions", settings.authentication.browser_options, disk);
	put_text(result["authentication"], "cookieFile", settings.authentication.cookie_file, disk);
	put_text(result["externalDownloader"], "arguments", settings.external_downloader.arguments, disk);
	return result;
}

core::application_settings core::read_application_settings(const nlohmann::json &value, bool disk)
{
	application_settings result;
	const auto &network {value.at("network")}, &auth {value.at("authentication")}, &external {value.at("externalDownloader")},
		&content {value.at("contentProcessing")}, &scheduler {value.at("scheduler")}, &queue {value.at("queueBehavior")}, &behavior {value.at("applicationBehavior")};
	result.network = {network.at("proxyEnabled"), text(network, "proxy", disk)};
	result.authentication = {auth.at("mode"), auth.at("browser"), text(auth, "browserOptions", disk), text(auth, "cookieFile", disk)};
	result.external_downloader = {external.at("enabled"), text(external, "arguments", disk)};
	result.content_processing = {content.at("sponsorMark"), content.at("sponsorRemove"), content.at("markCategories").get<std::vector<std::string>>(), content.at("removeCategories").get<std::vector<std::string>>()};
	result.scheduler = {scheduler.at("maxConcurrentDownloads"), scheduler.at("maxConcurrentMetadata"), scheduler.at("startNextDuringProcessing"), scheduler.at("continueAfterStop")};
	result.queue_behavior = {queue.at("autoStartRecoveredQueue"), queue.at("restoreFailed"), queue.at("autoRemoveCompleted")};
	result.application_behavior = {behavior.at("clipboardOnFocus"), behavior.at("displayCustomFilenames"), behavior.at("language"), behavior.at("folderOrigin")};
	if(value.contains("appearance"))
	{
		const auto &appearance {value.at("appearance")};
		result.appearance = {appearance.at("theme"), appearance.at("contrast"), appearance.at("snap"), appearance.at("noMinWidth"), appearance.at("exactBytes")};
		if(appearance.contains("queueDisplay"))
		{
			const auto &display {appearance.at("queueDisplay")};
			if(!display.is_object()) throw application_error {"invalidSettings", "web.invalid_settings"};
			for(const auto key : {"format", "note", "ext", "size", "siteIcon", "siteText"})
				if(display.contains(key) && !display[key].is_boolean()) throw application_error {"invalidSettings", "web.invalid_settings"};
			result.appearance.queue_display = {display.value("format", false), display.value("note", true), display.value("ext", true),
				display.value("size", false), display.value("siteIcon", true), display.value("siteText", false)};
		}
	}
	validate_application_settings(result); return result;
}

core::application_settings core::application_settings_from_execution(const download_settings &settings, scheduler_options scheduler)
{
	application_settings result;
	result.network = {settings.use_proxy, to_utf8(settings.proxy)};
	result.authentication.mode = settings.cookie_browser ? "browser" : settings.cookies ? "file" : "none";
	if(settings.cookie_browser) result.authentication.browser = to_utf8(cookie_browser_options.at(settings.cookie_browser));
	result.authentication.browser_options = settings.cookie_options; result.authentication.cookie_file = to_utf8(settings.cookies_path.wstring());
	result.external_downloader = {settings.aria, settings.aria_options};
	result.content_processing.mark = settings.sponsor_mark; result.content_processing.remove = settings.sponsor_remove;
	for(const auto index : settings.sponsor_mark_categories) if(index >= 0 && size_t(index) < sponsor_mark_options.size()) result.content_processing.mark_categories.push_back(to_utf8(sponsor_mark_options[index]));
	for(const auto index : settings.sponsor_remove_categories) if(index >= 0 && size_t(index) < sponsor_remove_options.size()) result.content_processing.remove_categories.push_back(to_utf8(sponsor_remove_options[index]));
	result.scheduler.max_downloads = scheduler.max_concurrent_downloads; result.scheduler.next_after_stop = scheduler.continue_after_stop;
	result.scheduler.next_during_processing = scheduler.next_during_processing;
	return result;
}

core::fs::path core::aria2c_path(const fs::path &ytdlp)
{
	const auto local {ytdlp.parent_path() / "aria2c.exe"};
	if(fs::is_regular_file(local)) return local;
	std::wstring found(32768, L'\0'); const auto size {SearchPathW(nullptr, L"aria2c.exe", nullptr, DWORD(found.size()), found.data(), nullptr)};
	if(!size || size >= found.size()) return {};
	found.resize(size); return found;
}

core::download_settings core::application_execution_settings(download_settings settings, const application_settings &application)
{
	settings.use_proxy = application.network.proxy_enabled; settings.proxy = to_wide(application.network.proxy);
	settings.cookies = application.authentication.mode == "file"; settings.cookies_path = fs::u8path(application.authentication.cookie_file);
	settings.cookie_browser = application.authentication.mode == "browser" ? unsigned(std::find(cookie_browser_options.begin(), cookie_browser_options.end(), to_wide(application.authentication.browser)) - cookie_browser_options.begin()) : 0;
	settings.cookie_options = application.authentication.browser_options;
	settings.aria = application.external_downloader.enabled; settings.aria_options = application.external_downloader.arguments;
	settings.aria_path = settings.aria ? aria2c_path(settings.ytdlp_path) : fs::path {};
	settings.sponsor_mark = application.content_processing.mark; settings.sponsor_remove = application.content_processing.remove;
	settings.sponsor_mark_categories = category_indices(application.content_processing.mark_categories, sponsor_mark_options);
	settings.sponsor_remove_categories = category_indices(application.content_processing.remove_categories, sponsor_remove_options);
	settings.application_owned_configuration = true;
	return settings;
}

std::vector<std::string> core::application_sensitive_values(const application_settings &settings)
{
	std::vector<std::string> result {settings.network.proxy, settings.authentication.cookie_file, settings.authentication.browser_options, settings.external_downloader.arguments};
	const auto options {download_options::tokens(settings.external_downloader.arguments)};
	for(size_t n {0}; n < options.size(); ++n)
	{
		const auto &option {options[n]};
		if(option.find("password") == std::string::npos && option.find("passwd") == std::string::npos && option.find("header") == std::string::npos && option.find("user") == std::string::npos) continue;
		const auto equal {option.find('=')};
		if(equal != std::string::npos) result.push_back(download_options::unquote(option.substr(equal + 1)));
		else if(n + 1 < options.size()) result.push_back(download_options::unquote(options[++n]));
	}
	const auto profile {settings.authentication.browser_options.find(':')};
	if(profile != std::string::npos) result.push_back(settings.authentication.browser_options.substr(profile + 1, settings.authentication.browser_options.find("::", profile + 1) - profile - 1));
	const auto authority {settings.network.proxy.find("@")};
	if(authority != std::string::npos)
	{
		const auto start {settings.network.proxy.find("://")};
		const auto user {start == std::string::npos ? 0 : start + 3}, colon {settings.network.proxy.find(':', user)};
		result.push_back(settings.network.proxy.substr(user, (colon < authority ? colon : authority) - user));
		if(colon < authority) result.push_back(settings.network.proxy.substr(colon + 1, authority - colon - 1));
	}
	std::erase_if(result, [](const auto &value) { return value.empty(); });
	return result;
}

std::string core::redact_values(std::string text, const std::vector<std::string> &values)
{
	text = redact_text(text);
	for(const auto &value : values)
		for(size_t position {0}; (position = text.find(value, position)) != std::string::npos; position += 10) text.replace(position, value.size(), "[redacted]");
	return text;
}

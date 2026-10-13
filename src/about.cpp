#include "about.hpp"
#include "build_info.hpp"
#include "output_model.hpp"
#include <zlib.h>

namespace
{
	const std::map<std::string, std::string> links {
		{"upstream", "https://github.com/ErrorFlynn/ytdlp-interface"},
		{"json", "https://github.com/nlohmann/json"}, {"zlib", "https://zlib.net/"},
		{"webview2", "https://learn.microsoft.com/en-us/microsoft-edge/webview2/"},
		{"lucide", "https://github.com/lucide-icons/lucide"},
		{"yt-dlp", "https://github.com/yt-dlp/yt-dlp"}, {"ffmpeg", "https://ffmpeg.org/"}, {"deno", "https://github.com/denoland/deno"}
	};
}

std::string core::about_link(const std::string &id)
{
	const auto found {links.find(id)};
	if(found == links.end()) throw application_error {"invalidPayload", "web.invalid_payload"};
	return found->second;
}

nlohmann::json core::about_information(const std::string &runtime_version)
{
	using json = nlohmann::json;
	json dependencies = json::array();
	const auto library = [&](const std::string &id, const std::string &name, const std::string &version, const std::string &kind = "build", const std::string &note = "")
	{
		dependencies.push_back({{"id", id}, {"name", name}, {"version", version}, {"versionKind", version.empty() ? "unknown" : kind}, {"source", about_link(id)}, {"noteKey", note}});
	};
	library("json", "JSON for Modern C++", std::to_string(NLOHMANN_JSON_VERSION_MAJOR) + "." + std::to_string(NLOHMANN_JSON_VERSION_MINOR) + "." + std::to_string(NLOHMANN_JSON_VERSION_PATCH));
	library("zlib", "zlib", ZLIB_VERSION);
	library("webview2", "WebView2 SDK", build_info::webview_sdk);
	library("webview2", "WebView2 Runtime", runtime_version, "installed");
	library("lucide", "Lucide", build_info::lucide);
	return {{"buildRevision", build_info::revision}, {"architecture", sizeof(void *) == 8 ? "x64" : "x86"},
		{"nanaHost", false}, {"upstream", about_link("upstream")}, {"modifiedRepository", ""}, {"guiReleaseSource", ""},
		{"dependencies", std::move(dependencies)}, {"license", build_info::project_license}, {"lucideLicense", build_info::lucide_license}};
}

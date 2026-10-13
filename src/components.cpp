#include "components.hpp"
#include "core_text.hpp"
#include <array>
#include <charconv>
#include <chrono>
#include <regex>

namespace
{
	std::optional<std::vector<unsigned>> version_parts(const std::string &version, bool date)
	{
		static const std::regex date_pattern {R"((\d{4})\.(\d{2})\.(\d{2})(?:\.(\d{6}))?)"}, semver_pattern {R"((?:v)?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*))"};
		std::smatch match;
		if(!std::regex_match(version, match, date ? date_pattern : semver_pattern)) return {};
		std::vector<unsigned> parts;
		for(size_t n {1}; n < match.size() && match[n].matched; ++n)
		{
			const auto text {match[n].str()}; unsigned value {0};
			if(std::from_chars(text.data(), text.data() + text.size(), value).ec != std::errc {}) return {};
			parts.push_back(value);
		}
		if(date)
		{
			using namespace std::chrono;
			if(parts[0] < 2000 || parts[0] > 9999 || !year_month_day {year {int(parts[0])}, month {parts[1]}, day {parts[2]}}.ok()) return {};
			if(parts.size() == 4 && (parts[3] / 10000 > 23 || parts[3] / 100 % 100 > 59 || parts[3] % 100 > 59)) return {};
		}
		return parts;
	}
}

void core::validate_component_settings(const component_settings &settings)
{
	if(settings.ytdlp_channel != "stable" && settings.ytdlp_channel != "nightly") throw application_error {"invalidChannel", "web.component.invalid_channel"};
	for(const auto &path : {settings.ytdlp_path, settings.ffmpeg_directory})
		if(!path.empty() && (path.size() > 32768 || path.find_first_of("\r\n\"") != std::string::npos || path.find('\0') != std::string::npos || !fs::u8path(path).is_absolute()))
			throw application_error {"invalidComponentPath", "web.component.invalid_path"};
}

nlohmann::json core::component_settings_json(const component_settings &settings)
{
	return {{"ytdlpPath", settings.ytdlp_path}, {"ffmpegDirectory", settings.ffmpeg_directory}, {"ytdlpChannel", settings.ytdlp_channel}, {"checkAtStartup", settings.check_at_startup}, {"includeFfplay", settings.include_ffplay}};
}

core::component_settings core::read_component_settings(const nlohmann::json &value)
{
	component_settings result {value.at("ytdlpPath"), value.at("ffmpegDirectory"), value.at("ytdlpChannel"), value.value("checkAtStartup", false), value.value("includeFfplay", false)}; validate_component_settings(result); return result;
}

nlohmann::json core::component_json(const component_snapshot &snapshot)
{
	const auto error_json = [](const auto &error) -> nlohmann::json
	{
		return error ? nlohmann::json {{"code", error->code}, {"messageKey", error->message_key}, {"systemError", error->exit_code}} : nlohmann::json(nullptr);
	};
	return {{"component", snapshot.id}, {"path", snapshot.path}, {"companionPath", snapshot.companion_path}, {"channel", snapshot.channel},
		{"currentVersion", snapshot.current_version}, {"latestVersion", snapshot.latest_version}, {"installation", snapshot.installation},
		{"status", snapshot.status}, {"comparison", snapshot.comparison}, {"source", snapshot.source}, {"checkedAt", snapshot.checked_at},
		{"error", error_json(snapshot.error)}, {"localError", error_json(snapshot.local_error)}, {"httpStatus", snapshot.http_status}, {"testSource", snapshot.test_source},
		{"installAvailable", snapshot.install_available}, {"installBlock", snapshot.install_block}};
}

std::string core::component_source(const std::string &id, const std::string &channel)
{
	if(id == "yt-dlp") return "https://api.github.com/repos/yt-dlp/" + std::string(channel == "stable" ? "yt-dlp" : "yt-dlp-nightly-builds") + "/releases/latest";
	if(id == "ffmpeg") return "https://api.github.com/repos/yt-dlp/FFmpeg-Builds/releases/latest";
	if(id == "deno") return "https://api.github.com/repos/denoland/deno/releases/latest";
	if(id != "gui") throw application_error {"invalidComponent", "web.invalid_payload"};
	return {};
}

std::string core::compare_component_versions(const std::string &id, const std::string &current, const std::string &latest)
{
	if(id != "yt-dlp" && id != "deno") return "unknown";
	const auto left {version_parts(current, id == "yt-dlp")}, right {version_parts(latest, id == "yt-dlp")};
	if(!left || !right) return "unknown";
	if(id == "yt-dlp" && left->size() != right->size() && std::equal(left->begin(), left->begin() + 3, right->begin())) return "unknown";
	return *left == *right ? "equal" : *left < *right ? "older" : "newer";
}

void core::read_component_release(component_snapshot &snapshot, const release_response &response)
{
	snapshot.http_status = response.status;
	if(!response.error.empty())
	{
		snapshot.status = response.error == "offline" ? "offline" : response.error == "unsupportedProxy" ? "unsupported" : "failed";
		snapshot.error = failure {response.error, "web.component." + response.error, response.system_error}; return;
	}
	try
	{
		if(response.status != 200) { snapshot.status = "failed"; snapshot.error = failure {"httpError", "web.component.httpError", response.status}; return; }
		const auto value {nlohmann::json::parse(response.body)};
		const auto tag {value.at("tag_name").get<std::string>()};
		if(value.at("draft").get<bool>() || value.at("prerelease").get<bool>()) throw std::runtime_error {"release"};
		const auto repository {snapshot.source.substr(29, snapshot.source.size() - 29 - 16)};
		if(value.at("html_url").get<std::string>() != "https://github.com/" + repository + "/releases/tag/" + tag) throw std::runtime_error {"source"};
		snapshot.release = value;
		if(snapshot.id == "ffmpeg")
		{
			static const std::regex name_pattern {R"(Latest Auto-Build \((\d{4})-(\d{2})-(\d{2}) (\d{2}):(\d{2})\))"}; std::smatch match;
			const auto name {value.at("name").get<std::string>()};
			if(tag != "latest" || !std::regex_match(name, match, name_pattern) || !version_parts(match[1].str() + "." + match[2].str() + "." + match[3].str() + "." + match[4].str() + match[5].str() + "00", true)) throw std::runtime_error {"version"};
			snapshot.latest_version = match[1].str() + "-" + match[2].str() + "-" + match[3].str() + " " + match[4].str() + ":" + match[5].str();
		}
		else
		{
			const auto parts {version_parts(tag, snapshot.id == "yt-dlp")};
			if(!parts || snapshot.id == "yt-dlp" && parts->size() != (snapshot.channel == "nightly" ? 4 : 3)) throw std::runtime_error {"version"};
			snapshot.latest_version = tag;
		}
		snapshot.comparison = compare_component_versions(snapshot.id, snapshot.current_version, snapshot.latest_version);
		snapshot.status = snapshot.installation == "missing" ? "notInstalled" : snapshot.comparison == "older" ? "updateAvailable" : snapshot.comparison == "unknown" ? "incomparable" : "current";
	}
	catch(const std::exception &) { snapshot.status = "failed"; snapshot.error = failure {"invalidRelease", "web.component.invalidRelease"}; }
}

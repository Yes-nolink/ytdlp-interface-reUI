#include "components.hpp"
#include "core_text.hpp"
#include <regex>

core::component_package core::select_component_package(const component_snapshot &snapshot, const nlohmann::json &release, const std::string &architecture)
{
	if(snapshot.id == "gui") throw application_error {"guiSourceUnavailable", "web.component.guiSourceUnavailable"};
	component_snapshot checked {snapshot}; checked.source = component_source(snapshot.id, snapshot.channel);
	read_component_release(checked, {release.dump(), 200});
	if(checked.error || checked.latest_version.empty()) throw application_error {"invalidRelease", "web.component.invalidRelease"};
	if(architecture != "x64" && architecture != "x86" && architecture != "arm64") throw application_error {"unsupportedPlatform", "web.update.unsupportedPlatform"};
	component_package package; package.component = snapshot.id; package.tag = release.at("tag_name"); package.version = checked.latest_version; package.architecture = architecture;
	package.repository = snapshot.id == "yt-dlp" ? snapshot.channel == "stable" ? "yt-dlp/yt-dlp" : "yt-dlp/yt-dlp-nightly-builds" : snapshot.id == "ffmpeg" ? "yt-dlp/FFmpeg-Builds" : "denoland/deno";
	package.asset = snapshot.id == "yt-dlp" ? architecture == "x64" ? "yt-dlp.exe" : architecture == "x86" ? "yt-dlp_x86.exe" : "yt-dlp_arm64.exe" : snapshot.id == "ffmpeg" ? "ffmpeg-master-latest-win" + std::string(architecture == "x64" ? "64" : architecture == "x86" ? "32" : "arm64") + "-gpl.zip" : architecture == "x64" ? "deno-x86_64-pc-windows-msvc.zip" : architecture == "arm64" ? "deno-aarch64-pc-windows-msvc.zip" : "";
	package.zip = snapshot.id != "yt-dlp";
	if(package.asset.empty() || !release.contains("assets") || !release.at("assets").is_array()) throw application_error {"packageUnavailable", "web.update.packageUnavailable"};
	unsigned matches {0};
	for(const auto &asset : release.at("assets"))
		if(asset.value("name", "") == package.asset)
		{
			++matches;
			package.url = asset.at("browser_download_url");
			if(package.url != "https://github.com/" + package.repository + "/releases/download/" + package.tag + "/" + package.asset || asset.value("state", "") != "uploaded" || asset.at("url").get<std::string>() != "https://api.github.com/repos/" + package.repository + "/releases/assets/" + std::to_string(asset.at("id").get<std::uint64_t>())) throw application_error {"invalidRelease", "web.component.invalidRelease"};
			const auto type {asset.at("content_type").get<std::string>()};
			if(type != "application/octet-stream" && type != (package.zip ? "application/zip" : "application/x-msdownload")) throw application_error {"packageType", "web.update.packageType"};
			package.size = asset.at("size").get<std::uint64_t>();
			if(!package.size || package.size > 512ull * 1024 * 1024) throw application_error {"packageTooLarge", "web.update.packageTooLarge"};
			const auto digest {asset.value("digest", nlohmann::json(nullptr))};
			if(!digest.is_string() || !std::regex_match(digest.get<std::string>(), std::regex {"sha256:[0-9a-f]{64}"})) throw application_error {"digestUnavailable", "web.update.digestUnavailable"};
			package.sha256 = digest.get<std::string>().substr(7);
		}
	if(matches != 1) throw application_error {"packageUnavailable", "web.update.packageUnavailable"};
	return package;
}

nlohmann::json core::component_update_json(const component_update_snapshot &snapshot)
{
	auto targets {nlohmann::json::array()}; for(const auto &path : snapshot.targets) targets.push_back(to_utf8(path.wstring()));
	return {{"component", snapshot.component}, {"updateId", snapshot.update_id}, {"generation", snapshot.generation}, {"revision", snapshot.revision},
		{"state", snapshot.state}, {"failedStage", snapshot.failed_stage}, {"downloadedBytes", snapshot.downloaded}, {"totalBytes", snapshot.total ? nlohmann::json(snapshot.total) : nlohmann::json(nullptr)},
		{"completedFiles", snapshot.completed_files}, {"totalFiles", snapshot.total_files}, {"targets", targets}, {"customPath", snapshot.custom_path}, {"includeFfplay", snapshot.include_ffplay},
		{"cancellable", snapshot.cancellable}, {"backupDirectory", snapshot.backup_directory}, {"resultVersion", snapshot.result_version}, {"recoveryRequired", snapshot.recovery_required}, {"testSource", snapshot.test_source},
		{"package", {{"tag", snapshot.package.tag}, {"version", snapshot.package.version}, {"repository", snapshot.package.repository}, {"asset", snapshot.package.asset}, {"url", snapshot.package.url}, {"sha256", snapshot.package.sha256}, {"architecture", snapshot.package.architecture}, {"size", snapshot.package.size}, {"integrity", "githubReleaseDigest"}}},
		{"error", snapshot.error ? nlohmann::json {{"code", snapshot.error->code}, {"messageKey", snapshot.error->message_key}, {"systemError", snapshot.error->exit_code}} : nlohmann::json(nullptr)},
		{"recoveryError", snapshot.recovery_error ? nlohmann::json {{"code", snapshot.recovery_error->code}, {"messageKey", snapshot.recovery_error->message_key}, {"systemError", snapshot.recovery_error->exit_code}} : nlohmann::json(nullptr)}};
}

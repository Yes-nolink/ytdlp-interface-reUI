#include "download_application.hpp"
#include "core_text.hpp"
#include <regex>

void core::download_application::component_update_event_locked(component_update_snapshot snapshot, const std::string &request_id)
{
	const auto found {component_updates.find(snapshot.component)};
	if(found != component_updates.end() && (found->second.update_id != snapshot.update_id || found->second.generation != snapshot.generation || found->second.state == "cancelled" && snapshot.state != "cancelled")) return;
	snapshot.revision = found == component_updates.end() ? 1 : found->second.revision + 1;
	if(snapshot.state == "rollbackFailed") { auto &component {components.at(snapshot.component)}; component.current_version.clear(); component.comparison = "unknown"; component.installation = "unknown"; component.status = "unsupported"; component.install_available = false; component.install_block = "recoveryRequired"; component.error = failure {"recoveryRequired", "web.update.recoveryRequired"}; component_event_locked(component); }
	component_updates[snapshot.component] = snapshot; application_event event {"componentUpdateChanged", request_id}; event.component_update = std::move(snapshot); enqueue(std::move(event));
}

void core::download_application::require_component_idle_locked() const
{
	if(component_committing || metadata_active) throw application_error {"componentBusy", "web.update.componentBusy"};
	for(const auto &[id, operation] : executions) if(!operation->done) throw application_error {"componentBusy", "web.update.componentBusy"};
	for(const auto &[id, operation] : parsing) if(!operation->done) throw application_error {"componentBusy", "web.update.componentBusy"};
}

bool core::download_application::components_block_execution_locked() const
{
	if(component_committing || component_recovery_blocked) return true;
	for(const auto &[id, snapshot] : component_updates) if(snapshot.recovery_required) return true;
	return false;
}

core::component_update_snapshot core::download_application::component_update_target_locked(const component_snapshot &snapshot) const
{
	component_update_snapshot update; update.component = snapshot.id; update.test_source = snapshot.test_source; update.include_ffplay = snapshot.id == "ffmpeg" && preferences.components.include_ffplay;
	if(snapshot.id == "gui") throw application_error {"guiSourceUnavailable", "web.component.guiSourceUnavailable"};
	const auto path {snapshot.path.empty() && snapshot.id == "deno" ? component_context.appdir / "deno.exe" : fs::u8path(snapshot.path)};
	if(snapshot.id == "deno" && path.parent_path() != component_context.appdir) throw application_error {"externalInstallation", "web.update.externalInstallation"};
	const auto architecture {fs::exists(path) ? component_file_architecture(path) : native_component_architecture()};
	update.package = select_component_package(snapshot, snapshot.release, architecture); update.targets.push_back(path);
	if(snapshot.id == "ffmpeg") { update.targets.push_back(path.parent_path() / "ffprobe.exe"); if(update.include_ffplay) update.targets.push_back(path.parent_path() / "ffplay.exe"); }
	update.custom_path = snapshot.id == "yt-dlp" ? !preferences.components.ytdlp_path.empty() : snapshot.id == "ffmpeg" && !preferences.components.ffmpeg_directory.empty();
	return update;
}

void core::download_application::restore_component_updates_locked()
{
	try
	{
		for(auto snapshot : read_component_transactions(component_context))
		{
			next_update_generation = std::max(next_update_generation, snapshot.generation);
			if(snapshot.recovery_required)
			{
				const auto &component {components.at(snapshot.component)}; auto allowed {fs::u8path(component.path)};
				if(allowed.empty() && snapshot.component == "deno") allowed = component_context.appdir / "deno.exe";
				std::vector<fs::path> paths {allowed}; if(snapshot.component == "ffmpeg") { paths.push_back(allowed.parent_path() / "ffprobe.exe"); if(snapshot.include_ffplay) paths.push_back(allowed.parent_path() / "ffplay.exe"); }
				if(snapshot.targets != paths) { component_recovery_blocked = true; snapshot.error = failure {"recoveryConflict", "web.update.recoveryConflict"}; }
			}
			const auto found {component_updates.find(snapshot.component)};
			if(found == component_updates.end() || snapshot.recovery_required || !found->second.recovery_required && snapshot.generation > found->second.generation) component_updates[snapshot.component] = std::move(snapshot);
		}
	}
	catch(const std::exception &) { component_recovery_blocked = true; component_update_snapshot snapshot; snapshot.component = "yt-dlp"; snapshot.state = "rollbackFailed"; snapshot.recovery_required = true; snapshot.backup_directory = to_utf8(component_context.update_root.wstring()); snapshot.error = failure {"invalidTransaction", "web.update.invalidTransaction"}; component_updates[snapshot.component] = snapshot; }
}

nlohmann::json core::download_application::get_component_updates() const
{
	std::lock_guard lock {mutex}; auto result {nlohmann::json::array()}; for(const auto &[id, snapshot] : component_updates) result.push_back(component_update_json(snapshot)); return result;
}

nlohmann::json core::download_application::prepare_component_update(const std::string &id, const std::string &request_id)
{
	std::lock_guard lock {mutex}; require_open(); require_component_idle_locked();
	if(component_recovery_blocked) throw application_error {"recoveryRequired", "web.update.recoveryRequired"};
	for(const auto &[key, snapshot] : component_updates) if(snapshot.recovery_required) throw application_error {"recoveryRequired", "web.update.recoveryRequired"};
	for(const auto &[key, operation] : component_operations) if(key.starts_with("update:") && !operation->done) throw application_error {"updateBusy", "web.update.updateBusy"};
	if(const auto found {component_operations.find("update:" + id)}; found != component_operations.end()) { if(found->second->worker.joinable()) found->second->worker.join(); component_operations.erase(found); }
	if(!components.contains(id)) throw application_error {"invalidComponent", "web.invalid_payload"};
	if(id == "gui") throw application_error {"guiSourceUnavailable", "web.component.guiSourceUnavailable"};
	const auto &component {components.at(id)}; if(!component.install_available) throw application_error {component.install_block.empty() ? "checkRequired" : component.install_block, "web.update." + (component.install_block.empty() ? std::string {"checkRequired"} : component.install_block)};
	auto snapshot {component_update_target_locked(component)}; snapshot.update_id = native_component_update_id(); snapshot.generation = ++next_update_generation; snapshot.state = "preparingInstall";
	component_updates.erase(id); component_update_event_locked(snapshot, request_id);
	auto operation {std::make_shared<parse_operation>()}; component_operations["update:" + id] = operation; const auto network {preferences.application.network};
	operation->worker = std::thread([this, operation, snapshot, request_id, network]() mutable
	{
		try { snapshot = prepare_component_install(snapshot, component_context, network.proxy_enabled ? network.proxy : "", operation->working); }
		catch(const application_error &error) { snapshot.state = error.detail.code == "cancelled" ? "cancelled" : "verificationFailed"; snapshot.error = error.detail; snapshot.failed_stage = "preparingInstall"; snapshot.cancellable = false; }
		catch(const std::exception &) { snapshot.state = "verificationFailed"; snapshot.error = failure {"updateFailed", "web.update.updateFailed"}; snapshot.cancellable = false; }
		std::lock_guard lock {mutex}; operation->done = true; component_update_event_locked(snapshot, request_id); dirty = true; wake.notify_one();
	});
	return component_update_json(snapshot);
}

core::component_update_snapshot &core::download_application::component_update_at_locked(const std::string &update_id, std::uint64_t generation)
{
	for(auto &[id, snapshot] : component_updates) if(snapshot.update_id == update_id && snapshot.generation == generation) return snapshot;
	throw application_error {"staleUpdate", "web.update.staleUpdate"};
}

void core::download_application::confirm_component_update(const std::string &update_id, std::uint64_t generation, bool custom_path_confirmed)
{
	std::lock_guard lock {mutex}; require_open(); auto snapshot {component_update_at_locked(update_id, generation)};
	if(snapshot.state != "waitingConfirmation") throw application_error {"staleUpdate", "web.update.staleUpdate"};
	require_component_idle_locked();
	if(snapshot.custom_path && !custom_path_confirmed) throw application_error {"customConfirmationRequired", "web.update.customConfirmationRequired"};
	if(const auto found {component_operations.find("update:" + snapshot.component)}; found != component_operations.end()) { if(!found->second->done) throw application_error {"updateBusy", "web.update.updateBusy"}; if(found->second->worker.joinable()) found->second->worker.join(); component_operations.erase(found); }
	if(const auto found {component_operations.find(snapshot.component)}; found != component_operations.end()) found->second->working = false;
	auto operation {std::make_shared<parse_operation>()}; component_operations["update:" + snapshot.component] = operation; const auto network {preferences.application.network}; snapshot.state = "downloading"; component_update_event_locked(snapshot);
	operation->worker = std::thread([this, operation, snapshot, network]() mutable
	{
		auto last {std::chrono::steady_clock::now()}; std::string phase; std::uint64_t bytes {0};
		const auto progress = [&](const component_update_snapshot &value)
		{
			if(value.state == "installed" || value.state == "rolledBack" || value.state == "rollbackFailed" || value.state == "downloadFailed" || value.state == "verificationFailed" || value.state == "replacementFailed" || value.state == "cancelled") return;
			const auto now {std::chrono::steady_clock::now()}; if(value.state == phase && value.state == "downloading" && value.downloaded - bytes < 1024 * 1024 && now - last < std::chrono::milliseconds {100}) return;
			last = now; phase = value.state; bytes = value.downloaded; std::lock_guard lock {mutex}; component_update_event_locked(value);
		};
		try
		{
			snapshot = execute_component_install(snapshot, component_context, network.proxy_enabled ? network.proxy : "", operation->working, progress, [&]
			{
				std::lock_guard lock {mutex}; require_open(); require_component_idle_locked(); if(!operation->working) throw application_error {"cancelled", "web.update.cancelled"};
				component_committing = true; auto current {component_updates.at(snapshot.component)}; current.cancellable = false; component_update_event_locked(current);
			});
		}
		catch(const application_error &error) { snapshot.state = error.detail.code == "cancelled" ? "cancelled" : "replacementFailed"; snapshot.error = error.detail; snapshot.cancellable = false; }
		catch(const std::exception &) { snapshot.state = "replacementFailed"; snapshot.error = failure {"updateFailed", "web.update.updateFailed"}; snapshot.cancellable = false; }
		std::atomic_bool probe_working {true}; auto component {component_snapshot {}};
			{ std::lock_guard lock {mutex}; component = components.at(snapshot.component); if(snapshot.state == "installed") { reset_components_locked(); component.path = components.at(snapshot.component).path; } }
		if(snapshot.state == "installed" || snapshot.state == "rolledBack")
		{
			component = probe_component(component, probe_working); component.comparison = compare_component_versions(component.id, component.current_version, component.latest_version);
			component.status = component.comparison == "older" ? "updateAvailable" : component.comparison == "unknown" ? "incomparable" : "current";
				if(component.id != "ffmpeg" && (component.comparison == "equal" || component.comparison == "newer")) { component.install_available = false; component.install_block = "noNewerRelease"; }
		}
		std::lock_guard lock {mutex}; component_committing = false; operation->done = true; component_update_event_locked(snapshot); if(snapshot.state == "installed" || snapshot.state == "rolledBack") { components[component.id] = component; component_event_locked(component); } dirty = true; wake.notify_one(); metadata_wake.notify_all();
	});
}

void core::download_application::cancel_component_update(const std::string &update_id, std::uint64_t generation)
{
	std::lock_guard lock {mutex}; require_open(); auto snapshot {component_update_at_locked(update_id, generation)};
	if(snapshot.state == "cancelled") return;
	if(!snapshot.cancellable || component_committing) throw application_error {"commitCannotCancel", "web.update.commitCannotCancel"};
	if(const auto found {component_operations.find("update:" + snapshot.component)}; found != component_operations.end()) found->second->working = false;
	snapshot.state = "cancelled"; snapshot.cancellable = false; snapshot.result_version.clear(); component_update_event_locked(snapshot);
}

void core::download_application::recover_component_update(const std::string &update_id, std::uint64_t generation)
{
	std::lock_guard lock {mutex}; require_open(); require_component_idle_locked(); auto snapshot {component_update_at_locked(update_id, generation)};
	if(!snapshot.recovery_required || component_recovery_blocked) throw application_error {"recoveryConflict", "web.update.recoveryConflict"};
	if(const auto found {component_operations.find("update:" + snapshot.component)}; found != component_operations.end()) { if(!found->second->done) throw application_error {"updateBusy", "web.update.updateBusy"}; if(found->second->worker.joinable()) found->second->worker.join(); component_operations.erase(found); }
	component_committing = true; auto operation {std::make_shared<parse_operation>()}; component_operations["update:" + snapshot.component] = operation;
	operation->worker = std::thread([this, operation, snapshot]() mutable
	{
		try { snapshot = recover_component_install(snapshot, component_context, [&](const auto &value) { std::lock_guard lock {mutex}; component_update_event_locked(value); }); }
		catch(const application_error &error) { snapshot.state = "rollbackFailed"; snapshot.error = error.detail; }
		catch(const std::exception &) { snapshot.state = "rollbackFailed"; snapshot.error = failure {"invalidTransaction", "web.update.invalidTransaction"}; }
		component_snapshot component; { std::lock_guard lock {mutex}; component = components.at(snapshot.component); }
		if(snapshot.state == "rolledBack") { std::atomic_bool working {true}; component = probe_component(component, working); component.error.reset(); component.status = component.installation == "missing" ? "notInstalled" : "notChecked"; }
		std::lock_guard lock {mutex}; operation->done = true; component_committing = false; component_update_event_locked(snapshot); if(snapshot.state == "rolledBack") { components[component.id] = component; component_event_locked(component); } dirty = true; wake.notify_one(); metadata_wake.notify_all();
	});
}

void core::download_application::set_component_update_preferences(bool startup, bool ffplay)
{
	std::lock_guard lock {mutex}; auto value {preferences}; value.components.check_at_startup = startup; value.components.include_ffplay = ffplay; save_settings_locked(std::move(value));
}

void core::download_application::load_component_release_notes(const std::string &id, unsigned page, const std::string &request_id)
{
	std::lock_guard lock {mutex}; require_open(); if(!components.contains(id) || id == "gui") throw application_error {"guiSourceUnavailable", "web.component.guiSourceUnavailable"}; if(!page || page > 20) throw application_error {"invalidPayload", "web.invalid_payload"};
	const auto key {"notes:" + id}; if(component_operations.contains(key)) throw application_error {"busy", "web.task_busy"};
	auto operation {std::make_shared<parse_operation>()}; component_operations[key] = operation; const auto component {components.at(id)}; const auto network {preferences.application.network};
	operation->worker = std::thread([this, operation, component, network, page, request_id]
	{
		application_event event {"componentReleaseNotes", request_id}; event.release_notes = {{"component", component.id}, {"page", page}, {"releases", nlohmann::json::array()}};
		try
		{
			const auto url {component.source.substr(0, component.source.size() - 7) + "?per_page=30&page=" + std::to_string(page)}; const auto response {component_http_get(url, network.proxy_enabled ? network.proxy : "", component_context, operation->working, 1024 * 1024)};
			if(!response.error.empty()) throw application_error {response.error, "web.component." + response.error}; if(response.status != 200) throw application_error {"httpError", "web.component.httpError"};
			const auto values {nlohmann::json::parse(response.body)}; if(!values.is_array() || values.size() > 30) throw application_error {"invalidRelease", "web.component.invalidRelease"};
			for(const auto &value : values)
			{
					if(component.id == "ffmpeg" && value.value("tag_name", "") != "latest")
					{
						std::smatch match; const auto tag {value.at("tag_name").get<std::string>()};
						if(!std::regex_match(tag, match, std::regex {R"(autobuild-(\d{4})-(\d{2})-(\d{2})-(\d{2})-(\d{2}))"})) continue;
						const auto date {match[1].str() + "." + match[2].str() + "." + match[3].str() + "." + match[4].str() + match[5].str() + "00"};
						if(value.at("draft").get<bool>() || value.at("prerelease").get<bool>() || compare_component_versions("yt-dlp", date, date) != "equal" || value.at("html_url") != "https://github.com/yt-dlp/FFmpeg-Builds/releases/tag/" + tag || value.at("name") != "Auto-Build " + match[1].str() + "-" + match[2].str() + "-" + match[3].str() + " " + match[4].str() + ":" + match[5].str()) continue;
					}
					else { auto checked {component}; checked.error.reset(); read_component_release(checked, {value.dump(), 200}); if(checked.error) continue; }
					const auto date_value {value.value("published_at", nlohmann::json(nullptr))}, body_value {value.value("body", nlohmann::json(nullptr))};
					if(!date_value.is_null() && !date_value.is_string() || !body_value.is_null() && !body_value.is_string()) throw application_error {"invalidRelease", "web.component.invalidRelease"};
					const auto date {date_value.is_null() ? std::string {} : date_value.get<std::string>()}, body {body_value.is_null() ? std::string {} : body_value.get<std::string>()}; if(date.size() > 40 || body.size() > 256 * 1024) throw application_error {"invalidRelease", "web.component.invalidRelease"};
				event.release_notes["releases"].push_back({{"tag", value.at("tag_name")}, {"publishedAt", date}, {"body", body}, {"url", value.at("html_url")}});
			}
			event.release_notes["hasMore"] = values.size() == 30;
		}
		catch(const application_error &error) { event.error = error.detail; }
		catch(const std::exception &) { event.error = failure {"invalidRelease", "web.component.invalidRelease"}; }
		std::lock_guard lock {mutex}; operation->done = true; if(operation->working && !closing) enqueue(std::move(event)); dirty = true; wake.notify_one();
	});
}

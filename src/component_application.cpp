#include "download_application.hpp"
#include "core_text.hpp"

void core::download_application::component_event_locked(const component_snapshot &snapshot, const std::string &request_id)
{
	application_event event {"componentChanged", request_id}; event.component = snapshot; enqueue(std::move(event));
}

void core::download_application::reset_components_locked()
{
	const auto paths {resolve_component_paths(component_context.appdir, preferences.components)};
	settings.ytdlp_path = paths.ytdlp; settings.ffmpeg_path = paths.ffmpeg.parent_path(); settings.deno_path = paths.deno;
	settings.use_ffmpeg_location = true;
	for(const auto id : {"gui", "yt-dlp", "ffmpeg", "deno"})
	{
		component_snapshot snapshot; snapshot.id = id;
		snapshot.channel = snapshot.id == "yt-dlp" ? preferences.components.ytdlp_channel : "";
		snapshot.path = to_utf8((snapshot.id == "yt-dlp" ? paths.ytdlp : snapshot.id == "ffmpeg" ? paths.ffmpeg : snapshot.id == "deno" ? paths.deno : fs::path {}).wstring());
		if(snapshot.id == "ffmpeg") snapshot.companion_path = to_utf8((paths.ffmpeg.parent_path() / "ffprobe.exe").wstring());
		snapshot.source = component_source(snapshot.id, snapshot.channel); snapshot.test_source = !component_context.test_origin.empty();
		const auto found {components.find(id)};
		if(found != components.end() && (snapshot.id == "gui" || found->second.path == snapshot.path && found->second.channel == snapshot.channel)) continue;
		if(component_operations.contains(id))
		{
			component_operations.at(id)->working = false;
			auto operation {component_operations.extract(id)}; operation.key() = "stale:" + snapshot.id + ":" + std::to_string(settings_revision); component_operations.insert(std::move(operation));
		}
		components[id] = snapshot; component_event_locked(snapshot);
	}
}

nlohmann::json core::download_application::get_components() const
{
	std::lock_guard lock {mutex}; auto result {nlohmann::json::array()};
	for(const auto id : {"gui", "yt-dlp", "ffmpeg", "deno"}) result.push_back(component_json(components.at(id)));
	return result;
}

void core::download_application::check_components(const std::vector<std::string> &requested, const std::string &request_id, bool latest)
{
	std::lock_guard lock {mutex}; require_open();
	const auto ids {requested.empty() ? std::vector<std::string> {"gui", "yt-dlp", "ffmpeg", "deno"} : requested};
	for(const auto &id : ids) if(!components.contains(id)) throw application_error {"invalidComponent", "web.invalid_payload"};
	for(const auto &id : ids)
	{
		if(component_updates.contains(id) && component_updates.at(id).recovery_required) { auto &snapshot {components.at(id)}; snapshot.current_version.clear(); snapshot.comparison = "unknown"; snapshot.installation = "unknown"; snapshot.status = "unsupported"; snapshot.install_available = false; snapshot.install_block = "recoveryRequired"; snapshot.error = failure {"recoveryRequired", "web.update.recoveryRequired"}; component_event_locked(snapshot, request_id); continue; }
		if(component_committing || component_operations.contains("update:" + id) && !component_operations.at("update:" + id)->done) continue;
		if(component_operations.contains(id) && component_operations.at(id)->done)
		{
			auto worker {std::move(component_operations.at(id)->worker)}; component_operations.erase(id); if(worker.joinable()) worker.join();
		}
			if(component_operations.contains(id)) continue;
		auto operation {std::make_shared<parse_operation>()}; component_operations[id] = operation;
		auto snapshot {components.at(id)}; snapshot.status = "checking"; snapshot.error.reset(); snapshot.local_error.reset(); snapshot.current_version.clear(); snapshot.latest_version.clear(); snapshot.comparison = "unknown"; snapshot.checked_at = 0; snapshot.http_status = 0; snapshot.release = {}; snapshot.install_available = false; snapshot.install_block.clear();
		components[id] = snapshot; component_event_locked(snapshot, request_id);
		const auto network {preferences.application.network};
		operation->worker = std::thread([this, operation, snapshot, request_id, latest, network]() mutable
		{
			try
			{
				snapshot = probe_component(std::move(snapshot), operation->working);
				if(snapshot.id == "gui") { snapshot.status = "unsupported"; snapshot.install_block = "guiSourceUnavailable"; snapshot.error = failure {"guiSourceUnavailable", "web.component.guiSourceUnavailable"}; }
				else if(latest && operation->working)
				{
					read_component_release(snapshot, component_http_get(snapshot.source, network.proxy_enabled ? network.proxy : "", component_context, operation->working, 1024 * 1024));
					if(!snapshot.error)
					{
						try
						{
							component_update_snapshot target; { std::lock_guard lock {mutex}; target = component_update_target_locked(snapshot); }
							target.update_id = native_component_update_id(); prepare_component_install(target, component_context, network.proxy_enabled ? network.proxy : "", operation->working, true);
							if(snapshot.id != "ffmpeg" && (snapshot.comparison == "equal" || snapshot.comparison == "newer")) snapshot.install_block = "noNewerRelease";
							else snapshot.install_available = true;
						}
						catch(const application_error &error) { snapshot.install_block = error.detail.code; }
						catch(const std::exception &) { snapshot.install_block = "invalidRelease"; }
					}
				}
				else snapshot.status = snapshot.installation == "missing" ? "notInstalled" : snapshot.local_error ? "currentUnknown" : "notChecked";
			}
			catch(const std::exception &) { snapshot.status = "failed"; snapshot.error = failure {"componentCheckFailed", "web.component.componentCheckFailed"}; }
			if(latest) snapshot.checked_at = timestamp_now();
			std::lock_guard lock {mutex}; operation->done = true;
			const auto &current {components.at(snapshot.id)};
			// 路径/频道已经改变时丢弃旧检查结果，不能覆盖新配置。
			if(!closing && (snapshot.id == "gui" || current.path == snapshot.path) && current.channel == snapshot.channel && operation->working)
			{ components[snapshot.id] = snapshot; component_event_locked(snapshot, request_id); }
			dirty = true; wake.notify_one();
		});
	}
}

void core::download_application::set_component_path(const std::string &id, const std::string &path, const std::string &request_id)
{
	if(id != "yt-dlp" && id != "ffmpeg") throw application_error {"invalidComponent", "web.invalid_payload"};
	std::lock_guard lock {mutex}; require_open();
	if(component_operations.contains("path:" + id)) throw application_error {"busy", "web.task_busy"};
	auto candidate {preferences.components}; (id == "yt-dlp" ? candidate.ytdlp_path : candidate.ffmpeg_directory) = path; validate_component_settings(candidate);
	auto operation {std::make_shared<parse_operation>()}; component_operations["path:" + id] = operation;
	operation->worker = std::thread([this, operation, id, path, request_id]
	{
		application_event event {"componentPathSaved", request_id};
		try
		{
			if(!path.empty()) validate_component_path(id, fs::u8path(path), operation->working);
			std::lock_guard lock {mutex}; require_open(); auto value {preferences};
			(id == "yt-dlp" ? value.components.ytdlp_path : value.components.ffmpeg_directory) = path;
			save_settings_locked(std::move(value)); enqueue(event);
		}
		catch(const application_error &error) { std::lock_guard lock {mutex}; event.name = "componentPathFailed"; event.error = error.detail; enqueue(event); }
		catch(const std::exception &) { std::lock_guard lock {mutex}; event.name = "componentPathFailed"; event.error = failure {"invalidComponentPath", "web.component.invalid_path"}; enqueue(event); }
		try { if(event.name == "componentPathSaved" && !closing) check_components({}, "", false); }
		catch(const application_error &) {}
		std::lock_guard lock {mutex}; operation->done = true; dirty = true; wake.notify_one();
	});
}

void core::download_application::set_component_channel(const std::string &channel)
{
	{ std::lock_guard lock {mutex}; auto value {preferences}; value.components.ytdlp_channel = channel; save_settings_locked(std::move(value)); }
	check_components({"yt-dlp"});
}

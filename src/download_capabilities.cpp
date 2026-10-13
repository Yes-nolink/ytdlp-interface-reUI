#include "download_application.hpp"
#include "naming_model.hpp"
#include "core_text.hpp"
#include <set>
#include <sstream>

namespace
{
	std::string format_kind(const nlohmann::json &format)
	{
		const auto video {format.value("vcodec", nlohmann::json(nullptr))}, audio {format.value("acodec", nlohmann::json(nullptr))};
		if(video == "none") return audio == "none" ? "other" : "audio";
		if(audio == "none") return "video";
		return "mixed";
	}
	void mark(core::processing_options &options, const std::string &key)
	{
		if(std::find(options.edited.begin(), options.edited.end(), key) == options.edited.end()) options.edited.push_back(key);
	}
}

void core::validate_media_options(const output_options &options, const nlohmann::json &metadata)
{
	validate_sections(options.processing.sections, metadata);
	const auto &p {options.processing};
	if(std::find(p.edited.begin(), p.edited.end(), "subTracks") != p.edited.end() && !p.sub_langs.empty())
	{
		std::istringstream languages {p.sub_langs};
		for(std::string language; std::getline(languages, language, ',');)
		{
			if(!metadata.contains("subtitles") || !metadata.at("subtitles").contains(language) || language == "live_chat") throw application_error {"invalidSubtitle", "web.invalid_subtitle"};
			const auto &files {metadata.at("subtitles").at(language)};
			if(!p.sub_format.empty() && std::none_of(files.begin(), files.end(), [&](const auto &file) { return file.value("ext", "") == p.sub_format; })) throw application_error {"invalidSubtitle", "web.invalid_subtitle"};
		}
	}
	const auto &ids {options.processing.format_ids};
	if(ids.empty()) return;
	if(!metadata.contains("formats") || !metadata["formats"].is_array()) throw application_error {"mediaNotReady", "web.refresh_first"};
	std::set<std::string> seen; unsigned video {0}, audio {0}, mixed {0};
	for(const auto &id : ids)
	{
		const auto &formats {metadata.at("formats")};
		const auto found {std::find_if(formats.begin(), formats.end(), [&](const auto &format) { return format.value("format_id", "") == id; })};
		if(found == formats.end() || !seen.insert(id).second || format_kind(*found) == "other") throw application_error {"invalidFormat", "web.invalid_format"};
		const auto kind {format_kind(*found)};
		video += kind == "video"; audio += kind == "audio"; mixed += kind == "mixed";
	}
	if(video > 1 || mixed && ids.size() > 1 || !options.processing.multi_audio && audio > 1)
		throw application_error {"conflictingFormats", "web.conflicting_formats"};
}

nlohmann::json core::download_application::get_media_details(const std::string &id_or_url) const
{
	std::lock_guard lock {mutex};
	const download_task *task {queue.contains(id_or_url) ? &queue.at(id_or_url) : nullptr};
	const auto url {task ? task->snapshot.url : id_or_url};
	if(!task && !media.contains(url)) throw application_error {"mediaNotReady", "web.parse_first"};
	const auto &info {task ? task->metadata : media.at(url)};
	nlohmann::json formats = nlohmann::json::array(), subtitles = nlohmann::json::array();
	if(info.contains("formats"))
		for(const auto &format : info.at("formats"))
		{
			const auto kind {format_kind(format)}; if(kind == "other") continue;
				auto value {visible_details(visible_metadata(format))}; value["kind"] = kind; formats.push_back(std::move(value));
		}
	if(info.contains("subtitles") && info.at("subtitles").is_object())
		for(const auto &entry : info.at("subtitles").items())
		{
			if(entry.key() == "live_chat" || !entry.value().is_array() || entry.value().empty()) continue;
			std::set<std::string> extensions;
			for(const auto &file : entry.value()) if(file.contains("ext") && file["ext"].is_string()) extensions.insert(file["ext"].get<std::string>());
			subtitles.push_back({{"language", entry.key()}, {"name", entry.value().front().value("name", entry.key())}, {"formats", extensions}});
		}
	const auto command {task && metadata_commands.contains(task->snapshot.id) ? metadata_commands.at(task->snapshot.id) : metadata_commands.contains(url) ? metadata_commands.at(url) : ""};
		return {{"taskId", task ? task->snapshot.id : ""}, {"url", redact(url)}, {"metadata", visible_details(visible_metadata(info))}, {"metadataCommand", command},
		{"formats", std::move(formats)}, {"subtitles", std::move(subtitles)}, {"refreshing", task && task->snapshot.refreshing},
		{"sectionsAvailable", info.value("is_live", nlohmann::json(false)) != true && info.value("live_status", nlohmann::json("")) != "is_live" && !info.contains("entries")},
		{"metadataReady", info.contains("formats")}, {"options", output_options_json(task ? task->snapshot.options : preferences.defaults)}};
}

nlohmann::json core::download_application::preview_options(const output_options &options, const std::string &id_or_url) const
{
	std::lock_guard lock {mutex};
	if(!id_or_url.empty())
	{
		if(queue.contains(id_or_url)) validate_media_options(options, queue.at(id_or_url).metadata);
		else if(media.contains(id_or_url)) validate_media_options(options, media.at(id_or_url));
		else throw application_error {"mediaNotReady", "web.parse_first"};
	}
	return visible_details(preview_output(options));
}

void core::download_application::refresh_metadata(const std::vector<std::string> &ids, const std::string &request_id)
{
	std::lock_guard lock {mutex}; require_open();
	const auto expanded {expand_items(ids)};
	for(const auto &id : expanded)
		if(active(queue.at(id).snapshot.status) || queue.at(id).snapshot.refreshing) throw application_error {"busy", "web.task_busy"};
	for(const auto &id : expanded)
	{
		auto &task {queue.at(id)}; auto &snapshot {task.snapshot}; snapshot.refreshing = true;
		change_status(snapshot, snapshot.status); task_event("taskUpdated", snapshot);
		auto operation {std::make_shared<parse_operation>()}; parsing["refresh:" + id] = operation;
		const auto url {snapshot.url};
		const auto execution_settings {settings_for_output(application_execution_settings(settings, preferences.application), snapshot.options)};
		operation->worker = std::thread([this, operation, id, url, execution_settings, request_id]
		{
			nlohmann::json info; application_event event {"metadataRefreshed", request_id, url};
			const auto arguments {metadata_arguments(execution_settings, to_wide(url), is_youtube(to_wide(url)), true, true) + L" --no-playlist"};
			try
			{
				const auto output {query_metadata_limited(execution_settings.ytdlp_path, arguments, operation->working)};
				const auto first {output.find('{')}, last {output.rfind('}')};
				if(first == std::string::npos || last == std::string::npos) throw std::runtime_error {"metadata"};
				info = nlohmann::json::parse(output.substr(first, last - first + 1));
				if(!entry_available(info) || info.contains("entries")) throw std::runtime_error {"metadata"};
			}
			catch(const std::exception &) { event.name = "metadataRefreshFailed"; event.error = failure {"metadataFailed", "web.parse_failed"}; }
			std::lock_guard lock {mutex};
			if(operation->working && queue.contains(id))
			{
				auto &task {queue.at(id)};
				if(!event.error)
				{
					task.metadata = info; task.snapshot.media = media_dto(url, info); media[url] = info;
					metadata_commands[id] = redact(to_utf8(L'"' + execution_settings.ytdlp_path.wstring() + L'"' + arguments));
				}
				task.snapshot.refreshing = false; change_status(task.snapshot, task.snapshot.status);
				event.task = task.snapshot; task_event("taskUpdated", task.snapshot); persist_locked(); enqueue(std::move(event));
			}
			operation->done = true; dirty = true; wake.notify_one();
		});
	}
}

core::task_snapshot core::download_application::choose_formats(const std::string &id, const std::vector<std::string> &formats, bool multi_audio)
{
	auto options {get_task(id).options}; options.processing.format_ids = formats; options.processing.multi_audio = multi_audio;
	mark(options.processing, "format"); return update_options(id, std::move(options));
}

core::task_snapshot core::download_application::choose_subtitles(const std::string &id, const std::vector<std::string> &languages, const std::string &format, bool future_default)
{
	const auto details {get_media_details(id)}; const auto &tracks {details.at("subtitles")}; std::set<std::string> seen;
	for(const auto &language : languages)
	{
		const auto found {std::find_if(tracks.begin(), tracks.end(), [&](const auto &track) { return track.at("language") == language; })};
		if(found == tracks.end() || !seen.insert(language).second || !format.empty() && format != "any" &&
			std::find(found->at("formats").begin(), found->at("formats").end(), format) == found->at("formats").end()) throw application_error {"invalidSubtitle", "web.invalid_subtitle"};
	}
	auto options {get_task(id).options}; options.processing.sub_langs.clear();
	for(const auto &language : languages) { if(!options.processing.sub_langs.empty()) options.processing.sub_langs += ','; options.processing.sub_langs += language; }
	options.processing.sub_format = format == "any" ? "" : format; mark(options.processing, "subTracks");
	const auto updated {update_options(id, options)};
	if(future_default)
	{
		auto prefs {get_settings()}; prefs.defaults.processing.sub_langs = options.processing.sub_langs; prefs.defaults.processing.sub_format = options.processing.sub_format;
		std::erase(prefs.defaults.processing.edited, "subTracks"); save_settings(std::move(prefs));
	}
	return updated;
}

core::task_snapshot core::download_application::set_sections(const std::string &id, const std::vector<time_section> &sections)
{
	auto options {get_task(id).options}; options.processing.sections = sections; return update_options(id, std::move(options));
}

core::task_snapshot core::download_application::override_filename(const std::string &id, const std::string &filename)
{
	return update_options(id, filename_options(get_task(id).options, filename));
}

void core::task_log_snapshot::append(const std::string &chunk)
{
	text += chunk; ++revision;
	if(text.size() <= capacity) return;
	auto offset {text.size() - capacity};
	const auto newline {text.find('\n', offset)};
	if(newline != std::string::npos && newline + 1 < text.size()) offset = newline + 1;
	else while(offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80) ++offset;
	text.erase(0, offset); truncated = true;
}

void core::download_application::append_log(const std::string &id, std::uint64_t generation, const std::string &text)
{
	if(text.empty()) return;
	std::lock_guard lock {mutex};
	if(!queue.contains(id) || queue.at(id).snapshot.execution_id != generation) return;
	auto &log {logs[id]}; const auto chunk {redact(text)}; log.append(chunk);
	application_event event {"taskLog"}; event.log = task_log_snapshot {id, log.command, chunk, generation, log.revision, log.truncated}; enqueue(std::move(event));
}

core::task_log_snapshot core::download_application::get_task_log(const std::string &id) const
{
	std::lock_guard lock {mutex}; queue.at(id);
	return logs.contains(id) ? logs.at(id) : task_log_snapshot {id};
}

core::output_options core::download_application::processing_source_locked(const std::string &id) const
{
	if(id.empty()) return shared_options ? *shared_options : preferences.defaults;
	if(collections.contains(id)) return collections.at(id).snapshot.defaults;
	return queue.at(id).snapshot.options;
}

void core::download_application::copy_processing_locked(task_queue &tasks,
	std::map<std::string, download_collection> &groups, const output_options &source) const
{
	for(const auto &before : tasks.list())
	{
		auto &task {tasks.at(before.id)}; auto options {copy_processing_options(before.options, source)};
		if(output_options_json(options) == output_options_json(before.options)) continue;
		if(before.refreshing) throw application_error {"scopeBusy", "web.scope_busy"};
		validate_options(options);
		// 恢复/集合摘要没有完整轨道；未修改的选择在原执行前查询中校验。
		if(task.metadata.contains("formats")) validate_media_options(options, task.metadata);
		task.snapshot.options = std::move(options); change_status(task.snapshot, before.status);
	}
	for(auto &[id, collection] : groups)
	{
		auto options {copy_processing_options(collection.snapshot.defaults, source)};
		if(output_options_json(options) == output_options_json(collection.snapshot.defaults)) continue;
		validate_options(options); collection.snapshot.defaults = std::move(options);
		collection.snapshot.updated_at = timestamp_now(); ++collection.snapshot.summary.revision;
	}
}

nlohmann::json core::download_application::commit_options_locked(task_queue tasks,
	std::map<std::string, download_collection> groups, std::string scope, std::optional<output_options> shared, const std::string &request_id)
{
	// 候选队列全部校验并安全落盘后才发布，保存失败不改变任务、作用范围或原有效文件。
	std::vector<std::string> updated, deferred, completed, added;
	for(const auto &task : tasks.list())
		if(!queue.contains(task.id) || task.revision != queue.at(task.id).snapshot.revision)
		{
			updated.push_back(task.id);
			if(!queue.contains(task.id)) added.push_back(task.id);
			if(active(task.status)) deferred.push_back(task.id);
			if(task.status == task_status::done) completed.push_back(task.id);
		}
	const auto previous_error {persistence.status.error.has_value()};
	if(persistence.status.enabled && (!persistence.status.writable || !persistence.save(queue_data_locked(tasks, groups, scope, shared))))
	{
		application_event event {"persistenceFailed"}; event.persistence = persistence.status; enqueue(std::move(event));
		throw application_error {"queueWriteFailed", "web.queue_save_failed"};
	}
	if(previous_error && !persistence.status.error) { application_event event {"persistenceRecovered"}; event.persistence = persistence.status; enqueue(std::move(event)); }
	const auto previous_groups {collections};
	queue = std::move(tasks); collections = std::move(groups); option_scope = std::move(scope); shared_options = std::move(shared);
	rebuild_group_summaries_locked();
	for(const auto &id : updated) task_event(std::find(added.begin(), added.end(), id) != added.end() ? "taskAdded" : "taskOptionsChanged", queue.at(id).snapshot, request_id);
	for(auto &[id, collection] : collections)
		if(!previous_groups.contains(id) || output_options_json(previous_groups.at(id).snapshot.defaults) != output_options_json(collection.snapshot.defaults))
			collection_event(collection, previous_groups.contains(id) ? "collectionUpdated" : "collectionAdded");
	++queue.revision; application_event event {"queueChanged"}; event.queue = queue_dto(); enqueue(std::move(event));
	enqueue({"taskOptionsScopeChanged"});
	return {{"mode", option_scope}, {"updatedTaskIds", updated}, {"deferredTaskIds", deferred}, {"completedTaskIds", completed}};
}

nlohmann::json core::download_application::copy_task_processing_options(const std::string &id)
{
	std::lock_guard lock {mutex}; require_open();
	if(id.empty()) throw application_error {"taskNotFound", "web.select_task_first"};
	const auto source {processing_source_locked(id)}; auto tasks {queue}; auto groups {collections};
	copy_processing_locked(tasks, groups, source);
	auto shared {shared_options}; if(option_scope == "shared") shared = copy_processing_options(default_output_options(source.output_folder), source);
	return commit_options_locked(std::move(tasks), std::move(groups), option_scope, std::move(shared));
}

nlohmann::json core::download_application::set_task_options_scope(const std::string &scope, const std::string &source_id)
{
	std::lock_guard lock {mutex}; require_open();
	if(scope != "shared" && scope != "individual") throw application_error {"invalidPayload", "web.invalid_payload"};
	if(scope == option_scope) return {{"mode", scope}, {"updatedTaskIds", nlohmann::json::array()}, {"deferredTaskIds", nlohmann::json::array()}, {"completedTaskIds", nlohmann::json::array()}};
	auto tasks {queue}; auto groups {collections}; std::optional<output_options> shared;
	if(scope == "shared")
	{
		const auto source {processing_source_locked(source_id)};
		shared = copy_processing_options(default_output_options(source.output_folder), source); copy_processing_locked(tasks, groups, *shared);
	}
	return commit_options_locked(std::move(tasks), std::move(groups), scope, std::move(shared));
}

core::output_options core::download_application::prepare_task_options(output_options options) const
{
	validate_options(options); std::lock_guard lock {mutex};
	return shared_options ? copy_processing_options(std::move(options), *shared_options) : options;
}

core::download_preferences core::download_application::get_settings() const
{
	std::lock_guard lock {mutex}; return preferences;
}

std::string core::download_application::clipboard_source(std::string text) const
{
	std::lock_guard lock {mutex};
	if(!preferences.application.application_behavior.clipboard_on_focus || text.find_first_of("\r\n") != std::string::npos) return {};
	const auto first {text.find_first_not_of(" \t")};
	if(first == std::string::npos) return {};
	text = text.substr(first, text.find_last_not_of(" \t") - first + 1);
	if(!valid_source_url(text)) return {};
	if(text.starts_with("https://www.youtube.com/watch?v=") && text.find("&list=") == 43) text.resize(43);
	return queue.contains_url(text) ? std::string {} : text;
}

core::persistence_status core::download_application::get_settings_status() const
{
	std::lock_guard lock {mutex}; return preferences_store.status;
}

core::download_preferences core::download_application::save_settings(download_preferences value)
{
	std::lock_guard lock {mutex};
	if(value.components.ytdlp_path != preferences.components.ytdlp_path || value.components.ffmpeg_directory != preferences.components.ffmpeg_directory)
		throw application_error {"componentPathCommandRequired", "web.component.path_command_required"};
	return save_settings_locked(std::move(value));
}

core::download_preferences core::download_application::save_settings_locked(download_preferences value)
{
	validate_preferences(value); require_open();
	if(value.components.ytdlp_path != preferences.components.ytdlp_path || value.components.ffmpeg_directory != preferences.components.ffmpeg_directory || value.components.ytdlp_channel != preferences.components.ytdlp_channel)
		for(const auto &[id, update] : component_updates) if(update.cancellable || update.recovery_required || component_committing) throw application_error {"updateBusy", "web.update.updateBusy"};
	auto before {preferences_json(preferences)}, after {preferences_json(value)};
	before["application"].erase("appearance"); after["application"].erase("appearance");
	before.erase("finishAction"); after.erase("finishAction");
	before.erase("components"); after.erase("components");
	const bool appearance_only {before == after};
	if(!preferences_store.save(value)) throw application_error {"settingsWriteFailed", "web.settings_save_failed"};
	if(preferences.finish_action != value.finish_action)
	{
		cancel_completion_locked("actionChanged"); if(completion.state == "idle") completion.action = value.finish_action; completion_event_locked();
	}
	preferences = std::move(value); ++settings_revision;
	reset_components_locked();
	if(!appearance_only)
	{
		apply_settings_locked();
		if(preferences.application.queue_behavior.remove_completed) cleanup_completed_locked();
		queue_event(false);
	}
	enqueue({"settingsChanged"}); return preferences;
}

core::naming_preset core::download_application::save_naming_preset(const std::string &output_template)
{
	const auto rule {preview_naming({{"outputTemplate", output_template}})};
	if(!rule.at("canApply").get<bool>()) throw application_error {"invalidTemplate", "web.invalid_template"};
	naming_preset preset {rule.at("name"), output_template}; std::lock_guard lock {mutex}; auto value {preferences};
	const auto found {std::find_if(value.naming_presets.begin(), value.naming_presets.end(), [&](const auto &entry) { return entry.name == preset.name; })};
	if(found == value.naming_presets.end()) value.naming_presets.push_back(preset); else *found = preset;
	save_settings_locked(std::move(value)); return preset;
}

std::string core::download_application::save_download_preset(output_options options)
{
	options.processing.filename.clear(); options.processing.format_ids.clear(); options.processing.sections.clear(); options.processing.multi_audio = false;
	validate_output_options(options); const auto base {output_preset_name(options)};
	std::lock_guard lock {mutex}; auto value {preferences};
	const auto existing {std::find_if(value.presets.begin(), value.presets.end(), [&](const auto &entry) { return output_options_json(entry.options) == output_options_json(options); })};
	if(existing != value.presets.end()) return existing->name;
	auto name {base}; unsigned suffix {2};
	while(std::any_of(value.presets.begin(), value.presets.end(), [&](const auto &entry) { return entry.name == name; })) name = base + "（" + std::to_string(suffix++) + "）";
	value.presets.push_back({name, std::move(options)}); save_settings_locked(std::move(value)); return name;
}

void core::download_application::save_application_preset(const std::string &name, bool replace)
{
	std::lock_guard lock {mutex}; auto value {preferences};
	const auto existing {std::find_if(value.application_presets.begin(), value.application_presets.end(), [&](const auto &entry) { return entry.name == name; })};
	if(replace && existing == value.application_presets.end()) throw application_error {"presetNotFound", "web.preset_not_found"};
	if(!replace && existing != value.application_presets.end()) throw application_error {"duplicatePresetName", "web.duplicate_preset_name"};
	const application_config_preset preset {name, value.defaults, value.application};
	auto config {application_preset_json(preset)}; config.erase("name");
	for(const auto &entry : value.application_presets)
	{
		auto other {application_preset_json(entry)}; other.erase("name");
		if(config == other) throw application_error {"duplicatePresetConfig", "web.duplicate_preset_config"};
	}
	if(replace) *existing = preset; else value.application_presets.push_back(preset);
	save_settings_locked(std::move(value));
}

void core::download_application::rename_application_preset(const std::string &name, const std::string &new_name)
{
	std::lock_guard lock {mutex}; auto value {preferences};
	const auto found {std::find_if(value.application_presets.begin(), value.application_presets.end(), [&](const auto &entry) { return entry.name == name; })};
	if(found == value.application_presets.end()) throw application_error {"presetNotFound", "web.preset_not_found"};
	if(std::any_of(value.application_presets.begin(), value.application_presets.end(), [&](const auto &entry) { return entry.name == new_name; })) throw application_error {"duplicatePresetName", "web.duplicate_preset_name"};
	found->name = new_name; save_settings_locked(std::move(value));
}

void core::download_application::delete_application_preset(const std::string &name)
{
	std::lock_guard lock {mutex}; auto value {preferences};
	if(!std::erase_if(value.application_presets, [&](const auto &entry) { return entry.name == name; })) throw application_error {"presetNotFound", "web.preset_not_found"};
	save_settings_locked(std::move(value));
}

void core::download_application::apply_application_preset(const std::string &name)
{
	std::lock_guard lock {mutex}; auto value {preferences};
	const auto found {std::find_if(value.application_presets.begin(), value.application_presets.end(), [&](const auto &entry) { return entry.name == name; })};
	if(found == value.application_presets.end()) throw application_error {"presetNotFound", "web.preset_not_found"};
	const auto appearance {value.application.appearance}; value.defaults = found->defaults; value.application = found->application; value.application.appearance = appearance;
	save_settings_locked(std::move(value));
}

void core::download_application::apply_settings_locked()
{
	reset_components_locked();
	const auto &config {preferences.application};
	scheduler.options = {config.scheduler.max_downloads, config.scheduler.next_after_stop, config.scheduler.next_during_processing};
	for(const auto &value : application_sensitive_values(config))
		if(std::find(sensitive_values.begin(), sensitive_values.end(), value) == sensitive_values.end()) sensitive_values.push_back(value);
	dirty = true; wake.notify_one(); metadata_wake.notify_all();
}

nlohmann::json core::download_application::get_settings_snapshot() const
{
	std::lock_guard lock {mutex}; auto result {preferences_json(preferences)};
	result["revision"] = settings_revision;
	result["taskOptionsScope"] = {{"mode", option_scope}};
	result["newTaskOptions"] = output_options_json(shared_options ? copy_processing_options(preferences.defaults, *shared_options) : preferences.defaults);
	result["naming"] = naming_catalog();
	result["capabilities"] = {{"maxMetadataConcurrency", metadata_cpu_limit()}, {"cookieBrowsers", nlohmann::json::array()},
		{"sponsorMarkCategories", nlohmann::json::array()}, {"sponsorRemoveCategories", nlohmann::json::array()}, {"ariaAvailable", !aria2c_path(settings.ytdlp_path).empty()}};
	for(size_t n {1}; n < cookie_browser_options.size(); ++n) result["capabilities"]["cookieBrowsers"].push_back(to_utf8(cookie_browser_options[n]));
	for(const auto &value : sponsor_mark_options) result["capabilities"]["sponsorMarkCategories"].push_back(to_utf8(value));
	for(const auto &value : sponsor_remove_options) result["capabilities"]["sponsorRemoveCategories"].push_back(to_utf8(value));
	result["diagnostics"] = {{"writable", preferences_store.status.writable}, {"enabled", preferences_store.status.enabled},
		{"restartRequired", active_language != preferences.application.application_behavior.language}, {"activeLanguage", active_language}, {"activeMetadata", metadata_active}};
	if(preferences_store.status.error) result["diagnostics"]["error"] = {{"code", preferences_store.status.error->code}, {"messageKey", preferences_store.status.error->message_key}};
	result["applyTiming"] = {{"scheduler", "immediate"}, {"metadataConcurrency", "immediate"}, {"networkAuthentication", "nextOperation"},
		{"externalDownloader", "nextDownload"}, {"contentProcessing", "nextDownload"}, {"downloadDefaults", "newTask"},
		{"autoStartRecoveredQueue", "nextLaunch"}, {"restoreFailed", "nextQueueSaveAndRestore"}, {"autoRemoveCompleted", "immediate"},
		{"clipboardOnFocus", "immediate"}, {"displayCustomFilenames", "immediate"}, {"folderOrigin", "nextDialog"}, {"language", "restart"}, {"appearance", "immediate"}};
	result["applyTiming"]["components"] = "nextOperation";
	return result;
}

std::string core::download_application::query_metadata_limited(const fs::path &ytdlp, const std::wstring &arguments, std::atomic_bool &working)
{
	std::unique_lock lock {mutex};
	if(components_block_execution_locked()) throw application_error {"componentBusy", "web.update.componentBusy"};
	metadata_wake.wait(lock, [&] { return closing || !working || metadata_active < preferences.application.scheduler.max_metadata; });
	if(closing || !working) return {};
	++metadata_active; lock.unlock();
	try
	{
		auto result {query_metadata(ytdlp, arguments, working)};
		lock.lock(); --metadata_active; metadata_wake.notify_all(); return result;
	}
	catch(...)
	{
		lock.lock(); --metadata_active; metadata_wake.notify_all(); throw;
	}
}

std::string core::download_application::redact(std::string text) const { return redact_values(std::move(text), sensitive_values); }
std::string core::download_application::visible_text(std::string text) const { std::lock_guard lock {mutex}; return redact(std::move(text)); }
nlohmann::json core::download_application::visible_details(nlohmann::json value) const
{
	if(value.is_string()) value = redact(value.get<std::string>());
	else if(value.is_object() || value.is_array()) for(auto &item : value) item = visible_details(std::move(item));
	return value;
}

void core::download_application::cleanup_completed_locked()
{
	std::set<std::string> groups;
	for(const auto &[id, collection] : collections)
		if(collection.summary().completed == collection.summary().total) groups.insert(id);
	std::vector<std::string> completed;
	for(const auto &task : queue.list())
		if(task.status == task_status::done && !task.refreshing && (task.parent_id.empty() || groups.contains(task.parent_id))) completed.push_back(task.id);
	if(completed.empty()) return;
	const auto removed {queue.remove_many(completed)}; detach_tasks(removed);
	for(const auto &task : removed) task_event("taskRemoved", task);
	queue_event();
}

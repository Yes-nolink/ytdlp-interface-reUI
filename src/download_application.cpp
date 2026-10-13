#include "download_application.hpp"
#include "naming_model.hpp"
#include <regex>
#include "core_text.hpp"
#include "download_options.hpp"
#include <algorithm>
#include <sstream>

namespace
{
		bool media_url(const std::string &text)
		{
			return core::valid_source_url(text);
	}
		std::string speed_text(const std::string &text)
	{
		const auto first {text.find(" at ")};
		if(first == std::string::npos) return {};
		const auto last {text.find(" ETA", first + 4)};
			return text.substr(first + 4, last == std::string::npos ? last : last - first - 4);
		}
		core::persisted_output persisted_options(const core::output_options &options)
		{
			return {options.kind, options.format, options.quality, options.naming, core::to_utf8(options.output_folder.wstring()), options.cover, options.metadata,
				options.output_template, options.playlist_prefix, options.zero_padding, options.playlist_folder, options.single_video, options.processing, options.bandcamp_template};
		}
			core::output_options restored_options(const core::persisted_output &options)
		{
				return {options.kind, options.format, options.quality, options.naming, core::fs::u8path(options.folder), options.cover, options.metadata,
					options.output_template, options.playlist_prefix, options.zero_padding, options.playlist_folder, options.single_video, options.processing, options.bandcamp_template};
			}
		void apply_failed_retention(core::persisted_queue &saved, bool restore_failed)
		{
			if(restore_failed) return;
			std::set<std::string> excluded, removed_groups;
			std::map<std::string, std::string> states;
			for(const auto &task : saved.tasks)
				if(task.status == "failed") excluded.insert(task.id); else states[task.id] = task.status;
			std::erase_if(saved.tasks, [&](const auto &task) { return excluded.contains(task.id); });
			for(auto &collection : saved.collections)
			{
				std::erase_if(collection.children, [&](const auto &id) { return excluded.contains(id); });
				for(auto &entry : collection.entries) if(excluded.contains(entry.task_id)) { entry.selected = false; entry.task_id.clear(); }
				if(collection.children.empty() || std::all_of(collection.children.begin(), collection.children.end(), [&](const auto &id) { return states.at(id) == "done"; })) removed_groups.insert(collection.id);
			}
			std::erase_if(saved.collections, [&](const auto &collection) { return removed_groups.contains(collection.id); });
		std::erase_if(saved.tasks, [&](const auto &task) { return removed_groups.contains(task.parent_id); });
		std::vector<std::string> items; std::set<std::string> seen;
		for(const auto &task : saved.tasks)
		{
			const auto &id {task.parent_id.empty() ? task.id : task.parent_id};
			if(seen.insert(id).second) items.push_back(id);
		}
		core::retain_queue_groups(saved.groups, items);
		}
}

core::download_application::download_application(download_settings settings, event_callback callback, scheduler_options options, fs::path queue_file, fs::path settings_file, output_options defaults, std::string language, completion_clock clock, component_environment component_environment, bool deferred_start) :
	settings {std::move(settings)}, callback {std::move(callback)}, scheduler {options}, persistence {std::move(queue_file)}, preferences_store {std::move(settings_file)}, completion_now {std::move(clock)}
{
	if(!options.max_concurrent_downloads) throw application_error {"invalidPayload", "web.invalid_payload"};
	preferences.defaults = defaults.kind.empty() ? default_output_options(fs::current_path()) : std::move(defaults);
	preferences.application = application_settings_from_execution(this->settings, options);
	preferences.application.application_behavior.language = std::move(language);
	component_context = std::move(component_environment);
	if(component_context.appdir.empty()) component_context.appdir = fs::absolute(this->settings.ytdlp_path).parent_path();
	const auto paths {resolve_component_paths(component_context.appdir, {})};
	if(!this->settings.ytdlp_path.empty() && fs::absolute(this->settings.ytdlp_path).lexically_normal() != paths.ytdlp.lexically_normal()) preferences.components.ytdlp_path = to_utf8(fs::absolute(this->settings.ytdlp_path).wstring());
	if(!this->settings.ffmpeg_path.empty() && fs::absolute(this->settings.ffmpeg_path / "ffmpeg.exe").lexically_normal() != paths.ffmpeg.lexically_normal()) preferences.components.ffmpeg_directory = to_utf8(fs::absolute(this->settings.ffmpeg_path).wstring());
	preferences.components.ytdlp_channel = component_context.channel;
	validate_preferences(preferences);
	if(const auto saved {preferences_store.load(preferences.application, preferences.components)}) preferences = *saved;
	completion.action = preferences.finish_action;
	active_language = preferences.application.application_behavior.language;
	apply_settings_locked();
	restore_component_updates_locked();
	restore_queue();
	if(!deferred_start) activate();
}

void core::download_application::activate()
{
	// Web 页面确认就绪之前不派发恢复任务，也不因启动失败退出而保存队列。
	if(closing || activated.exchange(true)) return;
	if(preferences.application.queue_behavior.auto_start_recovered && persistence.status.restored_tasks) start_all();
	dispatcher = std::thread([this] { dispatch(); });
	check_components({}, "", preferences.components.check_at_startup);
}

core::download_application::~download_application() { shutdown(); }

void core::download_application::require_open() const
{
	if(closing || completion_committing || !activated) throw application_error {"closing", "web.task_busy"};
}

// 以下事件帮助函数只在持有业务锁时调用；观察者始终在锁外执行。
void core::download_application::enqueue(application_event event)
{
	if(event.task || event.queue || event.name == "mediaParsing" || event.name == "sourceParsing") cancel_completion_locked("workChanged");
	events.push_back(std::move(event));
	dirty = true;
	wake.notify_one();
	metadata_wake.notify_all();
}

void core::download_application::task_event(const std::string &name, const task_snapshot &snapshot, const std::string &request_id)
{
	++queue.revision;
		enqueue({name, request_id, snapshot.url, {}, snapshot});
	if(!snapshot.parent_id.empty() && collections.contains(snapshot.parent_id) && name != "taskRemoved")
	{
		auto &collection {collections.at(snapshot.parent_id)};
		collection.observe(snapshot);
		if(name != "taskAdded")
		{
			application_event event {name == "taskProgress" ? "collectionProgress" : "collectionStatusChanged"};
			event.collection_progress = collection.summary();
				enqueue(std::move(event));
			}
		}
	group_task_event_locked(snapshot, name);
}

core::queue_snapshot core::download_application::queue_dto() const
{
	queue_snapshot result {queue.revision, scheduler.enabled, scheduler.options.max_concurrent_downloads, queue.list()};
	std::set<std::string> groups;
	for(const auto &task : result.tasks)
	{
		if(active(task.status)) ++result.active_tasks;
		if(task.parent_id.empty()) result.items.push_back(task.id);
		else if(groups.insert(task.parent_id).second)
		{
			result.items.push_back(task.parent_id);
			result.collections.push_back(collection_dto(collections.at(task.parent_id)));
		}
	}
	for(const auto &group : custom_groups) result.groups.push_back(group_dto(group.id));
	result.ungrouped = group_dto("");
	return result;
}

void core::download_application::queue_event(bool full)
{
	if(full) rebuild_group_summaries_locked();
	persist_locked();
	++queue.revision;
	application_event event {full ? "queueChanged" : "queueStateChanged"};
	event.queue = full ? queue_dto() : queue_snapshot {queue.revision, scheduler.enabled, scheduler.options.max_concurrent_downloads};
	if(!full)
		for(const auto &[id, execution] : executions) if(!execution->done) ++event.queue->active_tasks;
	enqueue(std::move(event));
}

void core::download_application::restore_queue()
{
	auto saved {persistence.load()};
	if(!saved) return;
	apply_failed_retention(*saved, preferences.application.queue_behavior.restore_failed);
	try
	{
		std::vector<task_snapshot> snapshots;
		for(const auto &record : saved->tasks)
		{
			task_snapshot snapshot;
			snapshot.id = record.id;
			snapshot.url = record.url;
			if(!media_url(snapshot.url)) throw application_error {"invalidUrl", "web.invalid_url"};
			snapshot.media = media_dto(record.url, record.media);
			snapshot.options = restored_options(record.output);
			validate_options(snapshot.options);
			snapshot.group_id = record.group_id;
			snapshot.parent_id = record.parent_id;
			if(record.context) snapshot.collection = collection_context {record.context->url, record.context->id, record.context->title,
				record.context->index, record.context->count, record.context->channel};
			snapshot.status = restored_status(record.status);
			snapshot.error = record.error;
			snapshot.created_at = record.created_at;
			snapshot.updated_at = record.updated_at;
			snapshot.started_at = record.started_at;
			snapshot.finished_at = record.finished_at;
			if(record.status == "running" || record.status == "processing") snapshot.started_at = snapshot.finished_at = 0;
			if(snapshot.status == task_status::done)
			{
				snapshot.percent = 100;
			}
			if(record.result) snapshot.result = task_result {fs::u8path(record.result->file), record.result->exit_code};
			if(record.result) for(const auto &file : record.result->files) snapshot.result->files.push_back(fs::u8path(file));
			// 代次和速度采用新进程默认值；Completed 子项只恢复可靠结果，不恢复旧执行上下文。
			snapshot.revision = 1;
			snapshots.push_back(std::move(snapshot));
		}
		task_queue restored;
		restored.restore(std::move(snapshots), saved->last_task_id);
		std::map<std::string, download_collection> restored_collections;
		for(const auto &record : saved->collections)
		{
			download_collection collection;
			auto &snapshot {collection.snapshot};
			snapshot.id = record.id;
			snapshot.defaults = restored_options(record.defaults); validate_options(snapshot.defaults);
			snapshot.created_at = record.created_at; snapshot.updated_at = record.updated_at;
			snapshot.children = record.children;
			snapshot.source = {record.id, record.url, record.resolved_url, record.kind, record.category, record.metadata};
			snapshot.source.revision = 1;
			for(const auto &entry : record.entries)
			{
				source_entry item {entry.id, entry.url, entry.task_id, entry.index, media_dto(entry.url, entry.media), entry.available, entry.selected};
				if(!item.available) item.error = failure {"entryUnavailable", "web.entry_unavailable"};
				snapshot.source.entries.push_back(std::move(item));
			}
			collection.index_children();
			for(const auto &child : snapshot.children) collection.observe(restored.at(child).snapshot);
			restored_collections.emplace(snapshot.id, std::move(collection));
		}
		std::optional<output_options> restored_shared;
		if(saved->shared_options)
		{
			restored_shared = restored_options(*saved->shared_options); validate_options(*restored_shared);
			if(!restored_shared->processing.filename.empty() || !restored_shared->processing.format_ids.empty() || !restored_shared->processing.sections.empty())
				throw application_error {"invalidScope", "web.invalid_payload"};
		}
		queue = std::move(restored); collections = std::move(restored_collections); next_collection_id = saved->last_collection_id;
		custom_groups = std::move(saved->groups); next_group_id = saved->last_group_id;
		rebuild_group_summaries_locked();
		option_scope = saved->option_scope; shared_options = std::move(restored_shared);
		for(const auto &snapshot : queue.list()) media[snapshot.url] = snapshot.media.details;
		persistence.status.restored_tasks = saved->tasks.size();
	}
	catch(const std::exception &) { persistence.reject_restore(); }
}

core::persisted_queue core::download_application::queue_data_locked(const task_queue &tasks,
	const std::map<std::string, download_collection> &groups, const std::string &scope, const std::optional<output_options> &shared) const
{
	persisted_queue saved;
	saved.last_task_id = tasks.last_id();
	saved.last_collection_id = next_collection_id;
	saved.last_group_id = next_group_id; saved.groups = custom_groups;
	saved.option_scope = scope;
	if(shared) saved.shared_options = persisted_options(*shared);
	std::set<std::string> retained;
	for(const auto &[id, collection] : groups)
	{
		const auto summary {collection.summary()};
		if(summary.completed == summary.total) continue;
		retained.insert(id);
		const auto &snapshot {collection.snapshot}; const auto &source {snapshot.source};
		persisted_collection record {id, source.url, source.resolved_url, source.kind, source.category, source.metadata,
			persisted_options(snapshot.defaults), {}, snapshot.children, snapshot.created_at, snapshot.updated_at};
		for(const auto &entry : source.entries) record.entries.push_back({entry.id, entry.url, entry.task_id, entry.index, entry.media.details, entry.available, entry.selected});
		saved.collections.push_back(std::move(record));
	}
	for(const auto &snapshot : tasks.list())
	{
		if(!snapshot.parent_id.empty() && !retained.contains(snapshot.parent_id) || snapshot.status == task_status::done && snapshot.parent_id.empty()) continue;
		const auto &options {snapshot.options};
		persisted_task record {snapshot.id, snapshot.url, snapshot.type == task_type::media ? "media" : "collection",
			snapshot.group_id, snapshot.parent_id, status_name(snapshot.status), snapshot.media.details,
			persisted_options(options),
			snapshot.error, snapshot.created_at, snapshot.updated_at, snapshot.started_at, snapshot.finished_at};
		if(snapshot.collection) record.context = persisted_context {snapshot.collection->url, snapshot.collection->id, snapshot.collection->title,
			snapshot.collection->index, snapshot.collection->count, snapshot.collection->channel};
		if(snapshot.result) record.result = persisted_result {to_utf8(snapshot.result->file.wstring()), snapshot.result->exit_code};
		if(snapshot.result) for(const auto &file : snapshot.result->files) record.result->files.push_back(to_utf8(file.wstring()));
		// 应用退出取消与用户 Stop 不同，退出后保留可显式继续的等待状态。
		if(executions.contains(snapshot.id) && executions.at(snapshot.id)->interrupted_by_shutdown)
		{
			record.status = "waiting";
			record.started_at = record.finished_at = 0;
			record.error.reset();
		}
			saved.tasks.push_back(std::move(record));
		}
	apply_failed_retention(saved, preferences.application.queue_behavior.restore_failed);
	std::vector<std::string> items; std::set<std::string> seen;
	for(const auto &task : saved.tasks)
	{
		const auto &id {task.parent_id.empty() ? task.id : task.parent_id};
		if(seen.insert(id).second) items.push_back(id);
	}
	retain_queue_groups(saved.groups, items);
	return saved;
}

void core::download_application::persist_locked()
{
	if(!persistence.status.enabled || !persistence.status.writable) return;
	const auto previous_code {persistence.status.error ? persistence.status.error->code : std::string {}};
	persistence.save(queue_data_locked(queue, collections, option_scope, shared_options));
	const auto current_code {persistence.status.error ? persistence.status.error->code : std::string {}};
	if(current_code != previous_code)
	{
		application_event event {persistence.status.error ? "persistenceFailed" : "persistenceRecovered"};
		event.persistence = persistence.status;
		enqueue(std::move(event));
	}
}

bool core::download_application::component_available() const { std::lock_guard lock {mutex}; std::error_code error; return fs::is_regular_file(settings.ytdlp_path, error); }

void core::download_application::validate_options(const output_options &options)
{
	validate_output_options(options);
}

core::media_snapshot core::download_application::media_dto(const std::string &url, const nlohmann::json &info)
{
	nlohmann::json dto {{"url", url}, {"title", info.at("title")}};
	for(const auto key : {"id", "duration", "thumbnail", "uploader", "channel", "upload_date", "webpage_url_domain", "ext", "format_id", "resolution", "format_note", "filesize", "filesize_approx"})
		dto[key] = info.contains(key) ? info[key] : nlohmann::json(nullptr);
	if(info.contains("requested_formats") && info["requested_formats"].is_array() && info["requested_formats"].size() == 2)
		for(const auto &format : info["requested_formats"])
			if(!format.contains("filesize") || format["filesize"].is_null()) dto["filesize_approx"] = nullptr;
	return {url, std::move(dto)};
}

void core::download_application::parse_media(const std::string &url, const std::string &request_id)
{
	if(!media_url(url)) throw application_error {"invalidUrl", "web.invalid_url"};
	std::lock_guard lock {mutex};
	require_open();
	if(parsing.contains(url)) throw application_error {"busy", "web.parsing"};
	if(media.contains(url)) { enqueue({"mediaParsed", request_id, url, media_dto(url, media.at(url))}); return; }
	auto operation {std::make_shared<parse_operation>()};
	parsing[url] = operation;
	enqueue({"mediaParsing", request_id, url});
	operation->worker = std::thread([this, operation, url, request_id, execution_settings = application_execution_settings(settings, preferences.application)]
	{
		application_event event {"mediaParseFailed", request_id, url};
		nlohmann::json info;
		try
		{
			const auto wide {to_wide(url)};
			const bool youtube {is_youtube(wide)};
			if(is_youtube_channel(wide) || (youtube && (wide.find(L"?list=") != std::wstring::npos || wide.find(L"&list=") != std::wstring::npos)) ||
				wide.find(L"bandcamp.com/album/") != std::wstring::npos || is_soundcloud_playlist(wide) ||
				wide.find(L".bandcamp.com/music") != std::wstring::npos || wide.ends_with(L".bandcamp.com") || wide.ends_with(L".bandcamp.com/"))
				throw application_error {"unsupportedMedia", "web.single_media"};
			const auto arguments {metadata_arguments(execution_settings, wide, youtube, youtube, true)};
			auto output {query_metadata_limited(execution_settings.ytdlp_path, arguments, operation->working)};
			const auto first {output.find('{')}, last {output.rfind('}')};
			if(first == std::string::npos || last == std::string::npos) throw application_error {"metadataFailed", "web.parse_failed"};
			info = nlohmann::json::parse(output.substr(first, last - first + 1));
			if(info.value("_type", "video") == "playlist" || info.contains("entries"))
				throw application_error {"unsupportedMedia", "web.single_media"};
			if(!info.contains("title") || !info["title"].is_string()) throw application_error {"metadataFailed", "web.parse_failed"};
			event.name = "mediaParsed";
			event.media = media_dto(url, info);
		}
		catch(const application_error &error) { event.error = error.detail; }
		catch(const std::exception &) { event.error = failure {"metadataFailed", "web.parse_failed"}; }
		std::lock_guard lock {mutex};
		if(operation->working && event.media)
		{
			media[url] = std::move(info);
			const auto wide {to_wide(url)};
			metadata_commands[url] = redact(to_utf8(L'"' + execution_settings.ytdlp_path.wstring() + L'"' + metadata_arguments(execution_settings, wide, is_youtube(wide), is_youtube(wide), true)));
		}
		operation->done = true;
		enqueue(std::move(event));
	});
}

void core::download_application::parse_source(const std::string &url, const std::string &request_id, const std::string &category, bool single_video, const std::string &source_id)
{
	if(!media_url(url)) throw application_error {"invalidUrl", "web.invalid_url"};
	const auto resolved {single_video ? url : channel_source_url(url, category)};
	const auto key {"source:" + resolved + (single_video ? ":single" : ":collection")};
	std::lock_guard lock {mutex};
	require_open();
	if(!source_id.empty() && !sources.contains(source_id)) throw application_error {"sourceNotFound", "web.parse_first"};
	if(parsing.contains(key)) throw application_error {"busy", "web.parsing"};
	if(!source_id.empty() && std::any_of(parsing.begin(), parsing.end(), [&](const auto &entry) { return entry.second->source_id == source_id; })) throw application_error {"busy", "web.parsing"};
	auto operation {std::make_shared<parse_operation>()};
	operation->request_id = request_id; operation->source_id = source_id;
	parsing[key] = operation;
	enqueue({"sourceParsing", request_id, url});
	enqueue({"mediaParsing", request_id, url});
	operation->worker = std::thread([this, operation, url, resolved, category, single_video, request_id, source_id, execution_settings = application_execution_settings(settings, preferences.application)]
	{
		application_event event {"sourceParseFailed", request_id, url};
		nlohmann::json info;
		std::string output;
		try
		{
			if(!component_available()) throw application_error {"componentMissing", "web.component_missing"};
			const auto wide {to_wide(resolved)};
			auto arguments {metadata_arguments(execution_settings, wide, is_youtube(wide), false, true)};
			arguments += single_video ? L" --no-playlist" : L" --yes-playlist";
			arguments += L" --ignore-errors --encoding UTF-8";
			output = query_metadata_limited(execution_settings.ytdlp_path, arguments, operation->working);
			const auto first {output.find('{')}, last {output.rfind('}')};
			if(first == std::string::npos || last == std::string::npos) throw application_error {"metadataFailed", "web.parse_failed"};
			info = nlohmann::json::parse(output.substr(first, last - first + 1));
			event.source = describe_source(url, resolved, category, info, single_video);
			if(event.source->kind != "media" && event.source->category != "playlists" && event.source->entries.empty() && event.source->playlists.empty()) throw application_error {"emptySource", "web.empty_source"};
			event.name = "sourceParsed";
		}
		catch(const application_error &error) { event.error = error.detail; }
		catch(const std::exception &) { event.error = failure {"metadataFailed", "web.parse_failed"}; }
		std::lock_guard lock {mutex};
		if(!operation->working) { event.source.reset(); event.name = "sourceParseFailed"; event.error = failure {"sourceParseCancelled", "web.source_parse_cancelled"}; output.clear(); }
		if(event.error) event.source.reset();
		if(operation->working && event.source)
		{
			if(source_id.empty()) event.source->id = "source-" + std::to_string(++next_source_id);
			else
			{
				// 同一来源更新保留仍存在媒体的身份与最新选择，不修改已入队快照。
				const auto &previous {sources.at(source_id)};
				event.source->id = source_id; event.source->revision = previous.revision + 1;
				std::map<std::string, const source_entry *> existing;
				for(const auto &entry : previous.entries) if(!entry.url.empty()) existing.try_emplace(entry.url, &entry);
				for(auto &entry : event.source->entries)
				{
					entry.id = "entry-" + std::to_string(event.source->revision) + '-' + entry.id;
					if(const auto found {existing.find(entry.url)}; found != existing.end())
					{
						entry.id = found->second->id; entry.selected = entry.available && found->second->selected;
						existing.erase(found);
					}
				}
			}
			sources[event.source->id] = *event.source;
			if(event.source->kind == "media")
			{
				media[url] = info;
				metadata_commands[url] = redact(to_utf8(L'"' + execution_settings.ytdlp_path.wstring() + L'"' + metadata_arguments(execution_settings, to_wide(resolved), is_youtube(to_wide(resolved)), false, true) + (single_video ? L" --no-playlist" : L" --yes-playlist") + L" --ignore-errors --encoding UTF-8"));
				enqueue({"mediaParsed", request_id, url, media_dto(url, info)});
			}
		}
		else if(event.error)
		{
			event.source.reset(); event.name = "sourceParseFailed"; event.diagnostic = redact(std::move(output));
			enqueue({"mediaParseFailed", request_id, url, {}, {}, event.error});
		}
		operation->done = true;
		enqueue(std::move(event));
	});
}

size_t core::download_application::cancel_source_parse(const std::string &request_id)
{
	std::lock_guard lock {mutex}; require_open(); size_t cancelled {0};
	for(const auto &[key, operation] : parsing)
		if(!operation->done && operation->working && !operation->request_id.empty() &&
			(operation->request_id == request_id || operation->request_id.starts_with(request_id + ':')))
		{ operation->working = false; ++cancelled; }
	if(!cancelled) throw application_error {"sourceParseNotActive", "web.source_parse_not_active"};
	metadata_wake.notify_all(); return cancelled;
}

nlohmann::json core::download_application::parse_sources(const std::string &text, const std::string &request_id, bool single_video, std::optional<size_t> input_index)
{
	nlohmann::json results = nlohmann::json::array(); std::set<std::string> seen;
	std::istringstream input {text}; size_t position {0};
	for(std::string item; input >> item;)
	{
		const auto index {position++}; if(input_index && *input_index != index) continue;
		// 从粘贴文本提取 HTTP(S) token，解析仍复用单 Source 管线。
		const auto begin {item.find("http://")}, secure {item.find("https://")};
		const auto start {std::min(begin, secure)};
		if(start != std::string::npos) item.erase(0, start);
		while(!item.empty() && std::string_view {",;<>\""}.find(item.back()) != std::string_view::npos) item.pop_back();
		const auto child {request_id + ":" + std::to_string(index)};
		std::string status {"accepted"};
		if(!valid_source_url(item)) status = "invalid";
		else
		{
			const auto resolved {single_video ? item : channel_source_url(item, "")};
			std::lock_guard lock {mutex};
			if(!seen.insert(resolved).second || queue.contains_url(item) || queue.contains_url(resolved) || std::any_of(collections.begin(), collections.end(), [&](const auto &entry)
				{ return entry.second.snapshot.source.url == item || entry.second.snapshot.source.resolved_url == resolved; })) status = "duplicate";
		}
		nlohmann::json result {{"input", visible_text(item)}, {"inputIndex", index}, {"status", status}, {"requestId", child}};
		if(status == "accepted")
			try { parse_source(item, child, "", single_video); }
			catch(const application_error &error) { result["status"] = "failed"; result["error"] = {{"code", error.detail.code}, {"messageKey", error.detail.message_key}}; }
		results.push_back(std::move(result));
	}
	if(input_index && results.empty()) throw application_error {"invalidInputIndex", "web.invalid_payload"};
	return results;
}

core::source_snapshot core::download_application::get_source(const std::string &id) const
{
	std::lock_guard lock {mutex};
	if(!sources.contains(id)) throw application_error {"sourceNotFound", "web.parse_first"};
	return sources.at(id);
}

core::source_snapshot core::download_application::select_source(const std::string &id, const std::vector<std::string> &entries)
{
	std::lock_guard lock {mutex};
	require_open();
	if(!sources.contains(id)) throw application_error {"sourceNotFound", "web.parse_first"};
	auto &source {sources.at(id)};
	select_entries(source, entries);
	application_event event {"sourceSelectionChanged"}; event.source = source; enqueue(std::move(event));
	return source;
}

core::download_application::source_tasks core::download_application::create_source_locked(const source_snapshot &source,
	output_options options, bool grouped, const std::string &request_id)
{
	std::set<std::string> urls;
	for(const auto &entry : source.entries)
		if(entry.selected)
		{
			if(!entry.available) throw application_error {"entryUnavailable", "web.entry_unavailable"};
			if(queue.contains_url(entry.url) || !urls.insert(entry.url).second) throw application_error {"duplicateTask", "web.duplicate_task"};
		}
	if(urls.empty()) throw application_error {"emptySelection", "web.empty_selection"};
	auto tasks {queue}; auto groups {collections}; auto shared {shared_options};
	if(option_scope == "shared")
	{
		shared = copy_processing_options(default_output_options(options.output_folder), options); copy_processing_locked(tasks, groups, *shared);
	}
	source_tasks added;
	if(grouped && source.kind != "media")
	{
		added.collection_id = "collection-" + std::to_string(next_collection_id + 1);
		download_collection collection;
		collection.snapshot.id = added.collection_id;
		collection.snapshot.source = source;
		collection.snapshot.source.id = added.collection_id;
		collection.snapshot.defaults = options;
		collection.snapshot.created_at = collection.snapshot.updated_at = timestamp_now();
		groups.emplace(added.collection_id, std::move(collection));
	}
	for(const auto &entry : source.entries)
		if(entry.selected)
		{
			const auto metadata {source.kind == "media" && media.contains(entry.url) ? media.at(entry.url) : entry.media.details};
			auto &task {tasks.add(entry.url, entry.media, options, metadata)};
			if(source.kind != "media") task.snapshot.collection = collection_context {source.url,
				source.metadata.value("id", nlohmann::json(nullptr)).is_string() ? source.metadata["id"].get<std::string>() : source.url,
			source.metadata.at("title").get<std::string>(), entry.index, source.entries.size(), source.kind == "channel"};
			if(!added.collection_id.empty())
			{
				task.snapshot.group_id = task.snapshot.parent_id = added.collection_id;
				auto &collection {groups.at(added.collection_id)};
				collection.snapshot.children.push_back(task.snapshot.id);
				// 遍历一次建立条目关联，统计索引在批量创建结束后统一建立。
			}
			added.tasks.push_back(task.snapshot);
		}
	if(!added.collection_id.empty())
	{
		auto &collection {groups.at(added.collection_id)};
		size_t child {0};
		for(auto &entry : collection.snapshot.source.entries) if(entry.selected) entry.task_id = added.tasks[child++].id;
		collection.index_children();
	}
	if(!added.collection_id.empty()) ++next_collection_id;
	try { commit_options_locked(std::move(tasks), std::move(groups), option_scope, std::move(shared), request_id); }
	catch(...) { if(!added.collection_id.empty()) --next_collection_id; throw; }
	return added;
}

core::download_application::source_tasks core::download_application::create_source_tasks(const std::string &id, output_options options,
	bool grouped, const std::string &request_id)
{
	validate_options(options);
	std::lock_guard lock {mutex};
	require_open();
	if(!sources.contains(id)) throw application_error {"sourceNotFound", "web.parse_first"};
	if(options.single_video && sources.at(id).kind != "media") throw application_error {"currentVideoMissing", "web.current_video_missing"};
	if(sources.at(id).kind != "media" && (!options.processing.format_ids.empty() || !options.processing.sections.empty() || !options.processing.filename.empty()))
		throw application_error {"taskSpecificDefault", "web.task_specific_default"};
	if(sources.at(id).kind == "media") validate_media_options(options, media.at(sources.at(id).entries.front().url));
	return create_source_locked(sources.at(id), std::move(options), grouped, request_id);
}

core::collection_snapshot core::download_application::collection_dto(const download_collection &collection) const
{
	auto snapshot {collection.snapshot}; snapshot.summary = collection.summary(); return snapshot;
}

core::download_collection &core::download_application::collection_at(const std::string &id)
{
	if(!collections.contains(id)) throw application_error {"collectionNotFound", "web.collection_not_found"};
	return collections.at(id);
}

core::collection_snapshot core::download_application::get_collection(const std::string &id) const
{
	std::lock_guard lock {mutex};
	if(!collections.contains(id)) throw application_error {"collectionNotFound", "web.collection_not_found"};
	return collection_dto(collections.at(id));
}

void core::download_application::collection_event(download_collection &collection, const std::string &name)
{
	application_event event {name}; event.collection = collection_dto(collection); enqueue(std::move(event));
}

void core::download_application::start_collection(const std::string &id)
{
	std::lock_guard lock {mutex}; require_open();
	bool starting {false};
	for(const auto &child : collection_at(id).snapshot.children)
	{
		auto &task {queue.at(child).snapshot};
		if(!starting && (task.can_retry() || task.can_start())) { begin_completion_run_locked(); starting = true; }
		if(task.can_retry()) reset_for_retry(task);
		if(task.can_start()) scheduler.start(child);
	}
	queue_event(false);
}

void core::download_application::stop_collection(const std::string &id)
{
	std::lock_guard lock {mutex}; require_open();
	for(const auto &child : collection_at(id).snapshot.children)
		if(queue.at(child).snapshot.can_start() || queue.at(child).snapshot.can_stop()) stop_locked(child);
	queue_event(false);
}

void core::download_application::retry_collection(const std::string &id)
{
	std::lock_guard lock {mutex}; require_open();
	bool starting {false};
	for(const auto &child : collection_at(id).snapshot.children)
		if(queue.at(child).snapshot.status == task_status::failed)
		{
			if(!starting) { begin_completion_run_locked(); starting = true; }
			reset_for_retry(queue.at(child).snapshot); scheduler.start(child);
		}
	queue_event(false);
}

void core::download_application::remove_collection(const std::string &id)
{
	remove_tasks({id});
}

void core::download_application::split_collection(const std::string &id)
{
	std::lock_guard lock {mutex}; require_open();
	auto &collection {collection_at(id)};
	if(!collection.summary().can_remove) throw application_error {"busy", "web.task_busy"};
	auto tasks {queue}; auto sources {collections}; auto groups {custom_groups};
	for(const auto &child : collection.snapshot.children)
	{
		auto &task {tasks.at(child).snapshot};
		if(task.refreshing) throw application_error {"busy", "web.task_busy"};
		task.group_id.clear(); task.parent_id.clear(); change_status(task, task.status);
	}
	for(auto &group : groups) if(std::erase(group.items, id)) group.items.insert(group.items.end(), collection.snapshot.children.begin(), collection.snapshot.children.end());
	sources.erase(id); retain_queue_groups(groups, logical_items(tasks));
	save_structure_locked(tasks, sources, groups, next_group_id);
	const auto children {collection.snapshot.children}; collection_event(collection, "collectionRemoved");
	queue = std::move(tasks); collections = std::move(sources); custom_groups = std::move(groups);
	rebuild_group_summaries_locked();
	for(const auto &child : children) task_event("taskUpdated", queue.at(child).snapshot);
	++queue.revision; application_event event {"queueChanged"}; event.queue = queue_dto(); enqueue(std::move(event));
}

void core::download_application::move_collection(const std::string &id, size_t position)
{
	move_queue_item(id, position);
}

void core::download_application::move_queue_item(const std::string &id, size_t position)
{
	std::lock_guard lock {mutex}; require_open();
	auto tasks {queue}; move_item_locked(tasks, id, position);
	commit_groups_locked(std::move(tasks), custom_groups, next_group_id);
}

std::vector<std::string> core::download_application::expand_items(const std::vector<std::string> &ids) const
{
	std::vector<std::string> result; std::set<std::string> seen;
	for(const auto &id : ids)
	{
		const auto children {collections.contains(id) ? collections.at(id).snapshot.children : std::vector<std::string> {queue.at(id).snapshot.id}};
		for(const auto &child : children) if(seen.insert(child).second) result.push_back(child);
	}
	return result;
}

core::collection_snapshot core::download_application::select_collection(const std::string &id, const std::vector<std::string> &ids)
{
	std::lock_guard lock {mutex}; require_open();
	auto &collection {collection_at(id)};
	if(!collection.summary().can_select) throw application_error {"busy", "web.task_busy"};
	auto selected {collection.snapshot.source}; select_entries(selected, ids);
	if(ids.empty()) throw application_error {"emptySelection", "web.empty_selection"};
	std::set<std::string> urls;
	for(const auto &entry : selected.entries)
		if(entry.selected && (!urls.insert(entry.url).second || entry.task_id.empty() && queue.contains_url(entry.url)))
			throw application_error {"duplicateTask", "web.duplicate_task"};
	const auto tasks {queue.list()};
	const auto first {std::find_if(tasks.begin(), tasks.end(), [&](const auto &task) { return task.parent_id == id; })};
	const auto position {size_t(first - tasks.begin())};
	std::vector<std::string> unselected;
	for(auto &entry : selected.entries)
		if(!entry.selected && !entry.task_id.empty())
		{
			unselected.push_back(entry.task_id); entry.task_id.clear();
		}
	for(const auto &removed : queue.remove_many(unselected)) { collection.forget(removed.id); task_event("taskRemoved", removed); }
	std::set<std::string> created;
	for(auto &entry : selected.entries)
		if(entry.selected && entry.task_id.empty())
		{
			auto &task {queue.add(entry.url, entry.media, collection.snapshot.defaults, entry.media.details)};
			task.snapshot.parent_id = task.snapshot.group_id = id;
			task.snapshot.collection = collection_context {selected.url, selected.metadata.value("id", nlohmann::json(nullptr)).is_string() ? selected.metadata["id"].get<std::string>() : selected.url,
				selected.metadata.at("title").get<std::string>(), entry.index, selected.entries.size(), selected.kind == "channel"};
			entry.task_id = task.snapshot.id;
			created.insert(entry.task_id);
		}
	collection.snapshot.source = std::move(selected); collection.snapshot.children.clear(); collection.snapshot.updated_at = timestamp_now();
	for(const auto &entry : collection.snapshot.source.entries) if(entry.selected) collection.snapshot.children.push_back(entry.task_id);
	collection.index_children();
	for(const auto &child : collection.snapshot.children) task_event(created.contains(child) ? "taskAdded" : "taskUpdated", queue.at(child).snapshot);
	queue.move_group(collection.snapshot.children, std::min(position, queue.list().size() - collection.snapshot.children.size()));
	collection_event(collection, "collectionUpdated"); queue_event(); return collection_dto(collection);
}

core::collection_snapshot core::download_application::update_collection_defaults(const std::string &id, output_options options)
{
	validate_options(options); std::lock_guard lock {mutex}; require_open();
	if(!options.processing.format_ids.empty() || !options.processing.sections.empty() || !options.processing.filename.empty()) throw application_error {"taskSpecificDefault", "web.task_specific_default"};
	collection_at(id); auto tasks {queue}; auto groups {collections}; auto shared {shared_options};
	const bool processing_changed {processing_scope_changed(collections.at(id).snapshot.defaults, options)};
	auto &collection {groups.at(id)}; collection.snapshot.defaults = std::move(options);
	collection.snapshot.updated_at = timestamp_now(); ++collection.snapshot.summary.revision;
	if(option_scope == "shared" && processing_changed)
	{
		shared = copy_processing_options(default_output_options(collection.snapshot.defaults.output_folder), collection.snapshot.defaults);
		copy_processing_locked(tasks, groups, *shared);
	}
	commit_options_locked(std::move(tasks), std::move(groups), option_scope, std::move(shared)); return collection_dto(collections.at(id));
}

void core::download_application::skip_tasks(const std::vector<std::string> &ids, bool skipped)
{
	std::lock_guard lock {mutex}; require_open();
	const auto expanded {expand_items(ids)};
	for(const auto &id : expanded)
		if(!queue.at(id).snapshot.can_edit()) throw application_error {"busy", "web.task_busy"};
	for(const auto &id : expanded)
	{
		auto &task {queue.at(id).snapshot};
		if(skipped && task.status != task_status::skipped)
		{
			if(task.can_retry()) reset_for_retry(task);
			change_status(task, task_status::skipped); task_event("taskStatusChanged", task);
		}
		else if(!skipped && task.status == task_status::skipped) reset_for_retry(task);
	}
	queue_event(false);
}

void core::download_application::invert_downloadable(const std::vector<std::string> &ids)
{
	std::lock_guard lock {mutex}; require_open(); const auto expanded {expand_items(ids)};
	for(const auto &id : expanded) if(!queue.at(id).snapshot.can_edit()) throw application_error {"busy", "web.task_busy"};
	for(const auto &id : expanded)
	{
		auto &task {queue.at(id).snapshot};
		if(task.status == task_status::skipped) reset_for_retry(task);
		else { if(task.can_retry()) reset_for_retry(task); change_status(task, task_status::skipped); task_event("taskStatusChanged", task); }
	}
	queue_event(false);
}

core::task_snapshot core::download_application::create_task(const std::string &url, output_options options, const std::string &request_id)
{
	validate_options(options);
	std::lock_guard lock {mutex};
	require_open();
	if(!media.contains(url)) throw application_error {"mediaNotReady", "web.parse_first"};
	validate_media_options(options, media.at(url));
	if(queue.contains_url(url)) throw application_error {"duplicateTask", "web.duplicate_task"};
	auto tasks {queue}; auto groups {collections}; auto shared {shared_options};
	if(option_scope == "shared")
	{
		shared = copy_processing_options(default_output_options(options.output_folder), options); copy_processing_locked(tasks, groups, *shared);
	}
	const auto added {tasks.add(url, media_dto(url, media.at(url)), std::move(options), media.at(url)).snapshot};
	commit_options_locked(std::move(tasks), std::move(groups), option_scope, std::move(shared), request_id);
	return queue.at(added.id).snapshot;
}

core::task_snapshot core::download_application::get_task(const std::string &id) const
{
	std::lock_guard lock {mutex};
	return queue.at(id).snapshot;
}

std::vector<core::task_snapshot> core::download_application::get_tasks() const
{
	std::lock_guard lock {mutex};
	return queue.list();
}

core::queue_snapshot core::download_application::get_queue() const
{
	std::lock_guard lock {mutex};
	return queue_dto();
}

core::persistence_status core::download_application::get_persistence_status() const
{
	std::lock_guard lock {mutex};
	return persistence.status;
}

core::task_snapshot core::download_application::update_options(const std::string &id, output_options options)
{
	validate_options(options);
	std::lock_guard lock {mutex};
	require_open();
	const auto &before {queue.at(id).snapshot};
	if(!before.can_edit() && !(option_scope == "shared" && active(before.status) && !before.refreshing &&
		processing_only_change(before.options, options)))
		throw application_error {"busy", "web.task_busy"};
	validate_media_options(options, queue.at(id).metadata);
	const bool processing_changed {processing_scope_changed(before.options, options)};
	auto tasks {queue}; auto groups {collections}; auto shared {shared_options}; auto &snapshot {tasks.at(id).snapshot};
	snapshot.options = std::move(options);
	change_status(snapshot, snapshot.status);
	if(option_scope == "shared" && processing_changed)
	{
		shared = copy_processing_options(default_output_options(snapshot.options.output_folder), snapshot.options); copy_processing_locked(tasks, groups, *shared);
	}
	commit_options_locked(std::move(tasks), std::move(groups), option_scope, std::move(shared));
	return queue.at(id).snapshot;
}

core::download_request core::download_application::request_for(const download_task &task) const
{
	const auto &options {task.snapshot.options};
	auto arguments {output_arguments(options)};
	const bool bandcamp {task.snapshot.collection && std::regex_search(task.snapshot.collection->url, std::regex {R"(^https?://[^/]+\.bandcamp\.com(?:/album/|/music|/?$))"})};
	const auto output {bandcamp && !options.bandcamp_template.empty() ? options.bandcamp_template : ordinary_template(options)};
	auto naming {task.snapshot.collection ? playlist_template(to_wide(output), to_wide(options.playlist_prefix),
		options.playlist_folder, options.zero_padding, task.snapshot.collection->count, task.snapshot.collection->channel) : to_wide(output)};
	if(!options.processing.filename.empty()) naming = fs::u8path(options.processing.filename).stem().wstring() + L".%(ext)s";
	if(options.processing.sections.size() > 1)
	{
		if(naming.ends_with(L".%(ext)s")) naming.resize(naming.size() - 8);
		naming += L" - section %(section_start)d to %(section_end)d.%(ext)s";
	}
	const auto tokens {download_options::normalized(arguments)};
	if(!std::any_of(tokens.begin(), tokens.end(), [](const auto &token) { return token == "-o" || token == "--output" || token.starts_with("--output="); }))
		arguments += " -o " + to_utf8(quote_argument(naming));
	arguments += " --no-playlist";
	download_request request;
	request.url = to_wide(task.snapshot.url);
	request.youtube = is_youtube(request.url);
	request.media = task.metadata;
	request.output_folder = options.output_folder;
	request.use_arguments = true;
	request.arguments = std::move(arguments);
	for(const auto &section : options.processing.sections) request.sections.emplace_back(to_wide(section.start), to_wide(section.end));
	request.collection = task.snapshot.collection;
	return request;
}

void core::download_application::reset_for_retry(task_snapshot &snapshot)
{
	change_status(snapshot, task_status::waiting);
	snapshot.percent = 0;
	snapshot.progress_text.clear();
	snapshot.speed.clear();
	snapshot.result.reset();
	snapshot.error.reset();
	snapshot.started_at = snapshot.finished_at = 0;
	task_event("taskStatusChanged", snapshot);
}

void core::download_application::start_task(const std::string &id)
{
	std::lock_guard lock {mutex};
	require_open();
	if(!queue.at(id).snapshot.can_start()) throw application_error {"invalidState", "web.task_busy"};
	begin_completion_run_locked();
	scheduler.start(id);
	queue_event();
}

void core::download_application::retry_task(const std::string &id)
{
	std::lock_guard lock {mutex};
	require_open();
	auto &snapshot {queue.at(id).snapshot};
	if(snapshot.status != task_status::failed && snapshot.status != task_status::stopped)
		throw application_error {"invalidState", "web.task_busy"};
	begin_completion_run_locked();
	reset_for_retry(snapshot);
	scheduler.start(id);
	queue_event();
}

void core::download_application::stop_locked(const std::string &id)
{
	auto &snapshot {queue.at(id).snapshot};
	if(snapshot.status == task_status::waiting)
	{
		change_status(snapshot, task_status::stopped);
		snapshot.finished_at = timestamp_now();
	}
	else if(snapshot.status == task_status::running || snapshot.status == task_status::processing)
	{
		change_status(snapshot, task_status::stopping);
		executions.at(id)->working = false;
	}
	else if(snapshot.status == task_status::stopping) return;
	else throw application_error {"invalidState", "web.task_not_running"};
	completion_consumed = true; cancel_completion_locked("workChanged");
	task_event("taskStatusChanged", snapshot);
}

void core::download_application::stop_task(const std::string &id)
{
	std::lock_guard lock {mutex};
	require_open();
	stop_locked(id);
	queue_event();
}

void core::download_application::start_all()
{
	std::lock_guard lock {mutex};
	require_open();
	start_all_locked();
}

void core::download_application::start_all_locked()
{
	const auto tasks {queue.list()};
	if(std::any_of(tasks.begin(), tasks.end(), [](const auto &task) { return task.can_start() || task.can_retry(); })) begin_completion_run_locked();
	for(const auto &task : queue.list())
		if(task.status == task_status::failed || task.status == task_status::stopped) reset_for_retry(queue.at(task.id).snapshot);
	scheduler.start_all();
	queue_event();
}

void core::download_application::stop_all()
{
	std::lock_guard lock {mutex};
	require_open();
	stop_all_locked();
}

void core::download_application::stop_all_locked()
{
	completion_consumed = true; cancel_completion_locked("workChanged");
	scheduler.stop_all();
	for(const auto &task : queue.list())
		if(active(task.status)) stop_locked(task.id);
	queue_event();
}

void core::download_application::start_tasks(const std::vector<std::string> &ids)
{
	std::lock_guard lock {mutex};
	require_open();
	const auto expanded {expand_items(ids)};
	start_tasks_locked(expanded);
}

void core::download_application::start_tasks_locked(const std::vector<std::string> &expanded)
{
	bool starting {false};
	for(const auto &id : expanded)
	{
		auto &snapshot {queue.at(id).snapshot};
		if(!starting && (snapshot.can_retry() || snapshot.can_start())) { begin_completion_run_locked(); starting = true; }
		if(snapshot.can_retry()) reset_for_retry(snapshot);
		if(snapshot.can_start()) scheduler.start(id);
	}
	queue_event();
}

void core::download_application::stop_tasks(const std::vector<std::string> &ids)
{
	std::lock_guard lock {mutex};
	require_open();
	const auto expanded {expand_items(ids)};
	stop_tasks_locked(expanded);
}

void core::download_application::stop_tasks_locked(const std::vector<std::string> &expanded)
{
	for(const auto &id : expanded)
		if(queue.at(id).snapshot.can_stop() || queue.at(id).snapshot.can_start()) stop_locked(id);
	queue_event();
}

void core::download_application::remove_task(const std::string &id) { remove_tasks({id}); }

void core::download_application::remove_tasks(const std::vector<std::string> &ids)
{
	std::lock_guard lock {mutex};
	require_open();
	const auto expanded {expand_items(ids)};
	remove_tasks_locked(expanded);
}

void core::download_application::remove_tasks_locked(const std::vector<std::string> &expanded)
{
	auto tasks {queue}; auto sources {collections}; auto groups {custom_groups};
	const auto removed {tasks.remove_many(expanded)};
	std::map<std::string, std::vector<std::string>> children;
	for(const auto &task : removed) if(!task.parent_id.empty()) children[task.parent_id].push_back(task.id);
	for(const auto &[id, members] : children) { sources.at(id).remove_children(members); if(sources.at(id).snapshot.children.empty()) sources.erase(id); }
	retain_queue_groups(groups, logical_items(tasks));
	save_structure_locked(tasks, sources, groups, next_group_id);
	queue = std::move(tasks); detach_tasks(removed); custom_groups = std::move(groups); rebuild_group_summaries_locked();
	if(!removed.empty()) { completion_consumed = true; cancel_completion_locked("workChanged"); }
	for(const auto &task : removed) task_event("taskRemoved", task);
	++queue.revision; application_event event {"queueChanged"}; event.queue = queue_dto(); enqueue(std::move(event));
}

void core::download_application::clear_completed()
{
	std::lock_guard lock {mutex};
	require_open();
	std::set<std::string> completed_groups;
	for(const auto &[id, collection] : collections)
		if(collection.summary().status == "done") completed_groups.insert(id);
	std::vector<std::string> completed;
	for(const auto &task : queue.list())
		if(task.status == task_status::done && (task.parent_id.empty() || completed_groups.contains(task.parent_id)))
			completed.push_back(task.id);
	const auto removed {queue.remove_many(completed)}; detach_tasks(removed);
	if(!removed.empty()) { completion_consumed = true; cancel_completion_locked("workChanged"); }
	for(const auto &task : removed) task_event("taskRemoved", task);
	queue_event();
}

void core::download_application::detach_tasks(const std::vector<task_snapshot> &tasks)
{
	for(const auto &task : tasks) { logs.erase(task.id); metadata_commands.erase(task.id); }
	std::map<std::string, std::vector<std::string>> groups;
	for(const auto &task : tasks) if(!task.parent_id.empty()) groups[task.parent_id].push_back(task.id);
	for(const auto &[id, children] : groups)
	{
		auto &collection {collections.at(id)}; collection.remove_children(children);
		if(collection.snapshot.children.empty())
		{
			application_event event {"collectionRemoved"}; event.collection_progress = collection.summary(); enqueue(std::move(event)); collections.erase(id);
		}
	}
}

void core::download_application::move_task(const std::string &id, size_t position)
{
	std::lock_guard lock {mutex};
	require_open();
	queue.move(id, position);
	queue_event();
}

void core::download_application::execute(std::shared_ptr<execution> operation, download_request request, download_settings execution_settings, output_options execution_options)
{
	download_result result;
	std::optional<failure> error;
	const auto &id {operation->task_id};
	const auto generation {operation->generation};
	try
	{
		if(!component_available()) throw application_error {"componentMissing", "web.component_missing"};
		if(execution_settings.aria && execution_settings.aria_path.empty()) throw application_error {"ariaUnavailable", "web.aria_unavailable"};
		if(request.collection || !request.media.contains("formats"))
		{
			const auto output {query_metadata_limited(execution_settings.ytdlp_path,
				metadata_arguments(execution_settings, request.url, is_youtube(request.url), true, true) + L" --no-playlist", operation->working)};
			const auto first {output.find('{')}, last {output.rfind('}')};
			try
			{
				if(first == std::string::npos || last == std::string::npos) throw std::runtime_error {"metadata"};
				request.media = nlohmann::json::parse(output.substr(first, last - first + 1));
				if(!entry_available(request.media)) throw std::runtime_error {"metadata"};
			}
			catch(const std::exception &) { throw application_error {"entryMetadataFailed", "web.entry_parse_failed"}; }
			if(!operation->working) throw application_error {"stopped", "web.task_not_running"};
			std::lock_guard lock {mutex};
			if(queue.contains(id) && accepts_execution(queue.at(id).snapshot, generation))
			{
				auto &task {queue.at(id)}; task.metadata = request.media; task.snapshot.media = media_dto(task.snapshot.url, request.media);
				validate_media_options(execution_options, request.media);
				metadata_commands[id] = redact(to_utf8(L'"' + execution_settings.ytdlp_path.wstring() + L'"' + metadata_arguments(execution_settings, request.url, is_youtube(request.url), true, true) + L" --no-playlist"));
				change_status(task.snapshot, task.snapshot.status); task_event("taskUpdated", task.snapshot); persist_locked();
			}
		}
		const auto plan {build_download(execution_settings, request)};
		{
			std::lock_guard lock {mutex};
			logs[id].command = redact(to_utf8(plan.display_command));
		}
		auto progress = [this, id, generation](std::uint64_t completed, std::uint64_t total, std::string text, int, int)
		{
			if(total == std::uint64_t(-1) || text.find("Unknown B/s") != std::string::npos) return;
			std::lock_guard lock {mutex};
			if(!queue.contains(id) || !accepts_execution(queue.at(id).snapshot, generation)) return;
			auto &snapshot {queue.at(id).snapshot};
			snapshot.percent = total ? std::clamp(100.0 * completed / total, 0.0, 100.0) : 0;
			snapshot.progress_text = std::move(text);
				snapshot.speed = speed_text(snapshot.progress_text);
			change_status(snapshot, snapshot.status);
			task_event("taskProgress", snapshot);
		};
		auto stage = [this, id, generation, operation](download_stage value)
		{
			std::lock_guard lock {mutex};
			if(!queue.contains(id) || !accepts_execution(queue.at(id).snapshot, generation)) return;
			auto &snapshot {queue.at(id).snapshot};
			const auto status {value == download_stage::processing ? task_status::processing : task_status::running};
			if(snapshot.status == status) return;
			change_status(snapshot, status);
			if(status == task_status::processing) snapshot.speed.clear();
			operation->processing_started = status == task_status::processing ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point {};
			task_event("taskStatusChanged", snapshot);
			persist_locked();
		};
		auto produced_file = [this, id, generation](const fs::path &file)
		{
			if(!file.is_absolute() || !fs::is_regular_file(file)) return;
			std::lock_guard lock {mutex};
			if(!queue.contains(id) || !accepts_execution(queue.at(id).snapshot, generation)) return;
			auto &snapshot {queue.at(id).snapshot};
			if(!snapshot.result) snapshot.result = task_result {file, 259};
			if(std::find(snapshot.result->files.begin(), snapshot.result->files.end(), file) == snapshot.result->files.end()) snapshot.result->files.push_back(file);
			change_status(snapshot, snapshot.status); task_event("taskUpdated", snapshot); persist_locked();
		};
		result = execute_download(plan, operation->working, [this, id, generation](const auto &text, bool keyword) { if(!keyword) append_log(id, generation, text); }, progress, stage, nullptr, produced_file);
		if(result.exit_code != 0 || result.file.empty() || !fs::is_regular_file(result.file) || std::any_of(result.files.begin(), result.files.end(), [](const auto &file) { return !fs::is_regular_file(file); }))
			error = failure {"downloadFailed", "web.download_failed", result.exit_code};
	}
	catch(const application_error &failure) { error = failure.detail; append_log(id, generation, "ERROR: " + failure.detail.code + "\n"); }
	catch(const std::exception &) { error = failure {"downloadFailed", "web.download_failed", result.exit_code}; append_log(id, generation, "ERROR: downloadFailed\n"); }
	std::lock_guard lock {mutex};
	if(queue.contains(id) && queue.at(id).snapshot.execution_id == generation)
	{
		auto &snapshot {queue.at(id).snapshot};
		const auto status {!operation->working ? task_status::stopped : error ? task_status::failed : task_status::done};
		if(status == task_status::done) completion_success = true; else completion_blocked = true;
		change_status(snapshot, status);
		snapshot.speed.clear();
		snapshot.finished_at = timestamp_now();
		if(status == task_status::failed) snapshot.error = error;
		if(!result.file.empty() && fs::is_regular_file(result.file)) snapshot.result = task_result {result.file, result.exit_code, result.files};
		if(status == task_status::done) snapshot.percent = 100;
		task_event(status == task_status::failed ? "taskFailed" : status == task_status::done ? "taskCompleted" : "taskStatusChanged", snapshot);
		scheduler.finished(id, status);
	}
	operation->done = true;
	if(preferences.application.queue_behavior.remove_completed) cleanup_completed_locked();
	queue_event(false);
}

void core::download_application::dispatch()
{
	for(;;)
	{
		std::deque<application_event> pending;
		std::vector<std::thread> finished;
		{
			std::unique_lock lock {mutex};
			auto deadline {std::chrono::steady_clock::time_point::max()};
			if(scheduler.options.next_during_processing)
				for(const auto &[id, operation] : executions)
					if(!operation->done && operation->processing_started != std::chrono::steady_clock::time_point {})
					{
						const auto release {operation->processing_started + download_scheduler::processing_delay};
						if(release > std::chrono::steady_clock::now()) deadline = std::min(deadline, release);
					}
			if(deadline == std::chrono::steady_clock::time_point::max()) wake.wait(lock, [this] { return closing || dirty; });
			else wake.wait_until(lock, deadline, [this] { return closing || dirty; });
			if(closing) break;
			dirty = false;
			for(auto it {parsing.begin()}; it != parsing.end();)
				if(it->second->done) { finished.push_back(std::move(it->second->worker)); it = parsing.erase(it); }
				else ++it;
			for(auto it {executions.begin()}; it != executions.end();)
				if(it->second->done) { finished.push_back(std::move(it->second->worker)); it = executions.erase(it); }
				else ++it;
			completion_reaping = finished.size();
			for(auto it {component_operations.begin()}; it != component_operations.end();)
				if(it->second->done) { finished.push_back(std::move(it->second->worker)); it = component_operations.erase(it); }
				else ++it;
			const auto was_enabled {scheduler.enabled};
			size_t occupied {0};
			for(const auto &[id, operation] : executions)
				if(scheduler.occupies_slot(queue.at(id).snapshot.status, std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - operation->processing_started))) ++occupied;
			if(scheduler.enabled && !components_block_execution_locked() && occupied < scheduler.options.max_concurrent_downloads)
			for(const auto &id : scheduler.select(queue.list(), occupied))
			{
				auto &task {queue.at(id)};
				auto operation {std::make_shared<execution>()};
				operation->task_id = id;
				operation->generation = ++task.snapshot.execution_id;
				change_status(task.snapshot, task_status::running);
				task.snapshot.started_at = timestamp_now();
				executions[id] = operation;
				completion_started = true;
				task_event("taskStatusChanged", task.snapshot);
				queue_event(false);
				logs[id] = {id, {}, {}, operation->generation};
				operation->worker = std::thread([this, operation, request = request_for(task), execution_settings = settings_for_output(application_execution_settings(settings, preferences.application), task.snapshot.options), execution_options = task.snapshot.options] { execute(operation, request, execution_settings, execution_options); });
			}
			if(was_enabled && !scheduler.enabled) queue_event(false);
			pending.swap(events);
		}
		for(auto &worker : finished) if(worker.joinable()) worker.join();
		{
			std::lock_guard lock {mutex}; completion_reaping = 0; update_completion_locked();
		}
		for(const auto &event : pending) if(!closing && callback) callback(event);
	}
	// 退出时队列不再派发新任务，取消后等待真实进程和线程退出。
	std::vector<std::thread> workers;
	{
		std::lock_guard lock {mutex};
		scheduler.stop_all();
		for(auto &[url, operation] : parsing) { operation->working = false; workers.push_back(std::move(operation->worker)); }
			for(auto &[id, operation] : component_operations) { if(!component_committing || !id.starts_with("update:")) operation->working = false; workers.push_back(std::move(operation->worker)); }
		for(auto &[id, operation] : executions)
		{
			if(!operation->done)
			{
				operation->interrupted_by_shutdown = queue.at(id).snapshot.can_stop();
				operation->working = false;
				auto &snapshot {queue.at(id).snapshot};
				if(snapshot.can_stop()) change_status(snapshot, task_status::stopping);
			}
			workers.push_back(std::move(operation->worker));
		}
		persist_locked();
	}
	for(auto &worker : workers) if(worker.joinable()) worker.join();
	std::lock_guard lock {mutex};
	persist_locked();
}

void core::download_application::shutdown()
{
	if(closing.exchange(true)) return;
	{ std::lock_guard lock {mutex}; completion_consumed = true; cancel_completion_locked("closing"); }
	wake.notify_one();
	metadata_wake.notify_all();
	if(dispatcher.joinable()) dispatcher.join();
}

#include "web_bridge.hpp"
#include "naming_model.hpp"
#include "core_text.hpp"
#include "i18n.hpp"
#include "windows_desktop.hpp"
#include <fstream>

web_bridge::web_bridge(core::download_application &application, windows_desktop &desktop, web_context context, std::function<void(const json &)> send) :
	application {application}, desktop {desktop}, context {std::move(context)}, send {std::move(send)} {}

void web_bridge::reply(const std::string &id, json payload)
{
	send({{"version", 1}, {"id", id}, {"ok", true}, {"payload", std::move(payload)}});
}

void web_bridge::error(const std::string &id, const std::string &code, const std::string &key)
{
	send({{"version", 1}, {"id", id}, {"ok", false}, {"error", {{"code", code}, {"message", i18n::text(key)}}}});
}

void web_bridge::emit(const std::string &name, json payload, const std::string &request_id)
{
	send({{"version", 1}, {"event", name}, {"requestId", request_id}, {"payload", std::move(payload)}});
}

void web_bridge::window_activated()
{
	if(!clipboard_ready || !application.get_settings().application.application_behavior.clipboard_on_focus) return;
	try { const auto url {application.clipboard_source(desktop.clipboard_text(true))}; if(!url.empty()) emit("clipboardUrl", {{"url", url}}); }
	catch(const desktop_error &failure) { emit("desktopFailed", {{"code", failure.code}, {"message", i18n::text(failure.key)}}); }
}

void web_bridge::desktop_changed() { emit("desktopChanged", desktop.snapshot()); }

web_bridge::json web_bridge::completion_json(const core::completion_snapshot &snapshot)
{
	auto value {core::completion_json(snapshot)};
	if(snapshot.error) value["error"]["message"] = i18n::text(snapshot.error->message_key);
	return value;
}

void web_bridge::cancel_background_completion() { application.poll_completion(false); }

void web_bridge::completion_tick()
{
	const auto current {application.poll_completion(clipboard_ready && desktop.completion_observable())};
	if(current.state != "pending" || current.seconds) return;
	if(const auto claim {application.claim_completion(current.token)})
	{
		try { desktop.execute_completion(claim->action, claim->token); application.complete_action(claim->token); }
		catch(const desktop_error &failure) { application.complete_action(claim->token, core::failure {failure.code, failure.key, failure.system_error}); }
		catch(const std::exception &) { application.complete_action(claim->token, core::failure {"completionFailed", "web.completion_failed", ERROR_INVALID_DATA}); }
	}
}

core::output_options web_bridge::read_options(const json &options)
{
	return core::read_output_options(options);
}

web_bridge::json web_bridge::options_json(const core::output_options &options)
{
	return core::output_options_json(options);
}

web_bridge::json web_bridge::source_json(const core::source_snapshot &source)
{
	json entries = json::array(), selected = json::array(), playlists = json::array();
	size_t available {0};
	for(const auto &entry : source.entries)
	{
		if(entry.available) ++available;
		if(entry.selected) selected.push_back(entry.id);
		entries.push_back({{"entryId", entry.id}, {"url", entry.url}, {"taskId", entry.task_id}, {"index", entry.index}, {"media", entry.media.details},
			{"available", entry.available}, {"selected", entry.selected}, {"error", entry.error ? json {{"code", entry.error->code}, {"message", i18n::text(entry.error->message_key)}} : json(nullptr)}});
	}
	for(const auto &playlist : source.playlists) playlists.push_back(playlist.details);
	return {{"sourceId", source.id}, {"url", source.url}, {"resolvedUrl", source.resolved_url}, {"kind", source.kind}, {"category", source.category},
		{"metadata", source.metadata}, {"entries", std::move(entries)}, {"playlists", std::move(playlists)}, {"selectedEntries", std::move(selected)}, {"availableCount", available}, {"revision", source.revision}};
}

web_bridge::json web_bridge::summary_json(const core::collection_summary &summary)
{
	return {{"collectionId", summary.id}, {"status", summary.status}, {"percent", summary.percent}, {"revision", summary.revision},
		{"total", summary.total}, {"completed", summary.completed}, {"failed", summary.failed}, {"stopped", summary.stopped}, {"skipped", summary.skipped}, {"waiting", summary.waiting},
		{"currentTaskId", summary.current_task_id}, {"currentTitle", summary.current_title}, {"currentIndex", summary.current_index}, {"speed", summary.speed},
		{"actions", {{"start", summary.can_start}, {"stop", summary.can_stop}, {"retry", summary.can_retry}, {"remove", summary.can_remove}, {"select", summary.can_select}, {"split", summary.can_remove}}}};
}

web_bridge::json web_bridge::collection_json(const core::collection_snapshot &collection)
{
	return {{"collectionId", collection.id}, {"source", source_json(collection.source)}, {"options", options_json(collection.defaults)},
		{"display", core::queue_display_json(collection.source.url, collection.source.metadata, nullptr, true)},
		{"children", collection.children}, {"summary", summary_json(collection.summary)}, {"timestamps", {{"created", collection.created_at}, {"updated", collection.updated_at}}}};
}

web_bridge::json web_bridge::group_json(const core::queue_group_snapshot &snapshot)
{
	auto summary {summary_json(snapshot.summary)}; summary.erase("collectionId"); summary["groupId"] = snapshot.group.id;
	summary["actions"].erase("select"); summary["actions"].erase("split");
	return {{"groupId", snapshot.group.id}, {"name", snapshot.group.name}, {"collapsed", snapshot.group.collapsed}, {"items", snapshot.group.items}, {"summary", std::move(summary)}};
}

web_bridge::json web_bridge::task_json(const core::task_snapshot &task)
{
	const auto &options {task.options};
	json files = json::array();
	if(task.result) for(const auto &file : task.result->files) files.push_back(core::to_utf8(file.wstring()));
	return {{"taskId", task.id}, {"url", task.url}, {"media", task.media.details},
		{"display", core::queue_display_json(task.url, task.media.details, task.result ? &*task.result : nullptr)},
		{"options", options_json(options)},
		{"type", task.type == core::task_type::media ? "media" : "collection"}, {"groupId", task.group_id}, {"parentId", task.parent_id},
		{"status", core::status_name(task.status)}, {"phase", core::phase_name(task.phase())}, {"percent", task.percent},
		{"text", task.progress_text}, {"speed", task.speed}, {"file", core::to_utf8(task.file().wstring())},
		{"executionId", task.execution_id}, {"revision", task.revision}, {"refreshing", task.refreshing},
		{"error", task.error ? json {{"code", task.error->code}, {"message", i18n::text(task.error->message_key)}, {"exitCode", task.error->exit_code}} : json(nullptr)},
		{"result", task.result ? json {{"file", core::to_utf8(task.result->file.wstring())}, {"files", std::move(files)}, {"exitCode", task.result->exit_code}} : json(nullptr)},
		{"timestamps", {{"created", task.created_at}, {"updated", task.updated_at}, {"started", task.started_at}, {"finished", task.finished_at}}},
		{"actions", {{"start", task.can_start()}, {"stop", task.can_stop()}, {"retry", task.can_retry()},
			{"remove", task.can_remove()}, {"edit", task.can_edit()}, {"refresh", !task.refreshing && !core::active(task.status)},
				{"details", !task.refreshing}, {"open", task.result.has_value()}}}};
}

web_bridge::json web_bridge::queue_json(const core::queue_snapshot &queue)
{
	json tasks = json::array();
	for(const auto &task : queue.tasks) tasks.push_back(task_json(task));
	json collections = json::array();
	for(const auto &collection : queue.collections) collections.push_back(collection_json(collection));
	json groups = json::array();
	for(const auto &group : queue.groups) groups.push_back(group_json(group));
	return {{"revision", queue.revision}, {"scheduling", queue.scheduling}, {"maxConcurrentDownloads", queue.max_concurrent_downloads},
		{"tasks", std::move(tasks)}, {"collections", std::move(collections)}, {"items", queue.items}, {"activeCount", queue.active_tasks}, {"groups", std::move(groups)}, {"ungrouped", group_json(queue.ungrouped)}};
}

web_bridge::json web_bridge::persistence_json(const core::persistence_status &status)
{
	const auto file {core::to_utf8(status.file.wstring())};
	return {{"file", file}, {"enabled", status.enabled}, {"writable", status.writable}, {"restoredTasks", status.restored_tasks},
		{"error", status.error ? json {{"code", status.error->code}, {"systemError", status.error->exit_code},
			{"message", i18n::text(status.error->message_key) + " (" + status.error->code + ") " + file}} : json(nullptr)}};
}

void web_bridge::handle(const json &message)
{
	std::string id;
	try
	{
		id = message.at("id").get<std::string>();
		if(message.at("version") != 1 || id.empty()) throw std::invalid_argument("version");
		const auto command {message.at("command").get<std::string>()};
		const auto payload {message.value("payload", json::object())};
		if(command == "initialize")
		{
			json strings = json::object(), document;
			std::ifstream file {context.appdir / "locales" / (context.language + ".json")};
			file >> document;
			for(const auto &entry : document.at("strings").items())
				if(entry.key().starts_with("web.")) strings[entry.key()] = i18n::text(entry.key());
			const auto queue {queue_json(application.get_queue())};
				reply(id, {{"language", context.language}, {"strings", strings}, {"tasks", queue.at("tasks")}, {"queue", queue},
					{"persistence", persistence_json(application.get_persistence_status())},
					{"settings", application.get_settings_snapshot()}, {"settingsPersistence", persistence_json(application.get_settings_status())},
					{"processingDefaults", core::processing_json({})},
					{"desktop", desktop.snapshot()},
					{"completion", completion_json(application.get_completion())},
					{"components", application.get_components()},
					{"componentUpdates", application.get_component_updates()},
				{"outputFolder", core::to_utf8(context.output_folder.wstring())},
				{"capabilities", {"parseUrl", "addDownload", "listQueue", "persistenceStatus", "startTask", "stopTask", "retryTask", "removeTask", "moveTask",
					"startAll", "stopAll", "startTasks", "stopTasks", "removeTasks", "clearCompleted", "updateTaskOptions", "chooseFolder", "openFile",
					"createQueueGroup", "updateQueueGroup", "deleteQueueGroup", "assignQueueGroup", "moveGroupItem", "operateQueueScope",
					"parseSource", "selectSource", "addCollection", "getCollection", "selectCollection", "startCollection", "stopCollection", "retryCollection",
					"removeCollection", "splitCollection", "moveCollection", "updateCollectionDefaults", "skipTasks", "unskipTasks", "loadSettings", "saveSettings", "previewOutput",
						"loadMediaDetails", "refreshMetadata", "chooseFormats", "chooseSubtitles", "setSections", "overrideFilename", "normalizeFilename", "loadTaskLog", "revealTaskFile", "chooseOutputFile", "chooseCookieFile",
						"parseSources", "cancelSourceParse", "invertDownloadable", "exportMetadata", "copyClipboard", "readClipboard", "desktopStatus", "previewNaming", "saveNamingPreset", "saveDownloadPreset",
					"listApplicationPresets", "saveApplicationPreset", "renameApplicationPreset", "deleteApplicationPreset", "applyApplicationPreset",
					"copyTaskProcessingOptions", "setTaskOptionsScope", "prepareTaskOptions", "getCompletion", "setFinishAction", "cancelCompletion",
					"getComponents", "checkComponents", "setComponentPath", "setComponentChannel", "chooseComponentPath", "getComponentUpdates", "prepareComponentUpdate", "confirmComponentUpdate", "cancelComponentUpdate", "recoverComponentUpdate", "setComponentUpdatePreferences", "loadReleaseNotes", "openComponentRelease", "loadAbout", "openAboutLink"}}});
			emit("componentStatusChanged", {{"component", "yt-dlp"}, {"available", application.component_available()}});
		}
		else if(command == "parseUrl" || command == "parseSource")
		{
			application.parse_source(payload.at("url").get<std::string>(), id, payload.value("category", ""), payload.contains("options") ? core::single_video_requested(read_options(payload.at("options"))) : payload.value("singleVideo", false), payload.value("sourceId", ""));
			reply(id);
		}
		else if(command == "parseSources")
		{
			auto results {application.parse_sources(payload.at("text"), id, payload.value("singleVideo", false), payload.contains("inputIndex") ? std::optional<size_t> {payload.at("inputIndex").get<size_t>()} : std::nullopt)};
			for(auto &result : results) if(result.contains("error")) result["error"]["message"] = i18n::text(result["error"]["messageKey"]);
			reply(id, {{"results", std::move(results)}});
		}
		else if(command == "cancelSourceParse") reply(id, {{"cancelled", application.cancel_source_parse(payload.at("requestId").get<std::string>())}});
		else if(command == "selectSource") reply(id, source_json(application.select_source(payload.at("sourceId").get<std::string>(), payload.at("entryIds").get<std::vector<std::string>>())));
		else if(command == "addCollection")
		{
			const auto mode {payload.at("mode").get<std::string>()};
			if(mode != "collection" && mode != "individual") throw std::invalid_argument {"mode"};
			const auto result {application.create_source_tasks(payload.at("sourceId").get<std::string>(), read_options(payload.at("options")), mode == "collection", id)};
			json tasks = json::array(); for(const auto &task : result.tasks) tasks.push_back(task.id);
			reply(id, {{"collectionId", result.collection_id}, {"taskIds", std::move(tasks)}});
		}
		else if(command == "getCollection" || command == "selectCollection" || command == "startCollection" || command == "stopCollection" ||
			command == "retryCollection" || command == "removeCollection" || command == "splitCollection" || command == "moveCollection" || command == "updateCollectionDefaults")
		{
			const auto collection_id {payload.at("collectionId").get<std::string>()};
			if(command == "getCollection") reply(id, collection_json(application.get_collection(collection_id)));
			else if(command == "selectCollection") reply(id, collection_json(application.select_collection(collection_id, payload.at("entryIds").get<std::vector<std::string>>())));
			else if(command == "updateCollectionDefaults") reply(id, collection_json(application.update_collection_defaults(collection_id, read_options(payload.at("options")))));
			else
			{
				if(command == "startCollection") application.start_collection(collection_id);
				else if(command == "stopCollection") application.stop_collection(collection_id);
				else if(command == "retryCollection") application.retry_collection(collection_id);
				else if(command == "removeCollection") application.remove_collection(collection_id);
				else if(command == "splitCollection") application.split_collection(collection_id);
				else application.move_collection(collection_id, payload.at("position").get<size_t>());
				reply(id);
			}
		}
		else if(command == "skipTasks" || command == "unskipTasks")
		{
			application.skip_tasks(payload.at("taskIds").get<std::vector<std::string>>(), command == "skipTasks"); reply(id);
		}
		else if(command == "invertDownloadable") { application.invert_downloadable(payload.at("taskIds").get<std::vector<std::string>>()); reply(id); }
		else if(command == "addDownload")
		{
			const auto task {application.create_task(payload.at("url").get<std::string>(), read_options(payload.at("options")), id)};
			reply(id, {{"taskId", task.id}});
		}
		else if(command == "listQueue") reply(id, queue_json(application.get_queue()));
		else if(command == "createQueueGroup") reply(id, {{"groupId", application.create_queue_group(payload.at("name"))}});
		else if(command == "updateQueueGroup") { application.update_queue_group(payload.at("groupId"), payload.at("name"), payload.at("collapsed"), payload.at("position")); reply(id); }
		else if(command == "deleteQueueGroup") { application.delete_queue_group(payload.at("groupId")); reply(id); }
		else if(command == "assignQueueGroup") { application.assign_queue_group(payload.at("itemIds"), payload.at("groupId")); reply(id); }
		else if(command == "moveGroupItem") { application.move_group_item(payload.at("itemId"), payload.at("position"), payload.at("groupId")); reply(id); }
		else if(command == "operateQueueScope") reply(id, application.operate_queue_scope(payload.at("action"), payload.at("scope"), payload.value("groupId", ""), payload.value("itemIds", std::vector<std::string> {})));
		else if(command == "uiReady") { clipboard_ready = true; reply(id); window_activated(); completion_tick(); }
		else if(command == "getCompletion") reply(id, completion_json(application.get_completion()));
		else if(command == "setFinishAction")
		{
			const auto action {payload.at("action").get<std::string>()};
			if(!desktop.completion_capabilities().value(action, false)) throw desktop_error {"completionUnavailable", "web.completion_unavailable"};
			reply(id, completion_json(application.set_finish_action(action)));
		}
		else if(command == "cancelCompletion") reply(id, completion_json(application.cancel_completion(payload.at("token").get<std::uint64_t>())));
		else if(command == "loadSettings") reply(id, application.get_settings_snapshot());
		else if(command == "loadAbout") reply(id, desktop.load_about(application.get_components(), context.appdir));
		else if(command == "openAboutLink") { desktop.open_about_link(payload.at("linkId"), context.appdir); reply(id, {{"opened", true}}); }
		else if(command == "getComponents") reply(id, application.get_components());
		else if(command == "getComponentUpdates") reply(id, application.get_component_updates());
		else if(command == "prepareComponentUpdate") reply(id, application.prepare_component_update(payload.at("component"), id));
		else if(command == "confirmComponentUpdate" || command == "cancelComponentUpdate" || command == "recoverComponentUpdate")
		{
			const auto update_id {payload.at("updateId").get<std::string>()}; const auto generation {payload.at("generation").get<std::uint64_t>()};
			if(command == "confirmComponentUpdate") { if(!payload.value("confirmed", false)) throw core::application_error {"confirmationRequired", "web.update.confirmationRequired"}; application.confirm_component_update(update_id, generation, payload.value("customPathConfirmed", false)); }
			else if(command == "recoverComponentUpdate") { if(!payload.value("confirmed", false)) throw core::application_error {"confirmationRequired", "web.update.confirmationRequired"}; application.recover_component_update(update_id, generation); }
			else application.cancel_component_update(update_id, generation);
			reply(id);
		}
		else if(command == "setComponentUpdatePreferences") { application.set_component_update_preferences(payload.at("checkAtStartup"), payload.at("includeFfplay")); reply(id, application.get_settings_snapshot()); }
		else if(command == "loadReleaseNotes") application.load_component_release_notes(payload.at("component"), payload.value("page", 1u), id);
		else if(command == "openComponentRelease") { desktop.open_component_release(payload.at("component"), payload.at("channel"), payload.at("tag")); reply(id); }
		else if(command == "checkComponents") { application.check_components(payload.value("components", std::vector<std::string> {}), id); reply(id); }
		else if(command == "setComponentPath") application.set_component_path(payload.at("component"), payload.at("path"), id);
		else if(command == "setComponentChannel") { application.set_component_channel(payload.at("channel")); reply(id, application.get_settings_snapshot()); }
		else if(command == "chooseComponentPath")
		{
			const auto component {payload.at("component").get<std::string>()};
			if(component != "yt-dlp" && component != "ffmpeg") throw std::invalid_argument {"component"};
			const auto values {application.get_components()};
			const auto current {std::find_if(values.begin(), values.end(), [&](const auto &value) { return value.at("component") == component; })};
			const auto folder {std::filesystem::u8path(current->at("path").template get<std::string>()).parent_path()};
			reply(id, desktop.choose(component == "ffmpeg" ? "folder" : "component", folder.empty() ? context.appdir : folder));
		}
		else if(command == "copyTaskProcessingOptions") reply(id, application.copy_task_processing_options(payload.at("sourceId").get<std::string>()));
		else if(command == "setTaskOptionsScope") reply(id, application.set_task_options_scope(payload.at("mode"), payload.value("sourceId", "")));
		else if(command == "prepareTaskOptions") reply(id, core::output_options_json(application.prepare_task_options(read_options(payload.at("options")))));
		else if(command == "saveSettings")
		{
			auto value {core::read_preferences(payload.at("settings"))};
			if(!payload.at("settings").contains("application")) value.application = application.get_settings().application;
			if(!payload.at("settings").contains("namingPresets")) value.naming_presets = application.get_settings().naming_presets;
			if(!payload.at("settings").contains("applicationPresets")) value.application_presets = application.get_settings().application_presets;
			if(!payload.at("settings").contains("finishAction")) value.finish_action = application.get_settings().finish_action;
			if(!payload.at("settings").contains("components")) value.components = application.get_settings().components;
			const auto saved {application.save_settings(std::move(value))}; desktop.apply(saved.application.appearance);
			reply(id, application.get_settings_snapshot()); desktop_changed();
		}
		else if(command == "previewOutput") reply(id, application.preview_options(read_options(payload.at("options")), payload.value("mediaId", "")));
		else if(command == "previewNaming") reply(id, core::preview_naming(payload));
		else if(command == "saveNamingPreset" || command == "saveDownloadPreset")
		{
			const auto name {command == "saveNamingPreset" ? application.save_naming_preset(payload.at("outputTemplate")).name : application.save_download_preset(read_options(payload.at("options")))};
			reply(id, {{"name", name}, {"settings", application.get_settings_snapshot()}});
		}
		else if(command == "listApplicationPresets") reply(id, {{"presets", application.get_settings_snapshot().at("applicationPresets")}});
		else if(command == "saveApplicationPreset" || command == "renameApplicationPreset" || command == "deleteApplicationPreset" || command == "applyApplicationPreset")
		{
			const auto name {payload.at("name").get<std::string>()};
			if(command == "saveApplicationPreset") application.save_application_preset(name, payload.value("replace", false));
			else if(command == "renameApplicationPreset") application.rename_application_preset(name, payload.at("newName"));
			else if(command == "deleteApplicationPreset") application.delete_application_preset(name);
			else { application.apply_application_preset(name); desktop.apply(application.get_settings().application.appearance); desktop_changed(); }
			reply(id, {{"settings", application.get_settings_snapshot()}});
		}
		else if(command == "normalizeFilename") reply(id, options_json(core::filename_options(read_options(payload.at("options")), payload.value("filename", ""))));
		else if(command == "loadMediaDetails") reply(id, application.get_media_details(payload.at("mediaId").get<std::string>()));
		else if(command == "refreshMetadata") { application.refresh_metadata(payload.at("taskIds").get<std::vector<std::string>>(), id); reply(id); }
		else if(command == "loadTaskLog")
		{
			const auto log {application.get_task_log(payload.at("taskId").get<std::string>())};
			reply(id, {{"taskId", log.task_id}, {"command", log.command}, {"text", log.text}, {"executionId", log.execution_id}, {"revision", log.revision}, {"truncated", log.truncated}});
		}
		else if(command == "chooseFormats") reply(id, task_json(application.choose_formats(payload.at("taskId").get<std::string>(), payload.at("formatIds").get<std::vector<std::string>>(), payload.value("multiAudio", false))));
		else if(command == "chooseSubtitles") reply(id, task_json(application.choose_subtitles(payload.at("taskId").get<std::string>(), payload.at("languages").get<std::vector<std::string>>(), payload.value("format", "any"), payload.value("futureDefault", false))));
		else if(command == "setSections")
		{
			std::vector<core::time_section> sections; for(const auto &section : payload.at("sections")) sections.push_back({section.at("start"), section.at("end")});
			reply(id, task_json(application.set_sections(payload.at("taskId").get<std::string>(), sections)));
		}
		else if(command == "overrideFilename") reply(id, task_json(application.override_filename(payload.at("taskId").get<std::string>(), payload.value("filename", ""))));
		else if(command == "revealTaskFile")
		{
			const auto task {application.get_task(payload.at("taskId").get<std::string>())}; desktop.reveal_file(task.file(), task.options.output_folder); reply(id);
		}
		else if(command == "chooseOutputFile")
		{
			const auto config {application.get_settings().application.application_behavior};
			const auto folder {config.folder_origin == "program" ? context.appdir : std::filesystem::u8path(payload.value("folder", core::to_utf8(context.output_folder.wstring())))};
			reply(id, desktop.choose("output", folder));
		}
		else if(command == "exportMetadata")
		{
			const auto details {application.get_media_details(payload.at("mediaId"))};
			reply(id, desktop.export_metadata(details.at("metadata"), payload.at("format"), context.output_folder));
		}
		else if(command == "desktopStatus") reply(id, desktop.snapshot());
		else if(command == "readClipboard") reply(id, {{"text", desktop.clipboard_text()}});
		else if(command == "copyClipboard")
		{
			const auto kind {payload.at("kind").get<std::string>()}; std::string text;
			if(kind == "urls")
			{
				const auto queue {application.get_queue()};
				for(const auto &item : payload.at("ids"))
				{
					const auto key {item.get<std::string>()};
					const auto collection {std::find_if(queue.collections.begin(), queue.collections.end(), [&](const auto &value) { return value.id == key; })};
					text += (text.empty() ? "" : "\r\n") + (collection == queue.collections.end() ? application.get_task(key).url : collection->source.url);
				}
			}
			else if(kind == "metadata")
			{
				const auto value {application.get_media_details(payload.at("mediaId")).at("metadata").at(json::json_pointer(payload.at("path").get<std::string>()))};
				text = value.is_string() ? value.get<std::string>() : value.dump();
			}
			else if(kind == "command" || kind == "log")
			{
				const auto log {application.get_task_log(payload.at("taskId"))};
				text = kind == "command" ? log.command : payload.value("selection", "");
				if(kind == "log" && log.text.find(text) == std::string::npos) throw std::invalid_argument {"selection"};
			}
			else throw std::invalid_argument {"kind"};
			desktop.copy_text(application.visible_text(std::move(text))); reply(id, {{"copied", true}});
		}
		else if(command == "persistenceStatus") reply(id, persistence_json(application.get_persistence_status()));
		else if(command == "startAll" || command == "stopAll" || command == "clearCompleted" ||
			command == "startTasks" || command == "stopTasks" || command == "removeTasks")
		{
			if(command == "startAll") application.start_all();
			else if(command == "stopAll") application.stop_all();
			else if(command == "clearCompleted") application.clear_completed();
			else
			{
				const auto ids {payload.at("taskIds").get<std::vector<std::string>>()};
				if(command == "startTasks") application.start_tasks(ids);
				else if(command == "stopTasks") application.stop_tasks(ids);
				else application.remove_tasks(ids);
			}
			reply(id);
		}
		else if(command == "startTask" || command == "stopTask" || command == "retryTask" || command == "removeTask" ||
			command == "moveTask" || command == "updateTaskOptions" || command == "openFile")
		{
			const auto task_id {payload.at("taskId").get<std::string>()};
			if(command == "startTask")
			{
				const auto status {application.get_task(task_id).status};
				if(status == core::task_status::failed || status == core::task_status::stopped) application.retry_task(task_id);
				else application.start_task(task_id);
			}
			else if(command == "stopTask") application.stop_task(task_id);
			else if(command == "retryTask") application.retry_task(task_id);
			else if(command == "removeTask") application.remove_task(task_id);
				else if(command == "moveTask") application.move_queue_item(task_id, payload.at("position").get<size_t>());
			else if(command == "updateTaskOptions") application.update_options(task_id, read_options(payload.at("options")));
			else
			{
				desktop.open_file(application.get_task(task_id).file());
			}
			reply(id);
		}
		else if(command == "chooseFolder" || command == "chooseCookieFile")
		{
			const auto config {application.get_settings().application.application_behavior};
			const auto origin {config.folder_origin == "program" ? context.appdir : std::filesystem::u8path(payload.value("folder", core::to_utf8(context.output_folder.wstring())))};
			reply(id, desktop.choose(command == "chooseFolder" ? "folder" : "cookie", origin));
		}
		else error(id, "unsupportedCommand", "web.unsupported");
	}
	catch(const core::application_error &failure) { error(id, failure.detail.code, failure.detail.message_key); }
	catch(const desktop_error &failure) { error(id, failure.code, failure.key); }
	catch(const std::exception &) { error(id, "invalidPayload", "web.invalid_payload"); }
}

web_bridge::json web_bridge::event_json(const core::application_event &event)
{
	json payload {{"url", event.url}};
	if(event.media) payload = event.media->details;
	if(event.task)
	{
		const auto &task {*event.task};
		payload = task_json(task);
		if(task.error) { payload["code"] = task.error->code; payload["message"] = i18n::text(task.error->message_key); payload["exitCode"] = task.error->exit_code; }
	}
	if(event.queue) payload = queue_json(*event.queue);
	if(event.queue && event.name == "queueStateChanged")
		payload = {{"revision", event.queue->revision}, {"scheduling", event.queue->scheduling}, {"maxConcurrentDownloads", event.queue->max_concurrent_downloads}, {"activeCount", event.queue->active_tasks}};
	if(event.source) payload = source_json(*event.source);
	if(event.collection) payload = collection_json(*event.collection);
	if(event.collection_progress) payload = summary_json(*event.collection_progress);
	if(event.group_progress) payload = group_json(*event.group_progress);
		if(event.persistence) payload = persistence_json(*event.persistence);
		if(event.log) payload = {{"taskId", event.log->task_id}, {"command", event.log->command}, {"text", event.log->text}, {"executionId", event.log->execution_id}, {"revision", event.log->revision}, {"truncated", event.log->truncated}};
		if(event.error) { payload["code"] = event.error->code; payload["message"] = i18n::text(event.error->message_key); }
		if(!event.diagnostic.empty()) payload["diagnostic"] = event.diagnostic;
			if(event.completion) payload = completion_json(*event.completion);
		if(event.component)
		{
			payload = core::component_json(*event.component);
			for(const auto key : {"error", "localError"}) if(!payload.at(key).is_null()) payload[key]["message"] = i18n::text(payload[key].at("messageKey"));
		}
		if(event.component_update) payload = core::component_update_json(*event.component_update);
		if(!event.release_notes.is_null()) { payload = event.release_notes; if(event.error) payload["error"] = {{"code", event.error->code}, {"message", i18n::text(event.error->message_key)}}; }
	return {{"event", event.name}, {"requestId", event.request_id}, {"payload", std::move(payload)},
		{"revision", event.task ? event.task->revision : event.collection ? event.collection->summary.revision : event.collection_progress ? event.collection_progress->revision : 0}};
}

void web_bridge::backend_event(const json &event)
{
	const auto &payload {event.at("payload")};
	if(event.at("event") == "queueChanged" || event.at("event") == "queueStateChanged")
	{
		const auto revision {payload.at("revision").get<std::uint64_t>()};
		if(revision < queue_revision) return;
		queue_revision = revision;
	}
	if(payload.contains("taskId") && event.at("event") != "taskLog")
	{
		const auto id {payload.at("taskId").get<std::string>()};
		const auto revision {event.at("revision").get<std::uint64_t>()};
		if(revision < revisions[id]) return;
		revisions[id] = revision;
	}
	if(payload.contains("collectionId"))
	{
		const auto id {payload.at("collectionId").get<std::string>()};
		const auto revision {event.at("revision").get<std::uint64_t>()};
		if(revision < revisions[id]) return;
		revisions[id] = revision;
	}
	const auto name {event.at("event").get<std::string>()};
	if(name == "componentPathSaved") { reply(event.at("requestId"), application.get_settings_snapshot()); return; }
	if(name == "componentPathFailed") { send({{"version", 1}, {"id", event.at("requestId")}, {"ok", false}, {"error", payload}}); return; }
	if(name == "componentReleaseNotes") { if(payload.contains("error")) send({{"version", 1}, {"id", event.at("requestId")}, {"ok", false}, {"error", payload.at("error")}}); else reply(event.at("requestId"), payload); return; }
	if(name == "queueChanged" || name == "queueStateChanged" || name == "taskProgress" || name == "taskStatusChanged" || name == "taskCompleted" || name == "taskFailed") desktop.update_progress(application.get_queue());
	emit(name, payload, event.at("requestId").get<std::string>());
}

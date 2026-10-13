#include "queue_persistence.hpp"
#include "core_text.hpp"
#include "collection_model.hpp"
#include <Windows.h>
#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>
#include <set>

namespace
{
	using json = nlohmann::json;
	namespace fs = std::filesystem;
	std::uint64_t unsigned_value(const json &value)
	{
		if(!value.is_number_unsigned()) throw std::runtime_error {"invalidNumber"};
		return value.get<std::uint64_t>();
	}
	std::uint64_t identity_number(const std::string &id, const std::string &prefix)
	{
		std::uint64_t number {0};
		if(!id.starts_with(prefix)) throw std::runtime_error {"invalidId"};
		const auto end {id.data() + id.size()};
		const auto result {std::from_chars(id.data() + prefix.size(), end, number)};
		if(result.ec != std::errc {} || result.ptr != end || !number || id != prefix + std::to_string(number))
			throw std::runtime_error {"invalidId"};
		return number;
	}
	bool source_without_credentials(const std::string &url)
	{
		const auto scheme {url.find("://")};
		if(scheme == std::string::npos) return false;
		const auto start {scheme + 3};
		const auto end {url.find_first_of("/?#", start)};
		return url.substr(start, end == std::string::npos ? end : end - start).find('@') == std::string::npos;
	}
	fs::path environment_path(const wchar_t *name)
	{
		const auto size {GetEnvironmentVariableW(name, nullptr, 0)};
		if(!size) return {};
		std::wstring value(size, L'\0');
		const auto count {GetEnvironmentVariableW(name, value.data(), size)};
		if(!count || count >= size) return {};
		value.resize(count);
		return value;
	}
	json output_json(const core::persisted_output &output)
	{
		auto processing {core::processing_json(output.processing)};
		processing.erase("rawArgs"); processing["rawArgsProtected"] = core::protect_text(output.processing.raw_args);
		return {{"kind", output.kind}, {"format", output.format}, {"quality", output.quality}, {"naming", output.naming},
			{"folder", output.folder}, {"cover", output.cover}, {"metadata", output.metadata}, {"template", output.output_template},
			{"playlistPrefix", output.playlist_prefix}, {"zeroPadding", output.zero_padding}, {"playlistFolder", output.playlist_folder}, {"singleVideo", output.single_video}, {"processing", std::move(processing)}, {"bandcampTemplate", output.bandcamp_template}};
	}
	core::persisted_output read_output(const json &value, std::uint64_t version)
	{
		core::persisted_output output {value.at("kind").get<std::string>(), value.at("format").get<std::string>(), value.at("quality").get<std::string>(),
			value.at("naming").get<std::string>(), value.at("folder").get<std::string>(), value.at("cover").get<bool>(), value.at("metadata").get<bool>()};
		output.bandcamp_template = value.value("bandcampTemplate", "");
		if(version != 1)
		{
			output.output_template = value.at("template").get<std::string>(); output.playlist_prefix = value.at("playlistPrefix").get<std::string>();
			output.zero_padding = value.at("zeroPadding").get<bool>(); output.playlist_folder = value.at("playlistFolder").get<bool>(); output.single_video = value.at("singleVideo").get<bool>();
		}
		if(version >= 3 || value.contains("processing"))
		{
			auto processing {value.at("processing")};
			processing["rawArgs"] = core::unprotect_text(processing.at("rawArgsProtected").get<std::string>());
			output.processing = core::read_processing(processing);
		}
		return output;
	}
	json serialize(const core::persisted_queue &queue)
	{
		json result {{"schemaVersion", core::persisted_queue::schema_version}, {"lastTaskId", queue.last_task_id},
			{"lastCollectionId", queue.last_collection_id}, {"tasks", json::array()}, {"collections", json::array()}};
		result["optionScope"] = queue.option_scope;
		result["sharedOptions"] = queue.shared_options ? output_json(*queue.shared_options) : json(nullptr);
		result["lastGroupId"] = queue.last_group_id;
		result["groups"] = json::array();
		for(const auto &group : queue.groups)
			result["groups"].push_back({{"id", group.id}, {"name", group.name}, {"collapsed", group.collapsed}, {"items", group.items}});
		for(const auto &task : queue.tasks)
		{
			const auto &output {task.output};
			json entry {{"id", task.id}, {"sourceUrlProtected", core::protect_text(task.url)}, {"type", task.type}, {"groupId", task.group_id}, {"parentId", task.parent_id},
				{"lastStatus", task.status}, {"media", core::persistent_media(task.media)},
				{"output", output_json(output)}, {"context", nullptr}, {"result", nullptr},
				{"createdAt", task.created_at}, {"updatedAt", task.updated_at}, {"startedAt", task.started_at}, {"finishedAt", task.finished_at}, {"error", nullptr}};
			if(task.error) entry["error"] = {{"code", task.error->code}, {"messageKey", task.error->message_key}, {"exitCode", task.error->exit_code}};
			if(task.context)
			{
				const auto &context {*task.context};
				entry["context"] = {{"sourceUrlProtected", core::protect_text(context.url)}, {"idProtected", core::protect_text(context.id)}, {"title", context.title}, {"index", context.index}, {"count", context.count}, {"channel", context.channel}};
			}
			if(task.result) entry["result"] = {{"file", task.result->file}, {"exitCode", task.result->exit_code}, {"files", task.result->files}};
			result["tasks"].push_back(std::move(entry));
		}
		for(const auto &collection : queue.collections)
		{
			json entries = json::array();
			for(const auto &entry : collection.entries)
				entries.push_back({{"id", entry.id}, {"sourceUrlProtected", core::protect_text(entry.url)}, {"taskId", entry.task_id}, {"index", entry.index},
					{"media", core::persistent_media(entry.media)}, {"available", entry.available}, {"selected", entry.selected}});
			result["collections"].push_back({{"id", collection.id}, {"sourceUrlProtected", core::protect_text(collection.url)}, {"resolvedUrlProtected", core::protect_text(collection.resolved_url)},
				{"kind", collection.kind}, {"category", collection.category}, {"metadata", core::persistent_media(collection.metadata)},
				{"defaults", output_json(collection.defaults)}, {"entries", std::move(entries)}, {"children", collection.children},
				{"createdAt", collection.created_at}, {"updatedAt", collection.updated_at}});
		}
		return result;
	}
	core::persisted_queue deserialize(const json &data, std::uint64_t version)
	{
		const bool version_one {version == 1};
		const auto url = [version](const json &object, const std::string &key)
		{
			return version >= 4 ? core::unprotect_text(object.at(key + "Protected").get<std::string>()) : object.at(key).get<std::string>();
		};
		core::persisted_queue queue;
		if(version >= 5)
		{
			queue.option_scope = data.at("optionScope").get<std::string>();
			if(queue.option_scope != "individual" && queue.option_scope != "shared") throw std::runtime_error {"invalidScope"};
			if(!data.at("sharedOptions").is_null()) queue.shared_options = read_output(data.at("sharedOptions"), version);
			if((queue.option_scope == "shared") != queue.shared_options.has_value()) throw std::runtime_error {"invalidScope"};
		}
		queue.last_task_id = unsigned_value(data.at("lastTaskId"));
		queue.last_collection_id = version_one ? 0 : unsigned_value(data.at("lastCollectionId"));
		if(queue.last_task_id == std::numeric_limits<std::uint64_t>::max() || !data.at("tasks").is_array())
			throw std::runtime_error {"invalidQueue"};
		std::set<std::string> ids, urls;
		for(const auto &entry : data.at("tasks"))
		{
			core::persisted_task task;
			task.id = entry.at("id").get<std::string>();
			task.url = url(entry, "sourceUrl");
			task.type = entry.at("type").get<std::string>();
			task.group_id = entry.at("groupId").get<std::string>();
			task.parent_id = entry.at("parentId").get<std::string>();
			task.status = entry.at("lastStatus").get<std::string>();
			core::restored_status(task.status);
			if(identity_number(task.id, "task-") > queue.last_task_id || !ids.insert(task.id).second || !urls.insert(task.url).second || task.type != "media" || !source_without_credentials(task.url))
				throw std::runtime_error {"invalidTask"};
			task.media = core::persistent_media(entry.at("media"));
			task.output = read_output(entry.at("output"), version);
			task.created_at = unsigned_value(entry.at("createdAt"));
			task.updated_at = unsigned_value(entry.at("updatedAt"));
			task.started_at = unsigned_value(entry.at("startedAt"));
			task.finished_at = unsigned_value(entry.at("finishedAt"));
			if(!task.created_at || task.updated_at < task.created_at) throw std::runtime_error {"invalidTime"};
			const auto &error {entry.at("error")};
			if(!error.is_null())
			{
				const auto code {unsigned_value(error.at("exitCode"))};
				if(code > std::numeric_limits<unsigned long>::max()) throw std::runtime_error {"invalidError"};
				task.error = core::failure {error.at("code").get<std::string>(), error.at("messageKey").get<std::string>(), (unsigned long)code};
				if(task.error->code.empty() || task.error->message_key.empty()) throw std::runtime_error {"invalidError"};
			}
			if((task.status == "failed") != task.error.has_value()) throw std::runtime_error {"invalidError"};
			if(version_one)
			{
				// v1 尚无 Collection；旧预留引用不代表真实父子关系。
				task.group_id.clear(); task.parent_id.clear();
				if(task.status == "done") continue;
			}
			else
			{
				const auto &context {entry.at("context")};
				if(!context.is_null())
				{
					task.context = core::persisted_context {url(context, "sourceUrl"), version >= 4 ? core::unprotect_text(context.at("idProtected").get<std::string>()) : context.at("id").get<std::string>(), context.at("title").get<std::string>(),
						unsigned_value(context.at("index")), unsigned_value(context.at("count")), context.at("channel").get<bool>()};
					if(!task.context->index || !task.context->count || !core::valid_source_url(task.context->url) || !source_without_credentials(task.context->url)) throw std::runtime_error {"invalidContext"};
				}
				const auto &result {entry.at("result")};
				if(!result.is_null())
				{
					const auto code {unsigned_value(result.at("exitCode"))};
					if(code > std::numeric_limits<unsigned long>::max() || task.status == "done" && code != 0) throw std::runtime_error {"invalidResult"};
					task.result = core::persisted_result {result.at("file").get<std::string>(), (unsigned long)code};
					task.result->files = result.value("files", std::vector<std::string> {task.result->file});
					for(const auto &file : task.result->files) if(!fs::u8path(file).is_absolute()) throw std::runtime_error {"invalidResult"};
					if(!fs::u8path(task.result->file).is_absolute()) throw std::runtime_error {"invalidResult"};
				}
			}
			queue.tasks.push_back(std::move(task));
		}
		if(version_one) return queue;
		if(queue.last_collection_id == std::numeric_limits<std::uint64_t>::max() || !data.at("collections").is_array()) throw std::runtime_error {"invalidCollection"};
		std::map<std::string, const core::persisted_task *> tasks;
		for(const auto &task : queue.tasks) tasks[task.id] = &task;
		std::set<std::string> parents, assigned;
		for(const auto &value : data.at("collections"))
		{
			core::persisted_collection collection;
			collection.id = value.at("id").get<std::string>(); collection.url = url(value, "sourceUrl");
			collection.resolved_url = url(value, "resolvedUrl"); collection.kind = value.at("kind").get<std::string>(); collection.category = value.at("category").get<std::string>();
			if(identity_number(collection.id, "collection-") > queue.last_collection_id || !parents.insert(collection.id).second ||
				!core::valid_source_url(collection.url) || !core::valid_source_url(collection.resolved_url) || !source_without_credentials(collection.url) || !source_without_credentials(collection.resolved_url) ||
				(collection.kind != "playlist" && collection.kind != "channel") || (!collection.category.empty() && collection.category != "videos" && collection.category != "shorts" && collection.category != "streams" && collection.category != "podcasts"))
				throw std::runtime_error {"invalidCollection"};
			collection.metadata = core::persistent_media(value.at("metadata")); collection.defaults = read_output(value.at("defaults"), version);
			collection.created_at = unsigned_value(value.at("createdAt")); collection.updated_at = unsigned_value(value.at("updatedAt"));
			if(!collection.created_at || collection.updated_at < collection.created_at) throw std::runtime_error {"invalidTime"};
			collection.children = value.at("children").get<std::vector<std::string>>();
			std::set<std::string> children, selected, entries;
			for(const auto &id : collection.children)
			{
				if(!tasks.contains(id) || !children.insert(id).second || !assigned.insert(id).second || tasks.at(id)->parent_id != collection.id ||
					tasks.at(id)->group_id != collection.id || !tasks.at(id)->context) throw std::runtime_error {"invalidRelation"};
			}
			if(children.empty() || !value.at("entries").is_array()) throw std::runtime_error {"invalidRelation"};
			for(const auto &item : value.at("entries"))
			{
				core::persisted_entry entry {item.at("id").get<std::string>(), url(item, "sourceUrl"), item.at("taskId").get<std::string>(),
					unsigned_value(item.at("index")), core::persistent_media(item.at("media")), item.at("available").get<bool>(), item.at("selected").get<bool>()};
				if(!entry.index || !entries.insert(entry.id).second || entry.selected != !entry.task_id.empty() ||
					!entry.url.empty() && !source_without_credentials(entry.url) ||
					entry.available && (!core::valid_source_url(entry.url) || !source_without_credentials(entry.url) || entry.media.at("title").get<std::string>().empty())) throw std::runtime_error {"invalidEntry"};
				if(entry.selected && (!entry.available || !children.contains(entry.task_id) || !selected.insert(entry.task_id).second || tasks.at(entry.task_id)->url != entry.url ||
					tasks.at(entry.task_id)->context->index != entry.index || tasks.at(entry.task_id)->context->url != collection.url)) throw std::runtime_error {"invalidRelation"};
				collection.entries.push_back(std::move(entry));
			}
			if(selected != children) throw std::runtime_error {"invalidRelation"};
			queue.collections.push_back(std::move(collection));
		}
		for(const auto &task : queue.tasks)
			if(!task.parent_id.empty() && !assigned.contains(task.id) || task.parent_id.empty() && !task.group_id.empty() || task.status == "done" && task.parent_id.empty()) throw std::runtime_error {"invalidRelation"};
		if(version >= 6)
		{
			queue.last_group_id = unsigned_value(data.at("lastGroupId"));
			if(queue.last_group_id == std::numeric_limits<std::uint64_t>::max() || !data.at("groups").is_array()) throw std::runtime_error {"invalidGroup"};
			std::set<std::string> group_ids, names, members, logical;
			for(const auto &task : queue.tasks) logical.insert(task.parent_id.empty() ? task.id : task.parent_id);
			for(const auto &value : data.at("groups"))
			{
				core::queue_group group {value.at("id").get<std::string>(), value.at("name").get<std::string>(), value.at("collapsed").get<bool>(), value.at("items").get<std::vector<std::string>>()};
				if(identity_number(group.id, "queue-group-") > queue.last_group_id || !group_ids.insert(group.id).second ||
					core::queue_group_name(group.name) != group.name || !names.insert(group.name).second) throw std::runtime_error {"invalidGroup"};
				for(const auto &id : group.items) if(!logical.contains(id) || !members.insert(id).second) throw std::runtime_error {"invalidGroupMember"};
				queue.groups.push_back(std::move(group));
			}
		}
		return queue;
	}
}

struct core::queue_persistence::file_lock
{
	HANDLE handle {INVALID_HANDLE_VALUE};
	~file_lock() { if(handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};

core::queue_persistence::queue_persistence(fs::path file) : status {std::move(file)} {}
core::queue_persistence::~queue_persistence() = default;

core::fs::path core::default_queue_file()
{
	// 验证宿主可注入独立文件；不修改用户环境或迁移 Settings。
	if(const auto file {environment_path(L"YTDLP_QUEUE_FILE")}; !file.empty()) return file;
	return environment_path(L"LOCALAPPDATA") / "ytdlp-interface" / "CoreQueue" / "queue-v1.json";
}

nlohmann::json core::persistent_media(const nlohmann::json &details)
{
	if(!details.is_object() || !details.at("title").is_string()) throw std::runtime_error {"invalidMedia"};
	json result {{"title", details.at("title")}};
	for(const auto key : {"id", "uploader", "channel", "upload_date", "webpage_url_domain", "ext"})
	{
		const auto value {details.value(key, json(nullptr))};
		if(!value.is_null() && !value.is_string()) throw std::runtime_error {"invalidMedia"};
		result[key] = value;
	}
	const auto duration {details.value("duration", json(nullptr))};
	if(!duration.is_null() && (!duration.is_number() || duration.get<double>() < 0)) throw std::runtime_error {"invalidMedia"};
	result["duration"] = duration;
	return result;
}

core::task_status core::restored_status(const std::string &status)
{
	if(status == "waiting" || status == "running" || status == "processing") return task_status::waiting;
	if(status == "stopping" || status == "stopped") return task_status::stopped;
	if(status == "failed") return task_status::failed;
	if(status == "skipped") return task_status::skipped;
	if(status == "done") return task_status::done;
	throw std::runtime_error {"invalidStatus"};
}

void core::queue_persistence::reject_restore(const std::string &code)
{
	status.writable = false;
	status.error = failure {code, "web.queue_restore_failed"};
}

std::optional<core::persisted_queue> core::queue_persistence::load()
{
	if(status.file.empty()) return {};
	status.enabled = true;
	try
	{
		if(!status.file.is_absolute()) throw std::runtime_error {"invalidPath"};
		fs::create_directories(status.file.parent_path());
		lock = std::make_unique<file_lock>();
		lock->handle = CreateFileW((status.file.wstring() + L".lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if(lock->handle == INVALID_HANDLE_VALUE)
		{
			status.error = failure {"queueFileInUse", "web.queue_restore_failed", GetLastError()};
			return {};
		}
		status.writable = true;
		if(!fs::exists(status.file)) return {};
		std::ifstream input {status.file, std::ios::binary};
		if(!input) throw std::runtime_error {"readFailed"};
		const auto data {json::parse(input)};
		const auto version {unsigned_value(data.at("schemaVersion"))};
			if(version < 1 || version > persisted_queue::schema_version) { reject_restore("queueUnsupportedVersion"); return {}; }
		return deserialize(data, version);
	}
	catch(const std::exception &) { reject_restore(); return {}; }
}

bool core::queue_persistence::save(const persisted_queue &queue)
{
	if(!status.enabled || !status.writable) return false;
	for(const auto &task : queue.tasks)
		if(!source_without_credentials(task.url) || task.context && !source_without_credentials(task.context->url))
		{
			status.error = failure {"queueSensitiveUrl", "web.queue_sensitive_url"};
			return false;
		}
	for(const auto &collection : queue.collections)
	{
		if(!source_without_credentials(collection.url) || !source_without_credentials(collection.resolved_url))
		{ status.error = failure {"queueSensitiveUrl", "web.queue_sensitive_url"}; return false; }
		for(const auto &entry : collection.entries)
			if(!entry.url.empty() && !source_without_credentials(entry.url))
			{ status.error = failure {"queueSensitiveUrl", "web.queue_sensitive_url"}; return false; }
	}
	HANDLE output {INVALID_HANDLE_VALUE};
	SetLastError(ERROR_INVALID_DATA);
	try
	{
		const auto bytes {serialize(queue).dump(2) + '\n'};
		const auto temporary {status.file.wstring() + L".tmp"};
		output = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if(output == INVALID_HANDLE_VALUE) throw std::runtime_error {"writeFailed"};
		for(size_t offset {0}; offset < bytes.size();)
		{
			DWORD written {0};
			const auto size {DWORD(std::min(bytes.size() - offset, size_t(std::numeric_limits<DWORD>::max())))};
			if(!WriteFile(output, bytes.data() + offset, size, &written, nullptr) || !written) throw std::runtime_error {"writeFailed"};
			offset += written;
		}
		if(!FlushFileBuffers(output)) throw std::runtime_error {"flushFailed"};
		const auto closed {CloseHandle(output)};
		output = INVALID_HANDLE_VALUE;
		if(!closed || !MoveFileExW(temporary.c_str(), status.file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			throw std::runtime_error {"replaceFailed"};
		status.error.reset();
		return true;
	}
	catch(const std::exception &)
	{
		const auto code {GetLastError()};
		if(output != INVALID_HANDLE_VALUE) CloseHandle(output);
		status.error = failure {"queueWriteFailed", "web.queue_save_failed", code};
		return false;
	}
}

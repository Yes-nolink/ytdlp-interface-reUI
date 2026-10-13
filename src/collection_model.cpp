#include "collection_model.hpp"
#include "core_text.hpp"
#include <algorithm>
#include <regex>

namespace
{
	using json = nlohmann::json;
	std::string string_value(const json &info, const char *key)
	{
		return info.contains(key) && info[key].is_string() ? info[key].get<std::string>() : std::string {};
	}
	json describe_media(const json &info, const std::string &url)
	{
		json value {{"url", url}, {"title", string_value(info, "title")}};
		for(const auto key : {"id", "uploader", "channel", "upload_date", "webpage_url_domain", "ext", "thumbnail", "format_id", "resolution", "format_note"})
			value[key] = info.contains(key) && info[key].is_string() ? info[key] : json(nullptr);
		for(const auto key : {"filesize", "filesize_approx"}) value[key] = info.contains(key) && info[key].is_number_integer() && info[key].get<double>() >= 0 ? info[key] : json(nullptr);
		if(info.contains("requested_formats") && info["requested_formats"].is_array() && info["requested_formats"].size() == 2)
			for(const auto &format : info["requested_formats"])
				if(!format.contains("filesize") || format["filesize"].is_null()) value["filesize_approx"] = nullptr;
		value["duration"] = info.contains("duration") && info["duration"].is_number() && info["duration"].get<double>() >= 0 ? info["duration"] : json(nullptr);
		if((value["thumbnail"].is_null() || value["thumbnail"] == "") && info.contains("thumbnails") && info["thumbnails"].is_array())
			for(const auto &image : info["thumbnails"])
				if(image.contains("url") && image["url"].is_string()) value["thumbnail"] = image["url"];
		return value;
	}
}

bool core::valid_source_url(const std::string &url)
{
	const auto start {url.find("://") + 3};
	if(!(url.starts_with("https://") || url.starts_with("http://")) || url.size() <= start ||
		url.find_first_of("\"\r\n \t\\") != std::string::npos || url.find('\0') != std::string::npos) return false;
	auto host {url.substr(start, url.find_first_of("/?#", start) - start)};
	if(const auto at {host.rfind('@')}; at != std::string::npos) host.erase(0, at + 1);
	if(host.starts_with('[')) return host.find(']') > 1 && host.find(']') != std::string::npos;
	return !host.substr(0, host.find(':')).empty();
}

std::string core::channel_source_url(const std::string &url, const std::string &category)
{
	if(!category.empty() && category != "videos" && category != "shorts" && category != "streams" && category != "playlists" && category != "podcasts") throw application_error {"invalidCategory", "web.invalid_payload"};
	if(!is_youtube_channel(to_wide(url))) return url;
	auto base {url.substr(0, url.find_first_of("?#"))};
	while(base.ends_with('/')) base.pop_back();
	const auto slash {base.rfind('/')};
	const auto tab {base.substr(slash + 1)};
	if(category.empty() && (tab == "videos" || tab == "shorts" || tab == "streams" || tab == "playlists" || tab == "podcasts")) return url;
	if(tab == "videos" || tab == "shorts" || tab == "streams" || tab == "playlists" || tab == "podcasts" || tab == "featured") base.erase(slash);
	return base + '/' + (category.empty() ? "videos" : category);
}

bool core::entry_available(const nlohmann::json &entry)
{
	if(!entry.is_object() || string_value(entry, "title").empty() || entry.contains("entries") ||
		string_value(entry, "_type") == "playlist" || string_value(entry, "ie_key") == "YoutubeTab") return false;
	const auto availability {string_value(entry, "availability")};
	return availability != "private" && availability != "needs_auth" && availability != "unavailable";
}

core::source_snapshot core::describe_source(std::string url, std::string resolved_url, std::string category,
	const nlohmann::json &info, bool single_video)
{
	if(!info.is_object()) throw application_error {"metadataFailed", "web.parse_failed"};
	const bool list {info.contains("entries")};
	if(single_video && list) throw application_error {"currentVideoMissing", "web.current_video_missing"};
	source_snapshot source;
	source.url = std::move(url);
	source.resolved_url = std::move(resolved_url);
	source.kind = !list ? "media" : is_youtube_channel(to_wide(source.url)) || !category.empty() ? "channel" : "playlist";
	source.category = source.kind == "channel" ? std::move(category) : std::string {};
	if(source.kind == "channel" && source.category.empty())
		for(const auto tab : {"videos", "shorts", "streams", "playlists", "podcasts"})
			if(source.resolved_url.substr(0, source.resolved_url.find_first_of("?#")).ends_with(std::string {"/"} + tab)) source.category = tab;
	source.metadata = describe_media(info, source.url);
	if(source.metadata["title"].get<std::string>().empty()) source.metadata["title"] = source.url;
	if(list && !info["entries"].is_array()) throw application_error {"metadataFailed", "web.parse_failed"};
	const auto entries {list ? info["entries"] : json::array({info})};
	if(source.kind == "channel" && source.category == "playlists")
	{
		// 频道目录中的播放列表不是可下载视频；选择后再走相同的来源解析管线。
		for(const auto &raw : entries)
			if(raw.is_object())
			{
				auto playlist_url {string_value(raw, "webpage_url")};
				if(!valid_source_url(playlist_url) || playlist_url == source.resolved_url) playlist_url = string_value(raw, "url");
				if(valid_source_url(playlist_url) && !string_value(raw, "title").empty())
					source.playlists.push_back({playlist_url, describe_media(raw, playlist_url)});
			}
		source.revision = 1;
		return source;
	}
	for(size_t n {0}; n < entries.size(); ++n)
	{
		const auto &raw {entries[n]};
		if(source.kind == "channel" && raw.is_object() && (raw.contains("entries") || string_value(raw, "ie_key") == "YoutubeTab" || string_value(raw, "_type") == "playlist"))
		{
			const auto playlist_url {string_value(raw, "webpage_url").empty() ? string_value(raw, "url") : string_value(raw, "webpage_url")};
			if(valid_source_url(playlist_url) && !string_value(raw, "title").empty()) source.playlists.push_back({playlist_url, describe_media(raw, playlist_url)});
			continue;
		}
		source_entry entry;
		entry.id = "entry-" + std::to_string(n + 1);
		entry.index = n + 1;
		if(raw.is_object())
		{
			entry.url = string_value(raw, "webpage_url");
			// Generic 多媒体页的网页地址相同，真正的下载单元是各个媒体 URL。
			if(!valid_source_url(entry.url) || entry.url == source.resolved_url && list) entry.url = string_value(raw, "url");
			if(entry.url.empty() && raw.contains("formats") && raw["formats"].is_array() && !raw["formats"].empty()) entry.url = string_value(raw["formats"].back(), "url");
			if(!list) entry.url = source.url;
			if(raw.contains("playlist_index") && raw["playlist_index"].is_number_unsigned() && raw["playlist_index"].get<std::uint64_t>()) entry.index = raw["playlist_index"];
			entry.media = {entry.url, describe_media(raw, entry.url)};
			if(entry.media.details["uploader"].is_null()) entry.media.details["uploader"] = source.metadata["uploader"];
			entry.available = entry_available(raw) && valid_source_url(entry.url);
		}
		else entry.media = {"", describe_media(json::object(), "")};
		entry.selected = !list && entry.available;
		if(!entry.available) entry.error = failure {"entryUnavailable", "web.entry_unavailable"};
		source.entries.push_back(std::move(entry));
	}
	if(!list && (source.entries.empty() || !source.entries.front().available)) throw application_error {"metadataFailed", "web.parse_failed"};
	source.revision = 1;
	return source;
}

void core::select_entries(source_snapshot &source, const std::vector<std::string> &ids)
{
	std::set<std::string> selected(ids.begin(), ids.end());
	for(const auto &entry : source.entries)
		if(selected.contains(entry.id))
		{
			if(!entry.available) throw application_error {"entryUnavailable", "web.entry_unavailable"};
			selected.erase(entry.id);
		}
	if(!selected.empty()) throw application_error {"entryNotFound", "web.invalid_payload"};
	selected.insert(ids.begin(), ids.end());
	for(auto &entry : source.entries) entry.selected = selected.contains(entry.id);
	++source.revision;
}

std::wstring core::playlist_template(std::wstring output, std::wstring prefix, bool folder, bool padding, size_t count, bool channel)
{
	if(!channel && !prefix.empty() && output.find(L"%(playlist_index") == std::wstring::npos)
	{
		const auto pos {prefix.find(L"%(playlist_index)d")};
		if(padding && count > 9 && pos != std::wstring::npos)
			prefix.replace(pos, 18, L"%(playlist_index)0" + std::to_wstring(std::to_string(count).size()) + L"d");
		output = prefix + output;
	}
	return (folder ? L"%(playlist_title)s\\" : L"") + output;
}

std::wstring core::context_arguments(const collection_context &context)
{
	std::wstring result;
	const std::vector<std::pair<std::string, std::string>> fields {
		{"playlist_title", context.title}, {"playlist", context.title}, {"playlist_id", context.id},
		{"playlist_index", std::to_string(context.index)}, {"n_entries", std::to_string(context.count)}};
	for(const auto &[field, value] : fields)
	{
		std::string replacement;
		for(const auto c : value) { if(c == '\\') replacement += '\\'; replacement += c; }
		result += L" --parse-metadata " + quote_argument(to_wide("title:" + field)) + L" --replace-in-metadata " +
			quote_argument(to_wide(field)) + L" \"^[\\s\\S]*$\" " + quote_argument(to_wide(replacement));
	}
	return result;
}

void core::download_collection::index_children()
{
	positions.clear(); current.clear();
	for(size_t n {0}; n < snapshot.children.size(); ++n)
	{
		positions[snapshot.children[n]] = n;
		if(contributions.contains(snapshot.children[n]) && active(contributions.at(snapshot.children[n]).status)) current.insert(n);
	}
}

void core::download_collection::observe(const task_snapshot &task)
{
	if(!positions.contains(task.id)) return;
	if(contributions.contains(task.id))
	{
		const auto &old {contributions.at(task.id)};
		--counts[size_t(old.status)];
		progress -= old.percent;
	}
	const auto percent {task.status == task_status::done ? 100.0 : task.percent};
	contributions[task.id] = {task.status, percent, task.media.details.value("title", ""), task.speed};
	++counts[size_t(task.status)];
	progress += percent;
	const auto position {positions.at(task.id)};
	if(active(task.status)) current.insert(position); else current.erase(position);
	snapshot.updated_at = std::max(snapshot.updated_at, task.updated_at);
	++snapshot.summary.revision;
}

void core::download_collection::forget(const std::string &id)
{
	if(!contributions.contains(id)) return;
	const auto &old {contributions.at(id)};
	--counts[size_t(old.status)];
	progress -= old.percent;
	current.erase(positions.at(id));
	contributions.erase(id);
	positions.erase(id);
	++snapshot.summary.revision;
}

core::collection_summary core::download_collection::summary() const
{
	collection_summary value;
	value.id = snapshot.id;
	value.revision = snapshot.summary.revision;
	value.total = contributions.size();
	auto count = [&](task_status status) { return counts[size_t(status)]; };
	value.completed = count(task_status::done); value.failed = count(task_status::failed);
	value.stopped = count(task_status::stopped); value.skipped = count(task_status::skipped); value.waiting = count(task_status::waiting);
	value.percent = value.total ? std::clamp(progress / value.total, 0.0, 100.0) : 0;
	value.can_stop = !current.empty(); value.can_remove = current.empty();
	value.can_select = current.empty() && !value.completed;
	value.can_start = value.waiting || value.stopped; value.can_retry = value.failed;
	value.status = count(task_status::running) ? "running" : count(task_status::processing) ? "processing" : count(task_status::stopping) ? "stopping" :
		value.waiting ? "waiting" : value.stopped ? "stopped" : value.failed ? (value.completed ? "partial" : "failed") :
		value.completed ? "done" : value.skipped ? "skipped" : "waiting";
	if(!current.empty())
	{
		const auto position {*current.begin()};
		value.current_task_id = snapshot.children.at(position);
		const auto &task {contributions.at(value.current_task_id)};
		value.current_index = position + 1; value.current_title = task.title; value.speed = task.speed;
	}
	return value;
}

void core::download_collection::remove_children(const std::vector<std::string> &ids)
{
	const std::set<std::string> removed(ids.begin(), ids.end());
	for(const auto &id : ids) forget(id);
	std::erase_if(snapshot.children, [&](const auto &id) { return removed.contains(id); });
	for(auto &entry : snapshot.source.entries)
		if(removed.contains(entry.task_id)) { entry.task_id.clear(); entry.selected = false; }
	index_children(); snapshot.updated_at = timestamp_now();
}

#include "task_model.hpp"
#include "core_text.hpp"
#include <chrono>

nlohmann::json core::queue_display_json(const std::string &url, const nlohmann::json &media, const task_result *result, bool collection)
{
	using json = nlohmann::json;
	auto text = [&](const char *key) { return media.contains(key) && media[key].is_string() ? media[key].get<std::string>() : std::string {}; };
	auto bytes = [&](const char *key) -> json
	{
		return media.contains(key) && media[key].is_number_integer() && media[key].get<double>() >= 0 ? media[key] : json(nullptr);
	};
	json display {{"format", collection ? "" : text("format_id")}, {"note", collection ? "" : text("resolution")},
		{"ext", collection ? "" : text("ext")}, {"size", nullptr}, {"approximate", false}, {"siteName", text("webpage_url_domain")}, {"siteIcon", ""}};
	if(!collection)
	{
		if(display["note"] == "") display["note"] = text("format_note");
		display["size"] = bytes("filesize");
		if(display["size"].is_null()) { display["size"] = bytes("filesize_approx"); display["approximate"] = !display["size"].is_null(); }
		if(result && !result->file.empty())
		{
			auto ext {to_utf8(result->file.extension().wstring())};
			display["ext"] = ext.empty() ? "" : ext.substr(1);
			std::error_code ec;
			const auto size {fs::file_size(result->file, ec)};
			display["size"] = ec ? json(nullptr) : json(size); display["approximate"] = false;
		}
	}
	const auto scheme {url.find("://")};
	if(scheme != std::string::npos && (url.starts_with("https://") || url.starts_with("http://")))
	{
		const auto origin {url.substr(0, url.find_first_of("/?#", scheme + 3))};
		if(origin.size() > scheme + 3 && origin.find('@') == std::string::npos) display["siteIcon"] = origin + "/favicon.ico";
	}
	return display;
}

const char *core::status_name(task_status status)
{
	switch(status)
	{
	case task_status::waiting: return "waiting";
	case task_status::running: return "running";
	case task_status::processing: return "processing";
	case task_status::stopping: return "stopping";
	case task_status::stopped: return "stopped";
	case task_status::done: return "done";
	case task_status::failed: return "failed";
	case task_status::skipped: return "skipped";
	}
	return "failed";
}

bool core::active(task_status status)
{
	return status == task_status::running || status == task_status::processing || status == task_status::stopping;
}

bool core::can_transition(task_status from, task_status to)
{
	if(from == to) return true;
	switch(from)
	{
	case task_status::waiting: return to == task_status::running || to == task_status::stopped || to == task_status::skipped;
	case task_status::running: return to == task_status::processing || to == task_status::stopping || to == task_status::done || to == task_status::failed;
	case task_status::processing: return to == task_status::running || to == task_status::stopping || to == task_status::done || to == task_status::failed;
	case task_status::stopping: return to == task_status::stopped;
	case task_status::failed: case task_status::stopped: case task_status::skipped: return to == task_status::waiting;
	case task_status::done: return false;
	}
	return false;
}

std::uint64_t core::timestamp_now()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

core::task_phase core::task_snapshot::phase() const
{
	switch(status)
	{
	case task_status::waiting: return task_phase::queued;
	case task_status::running: return task_phase::downloading;
	case task_status::processing: return task_phase::processing;
	case task_status::stopping: return task_phase::stopping;
	case task_status::skipped: return task_phase::skipped;
	default: return task_phase::finished;
	}
}

const char *core::phase_name(task_phase phase)
{
	switch(phase)
	{
	case task_phase::queued: return "queued";
	case task_phase::downloading: return "downloading";
	case task_phase::processing: return "processing";
	case task_phase::stopping: return "stopping";
	case task_phase::finished: return "finished";
	case task_phase::skipped: return "skipped";
	}
	return "finished";
}

bool core::accepts_execution(const task_snapshot &task, std::uint64_t execution_id)
{
	return task.execution_id == execution_id && (task.status == task_status::running || task.status == task_status::processing);
}

void core::change_status(task_snapshot &task, task_status status)
{
	if(!can_transition(task.status, status)) throw application_error {"invalidState", "web.task_busy"};
	task.status = status;
	task.updated_at = timestamp_now();
	++task.revision;
}

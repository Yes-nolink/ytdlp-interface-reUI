#pragma once

#include "download_application.hpp"
class windows_desktop;

struct web_context
{
	std::filesystem::path appdir, output_folder;
	std::string language;
};

// 只转换协议和 UI DTO，业务状态由应用服务持有。
class web_bridge
{
public:
	using json = nlohmann::json;
	web_bridge(core::download_application &application, windows_desktop &desktop, web_context context, std::function<void(const json &)> send);
	void handle(const json &message);
	static json event_json(const core::application_event &event);
	void backend_event(const json &event);
	void window_activated();
	void desktop_changed();
	void completion_tick();
	void cancel_background_completion();

private:
	core::download_application &application;
	windows_desktop &desktop;
	web_context context;
	std::function<void(const json &)> send;
	std::map<std::string, std::uint64_t> revisions;
	std::uint64_t queue_revision {0};
	bool clipboard_ready {false};
	void reply(const std::string &id, json payload = json::object());
	void error(const std::string &id, const std::string &code, const std::string &key);
	void emit(const std::string &name, json payload, const std::string &request_id = "");
	static json task_json(const core::task_snapshot &task);
	static json queue_json(const core::queue_snapshot &queue);
	static json persistence_json(const core::persistence_status &status);
	static json options_json(const core::output_options &options);
	static json source_json(const core::source_snapshot &source);
	static json collection_json(const core::collection_snapshot &collection);
	static json summary_json(const core::collection_summary &summary);
	static json group_json(const core::queue_group_snapshot &group);
	static core::output_options read_options(const json &options);
	static json completion_json(const core::completion_snapshot &snapshot);
};

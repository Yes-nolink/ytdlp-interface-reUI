#pragma once

#include "output_model.hpp"
#include <optional>
#include <stdexcept>

namespace core
{
	struct media_snapshot { std::string url; nlohmann::json details; };
	// 保留 v1 协议名称；waiting 是已入队，running 是下载执行阶段。
	enum class task_status { waiting, running, processing, stopping, stopped, done, failed, skipped };
	enum class task_type { media, collection };
	enum class task_phase { queued, downloading, processing, stopping, finished, skipped };
	const char *status_name(task_status status);
	const char *phase_name(task_phase phase);
	bool active(task_status status);
	bool can_transition(task_status from, task_status to);
	std::uint64_t timestamp_now();
	struct task_result { fs::path file; unsigned long exit_code {0}; std::vector<fs::path> files; };
	// 元数据描述与已报告的首个媒体产出；不从输出偏好推算实际格式。
	nlohmann::json queue_display_json(const std::string &url, const nlohmann::json &media, const task_result *result = nullptr, bool collection = false);
	struct task_snapshot
	{
		std::string id, url;
		media_snapshot media;
		output_options options;
		task_type type {task_type::media};
		// 子项始终是媒体 Task；逻辑集合另有模型，不拥有下载 execution。
		std::string group_id, parent_id;
		std::optional<collection_context> collection;
		task_status status {task_status::waiting};
		double percent {0};
		std::uint64_t revision {0}, execution_id {0};
		std::string progress_text, speed;
		std::optional<task_result> result;
		std::optional<failure> error;
		std::uint64_t created_at {0}, updated_at {0}, started_at {0}, finished_at {0};
		bool refreshing {false};
		task_phase phase() const;
		fs::path file() const { return result ? result->file : fs::path {}; }
		bool can_start() const { return !refreshing && status == task_status::waiting; }
		bool can_retry() const { return !refreshing && (status == task_status::failed || status == task_status::stopped); }
		bool can_stop() const { return status == task_status::running || status == task_status::processing; }
		bool can_remove() const { return !refreshing && !active(status); }
		bool can_edit() const { return !refreshing && !active(status) && status != task_status::done; }
	};
	bool accepts_execution(const task_snapshot &task, std::uint64_t execution_id);
	void change_status(task_snapshot &task, task_status status);
}

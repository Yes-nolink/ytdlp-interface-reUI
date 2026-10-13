#pragma once

#include "task_model.hpp"
#include <array>
#include <map>
#include <set>

namespace core
{
	// 来源解析与下载任务分开；条目身份和原始序号不会因选择子集而改变。
	struct source_entry
	{
		std::string id, url, task_id;
		std::uint64_t index {0};
		media_snapshot media;
		bool available {false}, selected {false};
		std::optional<failure> error;
	};
	struct source_snapshot
	{
		std::string id, url, resolved_url, kind, category;
		nlohmann::json metadata;
		std::vector<source_entry> entries;
		std::uint64_t revision {0};
		std::vector<media_snapshot> playlists;
	};
	bool valid_source_url(const std::string &url);
	std::string channel_source_url(const std::string &url, const std::string &category);
	bool entry_available(const nlohmann::json &entry);
	source_snapshot describe_source(std::string url, std::string resolved_url, std::string category,
		const nlohmann::json &info, bool single_video);
	void select_entries(source_snapshot &source, const std::vector<std::string> &ids);
	std::wstring playlist_template(std::wstring output, std::wstring prefix, bool folder, bool padding, size_t count, bool channel);
	std::wstring context_arguments(const collection_context &context);

	struct collection_summary
	{
		std::string id, status;
		size_t total {0}, completed {0}, failed {0}, stopped {0}, skipped {0}, waiting {0};
		double percent {0};
		std::string current_task_id, current_title, speed;
		size_t current_index {0};
		bool can_start {false}, can_stop {false}, can_retry {false}, can_remove {true}, can_select {true};
		std::uint64_t revision {0};
	};
	struct collection_snapshot
	{
		std::string id;
		source_snapshot source;
		output_options defaults;
		std::vector<std::string> children;
		collection_summary summary;
		std::uint64_t created_at {0}, updated_at {0};
	};
	// 缓存只由 Task 快照更新，计数采用差量；进度事件不携带完整条目列表。
	class download_collection
	{
	public:
		collection_snapshot snapshot;
		void observe(const task_snapshot &task);
		void forget(const std::string &task_id);
		void remove_children(const std::vector<std::string> &ids);
		void index_children();
		collection_summary summary() const;
	private:
		struct contribution { task_status status; double percent; std::string title, speed; };
		std::map<std::string, contribution> contributions;
		std::map<std::string, size_t> positions;
		std::set<size_t> current;
		std::array<size_t, 8> counts {};
		double progress {0};
	};
}

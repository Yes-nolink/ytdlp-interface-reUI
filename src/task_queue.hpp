#pragma once

#include "task_model.hpp"
#include "collection_model.hpp"
#include "queue_group.hpp"
#include <map>
#include <set>

namespace core
{
	struct download_task { task_snapshot snapshot; nlohmann::json metadata; };
	struct queue_snapshot
	{
		std::uint64_t revision {0};
		bool scheduling {false};
		unsigned max_concurrent_downloads {1};
		std::vector<task_snapshot> tasks;
		std::vector<collection_snapshot> collections;
		std::vector<std::string> items;
		size_t active_tasks {0};
		std::vector<queue_group_snapshot> groups;
		queue_group_snapshot ungrouped;
	};
	// 应用服务统一持锁访问；队列维护身份和顺序，不拥有 UI 或执行线程。
	class task_queue
	{
	public:
		download_task &add(std::string url, media_snapshot media, output_options options, nlohmann::json metadata);
		download_task &at(const std::string &id);
		const download_task &at(const std::string &id) const;
		bool contains(const std::string &id) const { return tasks.contains(id); }
		bool contains_url(const std::string &url) const { return urls.contains(url); }
		std::vector<task_snapshot> list() const;
		task_snapshot remove(const std::string &id);
		std::vector<task_snapshot> remove_many(const std::vector<std::string> &ids);
		void move(const std::string &id, size_t position);
		void move_group(const std::vector<std::string> &ids, size_t position);
		void restore(std::vector<task_snapshot> snapshots, std::uint64_t last_id);
		std::uint64_t last_id() const { return next_id; }
		std::uint64_t revision {0};
	private:
		std::map<std::string, download_task> tasks;
		std::set<std::string> urls;
		std::vector<std::string> order;
		std::uint64_t next_id {0};
	};
}

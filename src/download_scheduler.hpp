#pragma once

#include "task_model.hpp"
#include <chrono>
#include <set>

namespace core
{
	struct scheduler_options { unsigned max_concurrent_downloads {1}; bool continue_after_stop {true}; bool next_during_processing {false}; };
	class download_scheduler
	{
	public:
		explicit download_scheduler(scheduler_options options = {}) : options {options} {}
		scheduler_options options;
		bool enabled {false};
		void start(const std::string &id);
		void start_scope(const std::vector<std::string> &ids);
		void start_all();
		void stop_all();
			void finished(const std::string &id, task_status status);
			static constexpr std::chrono::milliseconds processing_delay {3000};
			bool occupies_slot(task_status status, std::chrono::milliseconds processing_duration) const
			{ return status != task_status::processing || !options.next_during_processing || processing_duration < processing_delay; }
		std::vector<std::string> select(const std::vector<task_snapshot> &tasks, size_t occupied);
		// Nana 将旧状态映射为独立快照后调用同一顺序选择规则。
		static std::vector<std::string> next_tasks(const std::vector<task_snapshot> &tasks,
			const std::string &current_id, size_t slots);
		static bool continue_queue(task_status status, bool continue_after_stop)
		{ return status != task_status::stopped || continue_after_stop; }
	private:
		std::string cursor;
		std::vector<std::string> priority;
		bool restricted {false};
		std::set<std::string> allowed;
	};
}

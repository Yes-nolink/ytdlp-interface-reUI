#include "download_scheduler.hpp"
#include <algorithm>

void core::download_scheduler::start(const std::string &id)
{
	enabled = true;
	if(restricted) allowed.insert(id);
	if(std::find(priority.begin(), priority.end(), id) == priority.end()) priority.push_back(id);
}

void core::download_scheduler::start_scope(const std::vector<std::string> &ids)
{
	// 范围只约束待启动 ID，已占用的执行槽位仍由同一 Scheduler 管理。
	if(!restricted || !enabled) allowed.clear();
	restricted = true;
	for(const auto &id : ids) start(id);
}

void core::download_scheduler::start_all() { enabled = true; restricted = false; allowed.clear(); cursor.clear(); priority.clear(); }
void core::download_scheduler::stop_all() { enabled = false; restricted = false; allowed.clear(); priority.clear(); }

void core::download_scheduler::finished(const std::string &id, task_status status)
{
	cursor = id;
	if(!continue_queue(status, options.continue_after_stop)) stop_all();
}

std::vector<std::string> core::download_scheduler::next_tasks(const std::vector<task_snapshot> &tasks,
	const std::string &current_id, size_t slots)
{
	std::vector<std::string> selected;
	if(tasks.empty()) return selected;
	const auto current {std::find_if(tasks.begin(), tasks.end(), [&](const auto &task) { return task.id == current_id; })};
	const size_t first {current == tasks.end() ? 0 : (size_t(current - tasks.begin()) + 1) % tasks.size()};
	for(size_t n {0}; n < tasks.size() && selected.size() < slots; ++n)
	{
		const auto &task {tasks[(first + n) % tasks.size()]};
		if(task.id != current_id && task.can_start()) selected.push_back(task.id);
	}
	return selected;
}

std::vector<std::string> core::download_scheduler::select(const std::vector<task_snapshot> &tasks, size_t occupied)
{
	std::vector<std::string> selected;
	if(!enabled || occupied >= options.max_concurrent_downloads) return selected;
	const auto slots {options.max_concurrent_downloads - occupied};
	for(const auto &id : priority)
	{
		const auto found {std::find_if(tasks.begin(), tasks.end(), [&](const auto &task) { return task.id == id; })};
		if(found != tasks.end() && (!restricted || allowed.contains(id)) && found->can_start() && selected.size() < slots) selected.push_back(id);
	}
	for(const auto &id : selected) std::erase(priority, id);
	if(!selected.empty()) cursor = selected.back();
	std::vector<task_snapshot> scoped;
	if(restricted) for(const auto &task : tasks) if(allowed.contains(task.id)) scoped.push_back(task);
	const auto &eligible {restricted ? scoped : tasks};
	for(const auto &id : next_tasks(eligible, cursor, slots))
		if(selected.size() < slots && std::find(selected.begin(), selected.end(), id) == selected.end()) selected.push_back(id);
	if(!selected.empty()) cursor = selected.back();
	else if(!occupied && std::none_of(eligible.begin(), eligible.end(), [](const auto &task) { return task.refreshing && task.status == task_status::waiting; })) enabled = false;
	return selected;
}

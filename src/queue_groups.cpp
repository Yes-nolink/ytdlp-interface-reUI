#include "download_application.hpp"
#include "core_text.hpp"
#include <algorithm>
#include <cctype>

std::string core::queue_group_name(std::string name)
{
	const auto blank = [](unsigned char ch) { return std::isspace(ch) != 0; };
	while(!name.empty() && blank(name.front())) name.erase(name.begin());
	while(!name.empty() && blank(name.back())) name.pop_back();
	if(name.empty() || to_wide(name).size() > 80 || std::any_of(name.begin(), name.end(), [](unsigned char ch) { return ch < 32 || ch == 127; }))
		throw application_error {"invalidGroupName", "web.group_invalid_name"};
	return name;
}

void core::retain_queue_groups(std::vector<queue_group> &groups, const std::vector<std::string> &items)
{
	for(auto &group : groups)
	{
		const std::set<std::string> members {group.items.begin(), group.items.end()};
		group.items.clear();
		for(const auto &id : items) if(members.contains(id)) group.items.push_back(id);
	}
}

std::vector<std::string> core::download_application::logical_items(const task_queue &tasks) const
{
	std::vector<std::string> items; std::set<std::string> seen;
	for(const auto &task : tasks.list())
	{
		const auto &id {task.parent_id.empty() ? task.id : task.parent_id};
		if(seen.insert(id).second) items.push_back(id);
	}
	return items;
}

void core::download_application::rebuild_group_summaries_locked()
{
	const auto items {logical_items(queue)};
	retain_queue_groups(custom_groups, items);
	item_groups.clear(); group_summaries.clear();
	group_summaries[""];
	for(const auto &group : custom_groups)
	{
		group_summaries[group.id].snapshot.id = group.id;
		for(const auto &id : group.items) item_groups[id] = group.id;
	}
	for(const auto &task : queue.list())
	{
		const auto &item {task.parent_id.empty() ? task.id : task.parent_id};
		group_summaries[item_groups.contains(item) ? item_groups.at(item) : ""].snapshot.children.push_back(task.id);
	}
	for(auto &[id, summary] : group_summaries)
	{
		summary.index_children();
		for(const auto &child : summary.snapshot.children) summary.observe(queue.at(child).snapshot);
	}
}

core::queue_group_snapshot core::download_application::group_dto(const std::string &id) const
{
	queue_group_snapshot result;
	if(id.empty())
	{
		for(const auto &item : logical_items(queue)) if(!item_groups.contains(item)) result.group.items.push_back(item);
	}
	else
	{
		const auto found {std::find_if(custom_groups.begin(), custom_groups.end(), [&](const auto &group) { return group.id == id; })};
		if(found == custom_groups.end()) throw application_error {"groupNotFound", "web.group_not_found"};
		result.group = *found;
	}
	if(group_summaries.contains(id)) result.summary = group_summaries.at(id).summary();
	result.summary.id = id; result.summary.revision = queue.revision;
	return result;
}

void core::download_application::group_task_event_locked(const task_snapshot &task, const std::string &name)
{
	const auto &item {task.parent_id.empty() ? task.id : task.parent_id};
	const auto id {item_groups.contains(item) ? item_groups.at(item) : ""};
	if(!group_summaries.contains(id)) return;
	auto &summary {group_summaries.at(id)};
	if(name == "taskRemoved") summary.forget(task.id); else summary.observe(task);
	application_event event {"queueGroupProgress"};
	// 进度事件仅传汇总，避免每次更新重发上千条成员及重建列表。
	event.group_progress = queue_group_snapshot {{id}, summary.summary()};
	event.group_progress->summary.revision = queue.revision;
	enqueue(std::move(event));
}

void core::download_application::save_structure_locked(const task_queue &tasks, const std::map<std::string, download_collection> &groups,
	const std::vector<queue_group> &custom, std::uint64_t last_id)
{
	auto saved {queue_data_locked(tasks, groups, option_scope, shared_options)};
	saved.last_group_id = last_id; saved.groups = custom;
	std::vector<std::string> retained;
	for(const auto &task : saved.tasks) if(task.parent_id.empty()) retained.push_back(task.id);
	for(const auto &collection : saved.collections) retained.push_back(collection.id);
	// 顺序来自当前逻辑队列，持久化仅剔除原保存规则不保留的完成/失败项目。
	const std::set<std::string> kept {retained.begin(), retained.end()};
	retained.clear();
	for(const auto &item : logical_items(tasks)) if(kept.contains(item)) retained.push_back(item);
	retain_queue_groups(saved.groups, retained);
	const auto previous_error {persistence.status.error.has_value()};
	if(persistence.status.enabled && (!persistence.status.writable || !persistence.save(saved)))
	{
		application_event event {"persistenceFailed"}; event.persistence = persistence.status; enqueue(std::move(event));
		throw application_error {"queueWriteFailed", "web.queue_save_failed"};
	}
	if(previous_error && !persistence.status.error) { application_event event {"persistenceRecovered"}; event.persistence = persistence.status; enqueue(std::move(event)); }
}

void core::download_application::commit_groups_locked(task_queue tasks, std::vector<queue_group> groups, std::uint64_t last_id)
{
	retain_queue_groups(groups, logical_items(tasks));
	save_structure_locked(tasks, collections, groups, last_id);
	queue = std::move(tasks); custom_groups = std::move(groups); next_group_id = last_id;
	rebuild_group_summaries_locked();
	++queue.revision; application_event event {"queueChanged"}; event.queue = queue_dto(); enqueue(std::move(event));
}

std::string core::download_application::create_queue_group(std::string name)
{
	std::lock_guard lock {mutex}; require_open(); name = queue_group_name(std::move(name));
	for(const auto &group : custom_groups) if(group.name == name) throw application_error {"duplicateGroupName", "web.group_duplicate_name"};
	const auto id {"queue-group-" + std::to_string(next_group_id + 1)};
	auto groups {custom_groups}; groups.push_back({id, std::move(name)});
	commit_groups_locked(queue, std::move(groups), next_group_id + 1);
	return id;
}

void core::download_application::update_queue_group(const std::string &id, std::string name, bool collapsed, size_t position)
{
	std::lock_guard lock {mutex}; require_open(); name = queue_group_name(std::move(name));
	auto groups {custom_groups};
	const auto found {std::find_if(groups.begin(), groups.end(), [&](const auto &group) { return group.id == id; })};
	if(found == groups.end()) throw application_error {"groupNotFound", "web.group_not_found"};
	if(position >= groups.size()) throw application_error {"invalidPayload", "web.invalid_payload"};
	for(const auto &group : groups) if(group.id != id && group.name == name) throw application_error {"duplicateGroupName", "web.group_duplicate_name"};
	auto updated {*found}; updated.name = std::move(name); updated.collapsed = collapsed;
	groups.erase(found); groups.insert(groups.begin() + position, std::move(updated));
	commit_groups_locked(queue, std::move(groups), next_group_id);
}

void core::download_application::delete_queue_group(const std::string &id)
{
	std::lock_guard lock {mutex}; require_open();
	auto groups {custom_groups};
	if(!std::erase_if(groups, [&](const auto &group) { return group.id == id; })) throw application_error {"groupNotFound", "web.group_not_found"};
	commit_groups_locked(queue, std::move(groups), next_group_id);
}

void core::download_application::assign_queue_group(const std::vector<std::string> &ids, const std::string &group_id)
{
	std::lock_guard lock {mutex}; require_open();
	const auto items {logical_items(queue)}; const std::set<std::string> valid {items.begin(), items.end()};
	for(const auto &id : ids) if(!valid.contains(id)) throw application_error {"invalidGroupMember", "web.group_invalid_member"};
	auto groups {custom_groups};
	if(!group_id.empty() && std::none_of(groups.begin(), groups.end(), [&](const auto &group) { return group.id == group_id; }))
		throw application_error {"groupNotFound", "web.group_not_found"};
	const std::set<std::string> selected {ids.begin(), ids.end()};
	for(auto &group : groups)
	{
		std::erase_if(group.items, [&](const auto &id) { return selected.contains(id); });
		if(group.id == group_id) group.items.insert(group.items.end(), selected.begin(), selected.end());
	}
	commit_groups_locked(queue, std::move(groups), next_group_id);
}

void core::download_application::move_item_locked(task_queue &tasks, const std::string &id, size_t position) const
{
	auto items {logical_items(tasks)};
	if(position >= items.size() || std::find(items.begin(), items.end(), id) == items.end()) throw application_error {"invalidPayload", "web.invalid_payload"};
	const auto children {collections.contains(id) ? collections.at(id).snapshot.children : std::vector<std::string> {tasks.at(id).snapshot.id}};
	std::erase(items, id);
	size_t task_position {0};
	for(size_t n {0}; n < position; ++n) task_position += collections.contains(items[n]) ? collections.at(items[n]).snapshot.children.size() : 1;
	tasks.move_group(children, task_position);
}

void core::download_application::move_group_item(const std::string &id, size_t position, const std::string &group_id)
{
	std::lock_guard lock {mutex}; require_open();
	auto tasks {queue}; move_item_locked(tasks, id, position);
	auto groups {custom_groups};
	if(!group_id.empty() && std::none_of(groups.begin(), groups.end(), [&](const auto &group) { return group.id == group_id; }))
		throw application_error {"groupNotFound", "web.group_not_found"};
	for(auto &group : groups) { std::erase(group.items, id); if(group.id == group_id) group.items.push_back(id); }
	commit_groups_locked(std::move(tasks), std::move(groups), next_group_id);
}

nlohmann::json core::download_application::operate_queue_scope(const std::string &action, const std::string &scope,
	const std::string &group_id, const std::vector<std::string> &ids)
{
	std::lock_guard lock {mutex}; require_open();
	const auto items {logical_items(queue)}; std::vector<std::string> selected;
	if(scope == "all") selected = items;
	else if(scope == "group") selected = group_dto(group_id).group.items;
	else if(scope == "selection")
	{
		const std::set<std::string> valid {items.begin(), items.end()};
		for(const auto &id : ids) if(!valid.contains(id) && !queue.contains(id)) throw application_error {"invalidGroupMember", "web.group_invalid_member"};
		selected = ids;
	}
	else throw application_error {"invalidScope", "web.invalid_payload"};
	if(action != "start" && action != "stop" && action != "retry" && action != "remove" && action != "clearCompleted")
		throw application_error {"invalidPayload", "web.invalid_payload"};
	std::vector<std::string> affected, ignored;
	for(const auto &id : expand_items(selected))
	{
		const auto &task {queue.at(id).snapshot};
		// 先检查整个范围，再执行；Remove 不能部分删除，Start/Stop 明确报告不适用项。
		if(task.refreshing || action == "remove" && !task.can_remove()) throw application_error {"busy", "web.task_busy"};
		bool eligible {action == "remove" || action == "start" && (task.can_start() || task.can_retry()) ||
			action == "retry" && task.can_retry() || action == "stop" && (task.can_stop() || task.can_start())};
		if(action == "stop" && scope == "all") eligible = task.can_stop();
		if(action == "clearCompleted") eligible = task.status == task_status::done && (task.parent_id.empty() || collections.at(task.parent_id).summary().status == "done");
		(eligible ? affected : ignored).push_back(id);
	}
	if(action == "start" && scope == "all") start_all_locked();
	else if(action == "stop" && scope == "all") stop_all_locked();
	else if(!affected.empty())
	{
		if(action == "start" || action == "retry") { scheduler.start_scope(affected); start_tasks_locked(affected); }
		else if(action == "stop") stop_tasks_locked(affected);
		else remove_tasks_locked(affected);
	}
	return {{"scope", scope}, {"groupId", group_id}, {"itemIds", selected}, {"affectedTaskIds", affected}, {"ignoredTaskIds", ignored}};
}

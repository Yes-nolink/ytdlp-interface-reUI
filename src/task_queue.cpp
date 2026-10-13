#include "task_queue.hpp"
#include <algorithm>

core::download_task &core::task_queue::add(std::string url, media_snapshot media, output_options options, nlohmann::json metadata)
{
	if(contains_url(url)) throw application_error {"duplicateTask", "web.duplicate_task"};
	urls.insert(url);
	download_task task;
	task.snapshot.id = "task-" + std::to_string(++next_id);
	task.snapshot.url = std::move(url);
	task.snapshot.media = std::move(media);
	task.snapshot.options = std::move(options);
	task.snapshot.created_at = task.snapshot.updated_at = timestamp_now();
	task.metadata = std::move(metadata);
	const auto id {task.snapshot.id};
	order.push_back(id);
	++revision;
	return tasks.emplace(id, std::move(task)).first->second;
}

core::download_task &core::task_queue::at(const std::string &id)
{
	if(!tasks.contains(id)) throw application_error {"taskNotFound", "web.task_not_found"};
	return tasks.at(id);
}

const core::download_task &core::task_queue::at(const std::string &id) const
{
	if(!tasks.contains(id)) throw application_error {"taskNotFound", "web.task_not_found"};
	return tasks.at(id);
}

std::vector<core::task_snapshot> core::task_queue::list() const
{
	std::vector<task_snapshot> result;
	for(const auto &id : order) result.push_back(at(id).snapshot);
	return result;
}

core::task_snapshot core::task_queue::remove(const std::string &id)
{
	return remove_many({id}).front();
}

std::vector<core::task_snapshot> core::task_queue::remove_many(const std::vector<std::string> &ids)
{
	const std::set<std::string> selected(ids.begin(), ids.end());
	for(const auto &id : selected) if(!at(id).snapshot.can_remove()) throw application_error {"busy", "web.task_busy"};
	std::vector<task_snapshot> removed;
	for(const auto &id : ids)
		if(tasks.contains(id))
		{
			auto snapshot {at(id).snapshot}; ++snapshot.revision; urls.erase(snapshot.url); tasks.erase(id); removed.push_back(std::move(snapshot));
		}
	std::erase_if(order, [&](const auto &id) { return selected.contains(id); });
	++revision;
	return removed;
}

void core::task_queue::move(const std::string &id, size_t position)
{
	at(id);
	if(position >= order.size()) throw application_error {"invalidPayload", "web.invalid_payload"};
	std::erase(order, id);
	order.insert(order.begin() + position, id);
	++revision;
}

void core::task_queue::restore(std::vector<task_snapshot> snapshots, std::uint64_t last_id)
{
	task_queue restored;
	restored.next_id = last_id;
	for(auto &snapshot : snapshots)
	{
		const auto id {snapshot.id};
		restored.order.push_back(id);
		restored.urls.insert(snapshot.url);
		auto metadata {snapshot.media.details};
		restored.tasks.emplace(id, download_task {std::move(snapshot), std::move(metadata)});
	}
	restored.revision = 1;
	*this = std::move(restored);
}

void core::task_queue::move_group(const std::vector<std::string> &ids, size_t position)
{
	const std::set<std::string> selected(ids.begin(), ids.end());
	for(const auto &id : ids) at(id);
	if(position > order.size() - selected.size()) throw application_error {"invalidPayload", "web.invalid_payload"};
	std::erase_if(order, [&](const auto &id) { return selected.contains(id); });
	order.insert(order.begin() + position, ids.begin(), ids.end());
	++revision;
}

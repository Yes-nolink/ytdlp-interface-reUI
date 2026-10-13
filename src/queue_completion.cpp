#include "download_application.hpp"
#include <algorithm>

nlohmann::json core::completion_json(const completion_snapshot &snapshot)
{
	nlohmann::json result {{"action", snapshot.action}, {"state", snapshot.state}, {"reason", snapshot.reason}, {"token", snapshot.token},
		{"revision", snapshot.revision}, {"queueRevision", snapshot.queue_revision}, {"seconds", snapshot.seconds}};
	if(snapshot.error) result["error"] = {{"code", snapshot.error->code}, {"messageKey", snapshot.error->message_key}, {"systemError", snapshot.error->exit_code}};
	return result;
}

void core::download_application::completion_event_locked()
{
	++completion.revision;
	application_event event {"completionChanged"}; event.completion = completion; enqueue(std::move(event));
}

void core::download_application::cancel_completion_locked(const std::string &reason)
{
	if(completion.state != "pending") return;
	completion.state = "cancelled"; completion.reason = reason; completion.seconds = 0;
	completion_event_locked();
}

void core::download_application::begin_completion_run_locked()
{
	cancel_completion_locked("workChanged");
	completion_started = std::any_of(executions.begin(), executions.end(), [](const auto &entry) { return !entry.second->done; });
	completion_success = completion_blocked = completion_consumed = false;
}

bool core::download_application::completion_eligible_locked() const
{
	if(closing || !completion_started || !completion_success || completion_blocked || completion_reaping || metadata_active || !parsing.empty() || !executions.empty()) return false;
	const auto tasks {queue.list()};
	return std::all_of(tasks.begin(), tasks.end(), [](const auto &task) { return task.status == task_status::done && !task.refreshing; });
}

void core::download_application::update_completion_locked()
{
	if(completion.state == "pending")
	{
		if(!completion_observable) cancel_completion_locked("background");
		else if(completion.queue_revision != queue.revision || !completion_eligible_locked()) cancel_completion_locked("workChanged");
		else
		{
			const auto remaining {completion_deadline - completion_now()};
			const auto seconds {unsigned(std::max<std::int64_t>(0, std::chrono::ceil<std::chrono::seconds>(remaining).count()))};
			if(completion.seconds != seconds) { completion.seconds = seconds; completion_event_locked(); }
		}
		return;
	}
	if(completion_consumed || !completion_eligible_locked()) return;
	completion_consumed = true;
	if(preferences.finish_action == "none") return;
	completion.action = preferences.finish_action; completion.error.reset(); completion.queue_revision = queue.revision;
	++completion.token; completion.seconds = completion_observable ? 15 : 0;
	completion.state = completion_observable ? "pending" : "cancelled";
	completion.reason = completion_observable ? "" : "background";
	completion_deadline = completion_now() + std::chrono::seconds {15};
	completion_event_locked();
}

core::completion_snapshot core::download_application::get_completion() const
{
	std::lock_guard lock {mutex}; return completion;
}

core::completion_snapshot core::download_application::set_finish_action(const std::string &action)
{
	std::lock_guard lock {mutex}; auto value {preferences}; value.finish_action = action;
	save_settings_locked(std::move(value)); return completion;
}

core::completion_snapshot core::download_application::poll_completion(bool observable)
{
	std::lock_guard lock {mutex}; completion_observable = observable; update_completion_locked(); return completion;
}

core::completion_snapshot core::download_application::cancel_completion(std::uint64_t token)
{
	std::lock_guard lock {mutex};
	if(completion_committing) throw application_error {"completionCommitted", "web.completion_committed"};
	if(completion.state == "pending" && completion.token == token) cancel_completion_locked("user");
	return completion;
}

std::optional<core::completion_snapshot> core::download_application::claim_completion(std::uint64_t token)
{
	std::lock_guard lock {mutex}; update_completion_locked();
	if(completion.state != "pending" || completion.token != token || completion_now() < completion_deadline || !completion_observable ||
		preferences.finish_action != completion.action || !completion_eligible_locked()) return {};
	completion_committing = true; completion.state = "executing"; completion.seconds = 0; completion_event_locked(); return completion;
}

void core::download_application::complete_action(std::uint64_t token, std::optional<failure> error)
{
	std::lock_guard lock {mutex};
	if(!completion_committing || completion.token != token) return;
	completion_committing = false; completion.state = error ? "failed" : "executed"; completion.error = std::move(error); completion_event_locked();
}

#pragma once

#include "task_queue.hpp"
#include "download_scheduler.hpp"
#include "queue_persistence.hpp"
#include "collection_model.hpp"
#include "download_preferences.hpp"
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace core
{
	struct task_log_snapshot
	{
		std::string task_id, command, text;
		std::uint64_t execution_id {0}, revision {0};
		bool truncated {false};
			static constexpr size_t capacity {1024 * 1024};
			void append(const std::string &chunk);
	};
	struct completion_snapshot
	{
		std::string action {"none"}, state {"idle"}, reason;
		std::uint64_t token {0}, revision {0}, queue_revision {0};
		unsigned seconds {0};
		std::optional<failure> error;
	};
	nlohmann::json completion_json(const completion_snapshot &snapshot);
	struct application_event
	{
		std::string name, request_id, url;
		std::optional<media_snapshot> media;
		std::optional<task_snapshot> task;
		std::optional<failure> error;
		std::optional<queue_snapshot> queue;
		std::optional<persistence_status> persistence;
		std::optional<source_snapshot> source;
		std::optional<collection_snapshot> collection;
		std::optional<collection_summary> collection_progress;
		std::optional<task_log_snapshot> log;
		std::optional<completion_snapshot> completion;
		std::optional<queue_group_snapshot> group_progress;
		std::string diagnostic;
		std::optional<component_snapshot> component;
		std::optional<component_update_snapshot> component_update;
		nlohmann::json release_notes;
	};

	// 命令、持久化与查询共享锁，事件在调度线程中按入队顺序交付。
	class download_application
	{
	public:
		using event_callback = std::function<void(const application_event &)>;
		using completion_clock = std::function<std::chrono::steady_clock::time_point()>;
				download_application(download_settings settings, event_callback callback, scheduler_options options = {}, fs::path queue_file = {}, fs::path settings_file = {}, output_options defaults = {}, std::string language = "zh-CN", completion_clock clock = std::chrono::steady_clock::now, component_environment components = {}, bool deferred_start = false);
		~download_application();
		void parse_media(const std::string &url, const std::string &request_id);
		void parse_source(const std::string &url, const std::string &request_id, const std::string &category = "", bool single_video = false, const std::string &source_id = "");
		size_t cancel_source_parse(const std::string &request_id);
		nlohmann::json parse_sources(const std::string &text, const std::string &request_id, bool single_video = false, std::optional<size_t> input_index = {});
		source_snapshot get_source(const std::string &id) const;
		source_snapshot select_source(const std::string &id, const std::vector<std::string> &entries);
		struct source_tasks { std::string collection_id; std::vector<task_snapshot> tasks; };
		source_tasks create_source_tasks(const std::string &source_id, output_options options, bool grouped, const std::string &request_id = "");
		collection_snapshot get_collection(const std::string &id) const;
		void start_collection(const std::string &id);
		void stop_collection(const std::string &id);
		void retry_collection(const std::string &id);
		void remove_collection(const std::string &id);
		void split_collection(const std::string &id);
		void move_collection(const std::string &id, size_t position);
		collection_snapshot select_collection(const std::string &id, const std::vector<std::string> &entries);
		collection_snapshot update_collection_defaults(const std::string &id, output_options options);
		void skip_tasks(const std::vector<std::string> &ids, bool skipped);
		void invert_downloadable(const std::vector<std::string> &ids);
		task_snapshot create_task(const std::string &url, output_options options, const std::string &request_id = "");
		void start_task(const std::string &id);
		void stop_task(const std::string &id);
		void retry_task(const std::string &id);
		void start_all();
		void stop_all();
		void start_tasks(const std::vector<std::string> &ids);
		void stop_tasks(const std::vector<std::string> &ids);
		void remove_task(const std::string &id);
		void remove_tasks(const std::vector<std::string> &ids);
		void clear_completed();
		void move_task(const std::string &id, size_t position);
		void move_queue_item(const std::string &id, size_t position);
		std::string create_queue_group(std::string name);
		void update_queue_group(const std::string &id, std::string name, bool collapsed, size_t position);
		void delete_queue_group(const std::string &id);
		void assign_queue_group(const std::vector<std::string> &ids, const std::string &group_id);
		void move_group_item(const std::string &id, size_t position, const std::string &group_id);
		nlohmann::json operate_queue_scope(const std::string &action, const std::string &scope,
			const std::string &group_id = "", const std::vector<std::string> &ids = {});
			task_snapshot update_options(const std::string &id, output_options options);
			nlohmann::json copy_task_processing_options(const std::string &id);
			nlohmann::json set_task_options_scope(const std::string &scope, const std::string &source_id = "");
			output_options prepare_task_options(output_options options) const;
		nlohmann::json get_media_details(const std::string &id_or_url) const;
		nlohmann::json preview_options(const output_options &options, const std::string &id_or_url = "") const;
		void refresh_metadata(const std::vector<std::string> &ids, const std::string &request_id = "");
		task_snapshot choose_formats(const std::string &id, const std::vector<std::string> &formats, bool multi_audio);
		task_snapshot choose_subtitles(const std::string &id, const std::vector<std::string> &languages, const std::string &format, bool future_default = false);
		task_snapshot set_sections(const std::string &id, const std::vector<time_section> &sections);
		task_snapshot override_filename(const std::string &id, const std::string &filename);
		task_log_snapshot get_task_log(const std::string &id) const;
			download_preferences get_settings() const;
		nlohmann::json get_settings_snapshot() const;
		std::string clipboard_source(std::string text) const;
		std::string visible_text(std::string text) const;
		download_preferences save_settings(download_preferences preferences);
		naming_preset save_naming_preset(const std::string &output_template);
		std::string save_download_preset(output_options options);
		void save_application_preset(const std::string &name, bool replace = false);
		void rename_application_preset(const std::string &name, const std::string &new_name);
		void delete_application_preset(const std::string &name);
		void apply_application_preset(const std::string &name);
		persistence_status get_settings_status() const;
		task_snapshot get_task(const std::string &id) const;
		std::vector<task_snapshot> get_tasks() const;
		queue_snapshot get_queue() const;
		persistence_status get_persistence_status() const;
		bool component_available() const;
		nlohmann::json get_components() const;
		void check_components(const std::vector<std::string> &ids = {}, const std::string &request_id = "", bool latest = true);
		void set_component_path(const std::string &id, const std::string &path, const std::string &request_id);
		void set_component_channel(const std::string &channel);
		void set_component_update_preferences(bool startup, bool ffplay);
		nlohmann::json get_component_updates() const;
		nlohmann::json prepare_component_update(const std::string &id, const std::string &request_id = "");
		void confirm_component_update(const std::string &update_id, std::uint64_t generation, bool custom_path_confirmed);
		void cancel_component_update(const std::string &update_id, std::uint64_t generation);
		void recover_component_update(const std::string &update_id, std::uint64_t generation);
		void load_component_release_notes(const std::string &id, unsigned page, const std::string &request_id);
		completion_snapshot get_completion() const;
		completion_snapshot set_finish_action(const std::string &action);
		completion_snapshot poll_completion(bool observable);
		completion_snapshot cancel_completion(std::uint64_t token);
		std::optional<completion_snapshot> claim_completion(std::uint64_t token);
		void complete_action(std::uint64_t token, std::optional<failure> error = {});
		void activate();
		void shutdown();

	private:
		struct parse_operation { std::atomic_bool working {true}; bool done {false}; std::thread worker; std::string request_id, source_id; };
		struct execution
		{
			std::string task_id;
			std::uint64_t generation;
			std::atomic_bool working {true};
			bool done {false};
				bool interrupted_by_shutdown {false};
				std::chrono::steady_clock::time_point processing_started {};
			std::thread worker;
		};
		download_settings settings;
		event_callback callback;
		mutable std::mutex mutex;
			std::condition_variable wake;
			std::condition_variable metadata_wake;
			unsigned metadata_active {0};
			std::uint64_t settings_revision {0};
			std::string active_language;
			std::vector<std::string> sensitive_values;
		std::atomic_bool closing {false};
		std::atomic_bool activated {false};
		bool dirty {false};
		std::map<std::string, std::shared_ptr<parse_operation>> parsing;
		component_environment component_context;
		std::map<std::string, component_snapshot> components;
		std::map<std::string, std::shared_ptr<parse_operation>> component_operations;
		std::map<std::string, component_update_snapshot> component_updates;
		std::uint64_t next_update_generation {0};
		bool component_committing {false}, component_recovery_blocked {false};
		std::map<std::string, nlohmann::json> media;
		std::map<std::string, source_snapshot> sources;
		std::map<std::string, download_collection> collections;
		std::uint64_t next_source_id {0}, next_collection_id {0};
		std::uint64_t next_group_id {0};
		std::vector<queue_group> custom_groups;
		// 仅缓存原 Task 的汇总贡献，不拥有调度、身份或执行生命周期。
		std::map<std::string, download_collection> group_summaries;
		std::map<std::string, std::string> item_groups;
		task_queue queue;
		download_scheduler scheduler;
		queue_persistence persistence;
		settings_persistence preferences_store;
			download_preferences preferences;
			std::string option_scope {"individual"};
			std::optional<output_options> shared_options;
		std::map<std::string, task_log_snapshot> logs;
		std::map<std::string, std::string> metadata_commands;
		std::map<std::string, std::shared_ptr<execution>> executions;
		std::deque<application_event> events;
		std::thread dispatcher;
		completion_clock completion_now;
		completion_snapshot completion;
		std::chrono::steady_clock::time_point completion_deadline;
		bool completion_started {false}, completion_success {false}, completion_blocked {false}, completion_consumed {true}, completion_observable {false}, completion_committing {false};
		size_t completion_reaping {0};
		void begin_completion_run_locked();
		void cancel_completion_locked(const std::string &reason);
		void completion_event_locked();
		bool completion_eligible_locked() const;
		void update_completion_locked();
		void dispatch();
		void reset_components_locked();
		void component_event_locked(const component_snapshot &snapshot, const std::string &request_id = "");
		void component_update_event_locked(component_update_snapshot snapshot, const std::string &request_id = "");
		void restore_component_updates_locked();
		void require_component_idle_locked() const;
		bool components_block_execution_locked() const;
		component_update_snapshot component_update_target_locked(const component_snapshot &snapshot) const;
		component_update_snapshot &component_update_at_locked(const std::string &update_id, std::uint64_t generation);
				void execute(std::shared_ptr<execution> operation, download_request request, download_settings execution_settings, output_options execution_options);
			std::string query_metadata_limited(const fs::path &ytdlp, const std::wstring &arguments, std::atomic_bool &working);
			std::string redact(std::string text) const;
			nlohmann::json visible_details(nlohmann::json value) const;
			void apply_settings_locked();
		download_preferences save_settings_locked(download_preferences value);
			void cleanup_completed_locked();
		void append_log(const std::string &id, std::uint64_t generation, const std::string &text);
		void enqueue(application_event event);
		void task_event(const std::string &name, const task_snapshot &snapshot, const std::string &request_id = "");
		void queue_event(bool full = true);
		void collection_event(download_collection &collection, const std::string &name);
		collection_snapshot collection_dto(const download_collection &collection) const;
		void detach_tasks(const std::vector<task_snapshot> &snapshots);
		download_collection &collection_at(const std::string &id);
		std::vector<std::string> expand_items(const std::vector<std::string> &ids) const;
		source_tasks create_source_locked(const source_snapshot &source, output_options options, bool grouped, const std::string &request_id);
			void persist_locked();
			persisted_queue queue_data_locked(const task_queue &tasks, const std::map<std::string, download_collection> &groups,
				const std::string &scope, const std::optional<output_options> &shared) const;
			nlohmann::json commit_options_locked(task_queue tasks, std::map<std::string, download_collection> groups,
				std::string scope, std::optional<output_options> shared, const std::string &request_id = "");
			void copy_processing_locked(task_queue &tasks, std::map<std::string, download_collection> &groups, const output_options &source) const;
			output_options processing_source_locked(const std::string &id) const;
		void restore_queue();
		queue_snapshot queue_dto() const;
		void require_open() const;
		void reset_for_retry(task_snapshot &snapshot);
		void stop_locked(const std::string &id);
		void start_all_locked();
		void stop_all_locked();
		void start_tasks_locked(const std::vector<std::string> &expanded);
		void stop_tasks_locked(const std::vector<std::string> &expanded);
		void remove_tasks_locked(const std::vector<std::string> &expanded);
		std::vector<std::string> logical_items(const task_queue &tasks) const;
		void rebuild_group_summaries_locked();
		queue_group_snapshot group_dto(const std::string &id) const;
		void group_task_event_locked(const task_snapshot &task, const std::string &name);
		void commit_groups_locked(task_queue tasks, std::vector<queue_group> groups, std::uint64_t last_id);
		void save_structure_locked(const task_queue &tasks, const std::map<std::string, download_collection> &groups,
			const std::vector<queue_group> &custom, std::uint64_t last_id);
		void move_item_locked(task_queue &tasks, const std::string &id, size_t position) const;
		static void validate_options(const output_options &options);
		static media_snapshot media_dto(const std::string &url, const nlohmann::json &info);
		download_request request_for(const download_task &task) const;
	};
}

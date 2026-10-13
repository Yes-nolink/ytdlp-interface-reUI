#pragma once

#include "task_model.hpp"
#include "queue_group.hpp"
#include <memory>

namespace core
{
	// 磁盘 DTO 只保存恢复任务所需的数据，与运行快照、线程和执行代次分开。
	struct persisted_output
	{
		std::string kind, format, quality, naming, folder;
		bool cover {false}, metadata {false};
		std::string output_template, playlist_prefix {"%(playlist_index)d - "};
		bool zero_padding {true}, playlist_folder {true}, single_video {false};
		processing_options processing;
		std::string bandcamp_template;
	};
	struct persisted_context
	{
		std::string url, id, title;
		std::uint64_t index {0}, count {0};
		bool channel {false};
	};
	struct persisted_result { std::string file; unsigned long exit_code {0}; std::vector<std::string> files; };
	struct persisted_task
	{
		std::string id, url, type, group_id, parent_id, status;
		nlohmann::json media;
		persisted_output output;
		std::optional<failure> error;
		std::uint64_t created_at {0}, updated_at {0}, started_at {0}, finished_at {0};
		std::optional<persisted_context> context;
		std::optional<persisted_result> result;
	};
	struct persisted_entry
	{
		std::string id, url, task_id;
		std::uint64_t index {0};
		nlohmann::json media;
		bool available {false}, selected {false};
	};
	struct persisted_collection
	{
		std::string id, url, resolved_url, kind, category;
		nlohmann::json metadata;
		persisted_output defaults;
		std::vector<persisted_entry> entries;
		std::vector<std::string> children;
		std::uint64_t created_at {0}, updated_at {0};
	};
	struct persisted_queue
	{
			static constexpr unsigned schema_version {6};
		std::uint64_t last_task_id {0}, last_collection_id {0};
		std::vector<persisted_task> tasks;
			std::vector<persisted_collection> collections;
			std::string option_scope {"individual"};
			std::optional<persisted_output> shared_options;
		std::uint64_t last_group_id {0};
		std::vector<queue_group> groups;
	};
	struct persistence_status
	{
		fs::path file;
		bool enabled {false}, writable {false};
		size_t restored_tasks {0};
		std::optional<failure> error;
	};
	fs::path default_queue_file();
	nlohmann::json persistent_media(const nlohmann::json &details);
	task_status restored_status(const std::string &status);
	class queue_persistence
	{
	public:
		explicit queue_persistence(fs::path file);
		~queue_persistence();
		std::optional<persisted_queue> load();
		bool save(const persisted_queue &queue);
		void reject_restore(const std::string &code = "queueInvalidData");
		persistence_status status;
	private:
		struct file_lock;
		std::unique_ptr<file_lock> lock;
	};
}

#pragma once

#include "download_core.hpp"
#include "task_model.hpp"

namespace core
{
	struct component_settings
	{
		std::string ytdlp_path, ffmpeg_directory, ytdlp_channel {"nightly"};
		bool check_at_startup {false}, include_ffplay {false};
	};
	struct component_paths { fs::path ytdlp, ffmpeg, deno; };
	struct component_snapshot
	{
		std::string id, path, companion_path, channel, current_version, latest_version;
		std::string installation {"unknown"}, status {"notChecked"}, comparison {"unknown"}, source;
		std::uint64_t checked_at {0};
		std::optional<failure> error, local_error;
		unsigned http_status {0};
		bool test_source {false};
		nlohmann::json release;
		std::string install_block;
		bool install_available {false};
	};
	struct release_response { std::string body; unsigned status {0}; unsigned long system_error {0}; std::string error; };
	struct component_environment
	{
		fs::path appdir;
		std::string channel {"nightly"};
		// 测试替代只接受本机 HTTP，生产查询地址仍由业务层固定。
		std::string test_origin;
		fs::path update_root, test_root;
		std::string test_fault;
	};
	struct component_package
	{
		std::string component, tag, repository, asset, url, sha256, architecture, version;
		std::uint64_t size {0};
		bool zip {false};
	};
	struct component_update_snapshot
	{
		std::string component, update_id, state {"waitingConfirmation"}, failed_stage, backup_directory, result_version;
		std::uint64_t generation {0}, revision {0}, downloaded {0}, total {0}, completed_files {0}, total_files {0};
		component_package package;
		std::vector<fs::path> targets;
		std::vector<std::string> original_hashes;
		std::vector<std::string> staged_hashes;
		std::optional<failure> error;
		std::optional<failure> recovery_error;
		bool cancellable {true}, custom_path {false}, include_ffplay {false}, recovery_required {false}, test_source {false};
	};
	using component_update_callback = std::function<void(const component_update_snapshot &)>;
	nlohmann::json component_update_json(const component_update_snapshot &snapshot);
	component_package select_component_package(const component_snapshot &snapshot, const nlohmann::json &release, const std::string &architecture);
	std::string native_component_architecture();
	std::string native_component_update_id();
	std::string component_file_hash(const fs::path &path, std::atomic_bool &working);
	std::string component_file_architecture(const fs::path &path);
	component_update_snapshot prepare_component_install(component_update_snapshot snapshot, const component_environment &environment, const std::string &proxy, std::atomic_bool &working, bool inspect_only = false);
	component_update_snapshot execute_component_install(component_update_snapshot snapshot, const component_environment &environment, const std::string &proxy, std::atomic_bool &working, const component_update_callback &progress, const std::function<void()> &enter_commit);
	std::vector<component_update_snapshot> read_component_transactions(const component_environment &environment);
	component_update_snapshot recover_component_install(component_update_snapshot snapshot, const component_environment &environment, const component_update_callback &progress);
	release_response component_http_get(const std::string &url, const std::string &proxy, const component_environment &environment, std::atomic_bool &working, std::uint64_t limit, const std::function<void(const char *, size_t, std::uint64_t)> &sink = {});
	std::string component_tool_version(const fs::path &path, const std::string &tool, std::atomic_bool &working, bool require_static = false);
	nlohmann::json component_settings_json(const component_settings &settings);
	component_settings read_component_settings(const nlohmann::json &value);
	void validate_component_settings(const component_settings &settings);
	nlohmann::json component_json(const component_snapshot &snapshot);
	std::string component_source(const std::string &id, const std::string &channel);
	std::string compare_component_versions(const std::string &id, const std::string &current, const std::string &latest);
	void read_component_release(component_snapshot &snapshot, const release_response &response);
	component_environment native_component_environment(fs::path appdir = {});
	component_paths resolve_component_paths(const fs::path &appdir, const component_settings &settings);
	component_snapshot probe_component(component_snapshot snapshot, std::atomic_bool &working);
	void validate_component_path(const std::string &id, const fs::path &path, std::atomic_bool &working);
	release_response query_component_release(const std::string &source, const std::string &proxy, const component_environment &environment);
}

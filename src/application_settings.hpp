#pragma once

#include "output_model.hpp"
#include "download_scheduler.hpp"
#include <algorithm>

namespace core
{
	unsigned metadata_cpu_limit();
	struct network_settings { bool proxy_enabled {false}; std::string proxy; };
	struct authentication_settings { std::string mode {"none"}, browser {"chrome"}, browser_options, cookie_file; };
	struct external_downloader_settings { bool enabled {false}; std::string arguments {"-x 6"}; };
	struct content_processing_settings
	{
		bool mark {false}, remove {false};
		std::vector<std::string> mark_categories, remove_categories;
	};
	struct application_scheduler_settings
	{
		unsigned max_downloads {1}, max_metadata {std::min(4u, metadata_cpu_limit())};
		bool next_during_processing {false}, next_after_stop {true};
	};
	struct queue_behavior_settings { bool auto_start_recovered {false}, restore_failed {true}, remove_completed {false}; };
	struct application_behavior_settings
	{
		bool clipboard_on_focus {false}, custom_filenames {false};
		std::string language {"zh-CN"}, folder_origin {"current"};
	};
	// 只保存表现层偏好；Windows 行为由 Web Desktop Service 消费。
	struct queue_display_settings
	{
		bool format {false}, note {true}, ext {true}, size {false}, site_icon {true}, site_text {false};
	};
	struct appearance_settings
	{
		std::string theme {"system"};
		unsigned contrast {0};
		bool snap {true}, no_min_width {false}, exact_bytes {false};
		queue_display_settings queue_display;
	};
	struct application_settings
	{
		network_settings network;
		authentication_settings authentication;
		external_downloader_settings external_downloader;
		content_processing_settings content_processing;
		application_scheduler_settings scheduler;
		queue_behavior_settings queue_behavior;
		application_behavior_settings application_behavior;
		appearance_settings appearance;
	};

	void validate_application_settings(const application_settings &settings);
	nlohmann::json application_settings_json(const application_settings &settings, bool disk = false);
	application_settings read_application_settings(const nlohmann::json &value, bool disk = false);
	application_settings application_settings_from_execution(const download_settings &settings, scheduler_options scheduler);
	download_settings application_execution_settings(download_settings settings, const application_settings &application);
	fs::path aria2c_path(const fs::path &ytdlp);
	std::vector<std::string> application_sensitive_values(const application_settings &settings);
	std::string redact_values(std::string text, const std::vector<std::string> &values);
}

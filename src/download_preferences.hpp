#pragma once

#include "queue_persistence.hpp"
#include "application_settings.hpp"
#include "components.hpp"

namespace core
{
	struct output_preset { std::string name; output_options options; };
	struct argument_history { std::string label, arguments; };
	struct naming_preset { std::string name, output_template; };
	struct application_config_preset { std::string name; output_options defaults; application_settings application; };
	struct download_preferences
	{
		output_options defaults;
		std::vector<output_preset> presets;
		std::vector<argument_history> arguments;
		std::vector<std::string> folders;
		application_settings application;
		std::vector<naming_preset> naming_presets;
		std::vector<application_config_preset> application_presets;
		std::string finish_action {"none"};
		component_settings components;
	};
	nlohmann::json application_preset_json(const application_config_preset &preset, bool disk = false);
	nlohmann::json preferences_json(const download_preferences &preferences, bool disk = false);
	download_preferences read_preferences(const nlohmann::json &data, bool disk = false);
	void validate_preferences(const download_preferences &preferences);
	fs::path default_settings_file();
	class settings_persistence
	{
	public:
		explicit settings_persistence(fs::path file);
		~settings_persistence();
		std::optional<download_preferences> load(application_settings defaults = {}, component_settings components = {});
		bool save(const download_preferences &preferences);
		persistence_status status;
	private:
		struct file_lock;
		std::unique_ptr<file_lock> lock;
	};
}

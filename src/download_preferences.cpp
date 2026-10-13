#include "download_preferences.hpp"
#include "core_text.hpp"
#include "naming_model.hpp"
#include <Windows.h>
#include <fstream>
#include <set>

namespace
{
	nlohmann::json output_json(const core::output_options &options, bool disk)
	{
		auto value {core::output_options_json(options)};
		if(disk)
		{
			value["processing"].erase("rawArgs");
			value["processing"]["rawArgsProtected"] = core::protect_text(options.processing.raw_args);
		}
		return value;
	}
	core::output_options read_output(nlohmann::json value, bool disk)
	{
		if(disk) value["processing"]["rawArgs"] = core::unprotect_text(value.at("processing").at("rawArgsProtected").get<std::string>());
		return core::read_output_options(value);
	}
}

nlohmann::json core::application_preset_json(const application_config_preset &preset, bool disk)
{
	auto application {application_settings_json(preset.application, disk)}; application.erase("appearance");
	return {{"name", preset.name}, {"defaults", output_json(preset.defaults, disk)}, {"application", std::move(application)}};
}

nlohmann::json core::preferences_json(const download_preferences &preferences, bool disk)
{
	nlohmann::json result {{"defaults", output_json(preferences.defaults, disk)}, {"presets", nlohmann::json::array()}, {"arguments", nlohmann::json::array()}, {"folders", preferences.folders}};
	for(const auto &preset : preferences.presets) result["presets"].push_back({{"name", preset.name}, {"options", output_json(preset.options, disk)}});
		for(const auto &entry : preferences.arguments) result["arguments"].push_back({{"label", entry.label}, {disk ? "argumentsProtected" : "arguments", disk ? protect_text(entry.arguments) : entry.arguments}});
		result["application"] = application_settings_json(preferences.application, disk);
	result["namingPresets"] = nlohmann::json::array(); result["applicationPresets"] = nlohmann::json::array();
	result["finishAction"] = preferences.finish_action;
	result["components"] = component_settings_json(preferences.components);
	for(const auto &preset : preferences.naming_presets) result["namingPresets"].push_back({{"name", preset.name}, {"outputTemplate", preset.output_template}});
	for(const auto &preset : preferences.application_presets) result["applicationPresets"].push_back(application_preset_json(preset, disk));
	return result;
}

core::download_preferences core::read_preferences(const nlohmann::json &data, bool disk)
{
	download_preferences result; result.defaults = read_output(data.at("defaults"), disk);
	result.finish_action = data.value("finishAction", std::string {"none"});
	if(data.contains("components")) result.components = read_component_settings(data.at("components"));
	for(const auto &preset : data.at("presets")) result.presets.push_back({preset.at("name"), read_output(preset.at("options"), disk)});
		for(const auto &entry : data.at("arguments")) result.arguments.push_back({entry.at("label"), disk ? unprotect_text(entry.at("argumentsProtected").get<std::string>()) : entry.at("arguments").get<std::string>()});
		if(data.contains("application")) result.application = read_application_settings(data.at("application"), disk);
	if(data.contains("namingPresets")) for(const auto &preset : data.at("namingPresets")) result.naming_presets.push_back({preset.at("name"), preset.at("outputTemplate")});
	if(data.contains("applicationPresets")) for(const auto &preset : data.at("applicationPresets")) result.application_presets.push_back({preset.at("name"), read_output(preset.at("defaults"), disk), read_application_settings(preset.at("application"), disk)});
	result.folders = data.at("folders").get<std::vector<std::string>>(); validate_preferences(result); return result;
}

void core::validate_preferences(const download_preferences &preferences)
{
		if(preferences.finish_action != "none" && preferences.finish_action != "exit" && preferences.finish_action != "sleep" && preferences.finish_action != "hibernate")
			throw application_error {"invalidFinishAction", "web.invalid_payload"};
		validate_output_options(preferences.defaults);
			validate_application_settings(preferences.application);
		validate_component_settings(preferences.components);
	const auto task_specific = [](const output_options &options) { return !options.processing.filename.empty() || !options.processing.format_ids.empty() || !options.processing.sections.empty(); };
	if(task_specific(preferences.defaults)) throw application_error {"taskSpecificDefault", "web.task_specific_default"};
	std::set<std::string> names;
	for(const auto &preset : preferences.presets)
	{
		validate_output_options(preset.options);
		if(preset.name.empty() || !names.insert(preset.name).second || task_specific(preset.options)) throw application_error {"invalidPreset", "web.invalid_preset"};
	}
	for(const auto &folder : preferences.folders)
		if(!fs::u8path(folder).is_absolute()) throw application_error {"invalidPayload", "web.invalid_payload"};
	names.clear();
	for(const auto &preset : preferences.naming_presets)
	{
		validate_naming_template(preset.output_template);
		if(preset.name.empty() || preset.output_template.empty() || !names.insert(preset.name).second) throw application_error {"invalidPreset", "web.invalid_preset"};
	}
	names.clear(); std::set<std::string> configurations;
	for(const auto &preset : preferences.application_presets)
	{
		validate_output_options(preset.defaults); validate_application_settings(preset.application);
		if(task_specific(preset.defaults) || !names.insert(preset.name).second) throw application_error {"duplicatePresetName", "web.duplicate_preset_name"};
		auto config {application_preset_json(preset)}; config.erase("name");
		if(!configurations.insert(config.dump()).second) throw application_error {"duplicatePresetConfig", "web.duplicate_preset_config"};
	}
}

core::fs::path core::default_settings_file()
{
	const auto env = [](const wchar_t *name)
	{
		const auto size {GetEnvironmentVariableW(name, nullptr, 0)}; if(!size) return fs::path {};
		std::wstring value(size, L'\0'); const auto count {GetEnvironmentVariableW(name, value.data(), size)};
		value.resize(count); return fs::path {value};
	};
	if(const auto path {env(L"YTDLP_SETTINGS_FILE")}; !path.empty()) return path;
	return env(L"LOCALAPPDATA") / "ytdlp-interface" / "CoreSettings" / "download-v1.json";
}

struct core::settings_persistence::file_lock
{
	HANDLE handle {INVALID_HANDLE_VALUE};
	~file_lock() { if(handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
core::settings_persistence::settings_persistence(fs::path file) : status {std::move(file)} {}
core::settings_persistence::~settings_persistence() = default;

std::optional<core::download_preferences> core::settings_persistence::load(application_settings defaults, component_settings components)
{
	if(status.file.empty()) return {};
	status.enabled = true;
	try
	{
		if(!status.file.is_absolute()) throw std::runtime_error {"settingsPath"};
		fs::create_directories(status.file.parent_path()); lock = std::make_unique<file_lock>();
		lock->handle = CreateFileW((status.file.wstring() + L".lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if(lock->handle == INVALID_HANDLE_VALUE) throw std::runtime_error {"settingsLock"};
		status.writable = true;
		if(!fs::exists(status.file)) return {};
		std::ifstream input {status.file, std::ios::binary}; const auto data {nlohmann::json::parse(input)};
			if(!data.at("schemaVersion").is_number_unsigned()) throw std::runtime_error {"settingsVersion"};
			const auto version {data.at("schemaVersion").get<unsigned>()};
				if(version != 1 && version != 2 && version != 3) throw std::runtime_error {"settingsVersion"};
				auto download {data.at("download")};
				if(version == 3)
				{
					if(!download.at("namingPresets").is_array() || !data.at("applicationPresets").is_array()) throw std::runtime_error {"settingsPresets"};
					download["applicationPresets"] = data.at("applicationPresets");
				}
				auto preferences {read_preferences(download, true)};
				if(!download.contains("components")) preferences.components = std::move(components);
				preferences.application = version == 1 ? defaults : read_application_settings(data.at("application"), true);
				validate_preferences(preferences);
			return preferences;
	}
	catch(const std::exception &) { status.writable = false; status.error = failure {"settingsRestoreFailed", "web.settings_restore_failed"}; return {}; }
}

bool core::settings_persistence::save(const download_preferences &preferences)
{
	if(!status.enabled) return true;
	if(!status.writable) return false;
	HANDLE file {INVALID_HANDLE_VALUE};
	try
	{
		validate_preferences(preferences);
				auto download {preferences_json(preferences, true)}; auto presets {download.at("applicationPresets")}; download.erase("application"); download.erase("applicationPresets");
				const auto bytes {nlohmann::json {{"schemaVersion", 3}, {"download", download}, {"application", application_settings_json(preferences.application, true)}, {"applicationPresets", presets}}.dump(2) + '\n'};
		const auto temporary {status.file.wstring() + L".tmp"};
		file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		DWORD written {0};
		if(file == INVALID_HANDLE_VALUE || !WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) || written != bytes.size() || !FlushFileBuffers(file)) throw std::runtime_error {"settingsWrite"};
		const auto closed {CloseHandle(file)}; file = INVALID_HANDLE_VALUE;
		if(!closed || !MoveFileExW(temporary.c_str(), status.file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error {"settingsReplace"};
		status.error.reset(); return true;
	}
	catch(const std::exception &)
	{
		const auto error {GetLastError()}; if(file != INVALID_HANDLE_VALUE) CloseHandle(file);
		status.error = failure {"settingsWriteFailed", "web.settings_save_failed", error}; return false;
	}
}

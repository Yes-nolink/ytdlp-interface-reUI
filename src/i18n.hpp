#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>
#include "json.hpp"


namespace i18n
{
	std::string load(const std::filesystem::path &directory, const std::string &language);
	void load_embedded(const std::string &language);
	std::string text(const std::string &key);
	std::wstring wide(const std::string &key);
	std::string status(const std::string &value);
	const nlohmann::json &dictionary();
	std::vector<std::pair<std::string, std::string>> languages(const std::filesystem::path &directory);
}

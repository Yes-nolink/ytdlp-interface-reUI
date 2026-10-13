#pragma once
#include "json.hpp"

namespace core
{
	nlohmann::json about_information(const std::string &runtime_version = {});
	std::string about_link(const std::string &id);
}

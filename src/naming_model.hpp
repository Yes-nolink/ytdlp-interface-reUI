#pragma once

#include "output_model.hpp"

namespace core
{
	void validate_naming_template(const std::string &value);
	std::string ordinary_template(const output_options &options);
	nlohmann::json naming_catalog();
	nlohmann::json preview_naming(const nlohmann::json &draft);
}

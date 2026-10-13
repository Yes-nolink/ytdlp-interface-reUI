#pragma once

#include <string>
#include <string_view>

namespace core
{
	std::wstring to_wide(std::string_view text);
	std::string to_utf8(std::wstring_view text);
	bool is_utf8(std::string_view text);
	std::wstring quote_argument(std::wstring_view text);
}

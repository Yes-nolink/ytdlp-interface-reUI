#include "core_text.hpp"
#include <Windows.h>
#include <stdexcept>

std::wstring core::to_wide(std::string_view text)
{
	if(text.empty()) return {};
	const auto size {MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0)};
	if(!size) throw std::invalid_argument("UTF-8");
	std::wstring result(size, L'\0');
	MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);
	return result;
}

std::string core::to_utf8(std::wstring_view text)
{
	if(text.empty()) return {};
	const auto size {WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr)};
	if(!size) throw std::invalid_argument("UTF-16");
	std::string result(size, '\0');
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
	return result;
}

bool core::is_utf8(std::string_view text)
{
	return text.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) != 0;
}

std::wstring core::quote_argument(std::wstring_view text)
{
	std::wstring result {L"\""};
	size_t slashes {0};
	for(const auto c : text)
	{
		if(c == L'\\') { ++slashes; continue; }
		result.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
		result += c;
		slashes = 0;
	}
	result.append(slashes * 2, L'\\');
	return result + L'"';
}

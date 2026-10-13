#pragma once
#include <string>
#include <vector>

struct startup_options
{
	std::wstring url;
	std::string error;
};

inline startup_options read_startup_options(const std::vector<std::wstring> &arguments)
{
	startup_options result;
	bool selected {false};
	for(const auto &argument : arguments)
	{
		if(argument == L"--web-ui")
		{
			if(selected) { result.error = "web.startup.arguments"; break; }
			selected = true;
		}
		else if(result.url.empty() && (argument.starts_with(L"https://") || argument.starts_with(L"http://"))) result.url = argument;
		else { result.error = "web.startup.arguments"; break; }
	}
	return result;
}

#pragma once
#include <filesystem>
#include <string>
int run_web_ui(const std::filesystem::path &appdir, const std::string &ytdlp_filename, const std::wstring &initial_url = {});

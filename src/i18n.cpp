#include "i18n.hpp"
#include "json.hpp"
#include <fstream>
#include "core_text.hpp"
#include <Windows.h>


namespace i18n
{
	static nlohmann::json strings = nlohmann::json::object();
	static std::vector<std::pair<std::string, std::string>> status_labels;

	void load_embedded(const std::string &language)
	{
		// 外部语言文件不可用时，仅用于显示启动错误；不把缺失资源当作正常启动。
		const auto module {GetModuleHandleW(nullptr)};
		const auto resource {FindResourceW(module, MAKEINTRESOURCEW(language == "en-US" ? 202 : 201), RT_RCDATA)};
		if(!resource) return;
		const auto memory {LoadResource(module, resource)};
		const auto data {static_cast<const char *>(LockResource(memory))};
		if(!data) return;
		const auto document {nlohmann::json::parse(data, data + SizeofResource(module, resource), nullptr, false)};
		if(!document.is_discarded()) strings = document.value("strings", nlohmann::json::object());
	}

	std::string load(const std::filesystem::path &directory, const std::string &language)
	{
		strings = nlohmann::json::object();
		status_labels.clear();
		auto read = [&](const std::string &code)
		{
			std::ifstream file {directory / (code + ".json")};
			if(!file) throw std::runtime_error((directory / (code + ".json")).string());
			nlohmann::json document;
			file >> document;
			const auto &values {document.at("strings")};
			if(!values.is_object()) throw std::runtime_error(code + ".json: strings");
			for(const auto &entry : values.items())
				if(!entry.value().is_string()) throw std::runtime_error(code + ".json: " + entry.key());
			for(const auto key : {"ui.queued", "ui.skip", "ui.done", "ui.stopped", "ui.stopped_2", "ui.processing", "ui.downloading", "ui.error"})
				if(values.contains(key)) status_labels.emplace_back(values[key].get<std::string>(), key);
			return values;
		};
		try { strings = read("zh-CN"); }
		catch(const std::exception &) {}
		try
		{
			strings.update(read(language));
		}
		catch(const std::exception &error) { return error.what(); }
		for(const auto &available : languages(directory))
			if(available.first != "zh-CN" && available.first != language)
				try { read(available.first); }
				catch(const std::exception &) {}
		return {};
	}


	std::string text(const std::string &key)
	{
		const auto entry {strings.find(key)};
		return entry == strings.end() ? key : entry->get<std::string>();
	}


	std::wstring wide(const std::string &key)
	{
		return core::to_wide(text(key));
	}


	std::string status(const std::string &value)
	{
		std::size_t matched {0};
		std::string key;
		for(const auto &label : status_labels)
			if(label.first.size() > matched && value.starts_with(label.first))
			{
				matched = label.first.size();
				key = label.second;
			}
		return key.empty() ? value : text(key) + value.substr(matched);
	}


	const nlohmann::json &dictionary() { return strings; }


	std::vector<std::pair<std::string, std::string>> languages(const std::filesystem::path &directory)
	{
		std::vector<std::pair<std::string, std::string>> result;
		std::error_code error;
		for(const auto &entry : std::filesystem::directory_iterator {directory, error})
		{
			if(entry.path().extension() != ".json") continue;
			try
			{
				std::ifstream file {entry.path()};
				nlohmann::json document;
				file >> document;
				result.emplace_back(entry.path().stem().string(), document.at("name").get<std::string>());
			}
			catch(const nlohmann::json::exception &) {}
		}
		return result;
	}
}

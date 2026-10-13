#pragma once

#include "download_core.hpp"
#include <stdexcept>

namespace core
{
	struct failure { std::string code, message_key; unsigned long exit_code {0}; };
	class application_error : public std::runtime_error
	{
	public:
		failure detail;
		application_error(std::string code, std::string key) : std::runtime_error {code}, detail {std::move(code), std::move(key)} {}
	};
	struct time_section { std::string start, end; };
	// 下载意图是值对象；raw_args 保留用户原文，edited 只记录显式替换的参数组。
	struct processing_options
	{
		std::string video_codec {"none"}, audio_codec {"none"}, m4a_mode {"convert"};
		bool higher_fps {false}, premium {true}, write_time {true}, keyframes {false};
		std::string rate, rate_unit {"M"}, chapters {"ignore"}, subtitles {"keep"}, retries {"keep"}, fragments {"keep"};
		bool embed_subs {false}, keep_files {true}, windows_names {true}, save_cover {false}, save_info {false}, save_description {false}, save_comments {false}, live_start {false};
		bool raw_enabled {false};
		std::string raw_args, sub_langs, sub_format;
		std::vector<std::string> edited, format_ids;
		bool multi_audio {false};
		std::vector<time_section> sections;
		std::string filename;
	};
	struct output_options
	{
		std::string kind, format, quality, naming;
		fs::path output_folder;
		bool cover {false}, metadata {false};
		std::string output_template, playlist_prefix {"%(playlist_index)d - "};
		bool zero_padding {true}, playlist_folder {true}, single_video {false};
		processing_options processing;
		std::string bandcamp_template {"%(artist)s - %(album)s - %(track_number)02d - %(track)s.%(ext)s"};
	};
	output_options default_output_options(const fs::path &folder);
	void validate_output_options(const output_options &options);
	void validate_media_options(const output_options &options, const nlohmann::json &metadata);
	void validate_sections(const std::vector<time_section> &sections, const nlohmann::json &media = {});
	double section_seconds(const std::string &text);
	nlohmann::json processing_json(const processing_options &options);
	processing_options read_processing(const nlohmann::json &value);
	nlohmann::json output_options_json(const output_options &options);
	output_options read_output_options(const nlohmann::json &value);
	std::string output_arguments(const output_options &options);
	std::string output_preset_name(const output_options &options);
	bool single_video_requested(const output_options &options);
		output_options filename_options(output_options options, const std::string &filename);
		output_options copy_processing_options(output_options target, const output_options &source);
		bool processing_only_change(const output_options &before, const output_options &after);
		bool processing_scope_changed(const output_options &before, output_options after);
	nlohmann::json preview_output(const output_options &options);
	download_settings settings_for_output(download_settings settings, const output_options &options);
	// 持久化的自由参数使用当前 Windows 用户的 DPAPI；日志和元数据出口隐藏认证值。
	std::string protect_text(const std::string &text);
	std::string unprotect_text(const std::string &text);
	std::string redact_text(const std::string &text);
	nlohmann::json visible_metadata(nlohmann::json value);
}

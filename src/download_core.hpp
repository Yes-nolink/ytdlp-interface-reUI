#pragma once

#include "json.hpp"
#include "process.hpp"
#include <filesystem>
#include <vector>
#include <optional>

namespace core
{
	namespace fs = std::filesystem;
	inline const std::vector<std::wstring>
		resolution_options {L"none", L"4320", L"2160", L"1440", L"1080", L"720", L"480", L"360", L"240", L"144"},
		audio_options {L"none", L"m4a", L"mp3", L"ogg", L"webm", L"flac", L"opus"},
		video_options {L"none", L"mp4", L"webm", L"mkv"},
		video_codec_options {L"none", L"av01", L"vp9.2", L"vp9", L"h265", L"h264", L"vp8", L"h263", L"theora"},
		audio_codec_options {L"none", L"flac", L"alac", L"wav", L"aiff", L"opus", L"vorbis", L"aac", L"mp4a", L"mp3", L"ac4", L"eac3", L"ac3", L"dts"},
		sponsor_mark_options {L"all", L"sponsor", L"intro", L"outro", L"selfpromo", L"preview", L"filler", L"interaction", L"music_offtopic", L"poi_highlight", L"chapter", L"hook"},
		sponsor_remove_options {L"all", L"sponsor", L"intro", L"outro", L"selfpromo", L"preview", L"filler", L"interaction", L"music_offtopic", L"hook"},
		cookie_browser_options {L"none", L"brave", L"chrome", L"chromium", L"edge", L"firefox", L"opera", L"safari", L"vivaldi", L"whale"};

	// 只包含执行所需值。主题、窗口、控件、线程和旧队列不进入业务边界。
	struct download_settings
	{
		fs::path ytdlp_path, ffmpeg_path, cookies_path, aria_path, deno_path;
		std::wstring output_template {L"%(title)s.%(ext)s"}, playlist_indexing {L"%(playlist_index)d - "},
			output_template_bandcamp {L"%(artist)s - %(album)s - %(track_number)02d - %(track)s.%(ext)s"}, proxy;
		std::string cookie_options, aria_options {"-x 6"}, sub_langs, sub_format;
		unsigned pref_res {0}, pref_video {0}, pref_audio {0}, pref_vcodec {0}, pref_acodec {0}, cookie_browser {0};
		bool aria {false}, cookies {false}, pref_fps {false}, premium {true}, sponsor_mark {false}, sponsor_remove {false},
			use_proxy {false}, playlist_folder {true}, zero_padding {true}, use_ffmpeg_location {true}, application_owned_configuration {false};
		std::vector<int> sponsor_mark_categories, sponsor_remove_categories;
	};

	struct collection_context
	{
		std::string url, id, title;
		std::uint64_t index {0}, count {0};
		bool channel {false};
	};
	struct download_request
	{
		std::wstring url, format, video_format, audio_format, playlist_items;
		fs::path output_folder, output_file;
		nlohmann::json media;
		std::string arguments, rate, sub_langs, sub_format;
		std::vector<std::pair<std::wstring, std::wstring>> sections;
		unsigned rate_unit {0}, chapters {0};
		size_t selected_media_count {0};
		bool youtube {false}, youtube_playlist {false}, youtube_channel {false}, channel_tab {false},
			soundcloud_playlist {false}, playlist {false}, bandcamp_playlist {false}, bandcamp_channel {false},
			use_format {false}, use_arguments {false}, embed_subtitles {false}, embed_thumbnail {false},
			mtime_is_write_time {false}, force_keyframes {false}, convert_mp3 {false};
		bool custom_format() const;
		bool has_media_value(const std::string &key) const { return media.contains(key) && !media[key].is_null(); }
		std::optional<collection_context> collection;
	};

	enum class download_stage { downloading, processing };
	struct download_plan { std::wstring command, display_command; fs::path result_file; };
	struct download_result { unsigned long exit_code {259}; bool stopped {false}; fs::path file; std::string output; std::vector<fs::path> files; };

	download_plan build_download(const download_settings &settings, const download_request &request);
	download_result execute_download(const download_plan &plan, std::atomic_bool &working,
		append_callback append = nullptr, progress_callback progress = nullptr,
		std::function<void(download_stage)> stage = nullptr, std::atomic_bool *graceful_exit = nullptr,
		std::function<void(const fs::path &)> produced_file = nullptr);
	// 元数据进程入口供 Application 与 Nana 共用，不携带集合展示或调度状态。
	std::string query_metadata(const fs::path &ytdlp, const std::wstring &arguments, std::atomic_bool &working);
	std::wstring metadata_arguments(const download_settings &settings, const std::wstring &url,
		bool youtube, bool single_json, bool approximate_filesize);
	std::wstring metadata_format_sort(const download_settings &settings);
	std::wstring cookie_arguments(const download_settings &settings);
	bool is_youtube(std::wstring url);
	bool is_youtube_channel(std::wstring url);
	bool is_soundcloud_playlist(std::wstring url);
	bool ffmpeg_in_config(const fs::path &ytdlp);
	std::string arguments_without_label(const std::string &text);
}

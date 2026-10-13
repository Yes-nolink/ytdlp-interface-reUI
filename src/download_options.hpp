#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <vector>
#include "i18n.hpp"


struct download_options
{
	struct choice { std::string label, args; };
	struct flag { std::string label, option, opposite, help; };
	static inline const std::array<std::vector<choice>, 4> choices {{
		{
			{"ui.use_main_window_format", ""},
			{"ui.best_video_and_audio", "-f \"bv*+ba/b\""},
			{"ui.prefer_mp4_video", "-f \"bv*[ext=mp4]+ba[ext=m4a]/b[ext=mp4]/b\" --merge-output-format mp4"},
			{"ui.best_original_audio", "-f ba"},
			{"ui.convert_to_m4a_audio", "-f \"ba/b\" -x --audio-format m4a --audio-quality 0"},
			{"ui.convert_audio_to_mp3", "-f \"ba/b\" -x --audio-format mp3 --audio-quality 0"}
		},
		{
			{"ui.keep_current_subtitle_languages", ""},
			{"ui.save_chinese_subtitles_manual", "--write-subs --sub-langs \"zh.*\" --convert-subs srt"},
			{"ui.save_chinese_subtitles_including_auto_generated", "--write-subs --write-auto-subs --sub-langs \"zh.*\" --convert-subs srt"},
			{"ui.save_chinese_and_english_subtitles", "--write-subs --write-auto-subs --sub-langs \"zh.*,en.*\" --convert-subs srt"},
			{"ui.save_subtitles_in_all_languages", "--write-subs --write-auto-subs --sub-langs all --convert-subs srt"}
		},
		{
			{"ui.use_default_retries", ""}, {"ui.retry_3_times", "--retries 3 --fragment-retries 3"},
			{"ui.retry_10_times", "--retries 10 --fragment-retries 10"},
			{"ui.retry_30_times", "--retries 30 --fragment-retries 30"},
			{"ui.keep_retrying", "--retries infinite --fragment-retries infinite"}
		},
		{
			{"ui.use_default_concurrency", ""}, {"ui.download_1_fragment_at_a_time", "-N 1"}, {"ui.download_4_fragments_at_a_time", "-N 4"},
			{"ui.download_8_fragments_at_a_time", "-N 8"}, {"ui.download_16_fragments_at_a_time", "-N 16"}
		}
	}};
	static inline const std::vector<flag> flags {
		{"ui.embed_thumbnail", "--embed-thumbnail", "--no-embed-thumbnail", "ui.embed_artwork_in_the_video_or_audio_for_players_to_display_requires_ffmp"},
		{"ui.embed_title_and_author", "--embed-metadata", "--no-embed-metadata", "ui.write_the_title_author_and_other_metadata_into_the_media_file"},
		{"ui.save_artwork_separately", "--write-thumbnail", "--no-write-thumbnail", "ui.save_an_artwork_image_alongside_the_downloaded_file"},
		{"ui.save_video_information", "--write-info-json", "--no-write-info-json", "ui.save_a_video_information_file_for_archiving_and_later_processing"},
		{"ui.save_video_description", "--write-description", "--no-write-description", "ui.save_the_video_description_as_a_separate_text_file"},
		{"ui.save_comments", "--write-comments", "--no-write-comments", "ui.save_comments_in_the_video_information_file_this_may_take_longer_to_retr"},
		{"ui.embed_subtitles", "--embed-subs", "--no-embed-subs", "ui.embed_subtitles_in_the_selected_languages_availability_depends_on_the_we"},
		{"ui.keep_existing_files", "--no-overwrites", "", "ui.keep_an_existing_file_when_its_name_matches_the_downloaded_file"},
		{"ui.keep_unicode_and_use_windows_filenames", "--windows-filenames", "--no-windows-filenames", "ui.keep_unicode_characters_and_replace_characters_that_windows_does_not_all"},
		{"ui.download_only_the_video_in_a_playlist_link", "--no-playlist", "--yes-playlist", "ui.download_only_the_video_when_a_url_contains_both_a_video_and_a_playlist"},
		{"ui.download_live_streams_from_the_start", "--live-from-start", "--no-live-from-start", "ui.download_from_the_beginning_of_a_live_stream_when_supported_by_yt_dlp_an"}
	};
	std::array<size_t, 4> selected {};
	std::array<std::string, 4> original_groups;
	std::vector<bool> enabled = std::vector<bool>(flags.size(), false);
	std::vector<bool> specified = std::vector<bool>(flags.size(), false);
	std::vector<std::string> disabled_flags = std::vector<std::string>(flags.size());
	std::string extra, thumbnail_conversion;

	static std::vector<std::string> tokens(const std::string &text)
	{
		std::vector<std::string> result;
		for(size_t pos {0}; pos < text.size();)
		{
			while(pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
			const auto start {pos};
			bool quoted {false};
			unsigned slashes {0};
			while(pos < text.size() && (quoted || !std::isspace(static_cast<unsigned char>(text[pos]))))
			{
				const auto c {text[pos++]};
				if(c == '"' && slashes % 2 == 0) quoted = !quoted;
				slashes = c == '\\' ? slashes + 1 : 0;
			}
			if(pos > start) result.push_back(text.substr(start, pos - start));
		}
		return result;
	}
	static std::string unquote(const std::string &text)
	{
		return text.size() >= 2 && text.front() == '"' && text.back() == '"' ? text.substr(1, text.size() - 2) : text;
	}
	static void append(std::string &text, const std::string &part)
	{
		if(part.empty()) return;
		if(!text.empty()) text += ' ';
		text += part;
	}
	static std::vector<std::string> normalized(const std::string &text)
	{
		auto result {tokens(text)};
		for(auto &token : result)
		{
			token = unquote(token);
			if(token == "--format") token = "-f";
			else if(token == "--extract-audio") token = "-x";
			else if(token == "--concurrent-fragments") token = "-N";
		}
		return result;
	}
	void read(const std::string &text)
	{
		*this = download_options {};
		const auto args {tokens(text)};
		for(size_t i {0}; i < args.size(); ++i)
		{
			auto key {unquote(args[i])};
			const auto equals {key.find('=')};
			const bool inline_value {key.starts_with("--") && equals != std::string::npos};
			if(inline_value) key.resize(equals);
			bool handled {false};
			for(size_t n {0}; n < flags.size(); ++n)
			{
				const auto &flag {flags[n]};
				if(!inline_value && (key == flag.option || (!flag.opposite.empty() && key == flag.opposite) || (n == 7 && (key == "-w" || key == "--force-overwrites" || key == "--no-force-overwrites"))))
				{
					enabled[n] = key == flag.option || key == "-w";
					specified[n] = true;
					disabled_flags[n] = enabled[n] ? "" : args[i];
					handled = true;
					break;
				}
			}
			if(handled) continue;
			int group {-1};
			bool takes_value {true};
			const bool attached {key.size() > 2 && !key.starts_with("--") && (key.starts_with("-f") || key.starts_with("-N") || key.starts_with("-S"))};
			if(attached) key.resize(2);
			if(key == "-f" || key == "--format" || key == "-S" || key == "--format-sort" || key == "--audio-format" ||
				key == "--audio-quality" || key == "--merge-output-format" || key == "--remux-video" || key == "--recode-video") group = 0;
			else if(key == "-x" || key == "--extract-audio" || key == "--no-extract-audio") { group = 0; takes_value = false; }
			else if(key == "--sub-langs" || key == "--sub-format" || key == "--convert-subs") group = 1;
			else if(key == "--write-subs" || key == "--write-auto-subs" || key == "--no-write-subs" ||
				key == "--no-write-auto-subs" || key == "--all-subs") { group = 1; takes_value = false; }
			else if(key == "--retries" || key == "--fragment-retries") group = 2;
			else if(key == "-N" || key == "--concurrent-fragments") group = 3;
			const bool conversion {key == "--convert-thumbnails"};
			if(group >= 0 || conversion)
			{
				if(takes_value && !inline_value && !attached && i + 1 == args.size()) { append(extra, args[i]); continue; }
				auto part {args[i]};
				if(takes_value && !inline_value && !attached) append(part, args[++i]);
				append(conversion ? thumbnail_conversion : original_groups[group], part);
			}
			else append(extra, args[i]);
		}
		for(size_t group {0}; group < choices.size(); ++group)
		{
			const auto &list {choices[group]};
			selected[group] = list.size();
			for(size_t n {0}; n < list.size(); ++n)
				if(normalized(original_groups[group]) == normalized(list[n].args)) { selected[group] = n; break; }
		}
	}
	std::string group_args(size_t group) const
	{
		return selected[group] < choices[group].size() ? choices[group][selected[group]].args : original_groups[group];
	}
	std::string command_line() const
	{
		std::string result {extra};
		for(size_t group {0}; group < choices.size(); ++group) append(result, group_args(group));
		for(size_t n {0}; n < flags.size(); ++n) append(result, enabled[n] ? flags[n].option : disabled_flags[n]);
		append(result, enabled[0] ? "--convert-thumbnails jpg" : thumbnail_conversion);
		return result;
	}
	std::string label() const
	{
		const std::array<std::string, 4> custom {i18n::text("ui.keep_existing_format"), i18n::text("ui.keep_existing_subtitle_settings"), i18n::text("ui.keep_existing_retries"), i18n::text("ui.keep_existing_concurrency")};
		std::string result;
		auto add = [&](const std::string &label) { if(!result.empty()) result += " + "; result += label; };
		for(size_t group {0}; group < choices.size(); ++group)
			if(selected[group] != 0) add(selected[group] < choices[group].size() ? i18n::text(choices[group][selected[group]].label) : custom[group]);
		for(size_t n {0}; n < flags.size(); ++n)
			if(enabled[n]) add(i18n::text(flags[n].label));
		if(!extra.empty()) add(i18n::text("ui.keep_other_arguments"));
		return result.empty() ? i18n::text("ui.keep_current_settings") : result;
	}
};

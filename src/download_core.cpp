#include "download_core.hpp"
#include "core_text.hpp"
#include "download_options.hpp"
#include "collection_model.hpp"
#include <Windows.h>
#include <fstream>
#include <sstream>
#include <set>

namespace
{
	std::string application_arguments(const std::string &arguments)
	{
		// 保存原始参数；执行时由 Application 的同一份配置决定这些选项。
		const std::set<std::string> values {"--proxy", "--cookies", "--cookies-from-browser", "--downloader", "--external-downloader", "--downloader-args", "--external-downloader-args", "--sponsorblock-mark", "--sponsorblock-remove", "--ffmpeg-location"};
		const std::set<std::string> flags {"--no-cookies", "--no-cookies-from-browser", "--no-sponsorblock"};
		const auto tokens {download_options::tokens(core::arguments_without_label(arguments))};
		std::string result;
		for(size_t n {0}; n < tokens.size(); ++n)
		{
			const auto token {download_options::unquote(tokens[n])}, name {token.substr(0, token.find('='))};
			if(flags.contains(name)) continue;
			if(values.contains(name)) { if(token.find('=') == std::string::npos && n + 1 < tokens.size()) ++n; continue; }
			if(!result.empty()) result += ' ';
			result += tokens[n];
		}
		return result;
	}
	std::wstring javascript_runtime(const core::download_settings &settings)
	{
		std::error_code error;
		if(core::fs::is_regular_file(settings.ytdlp_path.parent_path() / "qjs.exe", error)) return L"--js-runtimes quickjs ";
		return settings.application_owned_configuration && !settings.deno_path.empty() ? L"--js-runtimes " + core::quote_argument(L"deno:" + settings.deno_path.wstring()) + L" " : L"";
	}
	std::filesystem::path result_path()
	{
		wchar_t file[MAX_PATH];
		if(!GetTempFileNameW(std::filesystem::temp_directory_path().c_str(), L"ydl", 0, file))
			throw std::runtime_error("result file");
		return file;
	}
}

core::download_result core::execute_download(const download_plan &plan, std::atomic_bool &working,
	append_callback append, progress_callback progress, std::function<void(download_stage)> stage,
	std::atomic_bool *graceful_exit, std::function<void(const fs::path &)> produced_file)
{
	download_result result;
	auto output = [&](std::string text, bool keyword)
	{
		if(text.starts_with("[CoreFile]"))
		{
			if(keyword && produced_file)
			{
				text.erase(0, 10); while(!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.pop_back();
				produced_file(fs::u8path(text));
			}
			if(keyword) return;
		}
		if(!keyword)
			for(auto first {text.find("[CoreFile]")}; first != std::string::npos; first = text.find("[CoreFile]", first))
			{
				if(first && text[first - 1] != '\n') { first += 10; continue; }
				const auto end {text.find('\n', first)}; text.erase(first, end == std::string::npos ? end : end + 1 - first);
			}
		if(text.empty()) return;
		if(text.starts_with("[CoreDownload]") || text.starts_with("[CoreProcessing]"))
		{
			if(keyword && stage) stage(text.starts_with("[CoreDownload]") ? download_stage::downloading : download_stage::processing);
			return;
		}
		if(append) append(std::move(text), keyword);
	};
	auto on_progress = [&](std::uint64_t completed, std::uint64_t total, std::string text, int done, int count)
	{
		if(total == std::uint64_t(-1) && (text == "[ExtractAudio]" || text.starts_with("[Fixup") || text == "[Merger]"))
			if(stage) stage(download_stage::processing);
		if(progress) progress(completed, total, std::move(text), done, count);
	};
	result.output = run_process(plan.command, &working, output, on_progress, graceful_exit,
		plan.result_file.filename().string(), &result.exit_code);
	result.stopped = !working;
	if(!plan.result_file.empty())
	{
		std::ifstream file {plan.result_file, std::ios::binary};
		for(std::string path; std::getline(file, path);)
		{
			if(!path.empty() && path.back() == '\r') path.pop_back();
			if(!path.empty()) result.files.push_back(fs::u8path(path));
		}
		if(!result.files.empty()) result.file = result.files.front();
		file.close();
		std::error_code error;
		fs::remove(plan.result_file, error);
	}
	return result;
}

std::string core::query_metadata(const fs::path &ytdlp, const std::wstring &arguments, std::atomic_bool &working)
{
	return run_process(L'"' + ytdlp.wstring() + L'"' + arguments, &working);
}

std::wstring core::metadata_arguments(const download_settings &settings, const std::wstring &url,
	bool youtube, bool single_json, bool approximate_filesize)
{
	const auto quickjs {youtube ? javascript_runtime(settings) : L""};
	const auto cookies {cookie_arguments(settings)}, sort {metadata_format_sort(settings)};
	const auto output {settings.output_template.empty() ? L"" : L"-o \"" + settings.output_template + L'"'};
	std::wstring arguments;
	if(single_json)
		arguments = L" --no-warnings -j " + quickjs + cookies + output + sort + L'"' + url + L'"';
	else
		arguments = L" --no-warnings" + cookies + (approximate_filesize ? L" --compat-options manifest-filesize-approx" : L"") +
			L" -J --flat-playlist " + output + sort + L'"' + url + L'"';
	if(settings.use_proxy && !settings.proxy.empty()) arguments = L" --proxy " + quote_argument(settings.proxy) + arguments;
	else if(settings.application_owned_configuration) arguments = L" --proxy \"\"" + arguments;
	return arguments;
}

bool core::download_request::custom_format() const
{
	if(!use_arguments) return false;
	for(const auto &token : download_options::normalized(arguments_without_label(arguments)))
		if(token == "-f" || token.starts_with("-f") && !token.starts_with("--") || token.starts_with("--format=")) return true;
	return false;
}

core::download_plan core::build_download(const download_settings &settings, const download_request &input)
{
	auto request {input};
	if(settings.application_owned_configuration) request.arguments = application_arguments(request.arguments);
	std::wstring cmd {L'\"' + settings.ytdlp_path.wstring() + L'\"'};

	if(settings.aria)
	{
		cmd += L" --downloader " + (settings.aria_path.empty() ? L"aria2c" : quote_argument(settings.aria_path.wstring()));
		auto arguments {to_wide(download_options::unquote(settings.aria_options))};
		if(settings.application_owned_configuration)
		{
			// aria2 的协议代理优先于 all-proxy；显式配置不能被父进程环境覆盖。
			const auto proxy {settings.use_proxy ? settings.proxy : L""};
			arguments += L" --http-proxy " + quote_argument(proxy) + L" --https-proxy " + quote_argument(proxy);
		}
		if(!arguments.empty()) cmd += L" --downloader-args " + quote_argument(L"aria2c:" + arguments);
	}

	else if(settings.application_owned_configuration) cmd += L" --downloader native";

	if(request.youtube || request.youtube_playlist)
	{
		const auto runtime {javascript_runtime(settings)};
		if(!runtime.empty()) cmd += L' ' + runtime;
	}

	cmd += cookie_arguments(settings);

	if(!request.custom_format())
	{
		if(request.use_format)
		{
			if(request.video_format.empty() && settings.pref_audio == 6)
			{
				auto it {std::find_if(request.media["formats"].begin(), request.media["formats"].end(), [&](const auto &el)
				{
					return el["format"].get<std::string>().rfind(to_utf8(request.audio_format)) != -1;
				})};
				if(it != request.media["formats"].end())
				{
					std::string acodec, aext;
					auto &fmt {*it};
					if(fmt.contains("acodec") && fmt["acodec"] != nullptr)
						acodec = fmt["acodec"];
					if(fmt.contains("audio_ext") && fmt["audio_ext"] != nullptr)
						aext = fmt["audio_ext"];
					if(acodec == "opus" && aext != "opus")
						cmd += L" -x";
				}
			}
			else if(request.audio_format.find('+') != -1 || request.audio_format == L"mergeall")
			{
				cmd += L" --audio-multistreams";
				if(!request.use_arguments || request.arguments.find("--embed-metadata") == -1)
					cmd += L" --embed-metadata";
			}
			if(!request.video_format.empty() && settings.pref_video == 3)
				cmd += L" --remux-video mkv";

			cmd += L" -f " + request.format;
		}
		else
		{
			if(request.youtube && settings.premium && settings.pref_res <= 4 && (settings.pref_vcodec != 0 && settings.pref_vcodec != 3 || settings.pref_video == 2))
			{
				if(request.youtube_channel || request.channel_tab || request.youtube_playlist)
				{
					if(settings.pref_res == 4)
						cmd += L" -f \"bv*[format_note*=Premium]+ba / bv*+ba\"";
				}
				else
				{
					std::string fmtid_premium;
					const auto &formats {request.media["formats"]};
					for(auto &el : formats)
					{
						if(el.contains("format_note") && el["format_note"] != nullptr && el["format_note"] == "Premium")
						{
							fmtid_premium = el["format_id"];
							break;
						}
					}
					if(!fmtid_premium.empty())
					{
						int maxres {1080};
						if(settings.pref_res < 4)
						{
							for(auto &el : formats)
							{
								if(el.contains("height") && el["height"] != nullptr)
								{
									int height {el["height"]};
									if(height > maxres)
										maxres = height;
								}
							}
						}
						if(maxres == 1080 && !fmtid_premium.empty())
							cmd += L" -f " + to_wide(fmtid_premium) + L"+ba";
					}
				}
			}

			if(settings.pref_video == 3 && request.has_media_value("vcodec") && request.media["vcodec"] != "none")
				cmd += L" --remux-video mkv";

			std::wstring strpref {L" -S \""};
			if(settings.pref_res)
				strpref += L"res:" + resolution_options[settings.pref_res];
			if(settings.pref_video && settings.pref_video != 3)
			{
				if(strpref.size() > 5)
					strpref += ',';
				strpref += L"vext:" + video_options[settings.pref_video];
			}
			if(settings.pref_audio)
			{
				if(strpref.size() > 5)
					strpref += ',';
				strpref += L"aext:" + audio_options[settings.pref_audio];
			}
			if(settings.pref_fps)
			{
				if(strpref.size() > 5)
					strpref += ',';
				strpref += L"fps";
			}
			if(settings.pref_vcodec)
			{
				if(strpref.size() > 5)
					strpref += ',';
				strpref += L"vcodec:" + video_codec_options[settings.pref_vcodec];
			}
			if(settings.pref_acodec)
			{
				if(strpref.size() > 5)
					strpref += ',';
				strpref += L"acodec:" + audio_codec_options[settings.pref_acodec];
			}
			if(strpref.size() > 5)
				cmd += strpref + L"\" ";
		}
	}

	if(cmd.back() != ' ')
		cmd.push_back(' ');

	const auto &argset {request.arguments};
	if(request.chapters == 1 && argset.find("--embed-chapters") == -1)
		cmd += L"--embed-chapters ";
	if(request.embed_subtitles && argset.find("--embed-subs") == -1)
		cmd += L"--embed-subs ";
	if(request.embed_subtitles || argset.find("--embed-subs") != -1 || argset.find("--write-subs") != -1)
	{
		if(!request.sub_format.empty())
			cmd += L"--sub-format " + to_wide(request.sub_format) + L" ";
		else if(!settings.sub_format.empty())
			cmd += L"--sub-format " + to_wide(settings.sub_format) + L" ";
		if(!request.sub_langs.empty())
			cmd += L"--sub-langs " + to_wide(request.sub_langs) + L" ";
		else if(!settings.sub_langs.empty())
			cmd += L"--sub-langs " + to_wide(settings.sub_langs) + L" ";
	}
	if(request.embed_thumbnail && argset.find("--embed-thumbnail") == -1)
		cmd += L"--embed-thumbnail ";
	if(request.mtime_is_write_time && argset.find("--no-mtime") == -1)
		cmd += L"--no-mtime ";
	if(request.force_keyframes && argset.find("--force-keyframes-at-cuts") == -1)
		cmd += L"--force-keyframes-at-cuts ";
	if(settings.application_owned_configuration && !settings.ffmpeg_path.empty())
		cmd += L"--ffmpeg-location " + quote_argument(settings.ffmpeg_path.wstring()) + L" ";
	else if(!settings.ffmpeg_path.empty() && settings.ffmpeg_path != settings.ytdlp_path.parent_path())
	{
		if(argset.find("--ffmpeg-location") == -1 && settings.use_ffmpeg_location)
		{
			if(settings.ffmpeg_path != L".\\")
				cmd += L"--ffmpeg-location \"" + settings.ffmpeg_path.wstring() + L"\" ";
		}
	}
	if(!request.rate.empty() && request.rate != "0" && argset.find("-r ") == -1)
		cmd += L"-r " + to_wide(request.rate) + (request.rate_unit ? L"M " : L"K ");
	if(request.convert_mp3 && argset.find("--audio-format") == -1)
	{
		cmd += L"-x --audio-format mp3 ";
		if(argset.find("--audio-quality") == -1) 
			cmd += L"--audio-quality 0 ";
	}
	if(request.chapters == 2 && argset.find("--split-chapters") == -1)
		cmd += L"--split-chapters -o chapter:\"" + request.output_folder.wstring() + L"\\%(title)s - %(section_number)s -%(section_title)s.%(ext)s\" ";
	if(request.use_arguments && !argset.empty())
		cmd += to_wide(arguments_without_label(argset)) + L" ";
	for(auto &section : request.sections)
	{
		auto arg
		{
			L"--download-sections *" + (section.first.empty() ? L"0" : section.first) + L'-' +
			(section.second.empty() || section.second == L"0" ? L"inf" : section.second) + L' '
		};
		cmd += arg;
	}

	if(settings.application_owned_configuration)
		cmd += settings.sponsor_mark || settings.sponsor_remove ? L"--sponsorblock-mark=-all --sponsorblock-remove=-all " : L"--no-sponsorblock ";
	if(request.youtube || request.youtube_channel)
	{
		std::wstring mark, remove;
		if(settings.sponsor_mark && !settings.sponsor_mark_categories.empty())
		{
			if(settings.sponsor_mark_categories.front() == 0)
			{
				mark = L"all";
			}
			else if((sponsor_mark_options.size() - 1) / 2 < settings.sponsor_mark_categories.size())
			{
				mark = L"all";
				for(int n{1}; n < sponsor_mark_options.size(); n++)
				{
					if(std::find(settings.sponsor_mark_categories.begin(), settings.sponsor_mark_categories.end(), n) == settings.sponsor_mark_categories.end())
						mark += L",-" + sponsor_mark_options[n];
				}
			}
			else
			{
				for(auto idx : settings.sponsor_mark_categories)
					mark += sponsor_mark_options[idx] + L',';
				mark.pop_back();
			}
		}

		if(settings.sponsor_remove && !settings.sponsor_remove_categories.empty())
		{
			if(settings.sponsor_remove_categories.front() == 0)
			{
				remove = L"all";
			}
			else if((sponsor_remove_options.size() - 1) / 2 < settings.sponsor_remove_categories.size())
			{
				remove = L"all";
				for(int n {1}; n < sponsor_remove_options.size(); n++)
				{
					if(std::find(settings.sponsor_remove_categories.begin(), settings.sponsor_remove_categories.end(), n) == settings.sponsor_remove_categories.end())
						remove += L",-" + sponsor_remove_options[n];
				}
			}
			else
			{
				for(auto idx : settings.sponsor_remove_categories)
					remove += sponsor_remove_options[idx] + L',';
				remove.pop_back();
			}
		}

		if(!mark.empty() && request.arguments.find("--sponsorblock-mark ") == -1)
			cmd += L"--sponsorblock-mark " + mark + L' ';
		if(!remove.empty() && request.arguments.find("--sponsorblock-remove ") == -1)
			cmd += L"--sponsorblock-remove " + remove + L' ';
	}

	if(settings.use_proxy && !settings.proxy.empty() && request.arguments.find("--proxy ") == -1)
		cmd += L"--proxy " + quote_argument(settings.proxy) + L' ';
	else if(settings.application_owned_configuration) cmd += L"--proxy \"\" ";

	if(request.collection) cmd += context_arguments(*request.collection) + L' ';
	auto display_cmd {cmd};

	// --print 默认开启 quiet；在用户参数之前恢复原有日志与进度默认值。
	cmd.insert(settings.ytdlp_path.wstring().size() + 2, L" --no-quiet --progress");
	cmd += L"--encoding UTF-8 --progress-delta .8 ";

	if(!request.youtube_playlist && !request.soundcloud_playlist && !request.playlist && !request.bandcamp_playlist && !request.bandcamp_channel && !request.youtube_channel)
		cmd += L"--no-simulate --print before_dl:[CoreDownload] --print after_video:[CoreProcessing] ";
	fs::path tempfile;
	if(!request.youtube_playlist && !request.youtube_channel && !request.bandcamp_playlist)
	{
		tempfile = result_path();
		cmd += L"--print-to-file after_move:filepath \"" + tempfile.wstring() + L"\"";
		if(settings.application_owned_configuration) cmd += L" --print after_move:[CoreFile]%(filepath)s";
	}
	std::wstring cmd2;

	auto wstr {request.output_folder.wstring()};
	if(wstr.find(' ') == -1)
		cmd2 += L" -P " + wstr;
	else cmd2 += L" -P \"" + wstr + L"\"";

	const auto output_args {download_options::normalized(arguments_without_label(argset))};
	const bool custom_output {std::any_of(output_args.begin(), output_args.end(), [](const auto &arg) { return arg == "-o" || arg == "--output" || arg.starts_with("--output="); })};
	if((!request.use_arguments || !custom_output) && !settings.output_template.empty())
	{
		std::wstring folder;
		if(request.youtube_playlist || request.playlist)
		{
			cmd2 += L" -o \"" + playlist_template(settings.output_template, settings.playlist_indexing,
				settings.playlist_folder, settings.zero_padding, request.selected_media_count, false) + L'\"';
		}
		else if(request.bandcamp_playlist || request.bandcamp_channel)
		{
			if(settings.playlist_folder)
				folder = L"%(artist)s\\%(album)s\\";
			cmd2 += L" -o \"" + folder + settings.output_template_bandcamp + L'\"';
		}
		else if(request.youtube_channel)
		{
			cmd2 += L" -o \"" + playlist_template(settings.output_template, {}, settings.playlist_folder,
				false, 0, true) + L'\"';
		}
		else if(request.sections.size() > 1)
		{
			if(request.output_file.empty())
			{
				cmd2 += L" -o \"" + settings.output_template;
				if(cmd2.rfind(L".%(ext)s") == cmd2.size() - 8)
					cmd2.erase(cmd2.size() - 8);
			}
			else cmd2 += L" -o \"" + request.output_file.filename().wstring();
			cmd2 += L" - section %(section_start)d to %(section_end)d.%(ext)s\"";
		}
		else 
		{
			if(request.output_file.empty())
				cmd2 += L" -o \"" + settings.output_template + L'\"';
			else cmd2 += L" -o \"" + request.output_file.filename().wstring() + L".%(ext)s\"";
		}
	}
	if((request.youtube_playlist || request.bandcamp_playlist || request.soundcloud_playlist || request.playlist) && !request.playlist_items.empty())
		cmd2 += L" -I " + request.playlist_items + L" --compat-options no-youtube-unavailable-videos";
	cmd2 += L" \"" + request.url + L'\"';
	display_cmd += cmd2;
	cmd += cmd2;
	return {std::move(cmd), std::move(display_cmd), std::move(tempfile)};

}

bool core::is_youtube(std::wstring url)
{
	auto pos {url.find(L"www.")};
	if(pos != -1)
		url.erase(pos, 4);
	pos = url.find(L"m.youtube.");
	if(pos != -1)
		url.erase(pos, 2);
	if(url.starts_with(L"https://music.youtube.com"))
		return true;
	if(url.starts_with(L"https://youtube.com/clip/"))
		return true;
	if(url.starts_with(L"https://youtube.com/") && url.size() > 20 || url.starts_with(L"youtube.com/") && url.size() > 12)
		return true;
	if(url.starts_with(L"https://youtu.be/") && url.size() > 17 || url.starts_with(L"youtu.be/") && url.size() > 9)
		return true;
	return false;
}


bool core::is_youtube_channel(std::wstring url)
{
	if(url.find(L"youtube.com/c/") != -1 || url.find(L"youtube.com/@") != -1 ||
		url.find(L"youtube.com/channel/") != -1 || url.find(L"youtube.com/user/") != -1)
		return true;
	auto pos {url.find(L"youtube.com/")};
	if(pos != -1 && url.size() > pos + 12 && url.find(L"watch?v=") == -1 && url.find(L"list=") == -1 && url.find(L"/live/") == -1
		&& url.find(L"music.youtube.com") == -1 && url.find(L"youtube.com/clip/") == -1 && url.find(L"youtube.com/shorts/") == -1)
		return true;
	return false;
}


bool core::is_soundcloud_playlist(std::wstring url)
{
	if(url.find(L"soundcloud.com/") != -1)
	{
		auto pos_in {url.find(L"?in=")}, pos_sets {url.find(L"/sets/")};
		if(pos_sets != -1 && pos_in == -1)
			return true;
	}
	return false;
}


bool core::ffmpeg_in_config(const fs::path &ytdlp)
{
	std::ifstream infile {ytdlp.parent_path() / "yt-dlp.conf"};
	if(infile)
	{
		std::stringstream ss;
		ss << infile.rdbuf();
		if(ss.str().find("--ffmpeg-location") != -1)
			return true;
	}
	return false;
}


std::string core::arguments_without_label(const std::string &text)
{
	if(!text.empty() && text[0] == '[')
	{
		auto pos {text.find(']')};
		if(pos != std::string::npos)
		{
			auto start {text.find_first_not_of(" \t", pos + 1)};
			if(start != std::string::npos) return text.substr(start);
		}
	}
	return text;
}

std::wstring core::metadata_format_sort(const download_settings &settings)
{
	std::wstring fmt_sort {' '}, strpref {L" -S \""};
	if(settings.pref_res)
		strpref += L"res:" + resolution_options[settings.pref_res];
	if(settings.pref_video)
	{
		if(strpref.size() > 5)
			strpref += ',';
		strpref += L"vext:" + video_options[settings.pref_video];
	}
	if(settings.pref_audio)
	{
		if(strpref.size() > 5)
			strpref += ',';
		strpref += L"aext:" + audio_options[settings.pref_audio];
	}
	if(settings.pref_fps)
	{
		if(strpref.size() > 5)
			strpref += ',';
		strpref += L"fps";
	}
	if(settings.pref_vcodec)
	{
		if(strpref.size() > 5)
			strpref += ',';
		strpref += L"vcodec:" + video_codec_options[settings.pref_vcodec];
	}
	if(settings.pref_acodec)
	{
		if(strpref.size() > 5)
			strpref += ',';
		strpref += L"acodec:" + audio_codec_options[settings.pref_acodec];
	}
	if(strpref.size() > 5)
		fmt_sort = strpref + L"\" ";

	return fmt_sort;
}

std::wstring core::cookie_arguments(const download_settings &settings)
{
	std::wstring cookies;
	if(settings.application_owned_configuration)
		cookies = settings.cookie_browser ? L" --no-cookies " : settings.cookies ? L" --no-cookies-from-browser " : L" --no-cookies --no-cookies-from-browser ";
	if(settings.cookie_browser)
		cookies += L" --cookies-from-browser " + quote_argument(cookie_browser_options[settings.cookie_browser] + to_wide(settings.cookie_options)) + L" ";
	else if(settings.cookies && !settings.cookies_path.empty())
		cookies += L" --cookies \"" + settings.cookies_path.wstring() + L"\" ";

	return cookies;
}

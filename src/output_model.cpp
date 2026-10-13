#include "output_model.hpp"
#include "core_text.hpp"
#include "download_options.hpp"
#include "naming_model.hpp"
#include <Windows.h>
#include <wincrypt.h>
#include <charconv>
#include <cmath>
#include <regex>
#include <set>

namespace
{
	bool safe_text(const std::string &value) { return value.find_first_of("\"\r\n") == std::string::npos && value.find('\0') == std::string::npos; }
	bool edited(const core::processing_options &options, const std::string &key) { return std::find(options.edited.begin(), options.edited.end(), key) != options.edited.end(); }
	bool has_option(const std::string &text, const std::vector<std::string> &keys)
	{
		for(auto token : download_options::normalized(text))
		{
			if(token.size() > 2 && !token.starts_with("--") && (token.starts_with("-f") || token.starts_with("-N") || token.starts_with("-S") || token.starts_with("-r") || token.starts_with("-o") || token.starts_with("-P"))) token.resize(2);
			if(token.starts_with("--") && token.find('=') != std::string::npos) token.resize(token.find('='));
			if(std::find(keys.begin(), keys.end(), token) != keys.end()) return true;
		}
		return false;
	}
	std::string without_options(const std::string &text, const std::vector<std::string> &keys, bool takes_value)
	{
		std::string result;
		const auto tokens {download_options::tokens(text)};
		for(size_t n {0}; n < tokens.size(); ++n)
		{
			auto key {download_options::unquote(tokens[n])}; const auto eq {key.starts_with("--") ? key.find('=') : std::string::npos};
			const bool attached {key.size() > 2 && !key.starts_with("--") && (key.starts_with("-f") || key.starts_with("-N") || key.starts_with("-S") || key.starts_with("-r") || key.starts_with("-o") || key.starts_with("-P"))};
			if(attached) key.resize(2);
			if(eq != std::string::npos) key.resize(eq);
			if(std::find(keys.begin(), keys.end(), key) == keys.end()) download_options::append(result, tokens[n]);
			else if(takes_value && !attached && eq == std::string::npos && n + 1 < tokens.size()) ++n;
		}
		return result;
	}
	template<typename T> unsigned option_index(const T &values, const std::string &value)
	{
		return unsigned(std::find(values.begin(), values.end(), core::to_wide(value)) - values.begin());
	}
	std::string argument_text(const std::string &value) { return core::to_utf8(core::quote_argument(core::to_wide(value))); }
}

core::output_options core::default_output_options(const fs::path &folder)
{
	return {"audio", "mp3", "1080", "author", folder, true, true};
}

double core::section_seconds(const std::string &text)
{
	if(text.empty()) return 0;
	if(!std::regex_match(text, std::regex {R"(\d+(?::\d{1,2}){0,2})"})) throw application_error {"invalidSection", "web.invalid_section"};
	double result {0}; size_t start {0}; unsigned part {0};
	while(start < text.size())
	{
		const auto end {text.find(':', start)};
		const auto number {std::stod(text.substr(start, end == std::string::npos ? end : end - start))};
		if(part++ && number >= 60) throw application_error {"invalidSection", "web.invalid_section"};
		result = result * 60 + number;
		if(end == std::string::npos) break;
		start = end + 1;
	}
	if(!std::isfinite(result)) throw application_error {"invalidSection", "web.invalid_section"};
	return result;
}

void core::validate_sections(const std::vector<time_section> &sections, const nlohmann::json &media)
{
	if(!sections.empty() && media.is_object() && (media.value("is_live", nlohmann::json(false)) == true || media.value("live_status", nlohmann::json("")) == "is_live" || media.contains("entries")))
		throw application_error {"sectionsUnavailable", "web.sections_unavailable"};
	std::set<std::pair<double, double>> seen;
	for(const auto &section : sections)
	{
		const auto start {section_seconds(section.start)}, end {section_seconds(section.end)};
		if(start == 0 && end == 0 || end && end <= start || !seen.emplace(start, end).second)
			throw application_error {"invalidSection", "web.invalid_section"};
		if(media.contains("duration") && media["duration"].is_number())
		{
			const auto duration {media["duration"].get<double>()};
			if(start >= duration || end > duration) throw application_error {"invalidSection", "web.invalid_section"};
		}
	}
}

void core::validate_output_options(const output_options &options)
{
	const auto &p {options.processing};
	if((options.kind != "video" && options.kind != "audio") ||
		(option_index(options.kind == "video" ? video_options : audio_options, options.format) == (options.kind == "video" ? video_options.size() : audio_options.size())) ||
		option_index(resolution_options, options.quality) == resolution_options.size() || options.output_folder.empty() || !options.output_folder.is_absolute() ||
		!safe_text(to_utf8(options.output_folder.wstring())) || !safe_text(options.output_template) || !safe_text(options.playlist_prefix) ||
		(options.naming != "title" && options.naming != "author" && options.naming != "date" && !(options.naming == "custom" && !options.output_template.empty())) ||
		option_index(video_codec_options, p.video_codec) == video_codec_options.size() || option_index(audio_codec_options, p.audio_codec) == audio_codec_options.size() ||
		(p.m4a_mode != "native" && p.m4a_mode != "convert") || (p.rate_unit != "K" && p.rate_unit != "M") ||
		(p.chapters != "ignore" && p.chapters != "embed" && p.chapters != "split") ||
		(p.subtitles != "keep" && p.subtitles != "zh" && p.subtitles != "zh-auto" && p.subtitles != "zh-en" && p.subtitles != "all") ||
		(p.retries != "keep" && p.retries != "default" && p.retries != "3" && p.retries != "10" && p.retries != "30" && p.retries != "infinite") ||
		(p.fragments != "keep" && p.fragments != "default" && p.fragments != "1" && p.fragments != "4" && p.fragments != "8" && p.fragments != "16") ||
		!safe_text(p.sub_langs) || !safe_text(p.sub_format) || !safe_text(p.filename) || p.raw_args.find('\0') != std::string::npos)
		throw application_error {"invalidPayload", "web.invalid_payload"};
	validate_naming_template(options.output_template); validate_naming_template(options.bandcamp_template); validate_naming_template(options.playlist_prefix);
	if(!p.rate.empty() && !std::regex_match(p.rate, std::regex {R"(\d+(?:\.\d+)?)"})) throw application_error {"invalidPayload", "web.invalid_payload"};
	if(!p.rate.empty())
	{
		try { if(!std::isfinite(std::stod(p.rate))) throw std::runtime_error {"rate"}; }
		catch(const std::exception &) { throw application_error {"invalidPayload", "web.invalid_payload"}; }
	}
	if(!p.filename.empty() && (!fs::u8path(p.filename).is_absolute() || fs::u8path(p.filename).filename().empty())) throw application_error {"invalidFilename", "web.invalid_filename"};
	for(const auto &id : p.format_ids) if(id.empty() || !safe_text(id) || id.find_first_of("+/,[] ") != std::string::npos) throw application_error {"invalidFormat", "web.invalid_format"};
	validate_sections(p.sections);
}

nlohmann::json core::processing_json(const processing_options &p)
{
	nlohmann::json sections = nlohmann::json::array();
	for(const auto &section : p.sections) sections.push_back({{"start", section.start}, {"end", section.end}});
	return {{"videoCodec", p.video_codec}, {"audioCodec", p.audio_codec}, {"m4aMode", p.m4a_mode}, {"higherFps", p.higher_fps}, {"premium", p.premium},
		{"rate", p.rate}, {"rateUnit", p.rate_unit}, {"chapters", p.chapters}, {"writeTime", p.write_time}, {"keyframes", p.keyframes},
		{"subtitles", p.subtitles}, {"retries", p.retries}, {"fragments", p.fragments}, {"embedSubs", p.embed_subs},
		{"keepFiles", p.keep_files}, {"windowsNames", p.windows_names}, {"saveCover", p.save_cover}, {"saveInfo", p.save_info},
		{"saveDescription", p.save_description}, {"saveComments", p.save_comments}, {"liveStart", p.live_start},
		{"rawEnabled", p.raw_enabled}, {"rawArgs", p.raw_args}, {"subLangs", p.sub_langs}, {"subFormat", p.sub_format}, {"edited", p.edited},
		{"formatIds", p.format_ids}, {"multiAudio", p.multi_audio}, {"sections", std::move(sections)}, {"filename", p.filename}};
}

core::processing_options core::read_processing(const nlohmann::json &value)
{
	processing_options result; const auto defaults {processing_json(result)};
	auto data {defaults}; for(const auto &item : value.items()) data[item.key()] = item.value();
	result.video_codec = data.at("videoCodec"); result.audio_codec = data.at("audioCodec"); result.m4a_mode = data.at("m4aMode");
	result.higher_fps = data.at("higherFps"); result.premium = data.at("premium"); result.rate = data.at("rate"); result.rate_unit = data.at("rateUnit");
	result.chapters = data.at("chapters"); result.write_time = data.at("writeTime"); result.keyframes = data.at("keyframes");
	result.subtitles = data.at("subtitles"); result.retries = data.at("retries"); result.fragments = data.at("fragments");
	result.embed_subs = data.at("embedSubs"); result.keep_files = data.at("keepFiles"); result.windows_names = data.at("windowsNames");
	result.save_cover = data.at("saveCover"); result.save_info = data.at("saveInfo"); result.save_description = data.at("saveDescription"); result.save_comments = data.at("saveComments"); result.live_start = data.at("liveStart");
	result.raw_enabled = data.at("rawEnabled"); result.raw_args = data.at("rawArgs"); result.sub_langs = data.at("subLangs"); result.sub_format = data.at("subFormat");
	result.edited = data.at("edited").get<std::vector<std::string>>(); result.format_ids = data.at("formatIds").get<std::vector<std::string>>();
	result.multi_audio = data.at("multiAudio"); result.filename = data.at("filename");
	for(const auto &section : data.at("sections")) result.sections.push_back({section.at("start"), section.at("end")});
	return result;
}

nlohmann::json core::output_options_json(const output_options &o)
{
	return {{"kind", o.kind}, {"format", o.format}, {"quality", o.quality}, {"naming", o.naming}, {"outputFolder", to_utf8(o.output_folder.wstring())},
		{"cover", o.cover}, {"metadata", o.metadata}, {"outputTemplate", o.output_template}, {"playlistPrefix", o.playlist_prefix},
		{"zeroPadding", o.zero_padding}, {"playlistFolder", o.playlist_folder}, {"singleVideo", o.single_video}, {"processing", processing_json(o.processing)}, {"bandcampTemplate", o.bandcamp_template}};
}

core::output_options core::read_output_options(const nlohmann::json &value)
{
	output_options o {value.at("kind"), value.at("format"), value.at("quality"), value.at("naming"), fs::u8path(value.at("outputFolder").get<std::string>()),
		value.at("cover"), value.at("metadata"), value.value("outputTemplate", ""), value.value("playlistPrefix", "%(playlist_index)d - "),
		value.value("zeroPadding", true), value.value("playlistFolder", true), value.value("singleVideo", false)};
	if(value.contains("processing")) o.processing = read_processing(value.at("processing"));
	o.bandcamp_template = value.value("bandcampTemplate", o.bandcamp_template);
	return o;
}

core::download_settings core::settings_for_output(download_settings settings, const output_options &o)
{
	settings.pref_res = option_index(resolution_options, o.quality); settings.pref_video = o.kind == "video" ? option_index(video_options, o.format) : 0;
	settings.pref_audio = o.kind == "audio" ? option_index(audio_options, o.format) : 0;
	settings.pref_vcodec = option_index(video_codec_options, o.processing.video_codec); settings.pref_acodec = option_index(audio_codec_options, o.processing.audio_codec);
	settings.pref_fps = o.processing.higher_fps; settings.premium = o.processing.premium;
	settings.sub_langs = o.processing.sub_langs; settings.sub_format = o.processing.sub_format;
	return settings;
}

std::string core::output_arguments(const output_options &o)
{
	const auto &p {o.processing}; download_options args;
	args.read(p.raw_enabled ? arguments_without_label(p.raw_args) : "");
	if(!has_option(args.original_groups[0], {"-f", "--format", "-x", "--extract-audio", "--audio-format", "--recode-video", "--remux-video"}) || edited(p, "format") || !p.format_ids.empty())
	{
		std::string format;
		if(!p.format_ids.empty())
		{
			for(const auto &id : p.format_ids) { if(!format.empty()) format += '+'; format += id; }
			format = "-f " + argument_text(format);
				if(p.multi_audio) format += " --audio-multistreams --remux-video mkv --embed-metadata";
				else if(o.kind == "video" && o.format == "mkv") format += " --remux-video mkv";
				else if(o.kind == "audio")
				{
					format += " -x";
					if(o.format != "none" && o.format != "webm" && !(o.format == "m4a" && p.m4a_mode == "native"))
						format += " --audio-format " + (o.format == "ogg" ? "vorbis" : o.format) + " --audio-quality 0";
				}
		}
		else if(o.kind == "video")
		{
			format = o.format == "mp4" ? "-f \"bv*[ext=mp4]+ba[ext=m4a]/b[ext=mp4]/b\" --merge-output-format mp4" :
				o.format == "webm" ? "-f \"bv*[ext=webm]+ba[ext=webm]/b[ext=webm]/bv*+ba/b\" --merge-output-format webm --remux-video webm" : "-f \"bv*+ba/b\"";
			if(o.format == "mkv") format += " --remux-video mkv";
			if(p.premium && o.quality == "1080")
			{
				const auto selector {o.format == "mp4" ? "bv*[ext=mp4][format_note*=Premium]+ba[ext=m4a]/" : o.format == "webm" ? "bv*[ext=webm][format_note*=Premium]+ba[ext=webm]/" : "bv*[format_note*=Premium]+ba/"};
				format.insert(format.find('"') + 1, selector);
			}
		}
		else
		{
			format = o.format == "webm" ? "-f \"ba[ext=webm]/ba\" --remux-video webm" : o.format == "m4a" && p.m4a_mode == "native" ? "-f \"ba[ext=m4a]/ba/b\" -x" : "-f \"ba/b\" -x";
			if(o.format != "none" && o.format != "webm" && !(o.format == "m4a" && p.m4a_mode == "native"))
				format += " --audio-format " + (o.format == "ogg" ? "vorbis" : o.format) + " --audio-quality 0";
		}
		if(!edited(p, "format") && p.format_ids.empty()) download_options::append(format, args.original_groups[0]);
		args.original_groups[0] = format; args.selected[0] = download_options::choices[0].size();
	}
	if(p.subtitles != "keep")
	{
		const std::vector<std::string> values {"keep", "zh", "zh-auto", "zh-en", "all"};
		args.selected[1] = std::find(values.begin(), values.end(), p.subtitles) - values.begin();
	}
	for(const auto &[group, value] : std::vector<std::pair<size_t, std::string>> {{2, p.retries}, {3, p.fragments}})
		if(value != "keep")
		{
			const std::vector<std::string> values {"default", group == 2 ? "3" : "1", group == 2 ? "10" : "4", group == 2 ? "30" : "8", group == 2 ? "infinite" : "16"};
			args.selected[group] = std::find(values.begin(), values.end(), value) - values.begin();
		}
	const std::vector<std::pair<std::string, bool>> flags {{"cover", o.cover}, {"metadata", o.metadata}, {"saveCover", p.save_cover}, {"saveInfo", p.save_info},
		{"saveDescription", p.save_description}, {"saveComments", p.save_comments}, {"embedSubs", p.embed_subs}, {"keepFiles", p.keep_files}, {"windowsNames", p.windows_names}, {"singleVideo", o.single_video}, {"liveStart", p.live_start}};
	for(size_t n {0}; n < flags.size(); ++n)
		if(!args.specified[n] || edited(p, flags[n].first))
		{
			args.enabled[n] = flags[n].second;
			args.disabled_flags[n] = !flags[n].second && edited(p, flags[n].first) ? (n == 7 ? "--no-force-overwrites" : download_options::flags[n].opposite) : "";
		}
	auto result {args.command_line()};
	auto setting = [&](const std::string &key, const std::vector<std::string> &names, bool value, const std::string &generated)
	{
		if(edited(p, key)) result = without_options(result, names, value);
		if(!has_option(result, names)) download_options::append(result, generated);
	};
	const auto sort {to_utf8(metadata_format_sort(settings_for_output({}, o)))};
	setting("sort", {"-S", "--format-sort"}, true, sort);
	setting("rate", {"-r", "--limit-rate"}, true, p.rate.empty() || std::stod(p.rate) == 0 ? "" : "-r " + p.rate + p.rate_unit);
	setting("writeTime", {"--mtime", "--no-mtime"}, false, p.write_time ? "--no-mtime" : "--mtime");
	setting("keyframes", {"--force-keyframes-at-cuts", "--no-force-keyframes-at-cuts"}, false, p.keyframes ? "--force-keyframes-at-cuts" : "");
	setting("chapters", {"--embed-chapters", "--no-embed-chapters", "--split-chapters", "--no-split-chapters"}, false,
		p.chapters == "embed" ? "--embed-chapters" : p.chapters == "split" ? "--split-chapters" : "");
	if(edited(p, "subTracks")) result = without_options(result, {"--sub-langs", "--sub-format"}, true);
	if(!p.sub_langs.empty() && !has_option(result, {"--sub-langs"})) download_options::append(result, "--sub-langs " + argument_text(p.sub_langs));
	if(!has_option(result, {"--sub-format"}) && (!p.sub_format.empty() || !p.sub_langs.empty())) download_options::append(result, "--sub-format " + argument_text(p.sub_format.empty() ? "any" : p.sub_format));
	if(!p.sub_langs.empty())
	{
		if(edited(p, "subTracks")) result = without_options(result, {"--no-write-subs"}, false);
		if(!has_option(result, edited(p, "subTracks") ? std::vector<std::string> {"--write-subs"} : std::vector<std::string> {"--write-subs", "--no-write-subs", "--write-auto-subs", "--no-write-auto-subs", "--embed-subs", "--no-embed-subs"})) download_options::append(result, "--write-subs");
	}
	if(!p.filename.empty() || edited(p, "naming")) result = without_options(result, {"-o", "--output"}, true);
	if(!p.filename.empty() || edited(p, "folder")) result = without_options(result, {"-P", "--paths"}, true);
	return result;
}

bool core::single_video_requested(const output_options &options)
{
	download_options args; args.read(output_arguments(options)); return args.specified[9] && args.enabled[9];
}

std::string core::output_preset_name(const output_options &options)
{
	download_options args, raw; args.read(output_arguments(options)); raw.read(options.processing.raw_enabled ? arguments_without_label(options.processing.raw_args) : "");
	if(edited(options.processing, "format") || !has_option(raw.original_groups[0], {"-f", "--format", "-x", "--extract-audio", "--audio-format", "--recode-video", "--remux-video"}))
		args.selected[0] = options.kind == "video" ? options.format == "mp4" ? 2 : 1 : options.format == "mp3" ? 5 : options.format == "m4a" && options.processing.m4a_mode == "convert" ? 4 : 3;
	return args.label();
}

core::output_options core::filename_options(output_options options, const std::string &filename)
{
	options.processing.filename = filename;
	if(!filename.empty()) options.output_folder = fs::u8path(filename).parent_path();
	validate_output_options(options); return options;
}

core::output_options core::copy_processing_options(output_options target, const output_options &source)
{
	// 对应 Nana 的三个 propagate_*；媒体选择、命名及高级参数组保留在目标任务。
	target.cover = source.cover; target.output_folder = source.output_folder;
	// Web 的 MP3 转换已合并到下载类型/容器选择，不再引入独立开关。
	target.kind = source.kind; target.format = source.format;
	auto &p {target.processing}; const auto &from {source.processing};
	p.m4a_mode = from.m4a_mode;
	p.embed_subs = from.embed_subs; p.keyframes = from.keyframes; p.write_time = from.write_time;
	p.rate = from.rate; p.rate_unit = from.rate_unit; p.chapters = from.chapters;
	p.raw_enabled = from.raw_enabled; p.raw_args = from.raw_args;
	for(const auto &key : {"format", "cover", "embedSubs", "keyframes", "writeTime", "rate", "chapters", "folder"})
	{
		if(std::string_view {key} == "format" && !from.format_ids.empty()) continue;
		std::erase(p.edited, key);
		if(edited(from, key)) p.edited.push_back(key);
	}
	if(!p.format_ids.empty() && !edited(p, "format")) p.edited.push_back("format");
	if(!p.filename.empty()) target.output_folder = fs::u8path(p.filename).parent_path();
	return target;
}

bool core::processing_only_change(const output_options &before, const output_options &after)
{
	auto merged {output_options_json(copy_processing_options(before, after))}, proposed {output_options_json(after)};
	std::sort(merged["processing"]["edited"].begin(), merged["processing"]["edited"].end());
	std::sort(proposed["processing"]["edited"].begin(), proposed["processing"]["edited"].end());
	return merged == proposed;
}

bool core::processing_scope_changed(const output_options &before, output_options after)
{
	// 手选流的 format 标记仅用于本媒体；返回自动选择也不广播给其他任务。
	if(before.kind == after.kind && before.format == after.format && before.processing.m4a_mode == after.processing.m4a_mode &&
		(before.processing.format_ids != after.processing.format_ids || before.processing.multi_audio != after.processing.multi_audio))
	{
		after.processing.format_ids = before.processing.format_ids;
		std::erase(after.processing.edited, "format");
		if(edited(before.processing, "format")) after.processing.edited.push_back("format");
	}
	return output_options_json(copy_processing_options(default_output_options(before.output_folder), before)) !=
		output_options_json(copy_processing_options(default_output_options(after.output_folder), after));
}

nlohmann::json core::preview_output(const output_options &options)
{
	validate_output_options(options);
	download_options parsed; parsed.read(options.processing.raw_enabled ? arguments_without_label(options.processing.raw_args) : "");
	nlohmann::json flags = nlohmann::json::object();
	const std::vector<std::string> names {"cover", "metadata", "saveCover", "saveInfo", "saveDescription", "saveComments", "embedSubs", "keepFiles", "windowsNames", "singleVideo", "liveStart"};
	for(size_t n {0}; n < names.size(); ++n) if(parsed.specified[n] && !edited(options.processing, names[n])) flags[names[n]] = parsed.enabled[n];
	return {{"arguments", redact_text(output_arguments(options))}, {"rawFormat", !parsed.original_groups[0].empty() && !edited(options.processing, "format") && options.processing.format_ids.empty()},
		{"originalGroups", parsed.original_groups}, {"flags", std::move(flags)}, {"presetName", output_preset_name(options)},
		{"filename", preview_naming({{"outputTemplate", ordinary_template(options)}, {"extension", options.format}}).at("filename")}};
}

std::string core::protect_text(const std::string &text)
{
	if(text.empty()) return {};
	DATA_BLOB input {DWORD(text.size()), reinterpret_cast<BYTE *>(const_cast<char *>(text.data()))}, output {};
	if(!CryptProtectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) throw std::runtime_error {"protectFailed"};
	std::string result; static const char digits[] = "0123456789abcdef";
	for(DWORD n {0}; n < output.cbData; ++n) { result += digits[output.pbData[n] >> 4]; result += digits[output.pbData[n] & 15]; }
	LocalFree(output.pbData); return result;
}

std::string core::unprotect_text(const std::string &text)
{
	if(text.empty()) return {};
	if(text.size() % 2) throw std::runtime_error {"unprotectFailed"};
	std::vector<BYTE> bytes;
	for(size_t n {0}; n < text.size(); n += 2)
	{
		unsigned value {0}; const auto result {std::from_chars(text.data() + n, text.data() + n + 2, value, 16)};
		if(result.ec != std::errc {} || result.ptr != text.data() + n + 2) throw std::runtime_error {"unprotectFailed"};
		bytes.push_back(BYTE(value));
	}
	DATA_BLOB input {DWORD(bytes.size()), bytes.data()}, output {};
	if(!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) throw std::runtime_error {"unprotectFailed"};
	std::string result {reinterpret_cast<char *>(output.pbData), output.cbData}; LocalFree(output.pbData); return result;
}

std::string core::redact_text(const std::string &text)
{
	static const std::regex secret {R"((--(?:password|video-password|username|ap-password|ap-username|add-header|proxy|cookies|cookies-from-browser|netrc-location|netrc-cmd|extractor-args)(?:=|\s+))(?:"[^"]*"|\S+))", std::regex::icase};
	static const std::regex short_secret {R"(((?:^|\s)-[up]\s*)(?:"[^"]*"|\S+))"};
	static const std::regex credentials {R"((https?://)[^/\s"]+@)", std::regex::icase};
	static const std::regex query {R"((https?://[^\s"?]+)\?[^\s"]*)", std::regex::icase};
	return std::regex_replace(std::regex_replace(std::regex_replace(std::regex_replace(text, secret, "$1[redacted]"), short_secret, "$1[redacted]"), credentials, "$1[redacted]@"), query, "$1?[redacted]");
}

nlohmann::json core::visible_metadata(nlohmann::json value)
{
	if(value.is_object())
	{
		for(auto it {value.begin()}; it != value.end();)
			if(it.key() == "http_headers" || it.key() == "cookies" || it.key() == "cookie" || it.key() == "password") it = value.erase(it);
			else { it.value() = visible_metadata(it.value()); ++it; }
	}
	else if(value.is_array()) for(auto &item : value) item = visible_metadata(item);
	else if(value.is_string()) value = redact_text(value.get<std::string>());
	return value;
}

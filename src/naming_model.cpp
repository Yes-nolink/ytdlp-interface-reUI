#include "naming_model.hpp"
#include "i18n.hpp"
#include <regex>

namespace
{
	struct field { const char *id, *key, *value, *label, *sample; bool localized; };
	const std::vector<field> fields {
		{"title", "title", "%(title)s", "ui.title", "ui.seaside_sunset", true},
		{"author", "uploader", "ui.uploader_channel_unknown_uploader_s", "ui.uploader", "ui.photography_notes", true},
		{"channel", "channel", "ui.channel_uploader_unknown_channel_s", "ui.channel", "ui.photography_channel", true},
		{"date", "upload_date", "ui.upload_date_y_m_d_unknown_date_s", "ui.upload_date", "2026-10-04", true},
		{"year", "upload_date", "ui.upload_date_y_unknown_year_s", "ui.upload_year", "2026", true},
		{"id", "id", "%(id)s", "ui.video_id", "AbCd1234", false},
		{"artist", "artist", "ui.artist_uploader_unknown_artist_s", "ui.artist", "ui.artist_name", true},
		{"track", "track", "%(track,title)s", "ui.track_title", "ui.track_name", false},
		{"album", "album", "ui.album_unknown_album_s", "ui.album", "ui.album_name", true},
		{"trackNumber", "track_number", "%(track_number&{:02d}|)s", "ui.track_number", "01", false},
		{"playlist", "playlist_title", "%(playlist_title,playlist_id|)s", "ui.playlist_title", "ui.favorites", false},
		{"index", "playlist_index", "%(playlist_index&{:0>3}|)s", "ui.playlist_index", "001", false}
	};
	const std::vector<std::string> separators {" - ", "_", " "};
	const std::regex token {R"(%\(([^)]*)\)[#0\- +]*\d*(?:\.\d+)?[hlL]?[diouxXeEfFgGcrsajlhqBUDS])"};
	std::string text(const char *value) { return std::string {value}.starts_with("ui.") ? i18n::text(value) : value; }
	std::string value(const field &part) { return part.localized ? text(part.value) : part.value; }
	nlohmann::json describe(const std::string &original)
	{
		nlohmann::json parts = nlohmann::json::array(); auto body {original};
		if(body.ends_with(".%(ext)s")) body.resize(body.size() - 8);
		size_t pos {0}; std::string gap; bool simple {true};
		for(std::sregex_iterator it {body.begin(), body.end(), token}, end; it != end; ++it)
		{
			const auto &match {*it}; const auto between {body.substr(pos, match.position() - pos)};
			if(parts.empty()) simple = simple && between.empty();
			else { if(gap.empty()) gap = between; simple = simple && between == gap && std::find(separators.begin(), separators.end(), gap) != separators.end(); }
			const auto spec {match[1].str()}, key {spec.substr(0, spec.find_first_of(".>,&|"))};
			const auto found {std::find_if(fields.begin(), fields.end(), [&](const auto &part)
			{
				return part.key == key && (key != "upload_date" || (spec.find(">%Y") != std::string::npos && spec.find("%m") == std::string::npos && spec.find("%d") == std::string::npos) == (std::string {part.id} == "year"));
			})};
			if(found == fields.end()) { simple = false; break; }
			parts.push_back({{"id", found->id}, {"template", match.str()}}); pos = match.position() + match.length();
		}
		if(!simple || pos != body.size()) { parts = nlohmann::json::array(); if(!body.empty()) parts.push_back({{"id", "custom"}, {"template", body}}); }
		return {{"parts", parts}, {"separator", simple && !gap.empty() ? gap : separators.front()}};
	}
}

void core::validate_naming_template(const std::string &value)
{
	if(value.find_first_of("\"\r\n") != std::string::npos || value.find('\0') != std::string::npos) throw application_error {"invalidTemplate", "web.invalid_template"};
	for(size_t pos {0}; (pos = value.find('%', pos)) != std::string::npos;)
	{
		if(pos + 1 < value.size() && value[pos + 1] == '%') { pos += 2; continue; }
		if(pos + 1 < value.size() && value[pos + 1] == '(')
		{
			std::smatch match; const auto remaining {value.substr(pos)};
			if(!std::regex_search(remaining, match, token, std::regex_constants::match_continuous)) throw application_error {"invalidTemplate", "web.invalid_template"};
			pos += match.length();
		}
		else ++pos;
	}
}

std::string core::ordinary_template(const output_options &options)
{
	return !options.output_template.empty() ? options.output_template : options.naming == "title" ? "%(title)s.%(ext)s" : options.naming == "author" ?
		"%(uploader)s - %(title)s.%(ext)s" : "%(upload_date)s - %(uploader)s - %(title)s.%(ext)s";
}

nlohmann::json core::naming_catalog()
{
	nlohmann::json result {{"fields", nlohmann::json::array()}, {"templates", nlohmann::json::object()}, {"bandcampDefault", output_options {}.bandcamp_template}, {"prefixDefault", output_options {}.playlist_prefix}};
	for(const auto &part : fields) result["fields"].push_back({{"id", part.id}, {"label", i18n::text(part.label)}});
	for(const auto &key : {"title", "author", "date"}) { output_options options; options.naming = key; result["templates"][key] = ordinary_template(options); }
	return result;
}

nlohmann::json core::preview_naming(const nlohmann::json &draft)
{
	const auto original {draft.value("outputTemplate", "")}; validate_naming_template(original);
	auto rule {draft.contains("parts") ? draft : describe(original)};
	const auto separator {rule.value("separator", " - ")};
	if(std::find(separators.begin(), separators.end(), separator) == separators.end()) throw application_error {"invalidPayload", "web.invalid_payload"};
	std::string output, sample, name; nlohmann::json labels = nlohmann::json::array();
	for(const auto &part : rule.at("parts"))
	{
		const auto id {part.at("id").get<std::string>()}; const auto found {std::find_if(fields.begin(), fields.end(), [&](const auto &field) { return field.id == id; })};
		if(found == fields.end() && id != "custom") throw application_error {"invalidPayload", "web.invalid_payload"};
		const auto fragment {part.value("template", found == fields.end() ? "" : value(*found))}; validate_naming_template(fragment);
		const auto label {found == fields.end() ? i18n::text("ui.existing_custom_rule") : i18n::text(found->label)};
		if(!labels.empty()) { output += separator; sample += separator; name += separator; }
		output += fragment; sample += found == fields.end() ? i18n::text("ui.existing_custom_content") : text(found->sample); name += label; labels.push_back(label);
	}
	if(!rule.at("parts").empty()) { output += ".%(ext)s"; sample += "." + draft.value("extension", "mp3"); }
	return {{"parts", rule.at("parts")}, {"separator", separator}, {"labels", labels}, {"name", name}, {"filename", sample},
		{"outputTemplate", draft.contains("parts") ? output : original}, {"canApply", !rule.at("parts").empty() && !(draft.contains("parts") ? output : original).empty()}};
}

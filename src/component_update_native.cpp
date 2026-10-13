#include "components.hpp"
#include "core_text.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <Shlobj.h>
#include <sddl.h>
#include <aclapi.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <regex>
#include <set>
#include <utility>

namespace
{
	using namespace core;
	constexpr std::uint64_t max_file {512ull * 1024 * 1024}, max_expanded {1536ull * 1024 * 1024};
	struct file_handle
	{
		HANDLE value {INVALID_HANDLE_VALUE};
		file_handle(HANDLE handle = INVALID_HANDLE_VALUE) : value {handle} {}
		file_handle(const file_handle &) = delete;
		file_handle(file_handle &&other) noexcept : value {std::exchange(other.value, INVALID_HANDLE_VALUE)} {}
		file_handle &operator=(file_handle &&other) noexcept { if(value != INVALID_HANDLE_VALUE) CloseHandle(value); value = std::exchange(other.value, INVALID_HANDLE_VALUE); return *this; }
		~file_handle() { if(value != INVALID_HANDLE_VALUE) CloseHandle(value); }
	};
	struct http_handle { HINTERNET value {nullptr}; ~http_handle() { if(value) WinHttpCloseHandle(value); } };
	[[noreturn]] void fail(const std::string &code, DWORD system = 0)
	{
		application_error error {code, "web.update." + code}; error.detail.exit_code = system; throw error;
	}
	void cancelled(std::atomic_bool &working) { if(!working) fail("cancelled"); }
	std::string hash_file(HANDLE file, std::atomic_bool &working)
	{
		BCRYPT_ALG_HANDLE algorithm {nullptr}; BCRYPT_HASH_HANDLE hash {nullptr};
		if(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) fail("hashFailed");
		struct close_hash { BCRYPT_ALG_HANDLE algorithm; BCRYPT_HASH_HANDLE &hash; ~close_hash() { if(hash) BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(algorithm, 0); } } guard {algorithm, hash};
		if(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) != 0) fail("hashFailed");
		LARGE_INTEGER beginning {}; if(!SetFilePointerEx(file, beginning, nullptr, FILE_BEGIN)) fail("hashFailed", GetLastError());
		std::array<unsigned char, 65536> buffer; DWORD count {0};
		for(;;) { cancelled(working); if(!ReadFile(file, buffer.data(), DWORD(buffer.size()), &count, nullptr)) fail("hashFailed", GetLastError()); if(!count) break; if(BCryptHashData(hash, buffer.data(), count, 0) != 0) fail("hashFailed"); }
		unsigned char digest[32]; if(BCryptFinishHash(hash, digest, sizeof(digest), 0) != 0) fail("hashFailed");
		std::string result; for(const auto byte : digest) { result += "0123456789abcdef"[byte >> 4]; result += "0123456789abcdef"[byte & 15]; } return result;
	}
	file_handle guarded_file(const fs::path &path, bool rename, bool missing = false, DWORD sharing = FILE_SHARE_READ)
	{
		file_handle handle {CreateFileW(path.c_str(), GENERIC_READ | (rename ? DELETE : 0), sharing, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
		if(handle.value == INVALID_HANDLE_VALUE) { const auto error {GetLastError()}; if(missing && error == ERROR_FILE_NOT_FOUND) return handle; fail(error == ERROR_SHARING_VIOLATION ? "targetBusy" : "targetUnavailable", error); }
		BY_HANDLE_FILE_INFORMATION info {}; if(!GetFileInformationByHandle(handle.value, &info)) fail("targetUnavailable", GetLastError());
		if(info.nNumberOfLinks != 1 || info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) fail("sharedFile"); return handle;
	}
	void same_file(HANDLE first, HANDLE second)
	{
		BY_HANDLE_FILE_INFORMATION left {}, right {}; if(!GetFileInformationByHandle(first, &left) || !GetFileInformationByHandle(second, &right)) fail("targetUnavailable", GetLastError());
		if(left.dwVolumeSerialNumber != right.dwVolumeSerialNumber || left.nFileIndexHigh != right.nFileIndexHigh || left.nFileIndexLow != right.nFileIndexLow) fail("recoveryConflict");
	}
	file_handle validation_file(const fs::path &path, file_handle &current)
	{
		// 接续拒写句柄后释放 DELETE，再取得拒删的只读锁；身份不符时禁止执行未知程序。
		auto transition {guarded_file(path, false, false, FILE_SHARE_READ | FILE_SHARE_DELETE)}; same_file(current.value, transition.value); current = file_handle {};
		auto final {guarded_file(path, false)}; same_file(transition.value, final.value); return final;
	}
	void rename_file(HANDLE file, const fs::path &path, const std::string &error)
	{
		const auto text {path.wstring()}; std::vector<unsigned char> data(sizeof(FILE_RENAME_INFO) + text.size() * sizeof(wchar_t));
		const auto info {reinterpret_cast<FILE_RENAME_INFO *>(data.data())}; info->ReplaceIfExists = FALSE; info->RootDirectory = nullptr; info->FileNameLength = DWORD(text.size() * sizeof(wchar_t)); std::copy(text.begin(), text.end(), info->FileName);
		if(!SetFileInformationByHandle(file, FileRenameInfo, info, DWORD(data.size()))) fail(error, GetLastError());
	}
	std::wstring lower(std::wstring value) { std::transform(value.begin(), value.end(), value.begin(), towlower); return value; }
	bool inside(const fs::path &path, const fs::path &directory)
	{
		const auto left {lower(fs::absolute(path).lexically_normal().wstring())}, right {lower(fs::absolute(directory).lexically_normal().wstring())};
		return left.size() > right.size() && left.starts_with(right) && (left[right.size()] == L'\\' || left[right.size()] == L'/');
	}
	void plain_path(const fs::path &path)
	{
		const auto text {path.wstring()};
		if(!path.is_absolute() || text.starts_with(L"\\\\") || text.find(L':', 2) != std::wstring::npos || path != path.lexically_normal()) fail("unsafePath");
		for(auto current {path}; !current.empty(); current = current.parent_path())
		{
			const auto attributes {GetFileAttributesW(current.c_str())};
			if(attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) fail("unsafePath");
			if(current == current.parent_path()) break;
		}
	}
	void target_path(const fs::path &path, const component_environment &environment)
	{
		plain_path(path);
			if(!environment.test_origin.empty() || !environment.test_root.empty())
		{
			if(environment.test_root.empty() || !inside(path, environment.test_root / "install") || !fs::is_regular_file(environment.test_root / ".component-test-root")) fail("testIsolationRequired");
		}
		for(const auto id : {&FOLDERID_Windows, &FOLDERID_ProgramFiles, &FOLDERID_ProgramFilesX86, &FOLDERID_ProgramData})
		{
			PWSTR text {nullptr}; if(SUCCEEDED(SHGetKnownFolderPath(*id, 0, nullptr, &text))) { const fs::path directory {text}; CoTaskMemFree(text); if(inside(path, directory)) fail("protectedPath"); }
		}
		const auto text {lower(path.wstring())};
		for(const auto marker : {L"\\scoop\\", L"\\chocolatey\\", L"\\winget\\", L"\\.deno\\", L"\\node_modules\\", L"\\windowsapps\\"}) if(text.find(marker) != std::wstring::npos) fail("externalInstallation");
		if(fs::exists(path))
		{
			file_handle handle {CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr)}; BY_HANDLE_FILE_INFORMATION info {};
			if(handle.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.value, &info)) fail("targetUnavailable", GetLastError());
			if(info.nNumberOfLinks != 1 || info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) fail("sharedFile");
		}
	}
	fs::path update_root(const component_environment &environment)
	{
		fs::path root {environment.update_root};
			if(!environment.test_origin.empty() || !environment.test_root.empty())
		{
			if(environment.test_root.empty() || root != environment.test_root / "updates" || !fs::is_regular_file(environment.test_root / ".component-test-root")) fail("testIsolationRequired");
		}
		if(root.empty())
		{
			PWSTR text {nullptr}; if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &text))) fail("stagingUnavailable");
			root = fs::path {text} / "Temp" / "ytdlp-component-updates-v1"; CoTaskMemFree(text);
		}
		plain_path(root); return root;
	}
	void secure_root(const fs::path &root)
	{
		fs::create_directories(root.parent_path());
		file_handle token; if(!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) fail("stagingUnavailable", GetLastError());
		DWORD size {0}; GetTokenInformation(token.value, TokenUser, nullptr, 0, &size); std::vector<char> data(size);
		if(!GetTokenInformation(token.value, TokenUser, data.data(), size, &size)) fail("stagingUnavailable", GetLastError());
		PWSTR sid {nullptr}; if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid, &sid)) fail("stagingUnavailable", GetLastError());
			const auto acl {L"O:" + std::wstring {sid} + L"D:P(A;OICI;FA;;;" + std::wstring {sid} + L")"}; LocalFree(sid);
		PSECURITY_DESCRIPTOR descriptor {nullptr}; if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) fail("stagingUnavailable", GetLastError());
		SECURITY_ATTRIBUTES security {sizeof(security), descriptor, FALSE}; const auto created {CreateDirectoryW(root.c_str(), &security)}; const auto error {GetLastError()}; LocalFree(descriptor);
		if(!created && error != ERROR_ALREADY_EXISTS) fail("stagingUnavailable", error);
		plain_path(root);
			PSID owner {nullptr}; PACL permissions {nullptr}; PSECURITY_DESCRIPTOR existing {nullptr};
			const auto status {GetNamedSecurityInfoW(root.c_str(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr, &permissions, nullptr, &existing)};
			if(status != ERROR_SUCCESS) fail("stagingUnavailable", status);
			struct free_descriptor { PSECURITY_DESCRIPTOR value; ~free_descriptor() { LocalFree(value); } } guard {existing};
			SECURITY_DESCRIPTOR_CONTROL control {}; DWORD revision {0}; void *ace {nullptr};
			if(!EqualSid(owner, reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid) || !GetSecurityDescriptorControl(existing, &control, &revision) || !(control & SE_DACL_PROTECTED) || !permissions || permissions->AceCount != 1 || !GetAce(permissions, 0, &ace) || reinterpret_cast<ACE_HEADER *>(ace)->AceType != ACCESS_ALLOWED_ACE_TYPE) fail("stagingUnavailable");
			const auto allowed {reinterpret_cast<ACCESS_ALLOWED_ACE *>(ace)};
			if(allowed->Mask != FILE_ALL_ACCESS || !EqualSid(&allowed->SidStart, reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid)) fail("stagingUnavailable");
	}
	fs::path session_dir(const component_update_snapshot &snapshot, const component_environment &environment)
	{
		if(!std::regex_match(snapshot.update_id, std::regex {"[0-9a-f]{32}"})) fail("invalidTransaction");
		const auto directory {update_root(environment) / snapshot.update_id}; plain_path(directory); return directory;
	}
	fs::path sibling(const component_update_snapshot &snapshot, size_t index, const wchar_t *suffix)
	{
		return snapshot.targets.at(index).parent_path() / (L".ytdlp-update-" + to_wide(snapshot.update_id) + L"-" + snapshot.targets.at(index).filename().wstring() + suffix);
	}
	fs::path retained_file(const component_update_snapshot &snapshot, size_t index, const wchar_t *suffix)
	{
		return snapshot.targets.at(index).parent_path() / (L".ytdlp-" + to_wide(native_component_update_id()) + suffix);
	}
	void write_bytes(const fs::path &path, const void *data, size_t size)
	{
		plain_path(path); file_handle handle {CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
		DWORD written {0}; if(handle.value == INVALID_HANDLE_VALUE || size > MAXDWORD || !WriteFile(handle.value, data, DWORD(size), &written, nullptr) || written != size || !FlushFileBuffers(handle.value)) fail("transactionWriteFailed", GetLastError());
	}
	void journal(const component_update_snapshot &snapshot, const component_environment &environment)
	{
			if(environment.test_fault == "journal" && snapshot.state == "installed") fail("transactionWriteFailed", ERROR_ACCESS_DENIED);
		const auto directory {session_dir(snapshot, environment)};
		nlohmann::json value {{"schemaVersion", 1}, {"snapshot", component_update_json(snapshot)}, {"originalHashes", snapshot.original_hashes}, {"stagedHashes", snapshot.staged_hashes}};
		const auto text {value.dump()}; DATA_BLOB input {DWORD(text.size()), reinterpret_cast<BYTE *>(const_cast<char *>(text.data()))}, output {};
		if(!CryptProtectData(&input, L"Component transaction v1", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) fail("transactionWriteFailed", GetLastError());
		try { write_bytes(directory / "transaction.tmp", output.pbData, output.cbData); } catch(...) { LocalFree(output.pbData); throw; } LocalFree(output.pbData);
		if(!MoveFileExW((directory / "transaction.tmp").c_str(), (directory / "transaction.dat").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) fail("transactionWriteFailed", GetLastError());
	}
	void space(const fs::path &directory, std::uint64_t needed)
	{
		ULARGE_INTEGER free {}; if(!GetDiskFreeSpaceExW(directory.c_str(), &free, nullptr, nullptr)) fail("targetUnavailable", GetLastError());
		if(free.QuadPart < needed + 1024 * 1024) fail("insufficientSpace");
	}
	void writable_targets(component_update_snapshot &snapshot, const component_environment &environment, std::atomic_bool &working, bool remember)
	{
		if(remember) snapshot.original_hashes.clear();
		for(size_t n {0}; n < snapshot.targets.size(); ++n)
		{
			const auto &path {snapshot.targets[n]}; target_path(path, environment); cancelled(working);
			if(!fs::is_directory(path.parent_path())) fail("targetUnavailable");
			const bool exists {fs::exists(path)};
			if(exists)
			{
				file_handle handle {CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
				if(handle.value == INVALID_HANDLE_VALUE) fail(GetLastError() == ERROR_SHARING_VIOLATION ? "targetBusy" : "targetUnavailable", GetLastError());
			}
			file_handle probe {CreateFileW(sibling(snapshot, n, L".probe").c_str(), GENERIC_WRITE | DELETE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr)};
			if(probe.value == INVALID_HANDLE_VALUE) fail("targetUnavailable", GetLastError());
			const auto hash {exists ? component_file_hash(path, working) : ""};
			if(remember) snapshot.original_hashes.push_back(hash);
			else if(hash != snapshot.original_hashes.at(n)) fail("targetChanged");
		}
		space(snapshot.targets.front().parent_path(), snapshot.package.size * (snapshot.package.zip ? 5 : 2));
		if(environment.test_fault == "space") fail("insufficientSpace");
		if(environment.test_fault == "permission") fail("targetUnavailable", ERROR_ACCESS_DENIED);
	}
	std::uint16_t u16(const unsigned char *p) { return std::uint16_t(p[0]) | std::uint16_t(p[1]) << 8; }
	std::uint32_t u32(const unsigned char *p) { return std::uint32_t(u16(p)) | std::uint32_t(u16(p + 2)) << 16; }
	std::vector<unsigned char> read_at(HANDLE file, std::uint64_t position, size_t count)
	{
		std::vector<unsigned char> result(count); LARGE_INTEGER offset; offset.QuadPart = position; DWORD read {0};
		if(!SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) || !ReadFile(file, result.data(), DWORD(count), &read, nullptr) || read != count) fail("packageType", GetLastError()); return result;
	}
	std::vector<unsigned char> read_at(std::ifstream &file, std::uint64_t position, size_t count)
	{
		std::vector<unsigned char> result(count); file.clear(); file.seekg(position); file.read(reinterpret_cast<char *>(result.data()), count);
		if(size_t(file.gcount()) != count) fail("invalidArchive"); return result;
	}
	struct zip_entry { std::string name; unsigned method, flags, crc; std::uint64_t packed, size, offset; };
	void safe_zip_name(const std::string &name)
	{
		if(name.empty() || name.size() > 512 || name[0] == '/' || name.find_first_of("\\:\0") != std::string::npos) fail("unsafeArchivePath");
		std::string normalized {name}; if(normalized.back() == '/') normalized.pop_back();
		if(normalized.empty()) fail("unsafeArchivePath");
		for(size_t start {0}, end; start < normalized.size(); start = end + 1)
		{
			end = normalized.find('/', start); if(end == std::string::npos) end = normalized.size(); const auto part {normalized.substr(start, end - start)};
			if(part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ' || std::regex_match(part, std::regex {R"((?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?)", std::regex::icase})) fail("unsafeArchivePath");
			for(const unsigned char ch : part) if(ch < 32 || ch > 126 || std::string {"<>\"|?*"}.find(char(ch)) != std::string::npos) fail("unsafeArchivePath");
		}
	}
	std::vector<zip_entry> zip_entries(std::ifstream &file, std::uint64_t size)
	{
		if(size < 22 || size > max_file) fail("invalidArchive"); const auto tail {read_at(file, size - std::min<std::uint64_t>(size, 65557), size_t(std::min<std::uint64_t>(size, 65557)))};
		size_t end {tail.size() - 22}; while(u32(tail.data() + end) != 0x06054b50 || end + 22 + u16(tail.data() + end + 20) != tail.size()) { if(!end) fail("invalidArchive"); --end; }
		const auto p {tail.data() + end}; const auto count {u16(p + 10)}; const std::uint64_t directory {u32(p + 16)}, bytes {u32(p + 12)};
		if(u16(p + 4) || u16(p + 6) || u16(p + 8) != count || !count || count > 4096 || directory + bytes != size - tail.size() + end) fail("archiveLimit");
		std::vector<zip_entry> result; std::set<std::string> names; std::uint64_t total {0}, cursor {directory};
		for(unsigned n {0}; n < count; ++n)
		{
			if(cursor + 46 > directory + bytes) fail("invalidArchive"); const auto header {read_at(file, cursor, 46)}; const auto h {header.data()};
			if(u32(h) != 0x02014b50) fail("invalidArchive");
			zip_entry entry {{}, u16(h + 10), u16(h + 8), u32(h + 16), u32(h + 20), u32(h + 24), u32(h + 42)};
		const unsigned length {u16(h + 28)}, extra {u16(h + 30)}, comment {u16(h + 32)}, mode {u32(h + 38) >> 16 & 0170000};
			if(cursor + 46 + length + extra + comment > directory + bytes || u16(h + 34) || (mode && mode != 0100000 && mode != 0040000) || u32(h + 38) & FILE_ATTRIBUTE_REPARSE_POINT) fail("unsafeArchiveLink");
			const auto name {read_at(file, cursor + 46, length)}; entry.name.assign(name.begin(), name.end()); safe_zip_name(entry.name);
			std::string key {entry.name}; std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return char(tolower(ch)); });
			if(!names.insert(key).second || entry.flags & ~0x80eu || entry.method != 0 && entry.method != 8 || entry.size > max_file || entry.packed > max_file || entry.offset >= directory || (total += entry.size) > max_expanded) fail("archiveLimit");
			const auto local {read_at(file, entry.offset, 30)};
			if(u32(local.data()) != 0x04034b50 || u16(local.data() + 6) != entry.flags || u16(local.data() + 8) != entry.method || u16(local.data() + 26) != length) fail("invalidArchive");
			const auto local_name {read_at(file, entry.offset + 30, length)}; if(local_name != name) fail("unsafeArchivePath");
			entry.offset += 30 + length + u16(local.data() + 28);
			if(entry.offset + entry.packed > directory || !(entry.flags & 8) && (u32(local.data() + 14) != entry.crc || u32(local.data() + 18) != entry.packed || u32(local.data() + 22) != entry.size)) fail("invalidArchive");
			result.push_back(entry); cursor += 46 + length + extra + comment;
		}
		if(cursor != directory + bytes) fail("invalidArchive"); return result;
	}
	void extract_entry(std::ifstream &file, const zip_entry &entry, const fs::path &target, std::atomic_bool &working)
	{
		std::ofstream output {target, std::ios::binary | std::ios::trunc}; if(!output) fail("stagingUnavailable");
		z_stream stream {}; if(entry.method == 8 && inflateInit2(&stream, -MAX_WBITS) != Z_OK) fail("invalidArchive");
		struct end_inflate { z_stream *stream; bool enabled; ~end_inflate() { if(enabled) inflateEnd(stream); } } guard {&stream, entry.method == 8};
		std::array<unsigned char, 65536> input, buffer; std::uint64_t read {0}, written {0}; uLong crc {crc32(0, Z_NULL, 0)}; bool ended {entry.method == 0};
		file.clear(); file.seekg(entry.offset);
		while(read < entry.packed)
		{
			cancelled(working); const auto count {size_t(std::min<std::uint64_t>(input.size(), entry.packed - read))}; file.read(reinterpret_cast<char *>(input.data()), count); if(size_t(file.gcount()) != count) fail("invalidArchive"); read += count;
			stream.next_in = input.data(); stream.avail_in = uInt(count);
			do
			{
				stream.next_out = buffer.data(); stream.avail_out = uInt(buffer.size());
				int status {Z_OK}; size_t produced {count}; const unsigned char *data {input.data()};
				if(entry.method == 8) { status = inflate(&stream, Z_NO_FLUSH); produced = buffer.size() - stream.avail_out; data = buffer.data(); if(status != Z_OK && status != Z_STREAM_END) fail("invalidArchive"); if(status == Z_STREAM_END) ended = true; }
				if((written += produced) > entry.size) fail("archiveLimit"); output.write(reinterpret_cast<const char *>(data), produced); if(!output) fail("stagingUnavailable"); crc = crc32(crc, data, uInt(produced));
				if(entry.method == 0 || ended) break;
			} while(stream.avail_in || stream.avail_out == 0);
			if(entry.method == 8 && ended && (stream.avail_in || read != entry.packed)) fail("invalidArchive");
		}
		if(!ended || written != entry.size || crc != entry.crc) fail("invalidArchive"); output.close();
	}
	void unpack(component_update_snapshot &snapshot, const component_environment &environment, std::atomic_bool &working, const component_update_callback &progress)
	{
		const auto directory {session_dir(snapshot, environment)}; const auto archive {directory / "package.bin"};
		if(!snapshot.package.zip) { fs::copy_file(archive, directory / snapshot.targets.front().filename()); snapshot.completed_files = 1; progress(snapshot); return; }
		std::ifstream file {archive, std::ios::binary}; const auto entries {zip_entries(file, fs::file_size(archive))}; std::string prefix;
		for(size_t n {0}; n < snapshot.targets.size(); ++n)
		{
			const auto tool {to_utf8(snapshot.targets[n].filename().wstring())}; const zip_entry *selected {nullptr};
			for(const auto &entry : entries)
			{
				const bool match {snapshot.component == "deno" ? entry.name == "deno.exe" : std::regex_match(entry.name, std::regex {"[^/]+/bin/" + tool})};
				if(match) { if(selected) fail("invalidArchive"); selected = &entry; }
			}
			if(!selected) fail("packageFilesMissing");
			if(snapshot.component == "ffmpeg") { const auto current {selected->name.substr(0, selected->name.find('/'))}; if(!prefix.empty() && prefix != current) fail("packageFilesMissing"); prefix = current; }
			extract_entry(file, *selected, directory / snapshot.targets[n].filename(), working); snapshot.completed_files = n + 1; progress(snapshot);
		}
	}
	std::string verify_tools(const component_update_snapshot &snapshot, const fs::path &directory, std::atomic_bool &working)
	{
		std::string version;
		for(const auto &target : snapshot.targets)
		{
			const auto path {directory.empty() ? target : directory / target.filename()};
			if(component_file_architecture(path) != snapshot.package.architecture) fail("wrongArchitecture");
			const auto tool {snapshot.component == "ffmpeg" ? to_utf8(target.stem().wstring()) : snapshot.component};
			const auto found {component_tool_version(path, tool, working, snapshot.component == "ffmpeg")};
			if(version.empty()) version = found; else if(version != found) fail("toolVersionMismatch");
			if(snapshot.component == "yt-dlp" || snapshot.component == "deno") if(compare_component_versions(snapshot.component, found, snapshot.package.version) != "equal") fail("versionMismatch");
		}
		return version;
	}
	void cleanup(component_update_snapshot &snapshot, const component_environment &environment)
	{
		// 只移除本会话创建的已知暂存文件；备份和事务结果继续保留。
		try
		{
			const auto directory {session_dir(snapshot, environment)};
			for(const auto &target : snapshot.targets) { plain_path(directory / target.filename()); fs::remove(directory / target.filename()); }
			fs::remove(directory / "package.bin");
			for(size_t n {0}; n < snapshot.targets.size(); ++n) { const auto path {sibling(snapshot, n, L".new")}; target_path(snapshot.targets[n], environment); plain_path(path); fs::remove(path); }
		}
		catch(const std::exception &) { if(!snapshot.error) snapshot.error = failure {"cleanupFailed", "web.update.cleanupFailed"}; }
	}
	void rollback(component_update_snapshot &snapshot, const component_environment &environment, const component_update_callback &progress)
	{
			snapshot.state = "rollingBack"; snapshot.recovery_required = true; snapshot.cancellable = false; progress(snapshot); journal(snapshot, environment); std::atomic_bool working {true};
		for(size_t count {snapshot.targets.size()}; count; --count)
		{
			const auto n {count - 1}; const auto &target {snapshot.targets[n]}; target_path(target, environment);
			const auto backup {sibling(snapshot, n, L".bak")}; plain_path(backup);
			auto current_file {guarded_file(target, true, true)}; const auto current {current_file.value == INVALID_HANDLE_VALUE ? "" : hash_file(current_file.value, working)}; const auto &original {snapshot.original_hashes.at(n)};
			if(current == original) continue;
			if(!current.empty() && current != snapshot.staged_hashes.at(n)) fail("recoveryConflict");
			if(environment.test_fault == "rollback") fail("rollbackFailed", ERROR_ACCESS_DENIED);
			if(original.empty())
			{
				if(!current.empty()) rename_file(current_file.value, retained_file(snapshot, n, L".failed"), "rollbackFailed");
			}
			else
			{
				if(!fs::is_regular_file(backup)) fail("backupInvalid"); auto backup_file {guarded_file(backup, false)}; if(hash_file(backup_file.value, working) != original) fail("backupInvalid");
				const auto restore {sibling(snapshot, n, L".restore")}; plain_path(restore); auto restore_file {guarded_file(restore, true, true)};
				// 完整副本可重用；中断的部分副本保留在唯一旁路文件中，不覆盖未知内容。
				if(restore_file.value != INVALID_HANDLE_VALUE && hash_file(restore_file.value, working) != original) { rename_file(restore_file.value, retained_file(snapshot, n, L".restore"), "rollbackFailed"); restore_file = file_handle {}; }
				if(restore_file.value == INVALID_HANDLE_VALUE) { if(!CopyFileW(backup.c_str(), restore.c_str(), TRUE)) fail("rollbackFailed", GetLastError()); { file_handle file {CreateFileW(restore.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr)}; if(file.value == INVALID_HANDLE_VALUE || !FlushFileBuffers(file.value)) fail("rollbackFailed", GetLastError()); } restore_file = guarded_file(restore, true); }
				if(hash_file(restore_file.value, working) != original) fail("backupInvalid");
				if(environment.test_fault == "restore-crash") TerminateProcess(GetCurrentProcess(), 73);
				if(!current.empty()) rename_file(current_file.value, retained_file(snapshot, n, L".failed"), "rollbackFailed");
				rename_file(restore_file.value, target, "rollbackFailed");
				if(hash_file(restore_file.value, working) != original) fail("rollbackFailed");
				restore_file = validation_file(target, restore_file);
				component_tool_version(target, snapshot.component == "ffmpeg" ? to_utf8(target.stem().wstring()) : snapshot.component, working);
				const auto checked {guarded_file(target, false, false, FILE_SHARE_READ | FILE_SHARE_DELETE)}; same_file(restore_file.value, checked.value); if(hash_file(restore_file.value, working) != original) fail("rollbackFailed");
					if(environment.test_fault == "rollback-crash") TerminateProcess(GetCurrentProcess(), 73);
			}
		}
	snapshot.state = "rolledBack"; snapshot.recovery_required = false; snapshot.recovery_error.reset(); journal(snapshot, environment); progress(snapshot);
	}
}

std::string core::native_component_update_id()
{
	unsigned char bytes[16]; if(BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) fail("stagingUnavailable");
	std::string result; for(const auto byte : bytes) { result += "0123456789abcdef"[byte >> 4]; result += "0123456789abcdef"[byte & 15]; } return result;
}

std::string core::native_component_architecture()
{
	using version_function = LONG(WINAPI *)(OSVERSIONINFOW *); const auto function {reinterpret_cast<version_function>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))}; OSVERSIONINFOW version {sizeof(version)};
	if(!function || function(&version) != 0 || version.dwMajorVersion < 10) fail("unsupportedPlatform");
	SYSTEM_INFO system {}; GetNativeSystemInfo(&system);
	return system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "x64" : system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? "x86" : system.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "arm64" : "unknown";
}

std::string core::component_file_architecture(const fs::path &path)
{
	file_handle file {CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr)}; if(file.value == INVALID_HANDLE_VALUE) fail("packageType"); const auto dos {read_at(file.value, 0, 64)};
	if(u16(dos.data()) != 0x5a4d || u32(dos.data() + 60) > 1024 * 1024) fail("packageType"); const auto pe {read_at(file.value, u32(dos.data() + 60), 24)};
	if(u32(pe.data()) != 0x00004550 || u16(pe.data() + 22) & IMAGE_FILE_DLL || !(u16(pe.data() + 22) & IMAGE_FILE_EXECUTABLE_IMAGE)) fail("packageType");
	return u16(pe.data() + 4) == IMAGE_FILE_MACHINE_AMD64 ? "x64" : u16(pe.data() + 4) == IMAGE_FILE_MACHINE_I386 ? "x86" : u16(pe.data() + 4) == IMAGE_FILE_MACHINE_ARM64 ? "arm64" : "unknown";
}

std::string core::component_file_hash(const fs::path &path, std::atomic_bool &working)
{
	file_handle file {CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)}; if(file.value == INVALID_HANDLE_VALUE) fail("hashFailed", GetLastError()); return hash_file(file.value, working);
}

core::component_update_snapshot core::prepare_component_install(component_update_snapshot snapshot, const component_environment &environment, const std::string &proxy, std::atomic_bool &working, bool inspect_only)
{
	const auto native {native_component_architecture()};
	if(snapshot.component == "gui") fail("guiSourceUnavailable");
	if(snapshot.targets.empty()) fail("targetUnavailable");
	if(snapshot.component != "deno" && !fs::is_regular_file(snapshot.targets.front())) fail("installNotSupported");
	if(inspect_only) for(const auto &path : snapshot.targets) { target_path(path, environment); snapshot.original_hashes.push_back(fs::exists(path) ? component_file_hash(path, working) : ""); }
	else writable_targets(snapshot, environment, working, true);
	if(fs::exists(snapshot.targets.front()))
	{
		if(component_file_architecture(snapshot.targets.front()) != snapshot.package.architecture) fail("wrongArchitecture");
		if(snapshot.component == "yt-dlp")
		{
			const auto current {component_tool_version(snapshot.targets.front(), "yt-dlp", working)};
			const bool nightly {std::count(current.begin(), current.end(), '.') == 3}; component_snapshot local; local.id = "yt-dlp"; local.channel = nightly ? "nightly" : "stable";
			if(compare_component_versions("yt-dlp", current, current) != "equal") fail("nonOfficialInstallation");
			local.source = component_source(local.id, local.channel); const auto url {local.source.substr(0, local.source.size() - 6) + "tags/" + current};
			const auto response {component_http_get(url, proxy, environment, working, 1024 * 1024)};
			if(!response.error.empty() || response.status != 200) fail("localIdentityUnavailable", response.system_error ? response.system_error : response.status);
			const auto package {select_component_package(local, nlohmann::json::parse(response.body), snapshot.package.architecture)};
			if(package.sha256 != snapshot.original_hashes.front()) fail("nonOfficialInstallation");
		}
		else if(snapshot.component == "ffmpeg")
		{
			std::string version; for(size_t n {0}; n < 2; ++n) { const auto found {component_tool_version(snapshot.targets.at(n), n ? "ffprobe" : "ffmpeg", working, true)}; if(n && found != version) fail("toolVersionMismatch"); version = found; }
		}
		else if(compare_component_versions("deno", component_tool_version(snapshot.targets.front(), "deno", working), snapshot.package.version) == "unknown") fail("externalInstallation");
	}
	if(!inspect_only) { const auto root {update_root(environment)}; secure_root(root); space(root, snapshot.package.size * (snapshot.package.zip ? 6 : 2)); }
	snapshot.state = "waitingConfirmation"; return snapshot;
}

core::component_update_snapshot core::execute_component_install(component_update_snapshot snapshot, const component_environment &environment, const std::string &proxy, std::atomic_bool &working, const component_update_callback &progress, const std::function<void()> &enter_commit)
{
	const auto root {update_root(environment)}; secure_root(root);
	file_handle lock {CreateFileW((root / "update.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
	if(lock.value == INVALID_HANDLE_VALUE) fail("updateBusy", GetLastError());
	for(const auto &old : read_component_transactions(environment)) if(old.recovery_required) fail("recoveryRequired");
	const auto directory {session_dir(snapshot, environment)}; if(!CreateDirectoryW(directory.c_str(), nullptr)) fail("stagingUnavailable", GetLastError());
	bool committing {false};
	try
	{
			snapshot.state = "preparingInstall"; progress(snapshot); writable_targets(snapshot, environment, working, false); cancelled(working); journal(snapshot, environment);
		snapshot.state = "downloading"; progress(snapshot); std::ofstream output {directory / "package.bin", std::ios::binary}; if(!output) fail("stagingUnavailable");
		const auto response {component_http_get(snapshot.package.url, proxy, environment, working, max_file, [&](const char *data, size_t count, std::uint64_t total) { output.write(data, count); if(!output) fail("stagingUnavailable"); snapshot.downloaded += count; snapshot.total = total; progress(snapshot); })}; output.close();
		if(!response.error.empty()) fail(response.error, response.system_error); if(response.status != 200) fail("httpError", response.status);
		cancelled(working); snapshot.state = "verifying"; progress(snapshot);
		if(snapshot.downloaded != snapshot.package.size) fail("incompleteDownload"); if(component_file_hash(directory / "package.bin", working) != snapshot.package.sha256) fail("hashMismatch");
		snapshot.state = "unpacking"; snapshot.total_files = snapshot.targets.size(); progress(snapshot); unpack(snapshot, environment, working, progress);
		snapshot.state = "preparingInstall"; progress(snapshot); snapshot.result_version = verify_tools(snapshot, directory, working);
		for(const auto &path : snapshot.targets) snapshot.staged_hashes.push_back(component_file_hash(directory / path.filename(), working));
		writable_targets(snapshot, environment, working, false); cancelled(working);
		for(size_t n {0}; n < snapshot.targets.size(); ++n)
		{
			if(fs::exists(sibling(snapshot, n, L".bak")) || !CopyFileW((directory / snapshot.targets[n].filename()).c_str(), sibling(snapshot, n, L".new").c_str(), TRUE)) fail("replacementFailed", GetLastError());
			{ file_handle file {CreateFileW(sibling(snapshot, n, L".new").c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr)}; if(file.value == INVALID_HANDLE_VALUE || !FlushFileBuffers(file.value)) fail("replacementFailed", GetLastError()); }
			if(component_file_hash(sibling(snapshot, n, L".new"), working) != snapshot.staged_hashes[n]) fail("hashMismatch");
		}
			cancelled(working); enter_commit(); committing = true; snapshot.cancellable = false; snapshot.completed_files = 0; snapshot.state = "replacing"; snapshot.recovery_required = true;
			if(std::any_of(snapshot.original_hashes.begin(), snapshot.original_hashes.end(), [](const auto &hash) { return !hash.empty(); })) snapshot.backup_directory = to_utf8(snapshot.targets.front().parent_path().wstring());
			journal(snapshot, environment); progress(snapshot);
		std::atomic_bool verify_working {true};
		std::vector<file_handle> originals, replacements;
		for(size_t n {0}; n < snapshot.targets.size(); ++n)
		{
			target_path(snapshot.targets[n], environment);
			auto original {guarded_file(snapshot.targets[n], true, true)}; const auto current {original.value == INVALID_HANDLE_VALUE ? "" : hash_file(original.value, verify_working)}; if(current != snapshot.original_hashes[n]) fail("targetChanged");
			auto replacement {guarded_file(sibling(snapshot, n, L".new"), true)}; if(hash_file(replacement.value, verify_working) != snapshot.staged_hashes[n]) fail("hashMismatch");
			if(n && (environment.test_fault == "replace" || environment.test_fault == "rollback")) fail("replacementFailed", ERROR_ACCESS_DENIED);
			// 同一句柄核验后无覆盖重命名，期间拒绝其他写入/删除；两次重命名由事务记录恢复。
			if(original.value != INVALID_HANDLE_VALUE) rename_file(original.value, sibling(snapshot, n, L".bak"), "replacementFailed");
			if(environment.test_fault == "backup-crash" && !n) TerminateProcess(GetCurrentProcess(), 73);
			rename_file(replacement.value, snapshot.targets[n], "replacementFailed"); originals.push_back(std::move(original)); replacements.push_back(std::move(replacement));
			snapshot.completed_files = n + 1; journal(snapshot, environment); progress(snapshot);
			if(environment.test_fault == "crash" && !n) TerminateProcess(GetCurrentProcess(), 73);
		}
		for(size_t n {0}; n < snapshot.targets.size(); ++n) replacements[n] = validation_file(snapshot.targets[n], replacements[n]);
		snapshot.result_version = verify_tools(snapshot, {}, verify_working);
		for(size_t n {0}; n < snapshot.targets.size(); ++n) { const auto checked {guarded_file(snapshot.targets[n], false, false, FILE_SHARE_READ | FILE_SHARE_DELETE)}; same_file(replacements[n].value, checked.value); if(hash_file(replacements[n].value, verify_working) != snapshot.staged_hashes[n]) fail("hashMismatch"); }
			if(environment.test_fault == "verify" || environment.test_fault == "rollback-crash" || environment.test_fault == "restore-crash") fail("versionMismatch");
		snapshot.state = "installed"; snapshot.recovery_required = false; journal(snapshot, environment); progress(snapshot);
	}
	catch(const application_error &error)
	{
		snapshot.failed_stage = snapshot.state; snapshot.error = error.detail; snapshot.result_version.clear(); snapshot.cancellable = false;
		if(committing)
		{
			try { rollback(snapshot, environment, progress); }
			catch(const application_error &recovery) { snapshot.state = "rollbackFailed"; snapshot.recovery_required = true; snapshot.recovery_error = recovery.detail; try { journal(snapshot, environment); } catch(...) {} progress(snapshot); }
		}
		else { snapshot.state = error.detail.code == "cancelled" ? "cancelled" : snapshot.failed_stage == "downloading" ? "downloadFailed" : snapshot.failed_stage == "verifying" || snapshot.failed_stage == "unpacking" || snapshot.failed_stage == "preparingInstall" ? "verificationFailed" : "replacementFailed"; journal(snapshot, environment); progress(snapshot); }
	}
	catch(const std::exception &)
	{
		snapshot.failed_stage = snapshot.state; snapshot.result_version.clear(); snapshot.error = failure {"updateFailed", "web.update.updateFailed"}; snapshot.cancellable = false;
		if(committing) { try { rollback(snapshot, environment, progress); } catch(...) { snapshot.state = "rollbackFailed"; snapshot.recovery_required = true; try { journal(snapshot, environment); } catch(...) {} } }
		else { snapshot.state = "verificationFailed"; journal(snapshot, environment); } progress(snapshot);
	}
		if(!snapshot.recovery_required) { cleanup(snapshot, environment); try { journal(snapshot, environment); } catch(const application_error &error) { if(!snapshot.error) snapshot.error = error.detail; } } return snapshot;
}

std::vector<core::component_update_snapshot> core::read_component_transactions(const component_environment &environment)
{
	if(!environment.test_origin.empty() && environment.test_root.empty()) return {};
	std::vector<component_update_snapshot> result; const auto root {update_root(environment)}; if(!fs::exists(root)) return result; unsigned count {0};
	for(const auto &entry : fs::directory_iterator(root))
	{
		if(!entry.is_directory()) continue; if(++count > 1024) fail("invalidTransaction");
		component_update_snapshot snapshot; snapshot.update_id = to_utf8(entry.path().filename().wstring()); const auto directory {session_dir(snapshot, environment)}; const auto path {directory / "transaction.dat"}; plain_path(path);
		if(!fs::exists(path)) continue; const auto size {fs::file_size(path)}; if(size > 65536) fail("invalidTransaction");
		std::ifstream file {path, std::ios::binary}; const auto data {read_at(file, 0, size_t(size))}; DATA_BLOB input {DWORD(size), const_cast<BYTE *>(data.data())}, output {};
		if(!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) fail("invalidTransaction", GetLastError());
		nlohmann::json record; try { record = nlohmann::json::parse(output.pbData, output.pbData + output.cbData); } catch(...) { LocalFree(output.pbData); fail("invalidTransaction"); } LocalFree(output.pbData);
		if(record.at("schemaVersion") != 1) fail("invalidTransaction"); const auto &value {record.at("snapshot")};
		if(value.at("updateId") != snapshot.update_id) fail("invalidTransaction"); snapshot.component = value.at("component"); if(snapshot.component != "yt-dlp" && snapshot.component != "ffmpeg" && snapshot.component != "deno") fail("invalidTransaction");
		snapshot.generation = value.at("generation"); snapshot.revision = value.at("revision"); snapshot.state = value.at("state"); snapshot.failed_stage = value.at("failedStage"); snapshot.backup_directory = value.at("backupDirectory"); snapshot.result_version = value.at("resultVersion"); snapshot.recovery_required = value.at("recoveryRequired"); snapshot.cancellable = false; snapshot.test_source = value.at("testSource"); snapshot.include_ffplay = value.at("includeFfplay");
		for(const auto &target : value.at("targets")) { auto parsed {fs::u8path(target.get<std::string>())}; target_path(parsed, environment); snapshot.targets.push_back(parsed); }
		snapshot.original_hashes = record.at("originalHashes").get<std::vector<std::string>>(); snapshot.staged_hashes = record.at("stagedHashes").get<std::vector<std::string>>();
		if(snapshot.targets.empty() || snapshot.targets.size() > 3 || snapshot.original_hashes.size() != snapshot.targets.size() || snapshot.recovery_required && snapshot.staged_hashes.size() != snapshot.targets.size()) fail("invalidTransaction");
		for(const auto &hash : snapshot.original_hashes) if(!hash.empty() && !std::regex_match(hash, std::regex {"[0-9a-f]{64}"})) fail("invalidTransaction"); for(const auto &hash : snapshot.staged_hashes) if(!std::regex_match(hash, std::regex {"[0-9a-f]{64}"})) fail("invalidTransaction");
		const auto &package {value.at("package")}; snapshot.package.component = snapshot.component; snapshot.package.tag = package.at("tag"); snapshot.package.version = package.at("version"); snapshot.package.repository = package.at("repository"); snapshot.package.asset = package.at("asset"); snapshot.package.url = package.at("url"); snapshot.package.sha256 = package.at("sha256"); snapshot.package.architecture = package.at("architecture"); snapshot.package.size = package.at("size");
		if(!value.at("error").is_null()) { const auto &error {value.at("error")}; snapshot.error = failure {error.at("code"), error.at("messageKey"), error.at("systemError")}; }
		if(value.contains("recoveryError") && !value.at("recoveryError").is_null()) { const auto &error {value.at("recoveryError")}; snapshot.recovery_error = failure {error.at("code"), error.at("messageKey"), error.at("systemError")}; }
		if(snapshot.state == "downloading" || snapshot.state == "verifying" || snapshot.state == "unpacking" || snapshot.state == "preparingInstall" || snapshot.state == "waitingConfirmation") { snapshot.failed_stage = snapshot.state; snapshot.state = "cancelled"; snapshot.error = failure {"interrupted", "web.update.interrupted"}; }
		else if(snapshot.recovery_required) { if(snapshot.failed_stage.empty()) snapshot.failed_stage = snapshot.state; snapshot.state = "replacementFailed"; if(!snapshot.error) snapshot.error = failure {"interrupted", "web.update.recoveryRequired"}; }
		result.push_back(snapshot);
	}
	return result;
}

core::component_update_snapshot core::recover_component_install(component_update_snapshot snapshot, const component_environment &environment, const component_update_callback &progress)
{
	const auto root {update_root(environment)}; file_handle lock {CreateFileW((root / "update.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, 0, nullptr)}; if(lock.value == INVALID_HANDLE_VALUE) fail("updateBusy", GetLastError());
	try { rollback(snapshot, environment, progress); cleanup(snapshot, environment); }
	catch(const application_error &error) { snapshot.state = "rollbackFailed"; snapshot.recovery_error = error.detail; snapshot.recovery_required = true; journal(snapshot, environment); progress(snapshot); }
	return snapshot;
}

core::release_response core::component_http_get(const std::string &source, const std::string &proxy, const component_environment &environment, std::atomic_bool &working, std::uint64_t limit, const std::function<void(const char *, size_t, std::uint64_t)> &sink)
{
	release_response result; auto url {to_wide(source)}; std::wstring proxy_name, proxy_user, proxy_password;
	if(!environment.test_origin.empty())
	{
		if(source.starts_with("https://api.github.com/")) url = to_wide(environment.test_origin + source.substr(22));
		else if(source.starts_with("https://github.com/")) url = to_wide(environment.test_origin + "/assets" + source.substr(18));
		else { result.error = "invalidSource"; return result; }
	}
	if(!proxy.empty())
	{
		const auto value {to_wide(proxy.find("://") == std::string::npos ? "http://" + proxy : proxy)}; URL_COMPONENTS parts {sizeof(parts)};
		parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = parts.dwUrlPathLength = DWORD(-1);
		if(!WinHttpCrackUrl(value.c_str(), DWORD(value.size()), 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTP || !parts.dwHostNameLength || parts.dwUrlPathLength > 1) { result.error = "unsupportedProxy"; return result; }
		proxy_name = std::wstring {parts.lpszHostName, parts.dwHostNameLength} + L":" + std::to_wstring(parts.nPort);
		if(parts.dwUserNameLength) proxy_user.assign(parts.lpszUserName, parts.dwUserNameLength); if(parts.dwPasswordLength) proxy_password.assign(parts.lpszPassword, parts.dwPasswordLength);
	}
	http_handle session {WinHttpOpen(L"ytdlp-interface-components/2", proxy_name.empty() ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_NAMED_PROXY, proxy_name.empty() ? WINHTTP_NO_PROXY_NAME : proxy_name.c_str(), WINHTTP_NO_PROXY_BYPASS, 0)};
	const auto failed = [&]() { result.system_error = GetLastError(); result.error = !working ? "cancelled" : result.system_error == ERROR_WINHTTP_TIMEOUT ? "networkTimeout" : result.system_error == ERROR_WINHTTP_NAME_NOT_RESOLVED || result.system_error == ERROR_WINHTTP_CANNOT_CONNECT ? "offline" : "networkFailed"; return result; };
	if(!session.value || !WinHttpSetTimeouts(session.value, 4000, 4000, 4000, 4000)) return failed();
	for(unsigned redirects {0}; redirects <= 3; ++redirects)
	{
		if(!working) { result.error = "cancelled"; return result; }
		URL_COMPONENTS parts {sizeof(parts)}; parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);
		if(!WinHttpCrackUrl(url.c_str(), DWORD(url.size()), 0, &parts)) return failed();
		const std::wstring host {parts.lpszHostName, parts.dwHostNameLength}, route {std::wstring {parts.lpszUrlPath, parts.dwUrlPathLength} + (parts.dwExtraInfoLength ? std::wstring {parts.lpszExtraInfo, parts.dwExtraInfoLength} : L"")};
		const bool allowed {environment.test_origin.empty() ? parts.nScheme == INTERNET_SCHEME_HTTPS && parts.nPort == 443 && (redirects ? host == L"release-assets.githubusercontent.com" || host == L"objects.githubusercontent.com" : host == L"api.github.com" || host == L"github.com") : parts.nScheme == INTERNET_SCHEME_HTTP && host == L"127.0.0.1" && url.starts_with(to_wide(environment.test_origin + "/"))};
		if(!allowed || parts.dwUserNameLength || parts.dwPasswordLength || route.find(L'#') != std::wstring::npos) { result.error = "invalidSource"; return result; }
		http_handle connection {WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)}; if(!connection.value) return failed();
		http_handle request {WinHttpOpenRequest(connection.value, L"GET", route.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
		DWORD disabled {WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_REDIRECTS}, auth {WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH}, timeout {4000};
		if(!request.value || !WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)) || !WinHttpSetOption(request.value, WINHTTP_OPTION_AUTOLOGON_POLICY, &auth, sizeof(auth)) || !WinHttpSetOption(request.value, WINHTTP_OPTION_RECEIVE_RESPONSE_TIMEOUT, &timeout, sizeof(timeout))) return failed();
		if(!proxy_user.empty() && !WinHttpSetCredentials(request.value, WINHTTP_AUTH_TARGET_PROXY, WINHTTP_AUTH_SCHEME_BASIC, proxy_user.c_str(), proxy_password.c_str(), nullptr)) return failed();
		const wchar_t *headers {sink ? L"Accept: application/octet-stream\r\nAccept-Encoding: identity\r\n" : L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\nAccept-Encoding: identity\r\n"};
		if(!WinHttpSendRequest(request.value, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(request.value, nullptr)) return failed();
		DWORD status {0}, size {sizeof(status)}; if(!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) return failed(); result.status = status;
		const auto header = [&](DWORD kind) { DWORD bytes {0}; WinHttpQueryHeaders(request.value, kind, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes, WINHTTP_NO_HEADER_INDEX); if(!bytes || bytes > 16384) return std::wstring {}; std::wstring value(bytes / sizeof(wchar_t), L'\0'); if(!WinHttpQueryHeaders(request.value, kind, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &bytes, WINHTTP_NO_HEADER_INDEX)) return std::wstring {}; value.resize(bytes / sizeof(wchar_t)); return value; };
		if(status == 301 || status == 302 || status == 303 || status == 307 || status == 308)
		{
			if(!sink || redirects == 3) { result.error = "invalidSource"; return result; } url = header(WINHTTP_QUERY_LOCATION); if(url.empty()) { result.error = "invalidSource"; return result; } continue;
		}
		if(status != 200) return result;
		const auto encoding {header(WINHTTP_QUERY_CONTENT_ENCODING)}; if(!encoding.empty() && encoding != L"identity") { result.error = "invalidContent"; return result; }
		std::uint64_t total {0}, received {0}; const auto length {to_utf8(header(WINHTTP_QUERY_CONTENT_LENGTH))}; if(!length.empty()) { const auto parsed {std::from_chars(length.data(), length.data() + length.size(), total)}; if(parsed.ec != std::errc {} || parsed.ptr != length.data() + length.size() || total > limit) { result.error = "packageTooLarge"; return result; } }
		const auto deadline {std::chrono::steady_clock::now() + std::chrono::seconds {sink ? 600 : 12}};
		for(;;)
		{
			if(!working) { result.error = "cancelled"; return result; } char buffer[65536]; DWORD count {0}; if(!WinHttpReadData(request.value, buffer, sizeof(buffer), &count)) return failed(); if(!count) break;
			if((received += count) > limit) { result.error = "packageTooLarge"; return result; }
			if(sink) sink(buffer, count, total); else result.body.append(buffer, count);
			if(std::chrono::steady_clock::now() >= deadline) { result.error = "networkTimeout"; return result; }
		}
		if(!working) result.error = "cancelled"; else if(total && received != total) result.error = "incompleteDownload"; return result;
	}
	result.error = "invalidSource"; return result;
}

#include "process.hpp"
#include <Windows.h>
#include <algorithm>
#include <mutex>
#include <sstream>
#include <thread>

namespace
{
HWND process_window(DWORD pid)
{
	struct lparam_t { HWND hwnd {nullptr}; DWORD pid {0}; } lparam;

	WNDENUMPROC enumfn = [](HWND hwnd, LPARAM lparam) -> BOOL
	{
		auto &target {*reinterpret_cast<lparam_t*>(lparam)};
		DWORD pid {0};
		GetWindowThreadProcessId(hwnd, &pid);
		if(pid == target.pid)
		{
			target.hwnd = hwnd;
			return FALSE;
		}
		return TRUE;
	};

	lparam.pid = pid;
	EnumWindows(enumfn, reinterpret_cast<LPARAM>(&lparam));
	return lparam.hwnd;
}

}

std::string core::run_process
(
	std::wstring cmd, std::atomic_bool *working, append_callback cbappend, progress_callback cbprog, 
	std::atomic_bool *graceful_exit, std::string suppress, DWORD *process_exit_code, bool immediate_stop
)
{
	const std::string strtid {"[tid " + (std::stringstream {} << std::hex << std::this_thread::get_id()).str() + "]"};
	std::wstring modpath(4096, '\0');
	modpath.resize(GetModuleFileNameW(0, &modpath.front(), modpath.size()));

	std::string ret;
	HANDLE hPipeRead, hPipeWrite;

	SECURITY_ATTRIBUTES sa {sizeof(SECURITY_ATTRIBUTES)};
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

	if(!CreatePipe(&hPipeRead, &hPipeWrite, &sa, 0))
	{
		if(process_exit_code) *process_exit_code = GetLastError();
		return ret;
	}

	STARTUPINFOW si {sizeof(STARTUPINFOW)};
	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.hStdOutput = hPipeWrite;
	si.hStdError = hPipeWrite;
	si.wShowWindow = SW_HIDE;

	PROCESS_INFORMATION pi {0};
	// 将本次受控执行及子进程放入同一 Job，Stop 的强制回收不遗留 FFmpeg。
	HANDLE job {CreateJobObjectW(nullptr, nullptr)};

	BOOL res {CreateProcessW(NULL, &cmd.front(), NULL, NULL, TRUE, CREATE_NEW_CONSOLE|CREATE_NEW_PROCESS_GROUP|(job ? CREATE_SUSPENDED : 0), NULL, NULL, &si, &pi)};
	if(!res)
	{
		if(process_exit_code) *process_exit_code = GetLastError();
		CloseHandle(hPipeWrite);
		CloseHandle(hPipeRead);
		if(job) CloseHandle(job);
		return ret;
	}
	if(job)
	{
		if(!AssignProcessToJobObject(job, pi.hProcess)) { CloseHandle(job); job = nullptr; }
		ResumeThread(pi.hThread);
	}

	auto killproc = [&] (bool quick)
	{
		if(!immediate_stop)
		{
			static std::mutex mtx;
			std::lock_guard<std::mutex> lock {mtx};
			if(AttachConsole(pi.dwProcessId))
			{
				auto res {GenerateConsoleCtrlEvent(CTRL_C_EVENT, pi.dwProcessId)};
				FreeConsole();
				if(res && WaitForSingleObject(pi.hProcess, 6000) == WAIT_OBJECT_0)
				{
					if(job) TerminateJobObject(job, 0);
					if(graceful_exit)
						*graceful_exit = true;
					return;
				}
			}
		}
		auto hwnd {process_window(pi.dwProcessId)};
		if(hwnd && IsWindow(hwnd)) SendMessageTimeoutW(hwnd, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 1000, nullptr);
		const auto stopped {WaitForSingleObject(pi.hProcess, 1500) == WAIT_OBJECT_0};
		if(job) TerminateJobObject(job, 0);
		else if(!stopped) TerminateProcess(pi.hProcess, 0);
		WaitForSingleObject(pi.hProcess, INFINITE);
		if(graceful_exit)
			*graceful_exit = false;
	};

	const bool quick {cmd.find(L" -j ") != -1 || cmd.find(L" -J ") != -1};
	int playlist_complete {1}, playlist_total {0};
	bool procexit {false}, subs {false};
	std::string pending;
	while(!procexit)
	{
		procexit = WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0;

		for(;;)
		{
			char buf[1024];
			DWORD dwRead = 0;
			DWORD dwAvail = 0;

			if(!::PeekNamedPipe(hPipeRead, NULL, 0, NULL, &dwAvail, NULL))
				break;

			if(!dwAvail)
				break;

			if(!::ReadFile(hPipeRead, buf, std::min(static_cast<DWORD>(sizeof(buf) - 1), dwAvail), &dwRead, NULL) || !dwRead)
				break;

			if(working && !(*working))
			{
				killproc(quick);
				break;
			}

			buf[dwRead] = 0;
			std::string s;
			if(cbprog)
			{
				if(std::string(buf).find("Writing video subtitles") != -1)
					subs = true;
				std::string line, strbuf;
				pending.append(buf, dwRead);
				const auto last {pending.find_last_of("\r\n")};
					if(last == std::string::npos && pending.size() > 1024 * 1024 && cbappend && *working)
					{
						size_t offset {1024 * 1024};
						while(offset && (static_cast<unsigned char>(pending[offset]) & 0xc0) == 0x80) --offset;
						cbappend(pending.substr(0, offset), false); pending.erase(0, offset);
					}
				if(last == std::string::npos && !procexit) continue;
				if(last != std::string::npos)
				{
					strbuf = pending.substr(0, last + 1);
					pending.erase(0, last + 1);
				}
				else { strbuf = std::move(pending); pending.clear(); }
				size_t lnstart {0}, lnend {0};
				do
				{
					line.clear();
					if(lnend != -1)
					{
						lnend = strbuf.find_first_of("\r\n", lnstart);
						if(lnend != std::string::npos)
							line = strbuf.substr(lnstart, lnend - lnstart);
						else line = strbuf.substr(lnstart);
						if(line.size()) 
							line += '\n';
						if(lnend + 1 == strbuf.size())
							lnend = -1;
						else lnstart = lnend + 1;
					}
					if(!line.empty() && (suppress.empty() || line.find(suppress) == -1))
					{
						if(cbappend && *working && line[0] == '[')
						{
							auto pos {line.find(']')};
							if(pos != -1)
							{
								auto text {line.substr(0, pos + 1)};
								if(text != "[download]" && text[1] != '#')
								{
									if(text.size() > 3 && text[1] == 'D' && text[2] == 'L' && text[3] == ':')
									{
										cbprog(-1, -1, line, -1, -1);
										continue;
									}
									if(*working)
										cbappend(text == "[CoreFile]" ? line : text, true);
								}
								if(text == "[ExtractAudio]" || text.starts_with("[Fixup") || text == "[Merger]")
									cbprog(-1, -1, text, 0, 0);
								else if(!suppress.empty())
									if(text == "[download]" && (line.find("Destination:") == 11 || line.find("has already been downloaded") != -1))
										cbprog(-1, -1, line, 0, 0);
							}
						}
						const bool line_starts_with_download {line.starts_with("[download]")},
						           playlist_progress {line_starts_with_download && line.find(" Downloading item ") == 10};
						if(playlist_progress)
						{
							auto pos {line.find(" of ", 28)};
							if(pos != -1)
							{
								playlist_complete = std::stod(line.substr(28, pos - 28));
								playlist_total = std::stod(line.substr(pos + 4, line.size() - pos - 1));
							}
						}
						auto pos {line.find('%')};
						if(pos != -1 && line_starts_with_download)
						{
							if(subs)
							{
								s += line;
								if(line.find("100%") != -1)
									subs = false;
							}
							else
							{
								auto pos2 {line.rfind(' ', pos) + 1};
								if(pos2 != -1)
								{
									try {
										auto percent {std::stod(line.substr(pos2, pos - pos2))};
										line.pop_back();
										if(*working)
										{											
											auto pos {line.find("[download]", pos2)};
											std::string text {pos == -1 ? line.substr(pos2) : line.substr(pos2, pos-pos2)};
											cbprog(static_cast<ULONGLONG>(percent * 10), 1000, text, playlist_complete - 1, playlist_total);
										}
									} catch(...) {}
								}
							}
						}
						else if(pos != -1 && line.starts_with("[#") && line[line.size()-2] == ']')
						{
							auto paren {line.rfind('(', pos) + 1};
							auto strpct {line.substr(paren, pos - paren)};
							auto pos3 {line.find("ETA:")};
							std::string eta;
							if(pos3 != -1)
								eta = line.substr(pos3, line.size() - 2 - pos3);
							auto slash {line.find('/')}, first {line.find(' ')};
							std::string text {line.substr(first, slash-first) + " of "};
							text += line.substr(slash + 1, paren - slash - 2) + " ";
							text += line.substr(paren-1, line.rfind(']') - paren);
							if(*working)
								cbprog(static_cast<ULONGLONG>(std::stod(strpct) * 10), 1000, text, playlist_complete - 1, playlist_total);
						}
						else s += line;
					}
				} while(lnend != -1);
			}
			else ret += buf;
			if(cbappend && *working)
			{
				if(s.size() > 2)
				{
					auto pos {s.find_first_not_of(" \r\n")};
					if(pos == -1)
						s.clear();
					else s = s.substr(pos);
				}
				if(s.find("frag ") == -1 || s.starts_with("[download] Destination:") || s.starts_with("[fixup"))
					cbappend(s, false);
			}
		}
		if(working && !*working)
		{
			DWORD exit_code {0};
			GetExitCodeProcess(pi.hProcess, &exit_code);
			if(exit_code == STILL_ACTIVE)
			{
				killproc(quick);
			}
			break;
		}
	}

	if(!quick)
	{
		DWORD exit_code {0};
		GetExitCodeProcess(pi.hProcess, &exit_code);
		if(exit_code == 1 && cbprog)
			ret = "failed";
		if(exit_code == STILL_ACTIVE)
		{
			killproc(quick);
		}
	}

	if(process_exit_code) GetExitCodeProcess(pi.hProcess, process_exit_code);
	CloseHandle(hPipeWrite);
	CloseHandle(hPipeRead);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	if(job) CloseHandle(job);
	return ret;
}

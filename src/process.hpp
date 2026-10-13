#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace core
{
	using progress_callback = std::function<void(std::uint64_t, std::uint64_t, std::string, int, int)>;
	using append_callback = std::function<void(std::string, bool)>;
	// 进程基础设施；取消标志和回调由调用方提供，与窗口生命周期无关。
	std::string run_process(std::wstring command, std::atomic_bool *working = nullptr,
		append_callback append = nullptr, progress_callback progress = nullptr,
		std::atomic_bool *graceful_exit = nullptr, std::string suppress = "",
		unsigned long *exit_code = nullptr, bool immediate_stop = false);
}

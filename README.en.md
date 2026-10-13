<div align="center">

# yt-dlp Workbench · reUI

**Manage video and audio downloads, from pasting links to organizing files.**

Windows desktop app · English / 简体中文 · Light / dark themes

[![Download](https://img.shields.io/badge/Download-Windows_x64-3864f4?style=flat-square)](https://github.com/Yes-nolink/ytdlp-interface-reUI/releases/latest)
[![Platform](https://img.shields.io/badge/Platform-Windows_10_%2F_11-64748b?style=flat-square)](#download-and-run)
[![License](https://img.shields.io/badge/License-MIT-16a34a?style=flat-square)](LICENSE)

[简体中文](README.md) | **English**

[Download the portable app](https://github.com/Yes-nolink/ytdlp-interface-reUI/releases/latest) · [Get started](#start-a-download) · [Build from source](#build-from-source) · [Report an issue](https://github.com/Yes-nolink/ytdlp-interface-reUI/issues)

</div>

![English workbench in dark mode](docs/images/workbench-en-dark.png)

A Windows GUI rebuilt from [ErrorFlynn/ytdlp-interface v2.19.1](https://github.com/ErrorFlynn/ytdlp-interface/tree/v2.19.1). The current version presents a WebView2 workbench, while a C++ core runs yt-dlp and FFmpeg for downloads and media processing.

This is an independently maintained interface project that retains the upstream copyright and MIT license. Website support and media extraction are provided by [yt-dlp](https://github.com/yt-dlp/yt-dlp).

## What you can do

| Feature | How it works |
| --- | --- |
| Videos, playlists, and channels | Paste one or more links, then select individual entries or a range |
| Video and audio output | Choose resolution and media formats, download video, or extract MP3 / M4A audio |
| Subtitles, artwork, and metadata | Select subtitles and audio tracks, with optional embedding of artwork, subtitles, and media information |
| Tasks and collections | Track progress, stop, retry, filter, group, and expand collections to manage child tasks |
| Concurrency and queue recovery | Set download and metadata concurrency, and restore unfinished tasks |
| File names and saved presets | Compose file names from fields and save naming, argument, and download presets |
| Network and authentication | Configure a proxy, browser cookies, or a cookies file |
| Component management | Check versions, configure paths, update existing yt-dlp / FFmpeg, and install or update Deno |
| Languages and appearance | English and Simplified Chinese, with system, light, and dark themes |

Common actions live in the workbench and download tasks page. Seven preference categories are directly accessible from the sidebar. Long lists, settings, and drawers scroll independently while headings and action areas stay visible.

<details>
<summary>See the Simplified Chinese interface</summary>

![Simplified Chinese workbench in light mode](docs/images/workbench-zh.png)

</details>

## Download and run

1. Open [Releases](https://github.com/Yes-nolink/ytdlp-interface-reUI/releases/latest), download `ytdlp-interface-reUI-*-windows-x64.zip`, and extract the entire archive into a separate folder.
2. Make sure [Microsoft Edge WebView2 Runtime](https://developer.microsoft.com/en-us/microsoft-edge/webview2/) is installed. If it is missing, install the Evergreen x64 runtime.
3. Prepare the download components listed below and put their EXE files in the application folder. You can also configure existing component paths under **Components and updates**.
4. Run `ytdlp-interface.exe` and check component availability under **Components and updates**.

**The portable ZIP includes the GUI, UI assets, language files, and licenses. yt-dlp, FFmpeg, ffprobe, and Deno must be obtained separately.** The Windows `yt-dlp.exe` does not require a separate Python installation.

| Component | Purpose | Download |
| --- | --- | --- |
| `yt-dlp.exe` | Extract links and download media | [Official yt-dlp Windows x64 executable](https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe) |
| `ffmpeg.exe`, `ffprobe.exe` | Merge, convert, and inspect media | [yt-dlp FFmpeg Builds](https://github.com/yt-dlp/FFmpeg-Builds/releases/latest); choose a Windows x64 build and extract these two files |
| `deno.exe` | JavaScript runtime support for yt-dlp | [Official Deno releases](https://github.com/denoland/deno/releases/latest); choose `deno-x86_64-pc-windows-msvc.zip` |

Recommended folder layout:

```text
ytdlp-interface-reUI/
├─ ytdlp-interface.exe
├─ yt-dlp.exe
├─ ffmpeg.exe
├─ ffprobe.exe
├─ deno.exe
├─ ui/
├─ locales/
├─ licenses/
└─ LICENSE
```

Keep the complete `ui` and `locales` directories with the GUI executable. Place Deno alongside yt-dlp or make it available through the system PATH.

## Start a download

1. Paste a video, playlist, or channel link in the workbench and click **Parse links**. Multiple links can be entered one per line.
2. Select the entries you want in the source drawer and continue to the download settings.
3. Choose video or audio output, format and quality, and an output folder. Adjust subtitles, artwork, and file naming as needed.
4. Add the selection to the queue or start downloading. Use **Download tasks** to view progress, logs, and output files.

For audio downloads, select MP3 or M4A and enable artwork or metadata embedding as needed. **Download defaults** controls future tasks; edit an individual task from its details.

Switch the interface language under **Appearance and app**, then restart. Saved preset names, file names, and download settings are preserved. Website titles and raw component logs retain their original language.

## Updates and common questions

- **Missing runtime or assets on startup:** Install WebView2 Runtime and keep the complete `ui` and `locales` folders.
- **A link fails to parse or download:** Read the task log and check network access, proxy settings, cookies, and the yt-dlp version. Website restrictions still apply.
- **Merging or conversion fails:** Check that FFmpeg and ffprobe are both present and that the component path is correct.
- **Component updates:** Check for updates under **Components and updates**, then follow the prompts. Initial yt-dlp and FFmpeg installation is manual; Deno can be installed in the app.
- **GUI updates:** Download a new archive from this repository's Releases and extract it into a new folder. GUI auto-update is disabled to avoid replacing this interface with the upstream version.

Personal settings, queue data, and window state are stored under `%LOCALAPPDATA%\ytdlp-interface` by default. Removing the portable application folder does not delete downloaded media. Old Nana queues cannot be imported, and legacy custom themes are no longer supported. Physical mixed-DPI multi-monitor behavior and touchpad interaction still need further verification.

## Build from source

Requirements: Windows x64, PowerShell, an MSVC compiler with C++23 support, Windows SDK, ATL, CMake 3.20+, and Ninja. Install the **Desktop development with C++** workload in Visual Studio Build Tools, including ATL and the CMake tools.

Run the following in **x64 Developer PowerShell for Visual Studio**:

```powershell
git clone https://github.com/Yes-nolink/ytdlp-interface-reUI.git
cd ytdlp-interface-reUI
./build.ps1
./.build/web-package/ytdlp-interface.exe
```

The script downloads a pinned WebView2 SDK. WebView2 Runtime is still required to run the app. The required zlib sources, icon script, and JSON header are included; no frontend package installation is needed.

The default build directory is `.build/web-app`, and the output package is `.build/web-package`. The output directory must be new or empty. Use a different directory for another package:

```powershell
./build.ps1 -OutputDirectory ./release/my-build
```

An existing local portable toolchain under `.build` remains supported. The script only changes environment variables for the current process. Download components are not needed to compile the GUI; prepare them afterward using the instructions above.

## Repository layout

```text
src/         C++ download core and Windows / WebView2 host
ui/          HTML, CSS, JavaScript, and icon assets
locales/     English and Simplified Chinese language files
vendor/      Required zlib sources
licenses/    Upstream and third-party license notices
docs/images/ Screenshots used in the README
build.ps1    Build and packaging script
```

The public repository starts from the final source snapshot. It excludes personal settings, cookies, temporary builds, validation scripts, validation reports, and old installers.

## Credits and license

The application retains the upstream [MIT License](LICENSE) and ErrorFlynn's copyright notice. Keep the license and copyright information when modifying or redistributing the application.

Thanks to [ytdlp-interface](https://github.com/ErrorFlynn/ytdlp-interface), [yt-dlp](https://github.com/yt-dlp/yt-dlp), [FFmpeg](https://ffmpeg.org/), [Deno](https://github.com/denoland/deno), [WebView2](https://developer.microsoft.com/en-us/microsoft-edge/webview2/), [nlohmann/json](https://github.com/nlohmann/json), [zlib](https://zlib.net/), and [Lucide](https://lucide.dev/). Each component's license applies independently; notices are in [licenses](licenses/README.md).

Download only content you are entitled to access and follow the relevant website's terms.

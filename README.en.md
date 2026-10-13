<div align="center">

# yt-dlp Workbench · reUI

**Manage video and audio downloads, from pasting links to organizing files.**

Windows desktop app · English / 简体中文 · Light / dark themes

[![Download](https://img.shields.io/badge/Download-Windows_x64-3864f4?style=flat-square)](https://github.com/Yes-nolink/ytdlp-interface-reUI/releases/latest)
[![Platform](https://img.shields.io/badge/Platform-Windows_10_%2F_11-64748b?style=flat-square)](#download-and-install)
[![License](https://img.shields.io/badge/License-MIT-16a34a?style=flat-square)](LICENSE)

[简体中文](README.md) | **English**

[Download the installer](https://github.com/Yes-nolink/ytdlp-interface-reUI/releases/latest) · [Get started](#start-a-download) · [Build from source](#build-from-source) · [Report an issue](https://github.com/Yes-nolink/ytdlp-interface-reUI/issues)

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

## Download and install

**One installer contains everything needed to run the app.** Supports Windows 10 22H2 / Windows 11 x64.

1. Open the [latest release](https://github.com/Yes-nolink/ytdlp-interface-reUI/releases/latest) and download `ytdlp-interface-reUI-*-windows-x64-setup.exe`.
2. Run the setup wizard. It installs for the current user; no separate Python installation, PATH configuration, or manual component downloads are needed.
3. Launch reUI from the desktop or Start menu. Paste a link, choose an output folder, and start downloading.

The installer includes these components with their license notices and source information:

| Included component | Purpose |
| --- | --- |
| yt-dlp | Extract website links and download video or audio |
| FFmpeg / ffprobe | Merge, convert, and inspect media |
| Deno | JavaScript runtime support required by yt-dlp |
| Offline WebView2 installer | Automatically installs a missing UI runtime; uses an existing runtime when available |

**Setup works offline; extracting links and downloading media require internet access.** WebView2 uses Microsoft's [Evergreen deployment model](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution) and its runtime update mechanism.

The default install folder is `%LOCALAPPDATA%\Programs\ytdlp-interface-reUI`. Desktop and Start menu shortcuts are included, and the app can be removed from Windows **Installed apps**. Component versions, checksums, and sources are recorded in the [component manifest](licenses/components.json) and release notes.

## Start a download

1. Paste a video, playlist, or channel link in the workbench and click **Parse links**. Multiple links can be entered one per line.
2. Select the entries you want in the source drawer and continue to the download settings.
3. Choose video or audio output, format and quality, and an output folder. Adjust subtitles, artwork, and file naming as needed.
4. Add the selection to the queue or start downloading. Use **Download tasks** to view progress, logs, and output files.

For audio downloads, select MP3 or M4A and enable artwork or metadata embedding as needed. **Download defaults** controls future tasks; edit an individual task from its details.

Switch the interface language under **Appearance and app**, then restart. Saved preset names, file names, and download settings are preserved. Website titles and raw component logs retain their original language.

## Updates and common questions

- **Missing runtime or assets on startup:** Run the full installer again. For a build from source, install WebView2 Runtime and keep the complete `ui` and `locales` folders.
- **A link fails to parse or download:** Read the task log and check network access, proxy settings, cookies, and the yt-dlp version. Website restrictions still apply.
- **Merging or conversion fails:** Check that FFmpeg and ffprobe are both present and that the component path is correct.
- **Component updates:** Check for updates under **Components and updates**, then follow the prompts. The installer already includes the components needed for first use.
- **GUI updates:** Close the app, download a new installer from this repository's Releases, and install it into the same folder. GUI auto-update is disabled to avoid replacing this interface with the upstream version.

Personal settings, queue data, and window state are stored under `%LOCALAPPDATA%\ytdlp-interface` by default. Uninstalling keeps personal data, downloaded media, and the shared WebView2 Runtime. Old Nana queues cannot be imported, and legacy custom themes are no longer supported.

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

An existing local portable toolchain under `.build` remains supported. The script only changes environment variables for the current process. `build.ps1` builds the GUI; download components are not needed to compile it.

To build the full installer, also install NSIS 3. Obtain the components listed in the [manifest](licenses/components.json), place `yt-dlp.exe`, `ffmpeg.exe`, `ffprobe.exe`, and `deno.exe` in the output package, and obtain Microsoft's signed WebView2 x64 Evergreen Standalone Installer. Then run:

```powershell
./build-installer.ps1 -PackageDirectory ./.build/web-package `
    -WebView2Installer ./MicrosoftEdgeWebView2RuntimeInstallerX64.exe
```

The script checks required files and the WebView2 signature, then creates an EXE with setup, shortcuts, and an uninstaller.

## Repository layout

```text
src/         C++ download core and Windows / WebView2 host
ui/          HTML, CSS, JavaScript, and icon assets
locales/     English and Simplified Chinese language files
vendor/      Required zlib sources
licenses/    Upstream and third-party license notices
docs/images/ Screenshots used in the README
installer/   English and Chinese setup wizard
build.ps1    GUI build and packaging script
build-installer.ps1  Full installer build script
```

The public repository starts from the final source snapshot. It excludes personal settings, cookies, temporary builds, validation scripts, validation reports, and old installers.

## Credits and license

The application retains the upstream [MIT License](LICENSE) and ErrorFlynn's copyright notice. Keep the license and copyright information when modifying or redistributing the application.

Thanks to [ytdlp-interface](https://github.com/ErrorFlynn/ytdlp-interface), [yt-dlp](https://github.com/yt-dlp/yt-dlp), [FFmpeg](https://ffmpeg.org/), [Deno](https://github.com/denoland/deno), [WebView2](https://developer.microsoft.com/en-us/microsoft-edge/webview2/), [nlohmann/json](https://github.com/nlohmann/json), [zlib](https://zlib.net/), and [Lucide](https://lucide.dev/). Each component's license applies independently; notices are in [licenses](licenses/README.md).

Download only content you are entitled to access and follow the relevant website's terms.

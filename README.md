<div align="center">

# yt-dlp 下载工作台 · reUI

**用中文管理视频与音频下载，从粘贴链接到整理文件。**

Windows 桌面应用 · 简体中文 / English · 浅色 / 深色主题

[![下载](https://img.shields.io/badge/下载-Windows_x64-3864f4?style=flat-square)](https://github.com/Yes-nolink/ytdlp-interface-reUI/releases/latest)
[![平台](https://img.shields.io/badge/平台-Windows_10_%2F_11-64748b?style=flat-square)](#下载安装)
[![许可证](https://img.shields.io/badge/License-MIT-16a34a?style=flat-square)](LICENSE)

**简体中文** | [English](README.en.md)

[下载安装包](https://github.com/Yes-nolink/ytdlp-interface-reUI/releases/latest) · [使用说明](#开始下载) · [源码构建](#从源码构建) · [反馈问题](https://github.com/Yes-nolink/ytdlp-interface-reUI/issues)

</div>

![简体中文工作台](docs/images/workbench-zh.png)

基于 [ErrorFlynn/ytdlp-interface v2.19.1](https://github.com/ErrorFlynn/ytdlp-interface/tree/v2.19.1) 改造的 Windows 图形界面。当前版本使用 WebView2 展示下载工作台，由 C++ 核心调用 yt-dlp 和 FFmpeg 执行下载与处理。

这是一项独立维护的界面改造，保留上游版权与 MIT 许可。网站支持范围和媒体解析结果由 [yt-dlp](https://github.com/yt-dlp/yt-dlp) 决定。

## 可以做什么

| 能力 | 使用方式 |
| --- | --- |
| 视频、播放列表与频道 | 粘贴一个或多个链接，解析后按条目或范围选择下载内容 |
| 视频与音频输出 | 选择分辨率、音视频格式，下载视频或提取 MP3 / M4A 音频 |
| 字幕、封面与元数据 | 选择字幕和音轨，按需嵌入封面、字幕及媒体信息 |
| 下载任务与集合 | 查看进度，停止、重试、筛选、分组，展开集合管理子任务 |
| 并发与队列恢复 | 设置并发下载和解析数量，恢复未完成任务 |
| 文件命名与方案 | 用中文字段组合文件名，保存命名方案、参数方案和下载方案 |
| 网络与认证 | 配置代理、浏览器 Cookies 或 Cookies 文件 |
| 组件管理 | 检查组件版本、配置路径，更新已有 yt-dlp / FFmpeg，以及安装或更新 Deno |
| 双语与外观 | 简体中文和英文界面，系统、浅色和深色主题 |

常用操作集中在工作台和下载任务页；七类偏好设置可从左栏直接进入。长列表、设置正文和抽屉分别滚动，标题与操作区保持可见。

<details>
<summary>查看英文深色界面</summary>

![英文深色工作台](docs/images/workbench-en-dark.png)

A Windows desktop GUI for yt-dlp with a WebView2 workbench, a C++ download core, playlist selection, queue management, output presets, and Chinese / English interfaces.

</details>

## 下载安装

**下载一个安装包，装好即可使用。** 支持 Windows 10 22H2 / Windows 11 x64。

1. 打开 [最新版本下载页](https://github.com/Yes-nolink/ytdlp-interface-reUI/releases/latest)，下载 `ytdlp-interface-reUI-*-windows-x64-setup.exe`。
2. 双击安装，按向导完成操作。默认安装到当前用户目录，不需要单独安装 Python、配置 PATH 或手动下载组件。
3. 从桌面或开始菜单启动 reUI，粘贴链接，选择保存目录后开始下载。

安装包内置以下组件，并保留各自的许可声明与来源信息：

| 内置组件 | 用途 |
| --- | --- |
| yt-dlp | 解析网站链接、下载视频与音频 |
| FFmpeg / ffprobe | 合并、转码与读取媒体信息 |
| Deno | yt-dlp 所需的 JavaScript 运行支持 |
| WebView2 离线安装程序 | 自动补齐缺失的界面运行环境；已有环境时直接使用 |

**安装过程无需联网；解析链接和下载媒体需要网络。** WebView2 采用微软的 [Evergreen 部署方式](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution)，由其更新机制维护运行环境。

默认程序目录为 `%LOCALAPPDATA%\Programs\ytdlp-interface-reUI`，提供桌面与开始菜单快捷方式，也可从 Windows「已安装的应用」卸载。具体组件版本、校验和及来源见 [组件清单](licenses/components.json) 和每个 Release 的说明。

## 开始下载

1. 在工作台粘贴视频、播放列表或频道链接，点击「解析链接」。多条链接可按行粘贴。
2. 在来源抽屉选择需要的条目，进入下载设置。
3. 选择视频或音频输出、格式质量、保存目录；按需设置字幕、封面和文件命名。
4. 加入队列或立即下载，在「下载任务」中查看进度、日志和输出文件。

下载纯音频时选择 MP3 或 M4A，并按需开启嵌入封面和元数据。调整未来任务的默认配置，进入左栏「下载默认值」；修改具体任务则从任务详情进入编辑。

在「外观与应用」中切换界面语言，重启后生效。切换语言会保留用户保存的方案名称、文件名和下载设置；网站返回的标题与组件原始日志保留原文。

## 更新与常见问题

- **打开程序提示缺少运行环境或资源：** 重新运行完整安装包；使用自行构建的程序时，安装 WebView2 Runtime 并保留 `ui`、`locales` 目录。
- **链接无法解析或下载失败：** 查看任务日志，检查网络、代理、Cookies 和 yt-dlp 版本；网站自身的限制仍会影响下载。
- **合并或转码失败：** 检查 FFmpeg 与 ffprobe 是否成对存在，组件路径是否正确。
- **更新组件：** 在「组件与更新」中先检查更新，再按提示操作。安装包已包含首次运行所需的组件。
- **更新界面程序：** 关闭程序，从本仓库 Releases 下载新安装包，安装到原目录。当前界面自身的自动更新入口禁用，避免被上游原版覆盖。

个人设置、队列与窗口状态默认保存在 `%LOCALAPPDATA%\ytdlp-interface` 下。卸载保留个人数据、已下载的媒体文件和共享 WebView2 运行环境。旧 Nana 队列不支持导入，当前版本不支持旧版自定义主题。

## 从源码构建

需要 Windows x64、PowerShell、MSVC C++23 编译器、Windows SDK、ATL、CMake 3.20+ 和 Ninja。可安装 Visual Studio Build Tools 的「使用 C++ 的桌面开发」工作负载，并勾选 ATL 与 CMake 工具。

在 **x64 Developer PowerShell for Visual Studio** 中执行：

```powershell
git clone https://github.com/Yes-nolink/ytdlp-interface-reUI.git
cd ytdlp-interface-reUI
./build.ps1
./.build/web-package/ytdlp-interface.exe
```

脚本自动获取固定版本的 WebView2 SDK；运行时仍需安装 WebView2 Runtime。zlib 源码随仓库提供，图标脚本与 JSON 头文件也已包含，无需安装前端包。

默认构建目录为 `.build/web-app`，输出包为 `.build/web-package`。输出目录必须全新或为空；再次打包可指定新目录：

```powershell
./build.ps1 -OutputDirectory ./release/my-build
```

本地已有的 `.build` 便携工具链仍可使用。脚本只修改当前进程环境。`build.ps1` 构建界面程序；下载组件不参与界面编译。

制作完整安装包还需 NSIS 3：根据 [组件清单](licenses/components.json) 取得组件，把 `yt-dlp.exe`、`ffmpeg.exe`、`ffprobe.exe` 和 `deno.exe` 放入输出包；取得微软签名的 WebView2 x64 Evergreen 离线安装程序，然后执行：

```powershell
./build-installer.ps1 -PackageDirectory ./.build/web-package `
    -WebView2Installer ./MicrosoftEdgeWebView2RuntimeInstallerX64.exe
```

脚本会检查组件是否完整和 WebView2 签名，生成包含安装、快捷方式与卸载功能的 EXE。

## 项目结构

```text
src/        C++ 下载核心与 Windows / WebView2 宿主
ui/         HTML、CSS、JavaScript 界面和图标资源
locales/    简体中文与英文语言文件
vendor/     构建所需的 zlib 源码
licenses/   上游与第三方许可声明
docs/images/ 首页展示截图
installer/  中英文安装向导
build.ps1   界面构建与打包脚本
build-installer.ps1  完整安装包构建脚本
```

公开仓库从当前最终源码快照开始，不包含个人配置、Cookies、临时构建、验证脚本、验证报告或旧安装包。

## 来源与许可

应用沿用 [MIT License](LICENSE)，保留原作者 ErrorFlynn 的版权声明。修改与再分发时请保留许可与版权信息。

感谢 [ytdlp-interface](https://github.com/ErrorFlynn/ytdlp-interface)、[yt-dlp](https://github.com/yt-dlp/yt-dlp)、[FFmpeg](https://ffmpeg.org/)、[Deno](https://github.com/denoland/deno)、[WebView2](https://developer.microsoft.com/en-us/microsoft-edge/webview2/)、[nlohmann/json](https://github.com/nlohmann/json)、[zlib](https://zlib.net/) 和 [Lucide](https://lucide.dev/)。各组件的许可独立适用，声明见 [licenses](licenses/README.md)。

请仅下载你有权获取的内容，并遵守相关网站的使用条款。

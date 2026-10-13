# 许可证与来源

界面程序使用 nlohmann/json、zlib、WebView2 SDK/Loader 和 Lucide，沿用上游 MIT 许可。完整安装包同时包含 yt-dlp、FFmpeg、ffprobe、Deno，以及微软签名的 WebView2 Evergreen x64 离线安装程序。各组件许可证独立适用，MIT 许可不替代第三方条款。

| 组件 | 许可与来源 |
| --- | --- |
| yt-dlp 2026.08.19 | 源码为 Unlicense；官方 Windows EXE 包含其他许可代码，组合产物为 GPLv3+。见 `yt-dlp-Unlicense.txt`、`yt-dlp-THIRD-PARTY.txt` 和 [上游许可说明](https://github.com/yt-dlp/yt-dlp#licensing) |
| FFmpeg / ffprobe | BtbN GPLv3 静态构建，保留 `ffmpeg-GPLv3.txt`；对应源码与构建脚本见下方链接 |
| Deno 2.7.14 | MIT，见 `deno-MIT.txt`，V8 声明见 `deno-v8-LICENSE.txt`；依赖源码见上游仓库 |
| WebView2 | SDK 声明见 `WebView2-LICENSE.txt` 和 `WebView2-NOTICE.txt`；Runtime 按 [微软软件许可条款](https://developer.microsoft.com/en-us/microsoft-edge/webview2/) 再分发，未修改微软签名安装程序 |

组件的版本、官方下载地址和 SHA256 见 [components.json](components.json)。

FFmpeg 对应源码：[FFmpeg 4444a75590](https://github.com/FFmpeg/FFmpeg/tree/4444a75590)；构建系统与依赖获取脚本：[BtbN/FFmpeg-Builds 构建时快照](https://github.com/BtbN/FFmpeg-Builds/tree/28ae7513e7b6477da5c9ba7edb07aa940d485fa2)，安装包保留 `ffmpeg-build.txt` 中的版本输出与配置参数。yt-dlp 的源码与构建说明见 [2026.08.19](https://github.com/yt-dlp/yt-dlp/tree/2026.08.19)，Deno 源码见 [v2.7.14](https://github.com/denoland/deno/tree/v2.7.14)。

本目录保留原项目及第三方版权声明。`legacy-nana` 中的 Nana、JPEG、libpng、bit7z 和 7-Zip 声明仅保留上游历史归属；这些库已从当前应用构建和运行依赖移除，不随发布包提供旧 DLL 或静态库。

应用沿用原始 MIT 许可和文件版本。本仓库发布标签区分 Web 工作台构建与上游原版版本；第三方组件的许可证独立适用。

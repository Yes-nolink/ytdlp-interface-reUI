Unicode true
RequestExecutionLevel user
ManifestDPIAware true
SetCompressor /SOLID lzma
SetCompressorDictSize 32

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

!define APP_NAME "yt-dlp Workbench · reUI"
!define APP_KEY "Software\Yes-nolink\ytdlp-interface-reUI"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ytdlp-interface-reUI"
!define WEBVIEW_KEY "Software\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}"

Name "${APP_NAME}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\ytdlp-interface-reUI"
InstallDirRegKey HKCU "${APP_KEY}" "InstallDir"
BrandingText "ytdlp-interface-reUI"
VIProductVersion "2.19.1.1"
VIAddVersionKey /LANG=1033 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1033 "FileDescription" "reUI Windows x64 Setup"
VIAddVersionKey /LANG=1033 "FileVersion" "${RELEASE_VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "ErrorFlynn and reUI contributors"

!define MUI_ICON "..\src\Youtube-Round.ico"
!define MUI_UNICON "..\src\Youtube-Round.ico"
!define MUI_ABORTWARNING
!define MUI_LANGDLL_REGISTRY_ROOT HKCU
!define MUI_LANGDLL_REGISTRY_KEY "${APP_KEY}"
!define MUI_LANGDLL_REGISTRY_VALUENAME "InstallerLanguage"
!define MUI_WELCOMEPAGE_TEXT "$(WelcomeText)"
!define MUI_FINISHPAGE_RUN "$INSTDIR\ytdlp-interface.exe"
!define MUI_FINISHPAGE_RUN_TEXT "$(LaunchText)"
!define MUI_FINISHPAGE_LINK "GitHub · reUI"
!define MUI_FINISHPAGE_LINK_LOCATION "https://github.com/Yes-nolink/ytdlp-interface-reUI"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

LangString WelcomeText ${LANG_ENGLISH} "Install the video and audio download workbench with yt-dlp, FFmpeg, ffprobe and Deno.$\r$\n$\r$\nWebView2 Runtime is installed automatically if needed. All components are included; setup does not need an internet connection.$\r$\n$\r$\nThird-party license notices are included in the licenses folder. Close the app before installing an update."
LangString WelcomeText ${LANG_SIMPCHINESE} "安装视频与音频下载工作台，内置 yt-dlp、FFmpeg、ffprobe 和 Deno。$\r$\n$\r$\n缺少 WebView2 运行环境时会自动安装。组件已全部包含，安装过程无需联网。$\r$\n$\r$\n第三方许可声明随程序保存在 licenses 目录。更新安装前请先关闭程序。"
LangString LaunchText ${LANG_ENGLISH} "Launch reUI"
LangString LaunchText ${LANG_SIMPCHINESE} "启动 reUI 下载工作台"
LangString PlatformError ${LANG_ENGLISH} "This installer requires Windows 10 or 11 x64."
LangString PlatformError ${LANG_SIMPCHINESE} "本安装包需要 Windows 10 或 Windows 11 x64。"
LangString RuntimeInstall ${LANG_ENGLISH} "Installing Microsoft Edge WebView2 Runtime..."
LangString RuntimeInstall ${LANG_SIMPCHINESE} "正在安装 Microsoft Edge WebView2 运行环境…"
LangString RuntimeError ${LANG_ENGLISH} "WebView2 Runtime could not be installed. Please install it from Microsoft's WebView2 download page, then run setup again."
LangString RuntimeError ${LANG_SIMPCHINESE} "WebView2 运行环境安装未完成，请从微软 WebView2 下载页面安装后重新运行本安装包。"
LangString ShortcutName ${LANG_ENGLISH} "yt-dlp Workbench · reUI"
LangString ShortcutName ${LANG_SIMPCHINESE} "yt-dlp 下载工作台 · reUI"

Function .onInit
    SetShellVarContext current
    SetRegView 64
    IfSilent skipLanguage
    !insertmacro MUI_LANGDLL_DISPLAY
    skipLanguage:
    ${IfNot} ${RunningX64}
    ${OrIfNot} ${AtLeastWin10}
        MessageBox MB_ICONSTOP "$(PlatformError)" /SD IDOK
        SetErrorLevel 1
        Quit
    ${EndIf}
FunctionEnd

; 按微软部署文档检查机器和当前用户的 Evergreen Runtime。
Function HasWebView2
    SetRegView 32
    ReadRegStr $0 HKLM "${WEBVIEW_KEY}" "pv"
    ReadRegStr $1 HKCU "${WEBVIEW_KEY}" "pv"
    SetRegView 64
    ${If} $0 != ""
    ${AndIf} $0 != "0.0.0.0"
        Push 1
    ${ElseIf} $1 != ""
    ${AndIf} $1 != "0.0.0.0"
        Push 1
    ${Else}
        Push 0
    ${EndIf}
FunctionEnd

Section "reUI" SEC_APP
    Call HasWebView2
    Pop $0
    ${If} $0 == 0
        DetailPrint "$(RuntimeInstall)"
        InitPluginsDir
        SetOutPath "$PLUGINSDIR"
        File /oname=MicrosoftEdgeWebView2RuntimeInstallerX64.exe "${WEBVIEW_INSTALLER}"
        ExecWait '"$PLUGINSDIR\MicrosoftEdgeWebView2RuntimeInstallerX64.exe" /silent /install' $0
        Call HasWebView2
        Pop $1
        ${If} $1 == 0
            MessageBox MB_ICONSTOP "$(RuntimeError)" /SD IDOK
            SetErrorLevel 1
            Abort
        ${EndIf}
    ${EndIf}

    SetOutPath "$INSTDIR"
    File /r "${PACKAGE_DIR}\*"
    WriteUninstaller "$INSTDIR\uninstall.exe"
    CreateDirectory "$SMPROGRAMS\ytdlp-interface-reUI"
    CreateShortcut "$SMPROGRAMS\ytdlp-interface-reUI\reUI.lnk" "$INSTDIR\ytdlp-interface.exe" "" "$INSTDIR\ytdlp-interface.exe"
    CreateShortcut "$DESKTOP\ytdlp-interface-reUI.lnk" "$INSTDIR\ytdlp-interface.exe" "" "$INSTDIR\ytdlp-interface.exe"
    WriteRegStr HKCU "${APP_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "$(ShortcutName)"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${RELEASE_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Yes-nolink"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\ytdlp-interface.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/Yes-nolink/ytdlp-interface-reUI"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\uninstall.exe$\"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\uninstall.exe$\" /S'
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" ${INSTALLED_KB}
SectionEnd

Function un.onInit
    SetShellVarContext current
    SetRegView 64
    !insertmacro MUI_UNGETLANGUAGE
FunctionEnd

Section "Uninstall"
    ; 只删除打包时列出的应用文件，保留个人设置、媒体和共享运行环境。
    !include "${UNINSTALL_FILES}"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"
    Delete "$SMPROGRAMS\ytdlp-interface-reUI\reUI.lnk"
    RMDir "$SMPROGRAMS\ytdlp-interface-reUI"
    Delete "$DESKTOP\ytdlp-interface-reUI.lnk"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    DeleteRegKey HKCU "${APP_KEY}"
SectionEnd

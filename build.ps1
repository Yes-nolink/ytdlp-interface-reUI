param([switch]$WebUI, [switch]$NanaOnly, [string]$OutputDirectory, [string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$project = $PSScriptRoot
if ($NanaOnly) { throw 'Nana 已退役，不再支持 -NanaOnly；直接运行 build.ps1 构建纯 Web 应用' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $project '.build\web-package' }
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $project '.build\web-app' }
$buildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$packageDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $packageDirectory) {
    if (-not (Test-Path -LiteralPath $packageDirectory -PathType Container) -or @(Get-ChildItem -LiteralPath $packageDirectory -Force).Count) { throw '打包目录必须为空，不覆盖已有应用或个人文件' }
}
$compilerRoot = Join-Path $project '.build\toolchain\msvc'
if (Test-Path -LiteralPath (Join-Path $compilerRoot 'VC\Tools\MSVC')) {
    $vc = Get-ChildItem -LiteralPath (Join-Path $compilerRoot 'VC\Tools\MSVC') -Directory | Select-Object -First 1
    $sdkRoot = Join-Path $compilerRoot 'Windows Kits\10'
    $sdk = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory | Select-Object -First 1
    $cmake = Join-Path $project '.build\python-tools\cmake\data\bin\cmake.exe'
    $ninja = Join-Path $project '.build\python-tools\bin\ninja.exe'
    $atlInclude = Join-Path $vc.FullName 'atlmfc\include'
    # 仅设置当前构建进程的环境变量。
    $env:PATH = "$(Join-Path $vc.FullName 'bin\Hostx64\x64');$(Join-Path $sdkRoot "bin\$($sdk.Name)\x64");$env:PATH"
    $env:INCLUDE = "$(Join-Path $vc.FullName 'include');$(Join-Path $sdk.FullName 'ucrt');$(Join-Path $sdk.FullName 'shared');$(Join-Path $sdk.FullName 'um');$(Join-Path $sdk.FullName 'winrt');$atlInclude"
    $env:LIB = "$(Join-Path $vc.FullName 'lib\x64');$(Join-Path $sdkRoot "Lib\$($sdk.Name)\ucrt\x64");$(Join-Path $sdkRoot "Lib\$($sdk.Name)\um\x64");$(Join-Path $vc.FullName 'atlmfc\lib\x64')"
} else {
    if (-not $env:VCToolsInstallDir -or -not (Get-Command cl -ErrorAction SilentlyContinue)) { throw '请从 x64 Developer PowerShell 启动，并安装 MSVC、Windows SDK 和 ATL' }
    $cmake = (Get-Command cmake -ErrorAction Stop).Source
    $ninja = (Get-Command ninja -ErrorAction Stop).Source
    $atlInclude = Join-Path $env:VCToolsInstallDir 'atlmfc\include'
}

$sdkVersion = '1.0.4258.31'
$webviewSdk = Join-Path $project ".build\webview2\$sdkVersion"
if (-not (Test-Path -LiteralPath (Join-Path $webviewSdk 'build\native\include\WebView2.h'))) {
    New-Item -ItemType Directory -Path $webviewSdk -Force | Out-Null
    $archive = Join-Path $project '.build\webview2\sdk.zip'
    Invoke-WebRequest -Uri "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$sdkVersion/microsoft.web.webview2.$sdkVersion.nupkg" -OutFile $archive -UseBasicParsing
    Expand-Archive -LiteralPath $archive -DestinationPath $webviewSdk -Force
}

& $cmake -S $project -B $buildDirectory -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=Release "-DATL_INCLUDE_DIR=$atlInclude"
if ($LASTEXITCODE -ne 0) { throw '界面配置失败' }
& $cmake --build $buildDirectory --parallel 4
if ($LASTEXITCODE -ne 0) { throw '界面编译失败' }
New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $buildDirectory 'ytdlp-interface.exe') -Destination (Join-Path $packageDirectory 'ytdlp-interface.exe')
Copy-Item -LiteralPath (Join-Path $project 'locales') -Destination $packageDirectory -Recurse
Copy-Item -LiteralPath (Join-Path $project 'LICENSE') -Destination $packageDirectory
Copy-Item -LiteralPath (Join-Path $project '使用说明.txt') -Destination $packageDirectory
Copy-Item -LiteralPath (Join-Path $project 'licenses') -Destination $packageDirectory -Recurse
Copy-Item -LiteralPath (Join-Path $project 'ui') -Destination $packageDirectory -Recurse
Write-Output "已打包至 $packageDirectory；未覆盖 dist 或已安装组件"

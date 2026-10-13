param(
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [Parameter(Mandatory = $true)][string]$WebView2Installer,
    [string]$OutputInstaller,
    [string]$MakeNsis,
    [string]$ReleaseVersion = '2.19.1-reUI.20261013'
)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$runtime = (Resolve-Path -LiteralPath $WebView2Installer).Path
foreach ($name in @('ytdlp-interface.exe', 'yt-dlp.exe', 'ffmpeg.exe', 'ffprobe.exe', 'deno.exe', 'ui\index.html', 'locales\zh-CN.json', 'locales\en-US.json', 'LICENSE', 'licenses\components.json')) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $name) -PathType Leaf)) { throw "安装包缺少文件：$name" }
}
$signature = Get-AuthenticodeSignature -LiteralPath $runtime
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw 'WebView2 离线安装程序必须具有有效的微软签名' }
if (-not $MakeNsis) {
    $command = Get-Command makensis -ErrorAction SilentlyContinue
    if ($command) { $MakeNsis = $command.Source }
    else { $MakeNsis = Join-Path $PSScriptRoot '.build\nsis\nsis-3.13\Bin\makensis.exe' }
}
if (-not $OutputInstaller) { $OutputInstaller = Join-Path $PSScriptRoot ".build\installers\ytdlp-interface-reUI-$ReleaseVersion-windows-x64-setup.exe" }
$output = [IO.Path]::GetFullPath($OutputInstaller)
if (Test-Path -LiteralPath $output) { throw '安装包输出文件已存在，请指定新路径' }
New-Item -ItemType Directory -Path (Split-Path -Parent $output) -Force | Out-Null

# 生成明确的卸载清单，不递归删除用户选择的整个安装目录。
$files = @(Get-ChildItem -LiteralPath $package -File -Recurse)
$include = Join-Path (Split-Path -Parent $output) 'uninstall-files.nsh'
$lines = @($files | ForEach-Object { 'Delete "$INSTDIR\' + $_.FullName.Substring($package.Length + 1) + '"' })
$lines += @(Get-ChildItem -LiteralPath $package -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object { 'RMDir "$INSTDIR\' + $_.FullName.Substring($package.Length + 1) + '"' })
[IO.File]::WriteAllLines($include, $lines, [Text.UTF8Encoding]::new($true))
$installedKB = [int][Math]::Ceiling(($files | Measure-Object Length -Sum).Sum / 1KB)
try {
    & $MakeNsis /V2 "/DPACKAGE_DIR=$package" "/DWEBVIEW_INSTALLER=$runtime" "/DOUTPUT_FILE=$output" "/DRELEASE_VERSION=$ReleaseVersion" "/DUNINSTALL_FILES=$include" "/DINSTALLED_KB=$installedKB" (Join-Path $PSScriptRoot 'installer\reUI.nsi')
    if ($LASTEXITCODE -ne 0) { throw '安装包编译失败' }
} finally {
    Remove-Item -LiteralPath $include
}
Write-Output "安装包已生成：$output"
Get-FileHash -LiteralPath $output -Algorithm SHA256

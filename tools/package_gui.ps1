param(
    [string]$BuildDirectory = "build",
    [string]$Configuration = "Release",
    [string]$Destination = "artifacts/puyo-chain-generator-v0.3.0-windows-x64"
)
$ErrorActionPreference = "Stop"
$sourceDirectory = Split-Path $PSScriptRoot -Parent
$binaryDirectory = Join-Path $BuildDirectory $Configuration
if (!(Test-Path -LiteralPath (Join-Path $binaryDirectory "PuyoChainGenerator.exe"))) {
    $binaryDirectory = $BuildDirectory # Ninja / single-configuration build
}
$packageDirectory = [System.IO.Path]::GetFullPath($Destination)
$zipPath = "$packageDirectory.zip"
if ((Test-Path -LiteralPath $packageDirectory) -or (Test-Path -LiteralPath $zipPath)) {
    throw "Destination already exists. Choose a new destination to protect existing settings."
}
foreach ($name in @("PuyoChainGenerator.exe", "random_19_chain.exe")) {
    if (!(Test-Path -LiteralPath (Join-Path $binaryDirectory $name))) { throw "Missing binary: $name" }
}
New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null
foreach ($name in @("PuyoChainGenerator.exe", "random_19_chain.exe")) {
    Copy-Item -LiteralPath (Join-Path $binaryDirectory $name) -Destination $packageDirectory
}
foreach ($name in @("config.ini", "run.cmd", "GUI_README.md", "URL_FORMATS.md", "CHANGELOG.md", "README.md", "README.en.md", "LICENSE")) {
    Copy-Item -LiteralPath (Join-Path $sourceDirectory $name) -Destination $packageDirectory
}
foreach ($name in @("editor-demo.puyoboard", "sample-19chain.puyo")) {
    $example = Join-Path (Join-Path $sourceDirectory "examples") $name
    if (Test-Path -LiteralPath $example) { Copy-Item -LiteralPath $example -Destination $packageDirectory }
}
Compress-Archive -Path (Join-Path $packageDirectory "*") -DestinationPath $zipPath
Write-Output $zipPath

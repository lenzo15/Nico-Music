# Optional: provision this SOURCE PLUGIN when building from a source-only checkout.
# A distributed plugin package should already include the verified executable.
$ErrorActionPreference = 'Stop'
$version = '2026.11.23'
$sha256 = '622ed48023cac1daa52a3bf3a6ac38f03514de214b5f0fa069389fd0f6d17e5c'
$exe = Join-Path $PSScriptRoot 'yt-dlp.exe'
if ((Test-Path -LiteralPath $exe) -and (Get-FileHash -LiteralPath $exe).Hash -eq $sha256) { return }
$tmp = Join-Path $PSScriptRoot ([Guid]::NewGuid().ToString() + '.tmp')
try {
    Invoke-WebRequest -UseBasicParsing -Uri "https://github.com/yt-dlp/yt-dlp/releases/download/$version/yt-dlp.exe" -OutFile $tmp
    if ((Get-FileHash -LiteralPath $tmp).Hash -ne $sha256) { throw 'yt-dlp SHA-256 mismatch' }
    Move-Item -LiteralPath $tmp -Destination $exe -Force
} finally {
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp }
}

# Embedded in NicoMusic.exe; only the native host creates this job and its markers.
param([Parameter(Mandatory=$true)][int]$ParentId, [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{64}$')][string]$Sha256)
$ErrorActionPreference = 'Stop'
$job = $PSScriptRoot
# Never recursively clean an arbitrary directory when the script is run standalone.
if ([IO.Path]::GetFileName($job) -notmatch '^\.nicomusic-update-\{[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}\}$') { throw '无效的更新暂存目录。' }
$root = [IO.Directory]::GetParent($job).FullName
$stage = [IO.Path]::Combine($job, 'staged')
$backup = [IO.Path]::Combine($job, 'backup')
$exe = [IO.Path]::Combine($root, 'NicoMusic.exe')
$files = New-Object 'System.Collections.Generic.List[string]'
$applied = New-Object 'System.Collections.Generic.List[string]'
$keepBackup = $false
$appExited = $false
$parent = $null

function Check-Links([string]$path) {
    # Reject junctions/symlinks, including dangling links, at any component.
    while ($path) {
        try { $attrs = [IO.File]::GetAttributes($path) }
        catch [IO.FileNotFoundException] { $attrs = 0 }
        catch [IO.DirectoryNotFoundException] { $attrs = 0 }
        if ($attrs -band [IO.FileAttributes]::ReparsePoint) { throw "不支持链接路径：$path" }
        $path = [IO.Path]::GetDirectoryName($path)
    }
}
function Ensure-Parent([string]$path) { [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) }
function Restart-Player { [void][Diagnostics.Process]::Start((New-Object Diagnostics.ProcessStartInfo -Property @{ FileName=$exe; WorkingDirectory=$root; UseShellExecute=$false })) }
function Report([string]$message) { Add-Type -AssemblyName System.Windows.Forms; [void][Windows.Forms.MessageBox]::Show($message, 'NicoMusic 更新') }
Check-Links $job # validate before entering the cleanup scope
try {
    $parent = [Diagnostics.Process]::GetProcessById($ParentId)
    $null = $parent.Handle # retain the exact process, not a reusable PID
    if (![IO.File]::Exists($exe)) { throw '未找到 NicoMusic.exe，请保留主程序原文件名。' }
    Add-Type -AssemblyName System.IO.Compression
    $zip = [IO.File]::OpenRead([IO.Path]::Combine($job,'package.zip'))
    try {
        # Verify and extract through the same read-only handle (no swap after verification).
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $actual = [BitConverter]::ToString($sha.ComputeHash($zip)).Replace('-','') } finally { $sha.Dispose() }
        if ($actual -ne $Sha256) { throw 'SHA-256 校验失败。' }
        $zip.Position = 0
        $archive = New-Object IO.Compression.ZipArchive($zip, [IO.Compression.ZipArchiveMode]::Read, $true)
        try {
            if ($archive.Entries.Count -gt 4096) { throw '安装包文件过多。' }
            $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
            [long]$total = 0
            $buffer = New-Object byte[] 65536
            foreach ($entry in $archive.Entries) {
                $name = $entry.FullName
                if ($name -ceq 'NicoMusic/') { continue }
                if (!$name.StartsWith('NicoMusic/', [StringComparison]::Ordinal)) { throw '不支持的安装包目录结构。' }
                $rel = $name.Substring(10).TrimEnd('/')
                foreach ($part in $rel.Split('/')) {
                    if ($part -notmatch '^[a-z0-9_ -][a-z0-9_. -]*$' -or $part -match '[. ]$|^(con|prn|aux|nul|com[0-9]|lpt[0-9])(\.|$)') { throw "不安全的文件名：$name" }
                }
                if (!$seen.Add($rel)) { throw "重复路径：$rel" }
                if (($entry.ExternalAttributes -band 0x400) -or (($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000) { throw "不支持链接条目：$name" }
                if ($name.EndsWith('/')) { continue } # directory records are never materialized
                if ($rel -notmatch '^(NicoMusic(CLI)?\.exe|libmpv-2\.dll|README\.md|(?:ui|plugins|notices)/.+)$') { throw "不是程序文件：$rel" }
                $target = [IO.Path]::Combine($root,$rel)
                Check-Links $target
                $total += $entry.Length
                if ($total -gt 2GB) { throw '安装包解压后过大。' }
                $path = [IO.Path]::Combine($stage,$rel); Ensure-Parent $path
                $out = [IO.File]::Open($path, [IO.FileMode]::CreateNew)
                try {
                    $inputStream = $entry.Open()
                    try {
                        [long]$left = $entry.Length
                        while (($n = $inputStream.Read($buffer,0,$buffer.Length)) -gt 0) {
                            if ($n -gt $left) { throw '压缩文件长度异常。' }
                            $out.Write($buffer,0,$n); $left -= $n
                        }
                        if ($left -ne 0) { throw '压缩文件不完整。' }
                    } finally { $inputStream.Dispose() }
                } finally { $out.Dispose() }
                $files.Add($rel)
            }
        } finally { $archive.Dispose() }
    } finally { $zip.Dispose() }
    foreach ($required in 'NicoMusic.exe','NicoMusicCLI.exe','libmpv-2.dll','ui/index.html') {
        if (![IO.File]::Exists([IO.Path]::Combine($stage,$required))) { throw "缺少 $required" }
    }
    [IO.File]::WriteAllText([IO.Path]::Combine($job,'ready'),'')
    $parent.WaitForExit()
    $appExited = $true
    if (![IO.File]::Exists([IO.Path]::Combine($job,'apply'))) { return } # ordinary exit, not an install handoff
    # Make the main executable available last, after its resources are in place.
    foreach ($rel in (@($files | Where-Object { $_ -ne 'NicoMusic.exe' }) + 'NicoMusic.exe')) {
        $src = [IO.Path]::Combine($stage,$rel); $dst = [IO.Path]::Combine($root,$rel); $old = [IO.Path]::Combine($backup,$rel)
        Check-Links $dst; Ensure-Parent $dst
        # Same-volume moves. Record each backup immediately, even if placing the new file fails.
        if ([IO.File]::Exists($dst)) {
            Ensure-Parent $old; [IO.File]::Move($dst,$old); $applied.Add($rel)
            [IO.File]::Move($src,$dst)
        } else { [IO.File]::Move($src,$dst); $applied.Add($rel) }
    }
    Restart-Player
} catch {
    $reason = $_.Exception.Message
    if (!$appExited) {
        [IO.File]::WriteAllText([IO.Path]::Combine($job,'error.txt'), $reason)
    } else {
        for ($i=$applied.Count-1; $i -ge 0; $i--) {
            $rel=$applied[$i]; $dst=[IO.Path]::Combine($root,$rel); $old=[IO.Path]::Combine($backup,$rel)
            try {
                Check-Links $dst
                [IO.File]::Delete($dst)
                if ([IO.File]::Exists($old)) { [IO.File]::Move($old,$dst) }
            } catch { $keepBackup = $true }
        }
        if ($keepBackup) { Report "更新失败，部分文件无法还原，备份保留在 $backup。`n$reason" }
        else { Report "更新失败，已保留原版本。请检查文件占用和目录权限。`n$reason"; Restart-Player }
    }
    exit 1
} finally {
    if ((!$parent -or $parent.HasExited) -and !$keepBackup) { Remove-Item -LiteralPath $job -Recurse -Force -ErrorAction SilentlyContinue }
    if ($parent) { $parent.Dispose() }
}

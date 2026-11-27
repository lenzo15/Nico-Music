# Embedded in NicoMusic.exe; only the native host creates this job and its markers.
param([Parameter(Mandatory=$true)][int]$ParentId, [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{64}$')][string]$Sha256,
      [Parameter(Mandatory=$true)][ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')][version]$FromVersion)
$ErrorActionPreference = 'Stop'
$job = $PSScriptRoot
# Never recursively clean an arbitrary directory when the script is run standalone.
if ([IO.Path]::GetFileName($job) -notmatch '^\.nicomusic-update-\{[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}\}$') { throw '无效的更新暂存目录。' }
$root = [IO.Directory]::GetParent($job).FullName
$stage = [IO.Path]::Combine($job, 'staged')
$backup = [IO.Path]::Combine($job, 'backup')
$exe = [IO.Path]::Combine($root, 'NicoMusic.exe')
$files = New-Object 'System.Collections.Generic.List[string]'
$swapped = New-Object 'System.Collections.Generic.List[string]'
$applied = New-Object 'System.Collections.Generic.List[string]'
$createdDirs = New-Object 'System.Collections.Generic.List[string]'
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
# The backup will later be cleaned recursively. Never put a live junction in it.
function Check-PluginTree([string]$dir) {
    foreach ($entry in [IO.Directory]::EnumerateFileSystemEntries($dir)) {
        $attrs = [IO.File]::GetAttributes($entry)
        if ($attrs -band [IO.FileAttributes]::ReparsePoint) { throw "不支持链接路径：$entry" }
        if ($attrs -band [IO.FileAttributes]::Directory) { Check-PluginTree $entry }
    }
}
# Missing VERSION denotes a legacy package, not 0.0.0. Match readPluginVersion in the host.
function Get-PluginVersion([string]$folder) {
    $path = [IO.Path]::Combine($folder,'VERSION')
    if (![IO.File]::Exists($path) -and ![IO.Directory]::Exists($path)) { return $null }
    if (![IO.File]::Exists($path) -or (Get-Item -LiteralPath $path).Length -gt 64) { throw "无效的插件版本文件：$path" }
    $text = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($path))
    if ($text -cnotmatch '\A\uFEFF?((?:0|[1-9][0-9]{0,8})(?:\.(?:0|[1-9][0-9]{0,8})){2})(?:\r?\n)?\z') { throw "无效的插件版本号：$path" }
    return [version]$Matches[1]
}
function Ensure-Parent([string]$path) { [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) }
# Record only newly created installation directories, parent first. A failed update
# must not leave an empty plugins/<id> which the next run would treat as installed.
function Ensure-InstallParent([string]$path) {
    $dir = [IO.Path]::GetDirectoryName($path)
    if (![IO.Directory]::Exists($dir)) {
        Ensure-InstallParent $dir
        [void][IO.Directory]::CreateDirectory($dir)
        $createdDirs.Add($dir)
    }
}
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
    # Read only the verified incoming package, not the user's live plugin folder.
    # Packages without a catalog retain the legacy installed-only policy.
    $introduced = @{}
    $catalog = [IO.Path]::Combine($stage,'plugins','bundled-plugins.tsv')
    if ([IO.File]::Exists($catalog)) {
        if ((Get-Item -LiteralPath $catalog).Length -gt 64KB) { throw '随包插件清单过大。' }
        foreach ($line in [IO.File]::ReadAllLines($catalog)) {
            if (!$line -or $line.StartsWith('#')) { continue }
            if ($line -cnotmatch '^([a-z0-9][a-z0-9_-]{0,63})\t([0-9]+\.[0-9]+\.[0-9]+)$') { throw '随包插件清单格式错误。' }
            $id = $Matches[1]
            if ($introduced.ContainsKey($id)) { throw "随包插件清单中存在重复项：$id" }
            $introduced[$id] = [version]$Matches[2]
        }
    }
    # Validate each incoming package once, before asking the player to exit.
    $incoming = @{}
    foreach ($rel in $files) {
        if ($rel -match '^plugins/([^/]+)/') {
            $id = $Matches[1]
            if (!$incoming.ContainsKey($id)) { $incoming[$id] = Get-PluginVersion ([IO.Path]::Combine($stage,'plugins',$id)) }
        }
    }
    [IO.File]::WriteAllText([IO.Path]::Combine($job,'ready'),'')
    $parent.WaitForExit()
    $appExited = $true
    if (![IO.File]::Exists([IO.Path]::Combine($job,'apply'))) { return } # ordinary exit, not an install handoff
    # Decide for the whole package before moving ANY file (including its VERSION).
    $skip = @{}
    foreach ($id in $incoming.Keys) {
        $folder = [IO.Path]::Combine($root,'plugins',$id)
        Check-Links ([IO.Path]::Combine($folder,'VERSION'))
        if (![IO.Directory]::Exists($folder)) {
            $skip[$id] = !$introduced.ContainsKey($id) -or $introduced[$id] -le $FromVersion
        } else {
            try { $installed = Get-PluginVersion $folder }
            catch { $skip[$id] = $true; continue } # Preserve a locally damaged/unknown version.
            $skip[$id] = $null -ne $installed -and ($null -eq $incoming[$id] -or $incoming[$id] -le $installed)
        }
    }
    # Swap selected installation directories as units. Backups keep retired files
    # for rollback, but no stale scripts or extractors survive a successful upgrade.
    foreach ($id in $incoming.Keys) {
        if ($skip[$id]) { continue }
        $rel = "plugins/$id"
        $dst = [IO.Path]::Combine($root,$rel); $old = [IO.Path]::Combine($backup,$rel)
        $src = [IO.Path]::Combine($stage,$rel)
        Check-Links $dst
        Ensure-InstallParent $dst
        if ([IO.Directory]::Exists($dst)) {
            Check-PluginTree $dst
            Ensure-Parent $old
            [IO.Directory]::Move($dst,$old); $swapped.Add($rel)
            [IO.Directory]::Move($src,$dst)
        } else { [IO.Directory]::Move($src,$dst); $swapped.Add($rel) }
    }
    # Make the main executable available last, after its resources are in place.
    foreach ($rel in (@($files | Where-Object { $_ -ne 'NicoMusic.exe' }) + 'NicoMusic.exe')) {
        $src = [IO.Path]::Combine($stage,$rel); $dst = [IO.Path]::Combine($root,$rel); $old = [IO.Path]::Combine($backup,$rel)
        if ($rel -match '^plugins/[^/]+/') { continue }
        Check-Links $dst
        Ensure-InstallParent $dst
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
        for ($i=$swapped.Count-1; $i -ge 0; $i--) {
            $rel=$swapped[$i]; $dst=[IO.Path]::Combine($root,$rel); $old=[IO.Path]::Combine($backup,$rel)
            try {
                Check-Links $dst
                if ([IO.Directory]::Exists($dst)) { [IO.Directory]::Move($dst,[IO.Path]::Combine($stage,$rel)) }
                if ([IO.Directory]::Exists($old)) { [IO.Directory]::Move($old,$dst) }
            } catch { $keepBackup = $true }
        }
        for ($i=$createdDirs.Count-1; $i -ge 0; $i--) {
            try { [IO.Directory]::Delete($createdDirs[$i], $false) } catch { $keepBackup = $true }
        }
        if ($keepBackup) { Report "更新失败，部分文件无法还原，备份保留在 $backup。`n$reason" }
        else { Report "更新失败，已保留原版本。请检查文件占用和目录权限。`n$reason"; Restart-Player }
    }
    exit 1
} finally {
    if ((!$parent -or $parent.HasExited) -and !$keepBackup) { Remove-Item -LiteralPath $job -Recurse -Force -ErrorAction SilentlyContinue }
    if ($parent) { $parent.Dispose() }
}

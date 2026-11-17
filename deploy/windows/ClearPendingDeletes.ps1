# Cancels the "delete at the next restart" entries Windows holds for files and folders under the given folders.
#
# An uninstaller that could not delete a file in use (the running program, or itself) asks Windows to delete it
# at the next restart. When a new version is installed into the same folder right after, that restart deletes the
# new files of the same names: the program and its shortcuts are gone after the next reboot. The installer runs
# this after the old uninstaller and before writing the new files.
#
# Only delete entries (an empty target) under the given folders are dropped; every other entry is kept in order.
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Folders)

$ErrorActionPreference = 'Stop'
$key = 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager'

$folderList = @($Folders | Where-Object { $_ -and ($_.TrimEnd('\').Length -gt 3) } | ForEach-Object { $_.TrimEnd('\') })
if ($folderList.Count -eq 0) { exit 0 }

# Windows keeps such entries in two values; NSIS writes the first, newer Windows may use the second too
foreach ($name in @('PendingFileRenameOperations', 'PendingFileRenameOperations2')) {
    $operations = (Get-ItemProperty -LiteralPath $key -Name $name -ErrorAction SilentlyContinue).$name
    # Source and target come in pairs; anything else is left alone
    if (-not $operations -or (($operations.Count % 2) -ne 0)) { continue }

    $keep = New-Object System.Collections.Generic.List[string]
    $dropped = 0
    for ($i = 0; $i -lt $operations.Count; $i += 2) {
        $source = $operations[$i] -replace '^\\\?\?\\', ''
        $target = $operations[$i + 1]
        $ours = $false
        foreach ($folder in $folderList) {
            if (($source -ieq $folder) -or $source.StartsWith($folder + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
                $ours = $true
            }
        }
        if ($ours -and ($target -eq '')) {
            Write-Output ('Cancelled delete at restart: ' + $source)
            $dropped++
            continue
        }
        $keep.Add($operations[$i])
        $keep.Add($target)
    }

    if ($dropped -eq 0) { continue }
    if ($keep.Count -eq 0) {
        Remove-ItemProperty -LiteralPath $key -Name $name
    } else {
        Set-ItemProperty -LiteralPath $key -Name $name -Value ([string[]]$keep.ToArray()) -Type MultiString
    }
}
exit 0

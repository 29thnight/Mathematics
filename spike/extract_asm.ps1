# Extracts each probe_* function body from an assembly listing so Mathf's
# codegen can be diffed against DirectXMath's. Usage:
#   pwsh -File extract_asm.ps1 -AsmPath <file.asm> [-Flavor msvc|clang]

param(
    [Parameter(Mandatory = $true)][string]$AsmPath,
    [ValidateSet('msvc', 'clang')][string]$Flavor = 'msvc'
)

if (-not (Test-Path $AsmPath)) { throw "Assembly listing not found: $AsmPath" }
$lines = Get-Content $AsmPath

# Instructions that indicate a stack round-trip rather than register-only work.
$spillPattern = '^\s*(mov|movap|movup|movss)\w*\s+.*\b(rsp|rbp)\b'

function Show-Body {
    param([string[]]$Body, [string]$Name)

    $code = $Body | Where-Object {
        $_ -notmatch '^\s*(;|\.|$)' -and $_ -notmatch '^\s*\w+:\s*$'
    }
    $spills = @($code | Where-Object { $_ -match $spillPattern })

    # Write-Output throughout: mixing Write-Host with pipeline output reorders
    # the listing so bodies no longer follow their own headers.
    Write-Output ""
    Write-Output "--- $Name ---"
    Write-Output ("instructions: {0}   stack traffic: {1}" -f $code.Count, $spills.Count)
    $code | ForEach-Object { Write-Output ("    " + $_.Trim()) }
}

if ($Flavor -eq 'msvc') {
    $start = $null; $name = $null
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^(\S*probe_\w+\S*)\s+PROC') {
            $name = $Matches[1]; $start = $i
        } elseif ($start -ne $null -and $lines[$i] -match '\s+ENDP') {
            Show-Body -Body $lines[($start + 1)..($i - 1)] -Name $name
            $start = $null
        }
    }
} else {
    $start = $null; $name = $null
    for ($i = 0; $i -lt $lines.Count; $i++) {
        # Clang emits quoted labels with a trailing "# @..." comment.
        if ($lines[$i] -match '^"?([^"]*probe_\w+[^"]*)"?:') {
            $name = $Matches[1]; $start = $i
        } elseif ($start -ne $null -and $lines[$i] -match '^\s*(ret|\.seh_endproc)') {
            Show-Body -Body $lines[($start + 1)..$i] -Name $name
            $start = $null
        }
    }
}

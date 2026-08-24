# Extracts each probe_* function body from an assembly listing so Mathematics'
# codegen can be diffed against DirectXMath's. Usage:
#   pwsh -File extract_asm.ps1 -asm_path <file.asm> [-flavor msvc|clang]

param(
    [Parameter(Mandatory = $true)][string]$asm_path,
    [ValidateSet('msvc', 'clang')][string]$flavor = 'msvc'
)

if (-not (Test-Path $asm_path)) { throw "Assembly listing not found: $asm_path" }
$lines = Get-Content $asm_path

# Instructions that indicate a stack round-trip rather than register-only work.
$spill_pattern = '^\s*(mov|movap|movup|movss)\w*\s+.*\b(rsp|rbp)\b'

function show_body {
    param([string[]]$body, [string]$name)

    $code = $body | Where-Object {
        $_ -notmatch '^\s*(;|\.|$)' -and $_ -notmatch '^\s*\w+:\s*$'
    }
    $spills = @($code | Where-Object { $_ -match $spill_pattern })

    # Write-Output throughout: mixing Write-Host with pipeline output reorders
    # the listing so bodies no longer follow their own headers.
    Write-Output ""
    Write-Output "--- $name ---"
    Write-Output ("instructions: {0}   stack traffic: {1}" -f $code.Count, $spills.Count)
    $code | ForEach-Object { Write-Output ("    " + $_.Trim()) }
}

if ($flavor -eq 'msvc') {
    $start = $null; $name = $null
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^(\S*probe_\w+\S*)\s+PROC') {
            $name = $Matches[1]; $start = $i
        } elseif ($start -ne $null -and $lines[$i] -match '\s+ENDP') {
            show_body -body $lines[($start + 1)..($i - 1)] -name $name
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
            show_body -body $lines[($start + 1)..$i] -name $name
            $start = $null
        }
    }
}

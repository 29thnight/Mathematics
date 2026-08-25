[CmdletBinding()]
param(
    [string]$Source = (Join-Path $PSScriptRoot '..\tools\codegen_probes.cpp'),

    [string]$IncludeDirectory = (Join-Path $PSScriptRoot '..\include'),

    [string]$OutputDirectory = $env:TEMP
)

$ErrorActionPreference = 'Stop'

# A structural gate, not a timing one. docs/BASELINE.md section 8 measured a 2x
# swing between benchmarks whose instruction bytes were identical, purely from
# where the linker put .text; docs/BASELINE.md section 9 found the difference
# that does survive a relink -- whether a fixed-extent loop was expanded or left
# as a loop. This script reads that off an assembly listing.
#
# MSVC only, on purpose. clang expands every one of these shapes already, and
# the failure this guards against is specific to MSVC's refusal to unroll an
# iterator-driven loop nested inside another loop.

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$includePath = (Resolve-Path -LiteralPath $IncludeDirectory).Path

$compiler = Get-Command cl.exe -ErrorAction SilentlyContinue
if (-not $compiler) {
    throw "cl.exe is not on PATH. Run this from a Visual Studio developer prompt, or via scripts\build.bat's environment."
}

if (-not (Test-Path -LiteralPath $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
}
$assemblyPath = Join-Path $OutputDirectory 'mathematics-codegen-probes.asm'
$objectPath = Join-Path $OutputDirectory 'mathematics-codegen-probes.obj'
$logPath = Join-Path $OutputDirectory 'mathematics-codegen-probes.log'

# The bench target's flags. A different /arch or /fp would measure a different
# compiler, and the point is to gate what the benchmarks actually compile to.
$compilerArguments = @(
    '/nologo', '/c'
    '/std:c++latest', '/O2', '/EHsc', '/W4', '/permissive-'
    '/Zc:__cplusplus', '/Zc:preprocessor', '/utf-8'
    '/arch:AVX2', '/fp:fast'
    "/I$includePath"
    '/FAs', "/Fa$assemblyPath", "/Fo$objectPath"
    $sourcePath
)

Write-Host "Compiling codegen probes: $sourcePath"
& $compiler.Path @compilerArguments *> $logPath
if ($LASTEXITCODE -ne 0) {
    Get-Content -LiteralPath $logPath | Write-Host
    throw "Codegen probe compilation failed with exit code $LASTEXITCODE."
}

# CODEGEN-GATE: <probe> backward_branches<=<count>
$expectations = [ordered]@{}
foreach ($line in Get-Content -LiteralPath $sourcePath) {
    if ($line -match 'CODEGEN-GATE:\s+(\w+)\s+backward_branches<=(\d+)') {
        $expectations[$Matches[1]] = [int]$Matches[2]
    }
}
if ($expectations.Count -eq 0) {
    throw "No CODEGEN-GATE directives found in $sourcePath."
}

# One MSVC listing, one probe body at a time. A backward branch is a jump whose
# target label was already emitted inside the same body; the outer loop accounts
# for one, and any fixed-extent loop that survived adds another.
function Measure-BackwardBranches {
    param([AllowEmptyCollection()][AllowEmptyString()][string[]]$Body)

    $seenLabels = [System.Collections.Generic.HashSet[string]]::new()
    $count = 0
    foreach ($line in $Body) {
        $text = ($line -split ';', 2)[0].Trim()
        if (-not $text) { continue }
        if ($text -match '^([\$\.\w@]+):$') {
            [void]$seenLabels.Add($Matches[1])
            continue
        }
        if ($text -match '^j\w+\s+(?:SHORT\s+|NEAR\s+PTR\s+)?([\$\.\w@]+)') {
            if ($seenLabels.Contains($Matches[1])) { $count++ }
        }
    }
    return $count
}

$listing = Get-Content -LiteralPath $assemblyPath
$bodies = @{}
$startIndex = -1
$currentName = $null
for ($index = 0; $index -lt $listing.Count; $index++) {
    if ($listing[$index] -match '^(probe_\w+)\s+PROC') {
        $currentName = $Matches[1]
        $startIndex = $index
    } elseif ($startIndex -ge 0 -and $listing[$index] -match '\sENDP\b') {
        $bodies[$currentName] = $listing[($startIndex + 1)..($index - 1)]
        $startIndex = -1
    }
}

$failed = $false
foreach ($probe in $expectations.Keys) {
    if (-not $bodies.ContainsKey($probe)) {
        Write-Host ("[FAIL] {0}: no body in the listing. Was it inlined away or renamed?" -f $probe)
        $failed = $true
        continue
    }

    $allowed = $expectations[$probe]
    $observed = Measure-BackwardBranches -Body $bodies[$probe]
    $status = if ($observed -gt $allowed) { 'FAIL' } else { 'PASS' }
    Write-Host ('[{0}] {1}: {2} backward branch(es) of {3} allowed' -f
        $status, $probe, $observed, $allowed)
    if ($status -eq 'FAIL') { $failed = $true }
}

if ($failed) {
    throw "A fixed-extent shape stopped compiling to straight-line code. Listing: $assemblyPath"
}

Write-Host "Codegen gate passed. Listing: $assemblyPath"

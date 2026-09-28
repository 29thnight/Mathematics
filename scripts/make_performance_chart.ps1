<#
.SYNOPSIS
Render docs/assets/performance-comparison.png from benchmark JSON.

.DESCRIPTION
Reads wall-clock aggregate medians from one MSVC run and one clang-cl run,
lays the comparison out as HTML, and captures it with headless Chrome or Edge.

Every bar caption is computed from the same median that draws the bar, so a
caption cannot drift away from its chart the way a hand-transcribed one does.
The numbers used are printed, so the figure can be checked against the raw JSON.

Needs no toolchain beyond the browser already on the machine: the page reports
its own height through --dump-dom, so the capture is taken at the exact content
size and never has to be cropped.

.EXAMPLE
$bench = "$env:LOCALAPPDATA\MathematicsBuild\msvc-release\bench\mathematics_bench.exe"
& $bench --benchmark_filter=bm_mathematics_mul_add_throughput --benchmark_min_time=2s   # warm up, see BASELINE.md 8
& $bench --benchmark_min_time=0.4s --benchmark_repetitions=9 `
    --benchmark_enable_random_interleaving=true --benchmark_report_aggregates_only=true `
    --benchmark_out_format=json --benchmark_out="$env:TEMP\msvc.json"
scripts\make_performance_chart.ps1 -MsvcJson "$env:TEMP\msvc.json" -ClangJson "$env:TEMP\clang.json"
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MsvcJson,

    [Parameter(Mandatory = $true)]
    [string]$ClangJson,

    [string]$OutputPath,

    # The layout the capture was taken from. Goes to the temp directory unless
    # asked for elsewhere, so rendering into docs/assets leaves only the PNG.
    [string]$HtmlPath,

    [ValidateRange(600, 4000)]
    [int]$Width = 1200,

    [ValidateRange(1, 4)]
    [int]$Scale = 2,

    [string]$BrowserPath
)

$ErrorActionPreference = 'Stop'

$colors = @{
    Math = '#6C5CE7'
    Dx   = '#12A5A0'
}

function Get-BenchmarkMedians {
    param([string]$Path)

    $data = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $result = @{}
    foreach ($entry in $data.benchmarks) {
        if ($entry.error_occurred) { throw "benchmark '$($entry.run_name)' failed: $($entry.error_message)" }
        if ($entry.run_type -ne 'aggregate') { continue }
        if (-not $result.ContainsKey($entry.run_name)) {
            $result[$entry.run_name] = [pscustomobject]@{
                Ns = 0.0; Ips = 0.0; Cv = 0.0; Medians = 0; Cvs = 0
            }
        }
        $row = $result[$entry.run_name]
        switch ($entry.aggregate_name) {
            'median' {
                $row.Medians++
                $factor = switch ($entry.time_unit) {
                    'ns' { 1.0 }; 'us' { 1.0e3 }; 'ms' { 1.0e6 }; 's' { 1.0e9 }
                    default { throw "unsupported time unit '$($entry.time_unit)'" }
                }
                $row.Ns = [double]$entry.real_time * $factor
                if (-not [double]::IsFinite($row.Ns) -or $row.Ns -le 0) {
                    throw "invalid wall-clock median for '$($entry.run_name)'"
                }
                if ($null -ne $entry.items_per_second) {
                    # For odd repetition counts, median(ips) * median(cpu_time)
                    # recovers the batch size exactly. Divide by wall time,
                    # as check_performance.ps1 does; Windows CPU time is quantized.
                    if ($entry.repetitions % 2 -ne 1) {
                        throw 'throughput medians require an odd repetition count'
                    }
                    $items = [double]$entry.items_per_second * [double]$entry.cpu_time * $factor * 1.0e-9
                    $row.Ips = $items / ($row.Ns * 1.0e-9)
                    if (-not [double]::IsFinite($row.Ips) -or $row.Ips -le 0) {
                        throw "invalid wall-clock throughput for '$($entry.run_name)'"
                    }
                }
            }
            'cv' { $row.Cv = [double]$entry.real_time * 100.0; $row.Cvs++ }
        }
    }
    foreach ($entry in $result.GetEnumerator()) {
        if ($entry.Value.Medians -ne 1 -or $entry.Value.Cvs -ne 1 -or
            -not [double]::IsFinite($entry.Value.Cv)) {
            throw "missing, duplicate or invalid aggregates for '$($entry.Key)'"
        }
        if ($entry.Value.Cv -gt 10.0) {
            throw "unstable wall-clock sample for '$($entry.Key)': CV $($entry.Value.Cv)% exceeds 10%"
        }
    }
    return $result
}

function Get-Metric {
    param([hashtable]$Run, [string]$Name, [ValidateSet('Ns', 'Mps', 'Gps')][string]$As)

    if (-not $Run.ContainsKey($Name)) {
        throw "benchmark '$Name' is not in the JSON -- run the full suite, not a filtered subset."
    }
    switch ($As) {
        'Ns' { $value = $Run[$Name].Ns }
        'Mps' { $value = $Run[$Name].Ips / 1e6 }
        'Gps' { $value = $Run[$Name].Ips / 1e9 }
    }
    if (-not [double]::IsFinite($value) -or $value -le 0) {
        throw "missing or invalid $As metric for '$Name'"
    }
    return $value
}

# Percent by which $a exceeds $b. Positive means "more", which is better for a
# throughput panel and worse for a latency one -- the caller says which.
function Get-Percent {
    param([double]$A, [double]$B)
    return ($A - $B) / $B * 100.0
}

function New-Bar {
    param([string]$Label, [double]$Value, [string]$Color)
    return [pscustomobject]@{ Label = $Label; Value = $Value; Color = $Color }
}

function New-Panel {
    param([string]$Title, [string]$Unit, [string]$Better, [object[]]$Bars, [string]$Format = '{0:F2}')
    return [pscustomobject]@{
        Title = $Title; Unit = $Unit; Better = $Better
        Bars = $Bars; Format = $Format; Note = ''
    }
}

function Build-ComparisonPanels {
    param([hashtable]$Run, [object[]]$Specs)

    foreach ($spec in $Specs) {
        $math = Get-Metric -Run $Run -Name "bm_mathematics_$($spec[1])" -As $spec[2]
        $dx = Get-Metric -Run $Run -Name "bm_dx_math_$($spec[1])" -As $spec[2]
        $panel = New-Panel $spec[0] $spec[3] $spec[4] @(
            (New-Bar 'Mathematics' $math $colors.Math),
            (New-Bar 'DirectXMath' $dx $colors.Dx)) $spec[5]
        $metric = if ($spec[2] -eq 'Ns') { '지연' } else { '처리량' }
        $panel.Note = 'Mathematics {0}은 DirectXMath 대비 {1:+0.0;-0.0;+0.0}%' -f $metric, (Get-Percent $math $dx)
        $panel
    }
}

function Build-Panels {
    param([hashtable]$Run)

    $specs = @(
        @('vector4 add latency', 'add_latency', 'Ns', 'ns', '낮을수록 좋음', '{0:F2}'),
        @('vector4 multiply + add latency', 'mul_add_latency', 'Ns', 'ns', '낮을수록 좋음', '{0:F2}'),
        @('dot4 latency', 'dot4_latency', 'Ns', 'ns', '낮을수록 좋음', '{0:F2}'),
        @('multiply + add throughput', 'mul_add_throughput', 'Gps', 'Gitems/s', '높을수록 좋음', '{0:F3}'),
        @('vector3 cross throughput', 'cross_throughput', 'Mps', 'Mitems/s', '높을수록 좋음', '{0:F1}'),
        @('vector3 normalize throughput', 'vector3_normalize_throughput', 'Mps', 'Mitems/s', '높을수록 좋음', '{0:F1}'),
        @('matrix4x4 multiply throughput', 'matrix4x4_multiply_throughput', 'Mps', 'Mitems/s', '높을수록 좋음', '{0:F1}'),
        @('matrix4x4 inverse throughput', 'matrix4x4_inverse', 'Mps', 'Mitems/s', '높을수록 좋음', '{0:F1}'),
        @('matrix4x4 transpose throughput', 'matrix4x4_transpose', 'Mps', 'Mitems/s', '높을수록 좋음', '{0:F1}'),
        @('quaternion multiply throughput', 'quaternion_multiply_throughput', 'Mps', 'Mitems/s', '높을수록 좋음', '{0:F1}'),
        @('matrix4x4 multiply latency', 'matrix4x4_multiply_latency', 'Ns', 'ns', '낮을수록 좋음', '{0:F2}'),
        @('quaternion slerp throughput', 'quaternion_slerp', 'Mps', 'Mitems/s', '높을수록 좋음', '{0:F1}')
    )
    Build-ComparisonPanels -Run $Run -Specs $specs
}

function Build-ClangPanels {
    param([hashtable]$Run)

    $specs = @(
        @('vector3 normalize throughput', 'vector3_normalize_throughput', 'Mps', 'Mitems/s', '높을수록 좋음', '{0:F1}'),
        @('matrix4x4 multiply throughput', 'matrix4x4_multiply_throughput', 'Mps', 'Mitems/s', '높을수록 좋음', '{0:F1}'),
        @('matrix4x4 multiply latency', 'matrix4x4_multiply_latency', 'Ns', 'ns', '낮을수록 좋음', '{0:F2}'),
        @('quaternion slerp throughput', 'quaternion_slerp', 'Mps', 'Mitems/s', '높을수록 좋음', '{0:F1}')
    )
    Build-ComparisonPanels -Run $Run -Specs $specs
}
function Format-Tick {
    param([double]$Value, [string]$Format)
    if ($Value -ge 10) { return '{0:F0}' -f $Value }
    $text = $Format -f $Value
    if ($text.Contains('.')) { $text = $text.TrimEnd('0').TrimEnd('.') }
    if ([string]::IsNullOrEmpty($text)) { return '0' }
    return $text
}

function ConvertTo-PanelHtml {
    param([object]$Panel)

    $peak = ($Panel.Bars | ForEach-Object { $_.Value } | Measure-Object -Maximum).Maximum
    if ($peak -le 0) { $peak = 1.0 }

    $rows = foreach ($bar in $Panel.Bars) {
        $width = [Math]::Max($bar.Value / $peak * 100.0, 3.0)
        '<div class="row"><div class="lbl">{0}</div><div class="track">' -f $bar.Label +
        ('<div class="fill" style="width:{0:F2}%;background:{1}"><span class="v">{2}</span></div>' -f
            $width, $bar.Color, ($Panel.Format -f $bar.Value)) + '</div></div>'
    }
    $ticks = foreach ($i in 0..4) { '<span>{0}</span>' -f (Format-Tick ($peak * $i / 4) $Panel.Format) }

    return '<section class="panel"><div class="phead"><h3>{0}</h3><div class="unit">{1} · {2}</div></div><div class="rows">{3}</div><div class="axis">{4}</div><p class="pnote">{5}</p></section>' -f `
        $Panel.Title, $Panel.Better, $Panel.Unit, ($rows -join ''), ($ticks -join ''), $Panel.Note
}

function New-ChartHtml {
    param([object[]]$Panels, [object[]]$ClangPanels, [string]$Meta, [string]$Key, [string[]]$Footnotes, [int]$PageWidth)

    $css = @'
* { box-sizing: border-box; }
body { margin:0; background:#fff; color:#14161c;
       font-family:"Segoe UI","Malgun Gothic",system-ui,sans-serif;
       -webkit-font-smoothing:antialiased; }
.page { padding:36px 44px 30px; }
h1 { font-size:34px; margin:0 0 7px; letter-spacing:-.022em; font-weight:700; }
.sub { color:#5b6270; font-size:13px; margin:0 0 18px; }
.legend { display:flex; gap:22px; flex-wrap:wrap; font-size:12.5px; color:#3d434f;
          padding-bottom:15px; border-bottom:1px solid #e3e6ec; }
.legend i { width:10px; height:10px; border-radius:2px; display:inline-block; margin-right:7px; }
h2 { font-size:17.5px; margin:28px 0 3px; font-weight:700; }
.lede { color:#5b6270; font-size:12.5px; margin:0 0 16px; }
.grid { display:grid; grid-template-columns:1fr 1fr; gap:26px 38px; }
.panel { min-width:0; }
.phead { display:flex; justify-content:space-between; align-items:baseline; gap:12px; margin-bottom:12px; }
.phead h3 { font-size:14px; margin:0; font-weight:600; }
.unit { font-size:11.5px; color:#8a909c; white-space:nowrap; }
.rows { display:flex; flex-direction:column; gap:7px; }
.row { display:grid; grid-template-columns:136px 1fr; align-items:center; gap:10px; }
.lbl { font-size:13px; text-align:right; color:#2b303a; }
.track { height:27px; }
.fill { height:100%; border-radius:3px; position:relative; }
.v { position:absolute; right:9px; top:50%; transform:translateY(-50%); color:#fff;
     font-size:12.5px; font-weight:700; font-variant-numeric:tabular-nums; }
.axis { display:flex; justify-content:space-between; margin:7px 0 0 146px; font-size:11px;
        color:#9aa0ac; font-variant-numeric:tabular-nums; border-top:1px solid #e3e6ec; padding-top:5px; }
.pnote { font-size:11.5px; color:#6b7280; margin:9px 0 0; line-height:1.55; }
.key { margin:30px 0 0; padding:13px 17px; border-left:3px solid #6C5CE7; background:#f5f4fe;
       font-size:13px; line-height:1.62; color:#25272f; }
.foot { margin-top:20px; padding-top:15px; border-top:1px solid #e3e6ec; display:grid;
        grid-template-columns:1fr 1fr; gap:9px 38px; font-size:11px; color:#8a909c; line-height:1.68; }
'@

    $legendItems = @(
        @('Mathematics', $colors.Math), @('DirectXMath', $colors.Dx))
    $legend = ($legendItems | ForEach-Object { '<span><i style="background:{0}"></i>{1}</span>' -f $_[1], $_[0] }) -join ''
    $first = ($Panels[0..3] | ForEach-Object { ConvertTo-PanelHtml $_ }) -join ''
    $second = ($Panels[4..11] | ForEach-Object { ConvertTo-PanelHtml $_ }) -join ''
    $third = ($ClangPanels | ForEach-Object { ConvertTo-PanelHtml $_ }) -join ''
    $foot = ($Footnotes | ForEach-Object { '<div>{0}</div>' -f $_ }) -join ''

    return @"
<!doctype html><html lang="ko"><head><meta charset="utf-8">
<title>Mathematics 성능 비교</title>
<style>body { width:${PageWidth}px; }
$css</style></head>
<body><div class="page">
<h1>Mathematics 성능 비교</h1>
<p class="sub">$Meta</p>
<div class="legend">$legend</div>
<h2>MSVC · 저수준 연산 비교</h2>
<p class="lede">동일한 종속 체인과 배치 크기에서 직접 비교. 지연시간은 낮을수록, 처리량은 높을수록 좋습니다.</p>
<div class="grid">$first</div>
<h2>MSVC · 고수준 연산 비교</h2>
<p class="lede">Mathematics와 DirectXMath를 같은 입력과 배치 크기로 비교합니다.</p>
<div class="grid">$second</div>
<h2>clang-cl · 성능 개선 항목</h2>
<p class="lede">현재 구현을 DirectXMath와 비교한 값입니다. 이전 버전 대비 개선율은 docs/BASELINE.md §12·§13에 별도로 기록돼 있습니다.</p>
<div class="grid">$third</div>
<div class="key">$Key</div>
<div class="foot">$foot</div>
</div>
<script>
// Reported back through --dump-dom so the capture can use the exact content
// height instead of a guessed viewport that would need cropping afterwards.
document.body.setAttribute('data-page-height', document.documentElement.scrollHeight);
</script>
</body></html>
"@
}

function Find-Browser {
    param([string]$Explicit)

    if ($Explicit) {
        if (-not (Test-Path -LiteralPath $Explicit)) { throw "browser not found: $Explicit" }
        return (Resolve-Path -LiteralPath $Explicit).Path
    }
    $candidates = @(
        "$env:ProgramFiles\Google\Chrome\Application\chrome.exe"
        "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe"
        "$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe"
        "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }
    throw 'no Chrome or Edge found; pass -BrowserPath.'
}

# ------------------------------------------------------------------- main

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputPath) { $OutputPath = Join-Path $repoRoot 'docs\assets\performance-comparison.png' }
$outputFull = [System.IO.Path]::GetFullPath($OutputPath)
if (-not $HtmlPath) { $HtmlPath = Join-Path $env:TEMP 'mathematics-performance-chart.html' }
$htmlFull = [System.IO.Path]::GetFullPath($HtmlPath)

$msvc = Get-BenchmarkMedians -Path $MsvcJson
$clang = Get-BenchmarkMedians -Path $ClangJson
$panels = @(Build-Panels -Run $msvc)
$clangPanels = @(Build-ClangPanels -Run $clang)

foreach ($panel in @($panels) + @($clangPanels)) {
    $bars = ($panel.Bars | ForEach-Object { '{0}={1}' -f $_.Label, ($panel.Format -f $_.Value) }) -join '  '
    Write-Host ('{0,-32} {1}' -f $panel.Title, $bars)
    Write-Host ('{0,-32} -> {1}' -f '', $panel.Note)
}
$worst = $msvc.GetEnumerator() | Sort-Object { $_.Value.Cv } -Descending | Select-Object -First 1
Write-Host ''
Write-Host ('MSVC worst wall-clock CV: {0} {1:F1}%' -f $worst.Key, $worst.Value.Cv)
$clangWorst = $clang.GetEnumerator() | Sort-Object { $_.Value.Cv } -Descending | Select-Object -First 1
Write-Host ('clang-cl worst wall-clock CV: {0} {1:F1}%' -f $clangWorst.Key, $clangWorst.Value.Cv)

$context = (Get-Content -LiteralPath $MsvcJson -Raw | ConvertFrom-Json).context
$measured = ([datetime]$context.date).ToString('yyyy-MM-dd')
$clangMeasured = ([datetime](Get-Content -LiteralPath $ClangJson -Raw | ConvertFrom-Json).context.date).ToString('yyyy-MM-dd')
$meta = "Intel Core i7-8700K · Windows 11 · MSVC 19.51 / clang-cl 22.1.3 · C++23 · /O2 /arch:AVX2 /fp:fast · 벽시계 중앙값 · MSVC $measured / clang $clangMeasured"
$key = '<b>현재 구현:</b> clang-cl에서 Mathematics의 matrix4x4 곱 지연은 DX 대비 {0:+0.0;-0.0;+0.0}%, slerp 처리량은 {1:+0.0;-0.0;+0.0}%입니다. 컴파일러별 같은 실행 안에서 비교한 값입니다.' -f `
    (Get-Percent (Get-Metric -Run $clang -Name 'bm_mathematics_matrix4x4_multiply_latency' -As 'Ns') (Get-Metric -Run $clang -Name 'bm_dx_math_matrix4x4_multiply_latency' -As 'Ns')),
    (Get-Percent (Get-Metric -Run $clang -Name 'bm_mathematics_quaternion_slerp' -As 'Mps') (Get-Metric -Run $clang -Name 'bm_dx_math_quaternion_slerp' -As 'Mps'))

$footnotes = @(
    ('측정: Google Benchmark 1.9.0, 무작위 인터리빙 9회 반복, <code>--benchmark_min_time=0.4s</code>, 처리량 벤치 2초 예열. ' +
     '지연은 real_time 중앙값, 처리량은 배치 크기 / real_time입니다. Windows CPU 시간의 양자화를 피합니다. ' +
     '최대 CV: MSVC {0:F1}%, clang-cl {1:F1}%.' -f $worst.Value.Cv, $clangWorst.Value.Cv)
    ('기준: DirectXMath Windows SDK 10.0.26100. 두 컴파일러의 측정은 순차 실행했습니다. ' +
     'normalize의 퇴화 입력 계약은 precise 모드 기준이며, 이 도표는 fast 모드의 성능입니다.')
    ('모든 처리량 벤치는 입력과 출력을 한 공유 아레나에 페이지 고정 오프셋으로 배치합니다. ' +
     '독립 할당 시 4K 앨리어싱이 바이너리 배치에 따라 최대 29포인트의 거짓 격차를 만들었습니다.')
    (('주의: clang-cl 배치 정점 변환 처리량은 DX 대비 {0:+0.0;-0.0;+0.0}%였습니다. 코드 배치에 따른 변동과 비대칭 비교이며 docs/OPEN-ISSUES.md §4에 기록돼 있습니다. ' +
     'clang의 fast math에는 <code>-fno-finite-math-only</code>를 추가합니다.') -f
        (Get-Percent (Get-Metric -Run $clang -Name 'bm_mathematics_transform_point_stream' -As 'Mps') (Get-Metric -Run $clang -Name 'bm_dx_math_transform_coord_stream' -As 'Mps')))
)

$html = New-ChartHtml -Panels $panels -ClangPanels $clangPanels -Meta $meta -Key $key -Footnotes $footnotes -PageWidth $Width
$htmlDirectory = Split-Path -Parent $htmlFull
if ($htmlDirectory -and -not (Test-Path -LiteralPath $htmlDirectory)) {
    New-Item -ItemType Directory -Path $htmlDirectory -Force | Out-Null
}
Set-Content -LiteralPath $htmlFull -Value $html -Encoding utf8NoBOM

$browser = Find-Browser -Explicit $BrowserPath
$uri = ([System.Uri]$htmlFull).AbsoluteUri
$common = @('--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-sandbox')

$dom = & $browser @common '--dump-dom' $uri 2>$null | Out-String
$match = [regex]::Match($dom, 'data-page-height="(\d+)"')
if (-not $match.Success) { throw 'the page did not report its height; the browser may have failed to run the probe.' }
$height = [int]$match.Groups[1].Value

& $browser @common "--force-device-scale-factor=$Scale" "--screenshot=$outputFull" `
    "--window-size=$Width,$height" $uri 2>$null | Out-Null
if (-not (Test-Path -LiteralPath $outputFull)) { throw "the browser did not write $outputFull." }

$size = (Get-Item -LiteralPath $outputFull).Length
Write-Host ''
Write-Host ('wrote {0} -- {1}x{2} px at {3}x ({4} KiB)' -f `
    $outputFull, ($Width * $Scale), ($height * $Scale), $Scale, [int]($size / 1KB))
Write-Host ("layout: $htmlFull")

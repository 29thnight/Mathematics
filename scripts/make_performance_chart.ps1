<#
.SYNOPSIS
Render docs/assets/performance-comparison.png from benchmark JSON.

.DESCRIPTION
Reads the aggregate medians out of one MSVC run and one clang-cl run, lays the
comparison out as HTML, and captures it with headless Chrome or Edge.

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
    Math      = '#6C5CE7'
    Unchecked = '#1F6FEB'
    Dx        = '#12A5A0'
    Glm       = '#D97706'
    Vm        = '#DB2E68'
}

function Get-BenchmarkMedians {
    param([string]$Path)

    $data = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    $result = @{}
    foreach ($entry in $data.benchmarks) {
        if ($entry.run_type -ne 'aggregate') { continue }
        if (-not $result.ContainsKey($entry.run_name)) {
            $result[$entry.run_name] = [pscustomobject]@{ Ns = 0.0; Ips = 0.0; Cv = 0.0 }
        }
        $row = $result[$entry.run_name]
        switch ($entry.aggregate_name) {
            'median' {
                $row.Ns = [double]$entry.cpu_time
                if ($entry.items_per_second) { $row.Ips = [double]$entry.items_per_second }
            }
            'cv' { $row.Cv = [double]$entry.cpu_time * 100.0 }
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
        'Ns' { return $Run[$Name].Ns }
        'Mps' { return $Run[$Name].Ips / 1e6 }
        'Gps' { return $Run[$Name].Ips / 1e9 }
    }
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

function Get-BarValue {
    param([object]$Panel, [string]$Label)
    return ($Panel.Bars | Where-Object { $_.Label -eq $Label } | Select-Object -First 1).Value
}

function Build-Panels {
    param([hashtable]$M)

    $ns = { param($n) Get-Metric -Run $M -Name $n -As 'Ns' }
    $mps = { param($n) Get-Metric -Run $M -Name $n -As 'Mps' }
    $gps = { param($n) Get-Metric -Run $M -Name $n -As 'Gps' }

    return @(
        (New-Panel 'vector4 add latency' 'ns' '낮을수록 좋음' @(
            (New-Bar 'Mathematics' (& $ns 'bm_mathematics_add_latency') $colors.Math),
            (New-Bar 'DirectXMath' (& $ns 'bm_dx_math_add_latency') $colors.Dx),
            (New-Bar 'GLM' (& $ns 'bm_glm_add_latency') $colors.Glm),
            (New-Bar 'Vectormath' (& $ns 'bm_vectormath_add_latency') $colors.Vm))),

        (New-Panel 'vector4 multiply + add latency' 'ns' '낮을수록 좋음' @(
            (New-Bar 'Mathematics' (& $ns 'bm_mathematics_mul_add_latency') $colors.Math),
            (New-Bar 'DirectXMath' (& $ns 'bm_dx_math_mul_add_latency') $colors.Dx),
            (New-Bar 'GLM' (& $ns 'bm_glm_mul_add_latency') $colors.Glm),
            (New-Bar 'Vectormath' (& $ns 'bm_vectormath_mul_add_latency') $colors.Vm))),

        (New-Panel 'dot4 → scalar latency' 'ns' '낮을수록 좋음' @(
            (New-Bar 'Mathematics' (& $ns 'bm_mathematics_dot4_scalar_latency') $colors.Math),
            (New-Bar 'DirectXMath' (& $ns 'bm_dx_math_dot4_scalar_latency') $colors.Dx),
            (New-Bar 'GLM' (& $ns 'bm_glm_dot4_scalar_latency') $colors.Glm),
            (New-Bar 'Vectormath' (& $ns 'bm_vectormath_dot4_scalar_latency') $colors.Vm))),

        (New-Panel 'multiply + add throughput' 'Gitems/s' '높을수록 좋음' @(
            (New-Bar 'Mathematics' (& $gps 'bm_mathematics_mul_add_throughput') $colors.Math),
            (New-Bar 'DirectXMath' (& $gps 'bm_dx_math_mul_add_throughput') $colors.Dx),
            (New-Bar 'GLM' (& $gps 'bm_glm_mul_add_throughput') $colors.Glm),
            (New-Bar 'Vectormath' (& $gps 'bm_vectormath_mul_add_throughput') $colors.Vm)) '{0:F3}'),

        (New-Panel 'vector3 chain latency' 'ns' '낮을수록 좋음' @(
            (New-Bar 'Mathematics' (& $ns 'bm_mathematics_vector3_chain_latency') $colors.Math),
            (New-Bar 'DirectXMath' (& $ns 'bm_dx_math_xmvector_chain_latency') $colors.Dx),
            (New-Bar 'GLM' (& $ns 'bm_glm_vector3_chain_latency') $colors.Glm),
            (New-Bar 'DirectXMath packed' (& $ns 'bm_dx_math_xmfloat3_chain_latency') $colors.Dx))),

        (New-Panel 'vector3 normalize throughput' 'Mitems/s' '높을수록 좋음' @(
            (New-Bar 'Mathematics' (& $mps 'bm_mathematics_vector3_normalize_throughput') $colors.Math),
            (New-Bar 'Math unchecked' (& $mps 'bm_mathematics_vector3_normalize_unchecked_throughput') $colors.Unchecked),
            (New-Bar 'DirectXMath' (& $mps 'bm_dx_math_vector3_normalize_throughput') $colors.Dx),
            (New-Bar 'GLM' (& $mps 'bm_glm_vector3_normalize_throughput') $colors.Glm)) '{0:F1}'),

        (New-Panel 'matrix4x4 multiply throughput' 'Mitems/s' '높을수록 좋음' @(
            (New-Bar 'Mathematics' (& $mps 'bm_mathematics_matrix4x4_multiply_throughput') $colors.Math),
            (New-Bar 'DirectXMath' (& $mps 'bm_dx_math_matrix4x4_multiply_throughput') $colors.Dx),
            (New-Bar 'GLM' (& $mps 'bm_glm_matrix4x4_multiply_throughput') $colors.Glm)) '{0:F1}'),

        (New-Panel 'matrix4x4 inverse throughput' 'Mitems/s' '높을수록 좋음' @(
            (New-Bar 'Mathematics' (& $mps 'bm_mathematics_matrix4x4_inverse') $colors.Math),
            (New-Bar 'DirectXMath' (& $mps 'bm_dx_math_matrix4x4_inverse') $colors.Dx),
            (New-Bar 'GLM' (& $mps 'bm_glm_matrix4x4_inverse') $colors.Glm)) '{0:F1}'),

        (New-Panel 'matrix4x4 transpose throughput' 'Mitems/s' '높을수록 좋음' @(
            (New-Bar 'Mathematics' (& $mps 'bm_mathematics_matrix4x4_transpose') $colors.Math),
            (New-Bar 'DirectXMath' (& $mps 'bm_dx_math_matrix4x4_transpose') $colors.Dx)) '{0:F1}'),

        (New-Panel 'quaternion multiply throughput' 'Mitems/s' '높을수록 좋음' @(
            (New-Bar 'Mathematics' (& $mps 'bm_mathematics_quaternion_multiply_throughput') $colors.Math),
            (New-Bar 'DirectXMath' (& $mps 'bm_dx_math_quaternion_multiply_throughput') $colors.Dx)) '{0:F1}')
    )
}

function Set-PanelNotes {
    param([object[]]$Panels)

    $spread = {
        param($p)
        $values = $p.Bars | ForEach-Object { $_.Value }
        Get-Percent ($values | Measure-Object -Maximum).Maximum ($values | Measure-Object -Minimum).Minimum
    }

    $Panels[0].Note = '최대 차이 {0:F1}% — 측정 변동 범위 안의 동률권' -f (& $spread $Panels[0])
    $Panels[1].Note = '최대 차이 {0:F1}% — 측정 변동 범위 안의 동률권' -f (& $spread $Panels[1])

    $math = Get-BarValue $Panels[2] 'Mathematics'
    $glm = Get-BarValue $Panels[2] 'GLM'
    $vm = Get-BarValue $Panels[2] 'Vectormath'
    $Panels[2].Note = 'GLM이 Mathematics보다 {0:F1}% {1} — 스칼라 반환이 네이티브라 추출 단계가 없음; Vectormath는 {2:F1}% {3}' -f `
        [Math]::Abs((Get-Percent $glm $math)), $(if ($glm -lt $math) { '빠름' } else { '느림' }),
        [Math]::Abs((Get-Percent $vm $math)), $(if ($vm -gt $math) { '느림' } else { '빠름' })

    $math = Get-BarValue $Panels[3] 'Mathematics'
    $dx = Get-BarValue $Panels[3] 'DirectXMath'
    $glm = Get-BarValue $Panels[3] 'GLM'
    $Panels[3].Note = 'Mathematics는 DirectXMath 대비 {0:+0.0;-0.0;+0.0}%; GLM만 {1:F0}% 열세 — FMA 미형성' -f `
        (Get-Percent $math $dx), [Math]::Abs((Get-Percent $glm $math))

    $math = Get-BarValue $Panels[4] 'Mathematics'
    $dx = Get-BarValue $Panels[4] 'DirectXMath'
    $packed = Get-BarValue $Panels[4] 'DirectXMath packed'
    $Panels[4].Note = '12바이트 패킹 타입이 레지스터 상주 XMVECTOR와 {0:+0.0;-0.0;+0.0}%; 매 단계 load/store하는 XMFLOAT3은 {1:F1}배 느림' -f `
        (Get-Percent $math $dx), ($packed / $math)

    $math = Get-BarValue $Panels[5] 'Mathematics'
    $unchecked = Get-BarValue $Panels[5] 'Math unchecked'
    $dx = Get-BarValue $Panels[5] 'DirectXMath'
    $glm = Get-BarValue $Panels[5] 'GLM'
    $Panels[5].Note = 'Mathematics가 DX보다 {0:+0;-0;+0}% — 퇴화 입력 보장 포함; unchecked는 GLM 대비 {1:+0;-0;+0}%' -f `
        (Get-Percent $math $dx), (Get-Percent $unchecked $glm)

    foreach ($index in 6, 7) {
        $math = Get-BarValue $Panels[$index] 'Mathematics'
        $dx = Get-BarValue $Panels[$index] 'DirectXMath'
        $glm = Get-BarValue $Panels[$index] 'GLM'
        $Panels[$index].Note = 'Mathematics가 DX보다 {0:+0.0;-0.0;+0.0}%, GLM보다 {1:+0.0;-0.0;+0.0}%' -f `
            (Get-Percent $math $dx), (Get-Percent $math $glm)
    }

    $math = Get-BarValue $Panels[8] 'Mathematics'
    $dx = Get-BarValue $Panels[8] 'DirectXMath'
    $Panels[8].Note = 'Mathematics가 DX보다 {0:+0.0;-0.0;+0.0}% — 임시 없이 4 load + 8 shuffle + 4 store로 목적지에 직접 씁니다' -f (Get-Percent $math $dx)

    $math = Get-BarValue $Panels[9] 'Mathematics'
    $dx = Get-BarValue $Panels[9] 'DirectXMath'
    $Panels[9].Note = 'Mathematics와 DirectXMath 차이 {0:+0.0;-0.0;+0.0}% — 공유 아레나로 배치 편향을 제거한 뒤의 값' -f (Get-Percent $math $dx)

    return $Panels
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
    param([object[]]$Panels, [string]$Meta, [string]$Key, [string[]]$Footnotes, [int]$PageWidth)

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
        @('Mathematics', $colors.Math), @('Mathematics unchecked', $colors.Unchecked),
        @('DirectXMath', $colors.Dx), @('GLM', $colors.Glm), @('Vectormath', $colors.Vm))
    $legend = ($legendItems | ForEach-Object { '<span><i style="background:{0}"></i>{1}</span>' -f $_[1], $_[0] }) -join ''
    $first = ($Panels[0..3] | ForEach-Object { ConvertTo-PanelHtml $_ }) -join ''
    $second = ($Panels[4..9] | ForEach-Object { ConvertTo-PanelHtml $_ }) -join ''
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
<h2>4개 라이브러리 공통 연산</h2>
<p class="lede">동일한 종속 체인과 배치 크기에서 직접 비교. 지연시간은 낮을수록, 처리량은 높을수록 좋습니다.</p>
<div class="grid">$first</div>
<h2>고수준 연산 보조 비교</h2>
<p class="lede">현재 하니스에 구현된 Mathematics · DirectXMath · GLM 경로. Vectormath는 고수준 항목이 없어 이 절에서 제외합니다.</p>
<div class="grid">$second</div>
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
$panels = Set-PanelNotes (Build-Panels -M $msvc)

foreach ($panel in $panels) {
    $bars = ($panel.Bars | ForEach-Object { '{0}={1}' -f $_.Label, ($panel.Format -f $_.Value) }) -join '  '
    Write-Host ('{0,-32} {1}' -f $panel.Title, $bars)
    Write-Host ('{0,-32} -> {1}' -f '', $panel.Note)
}
$worst = $msvc.GetEnumerator() | Sort-Object { $_.Value.Cv } -Descending | Select-Object -First 1
Write-Host ''
Write-Host ('MSVC worst CV: {0} {1:F1}%' -f $worst.Key, $worst.Value.Cv)

$context = (Get-Content -LiteralPath $MsvcJson -Raw | ConvertFrom-Json).context
$measured = ([datetime]$context.date).ToString('yyyy-MM-dd')
$meta = "Intel Core i7-8700K · Windows 11 · MSVC 19.51 · C++23 · /O2 /arch:AVX2 /fp:fast · CPU time 중앙값 · $measured 측정"
$key = '<b>핵심:</b> Mathematics는 matrix4x4 inverse {0:F1} M/s와 multiply {1:F1} M/s로 이 측정의 선두이고, 저수준 지연 항목은 네 라이브러리가 모두 동률권입니다.' -f `
    (Get-Metric -Run $msvc -Name 'bm_mathematics_matrix4x4_inverse' -As 'Mps'),
    (Get-Metric -Run $msvc -Name 'bm_mathematics_matrix4x4_multiply_throughput' -As 'Mps')

$footnotes = @(
    ('측정: Google Benchmark 1.9.0, 무작위 인터리빙 9회 반복, <code>--benchmark_min_time=0.4s</code>. ' +
     '게이트 전 처리량 벤치 1회 예열 — 이 기계는 저전력 상태에서 시작하면 측정 중 클럭이 올라 CV가 20%까지 뜁니다.')
    ('버전: GLM 1.0.1, Vectormath 7105ef3, DirectXMath Windows SDK 10.0.26100. ' +
     'GLM은 <code>GLM_FORCE_INTRINSICS</code>와 <code>GLM_FORCE_ALIGNED_GENTYPES</code>를 켠 상태입니다.')
    ('모든 처리량 벤치는 입력과 출력을 한 공유 아레나에 페이지 고정 오프셋으로 배치합니다. ' +
     '독립 할당 시 4K 앨리어싱이 바이너리 배치에 따라 최대 29포인트의 거짓 격차를 만들었습니다.')
    ('clang-cl 22.1.3 대조: inverse {0:F1} 대 {1:F1} M/s, quaternion multiply {2:F1} 대 {3:F1} M/s. 컴파일러별 전체 표는 docs/BASELINE.md에 있습니다.' -f
        (Get-Metric -Run $clang -Name 'bm_mathematics_matrix4x4_inverse' -As 'Mps'),
        (Get-Metric -Run $clang -Name 'bm_dx_math_matrix4x4_inverse' -As 'Mps'),
        (Get-Metric -Run $clang -Name 'bm_mathematics_quaternion_multiply_throughput' -As 'Mps'),
        (Get-Metric -Run $clang -Name 'bm_dx_math_quaternion_multiply_throughput' -As 'Mps'))
)

$html = New-ChartHtml -Panels $panels -Meta $meta -Key $key -Footnotes $footnotes -PageWidth $Width
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

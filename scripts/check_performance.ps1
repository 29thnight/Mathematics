[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BenchmarkExe,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [ValidateRange(0.0, 100.0)]
    [double]$MaxRegressionPercent = 5.0,

    [ValidateRange(0.0, 100.0)]
    [double]$MaxCvPercent = 10.0,

    [ValidatePattern('^[0-9]+(?:\.[0-9]+)?[sm]$')]
    [string]$MinTime = '0.4s',

    [ValidateRange(3, 99)]
    [int]$Repetitions = 9
)

$ErrorActionPreference = 'Stop'

$benchmarkPath = (Resolve-Path -LiteralPath $BenchmarkExe).Path
$outputFullPath = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = Split-Path -Parent $outputFullPath
if ($outputDirectory -and -not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

# Tolerance is per comparison, because the five do not resolve to the same
# precision and pretending they do is what made this gate unactionable.
#
# The two latency comparisons are register-resident chains. They reproduce: the
# same cross latency figure, 6.452 ns, came back from separate runs on separate
# runners, at a CV around 1%. They keep -MaxRegressionPercent, and a change of a
# few percent there is a real change.
#
# The throughput comparisons write to memory, and their absolute numbers move
# with where the linker happened to put things. Measured on DirectXMath's own
# benchmarks, whose source did not change at all across these runs:
#
#   dx cross throughput      741.4 -> 793.5 M/s  (+7%)  same runner, our
#                                                        library relinked
#   dx matrix4x4 transpose   477.4 -> 631.4 M/s (+32%)  runner swapped, and the
#                                                        verdict on that row went
#                                                        from -46% to +0.2%
#   quaternion multiply      0.00% on one runner, +10.59% on another, from an
#                            instruction stream identical to DirectXMath's
#
# docs/BASELINE.md 8 records the same effect from the other side -- that family
# moves with .text alignment. The arena fixed the data placement; nothing here
# fixes the code placement, and a 5% bar on a measurement that swings 30 points
# reports the linker's mood. The numbers below are what each row was actually
# observed to swing, so a failure means a real regression rather than a rebuild.
# They are a tripwire for gross loss, not a precision instrument: read the
# uploaded JSON for anything finer.
$comparisons = @(
    [pscustomobject]@{
        Label = 'Cross latency'
        Candidate = 'bm_mathematics_cross_latency'
        Baseline = 'bm_dx_math_cross_latency_packed'
        Metric = 'latency'
        Tolerance = $MaxRegressionPercent
    },
    [pscustomobject]@{
        Label = 'Cross throughput'
        Candidate = 'bm_mathematics_cross_throughput'
        Baseline = 'bm_dx_math_cross_throughput'
        Metric = 'throughput'
        Tolerance = 35.0
    },
    [pscustomobject]@{
        Label = 'Matrix4x4 transpose'
        Candidate = 'bm_mathematics_matrix4x4_transpose'
        Baseline = 'bm_dx_math_matrix4x4_transpose'
        Metric = 'throughput'
        Tolerance = 35.0
    },
    [pscustomobject]@{
        Label = 'Quaternion multiply latency'
        Candidate = 'bm_mathematics_quaternion_multiply_latency'
        Baseline = 'bm_dx_math_quaternion_multiply_latency_packed'
        Metric = 'latency'
        Tolerance = $MaxRegressionPercent
    },
    [pscustomobject]@{
        Label = 'Quaternion multiply throughput'
        Candidate = 'bm_mathematics_quaternion_multiply_throughput'
        Baseline = 'bm_dx_math_quaternion_multiply_throughput'
        Metric = 'throughput'
        Tolerance = 20.0
    }
)

$benchmarkNames = @($comparisons | ForEach-Object { $_.Candidate; $_.Baseline })
$filter = '^(' + (($benchmarkNames | Sort-Object -Unique) -join '|') + ')$'
$arguments = @(
    "--benchmark_filter=$filter"
    "--benchmark_min_time=$MinTime"
    "--benchmark_repetitions=$Repetitions"
    '--benchmark_enable_random_interleaving=true'
    '--benchmark_report_aggregates_only=true'
    "--benchmark_out=$outputFullPath"
    '--benchmark_out_format=json'
)

Write-Host "Running performance gate: $benchmarkPath"
& $benchmarkPath @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Benchmark process failed with exit code $LASTEXITCODE."
}

$report = Get-Content -LiteralPath $outputFullPath -Raw | ConvertFrom-Json

function Get-MedianResult {
    param([Parameter(Mandatory = $true)][string]$RunName)

    $matches = @($report.benchmarks | Where-Object {
        $_.aggregate_name -eq 'median' -and
        ($_.run_name -eq $RunName -or $_.name -eq "${RunName}_median")
    })
    if ($matches.Count -ne 1) {
        throw "Expected exactly one median result for '$RunName', found $($matches.Count)."
    }
    if ($matches[0].error_occurred) {
        throw "Benchmark '$RunName' reported an error: $($matches[0].error_message)"
    }
    return $matches[0]
}

function Get-CvResult {
    param([Parameter(Mandatory = $true)][string]$RunName)

    $matches = @($report.benchmarks | Where-Object {
        $_.aggregate_name -eq 'cv' -and
        ($_.run_name -eq $RunName -or $_.name -eq "${RunName}_cv")
    })
    if ($matches.Count -ne 1) {
        throw "Expected exactly one CV result for '$RunName', found $($matches.Count)."
    }
    return $matches[0]
}

function Convert-TimeToNanoseconds {
    param(
        [Parameter(Mandatory = $true)][double]$Value,
        [Parameter(Mandatory = $true)][string]$Unit
    )

    switch ($Unit) {
        'ns' { return $Value }
        'us' { return $Value * 1.0e3 }
        'ms' { return $Value * 1.0e6 }
        's'  { return $Value * 1.0e9 }
        default { throw "Unsupported benchmark time unit '$Unit'." }
    }
}

function Assert-FiniteMetric {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][double]$Value
    )

    if (-not [double]::IsFinite($Value)) {
        throw "Benchmark metric '$Label' is not finite."
    }
}

$failed = $false
foreach ($comparison in $comparisons) {
    $candidate = Get-MedianResult -RunName $comparison.Candidate
    $baseline = Get-MedianResult -RunName $comparison.Baseline
    $candidateCvResult = Get-CvResult -RunName $comparison.Candidate
    $baselineCvResult = Get-CvResult -RunName $comparison.Baseline

    if ($comparison.Metric -eq 'latency') {
        $candidateCv = 100.0 * [double]$candidateCvResult.cpu_time
        $baselineCv = 100.0 * [double]$baselineCvResult.cpu_time
        $candidateValue = Convert-TimeToNanoseconds -Value $candidate.cpu_time -Unit $candidate.time_unit
        $baselineValue = Convert-TimeToNanoseconds -Value $baseline.cpu_time -Unit $baseline.time_unit
        $regression = (($candidateValue / $baselineValue) - 1.0) * 100.0
        $display = '{0:N3} ns vs {1:N3} ns' -f $candidateValue, $baselineValue
    } else {
        $candidateCv = 100.0 * [double]$candidateCvResult.items_per_second
        $baselineCv = 100.0 * [double]$baselineCvResult.items_per_second
        $candidateValue = [double]$candidate.items_per_second
        $baselineValue = [double]$baseline.items_per_second
        $regression = (1.0 - ($candidateValue / $baselineValue)) * 100.0
        $display = '{0:N3} M/s vs {1:N3} M/s' -f ($candidateValue / 1.0e6), ($baselineValue / 1.0e6)
    }

    Assert-FiniteMetric -Label "$($comparison.Label) Mathematics value" -Value $candidateValue
    Assert-FiniteMetric -Label "$($comparison.Label) baseline value" -Value $baselineValue
    Assert-FiniteMetric -Label "$($comparison.Label) Mathematics CV" -Value $candidateCv
    Assert-FiniteMetric -Label "$($comparison.Label) baseline CV" -Value $baselineCv
    Assert-FiniteMetric -Label "$($comparison.Label) regression" -Value $regression
    if ($candidateValue -le 0.0 -or $baselineValue -le 0.0) {
        throw "Benchmark '$($comparison.Label)' returned a non-positive metric."
    }

    if ($candidateCv -gt $MaxCvPercent -or $baselineCv -gt $MaxCvPercent) {
        throw ('Unstable sample for {0}: CV {1:N2}% vs {2:N2}% exceeds {3:N2}%.' -f
            $comparison.Label, $candidateCv, $baselineCv, $MaxCvPercent)
    }

    $tolerance = [double]$comparison.Tolerance
    $status = if ($regression -gt $tolerance) { 'FAIL' } else { 'PASS' }
    Write-Host ('[{0}] {1}: {2}; regression {3:N2}% of {4:N0}% allowed; CV {5:N2}%/{6:N2}%' -f
        $status, $comparison.Label, $display, $regression, $tolerance, $candidateCv, $baselineCv)
    if ($status -eq 'FAIL') {
        $failed = $true
    }
}

if ($failed) {
    throw "Performance regression exceeded the per-comparison tolerance. JSON: $outputFullPath"
}

Write-Host "Performance gate passed. JSON: $outputFullPath"

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
    [int]$Repetitions = 9,

    # Gate runs the five comparisons CI blocks on. Full adds the rest of the
    # docs/PLAN.md 4.2 table, the one the release criterion is written against,
    # as report-only rows.
    [ValidateSet('Gate', 'Full')]
    [string]$Table = 'Gate',

    # Measure and report, never fail on a regression. For sampling runs, whose
    # job is to collect numbers across runners and layouts, not to judge them.
    # A benchmark that errors still throws -- a sample that failed to measure is
    # not a sample -- but an unstable row is labelled rather than fatal.
    [switch]$ReportOnly
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

# The rest of the docs/PLAN.md 4.2 table. Tolerance $null means the row is
# measured and reported but never judged: these are the items the release
# criterion names and the gate does not yet enforce, and the point of running
# them is to have their numbers from the same runs, on the same runners, as the
# five that are enforced.
if ($Table -eq 'Full') {
    $comparisons += @(
        [pscustomobject]@{ Label = 'Add latency'; Metric = 'latency'; Tolerance = $null
            Candidate = 'bm_mathematics_add_latency'; Baseline = 'bm_dx_math_add_latency' },
        [pscustomobject]@{ Label = 'Mul-add latency'; Metric = 'latency'; Tolerance = $null
            Candidate = 'bm_mathematics_mul_add_latency'; Baseline = 'bm_dx_math_mul_add_latency' },
        [pscustomobject]@{ Label = 'Mul-add throughput'; Metric = 'throughput'; Tolerance = $null
            Candidate = 'bm_mathematics_mul_add_throughput'; Baseline = 'bm_dx_math_mul_add_throughput' },
        [pscustomobject]@{ Label = 'Dot3 latency'; Metric = 'latency'; Tolerance = $null
            Candidate = 'bm_mathematics_dot3_latency'; Baseline = 'bm_dx_math_dot3_latency' },
        [pscustomobject]@{ Label = 'Dot4 latency'; Metric = 'latency'; Tolerance = $null
            Candidate = 'bm_mathematics_dot4_latency'; Baseline = 'bm_dx_math_dot4_latency' },
        [pscustomobject]@{ Label = 'Vector3 normalize throughput'; Metric = 'throughput'; Tolerance = $null
            Candidate = 'bm_mathematics_vector3_normalize_throughput'
            Baseline = 'bm_dx_math_vector3_normalize_throughput' },
        [pscustomobject]@{ Label = 'Matrix4x4 multiply latency'; Metric = 'latency'; Tolerance = $null
            Candidate = 'bm_mathematics_matrix4x4_multiply_latency'
            Baseline = 'bm_dx_math_matrix4x4_multiply_latency' },
        [pscustomobject]@{ Label = 'Matrix4x4 multiply throughput'; Metric = 'throughput'; Tolerance = $null
            Candidate = 'bm_mathematics_matrix4x4_multiply_throughput'
            Baseline = 'bm_dx_math_matrix4x4_multiply_throughput' },
        [pscustomobject]@{ Label = 'Matrix4x4 inverse'; Metric = 'throughput'; Tolerance = $null
            Candidate = 'bm_mathematics_matrix4x4_inverse'; Baseline = 'bm_dx_math_matrix4x4_inverse' },
        [pscustomobject]@{ Label = 'Quaternion slerp'; Metric = 'throughput'; Tolerance = $null
            Candidate = 'bm_mathematics_quaternion_slerp'; Baseline = 'bm_dx_math_quaternion_slerp' },
        # Asymmetric, see docs/BASELINE.md 8: a compile-time count against a
        # library call that cannot specialize on it. The ratio flatters us.
        [pscustomobject]@{ Label = 'Batch transform (asymmetric)'; Metric = 'throughput'; Tolerance = $null
            Candidate = 'bm_mathematics_transform_point_stream'
            Baseline = 'bm_dx_math_transform_coord_stream' }
    )
}

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

    # Wall-clock time, not CPU time. On Windows a thread's CPU time advances in
    # 15.6 ms scheduler ticks, and with random interleaving each repetition runs
    # for a few tens of milliseconds, so cpu_time is quantized to a large
    # fraction of what it measures. Over 1,024 benchmark-samples from 32
    # sampler jobs its CV had a p99 of 34% and a maximum of 107% -- one
    # repetition recorded no CPU time at all and made items_per_second
    # infinite -- and 82 exceeded the 10% limit below. real_time over the same
    # repetitions: p99 6.4%, three over the limit. docs/BASELINE.md section 11
    # has the table. The values that kept recurring exactly across CI runs
    # (559.241 M/s and the like) were those quantization steps. These
    # benchmarks are single threaded and never sleep, so wall-clock time is
    # the quantity anyway.
    #
    # items_per_second is N / cpu_time for every repetition, a monotone map, so
    # with an odd repetition count N = median(ips) * median(cpu_time) exactly,
    # and the wall-clock throughput median is N / median(real_time).
    $candidateCv = 100.0 * [double]$candidateCvResult.real_time
    $baselineCv = 100.0 * [double]$baselineCvResult.real_time
    $candidateTime = Convert-TimeToNanoseconds -Value $candidate.real_time -Unit $candidate.time_unit
    $baselineTime = Convert-TimeToNanoseconds -Value $baseline.real_time -Unit $baseline.time_unit
    if ($comparison.Metric -eq 'latency') {
        $candidateValue = $candidateTime
        $baselineValue = $baselineTime
        $regression = (($candidateValue / $baselineValue) - 1.0) * 100.0
        $display = '{0:N3} ns vs {1:N3} ns' -f $candidateValue, $baselineValue
    } else {
        $candidateItems = [double]$candidate.items_per_second *
            (Convert-TimeToNanoseconds -Value $candidate.cpu_time -Unit $candidate.time_unit) * 1.0e-9
        $baselineItems = [double]$baseline.items_per_second *
            (Convert-TimeToNanoseconds -Value $baseline.cpu_time -Unit $baseline.time_unit) * 1.0e-9
        $candidateValue = $candidateItems / ($candidateTime * 1.0e-9)
        $baselineValue = $baselineItems / ($baselineTime * 1.0e-9)
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

    $gated = ($null -ne $comparison.Tolerance) -and -not $ReportOnly
    $unstable = $candidateCv -gt $MaxCvPercent -or $baselineCv -gt $MaxCvPercent
    if ($unstable -and $gated) {
        throw ('Unstable sample for {0}: CV {1:N2}% vs {2:N2}% exceeds {3:N2}%.' -f
            $comparison.Label, $candidateCv, $baselineCv, $MaxCvPercent)
    }

    # A report-only row is printed with the bar it would face if it were gated,
    # or none, and never changes the outcome. A noisy one is kept and labelled:
    # dropping it would lose the rest of an otherwise good sample.
    if ($unstable) {
        $status = 'NOISY'
    } elseif ($null -eq $comparison.Tolerance) {
        $status = 'INFO'
    } elseif ($regression -gt [double]$comparison.Tolerance) {
        $status = if ($gated) { 'FAIL' } else { 'OVER' }
    } else {
        $status = 'PASS'
    }
    $bar = if ($null -eq $comparison.Tolerance) { 'report only' } else {
        '{0:N0}% allowed' -f [double]$comparison.Tolerance }
    Write-Host ('[{0}] {1}: {2}; regression {3:N2}% ({4}); CV {5:N2}%/{6:N2}%' -f
        $status, $comparison.Label, $display, $regression, $bar, $candidateCv, $baselineCv)
    if ($status -eq 'FAIL') {
        $failed = $true
    }
}

if ($failed) {
    throw "Performance regression exceeded the per-comparison tolerance. JSON: $outputFullPath"
}

$verb = if ($ReportOnly) { 'Performance sample recorded' } else { 'Performance gate passed' }
Write-Host "$verb. JSON: $outputFullPath"

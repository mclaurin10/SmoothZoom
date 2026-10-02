# =============================================================================
# SmoothZoom dev tools: evaluate bench results (aggregate.json from run-bench.ps1)
# against bench\criteria.json. Dot-source:  . "$PSScriptRoot\_evaluate.ps1"
# =============================================================================

function Get-SzMetric($obj, [string]$path) {
    $cur = $obj
    foreach ($part in $path.Split('.')) {
        if ($null -eq $cur) { return $null }
        $prop = $cur.PSObject.Properties[$part]
        if ($null -eq $prop) { return $null }
        $cur = $prop.Value
    }
    return $cur
}

function Get-SzMedian([double[]]$v) {
    if ($v.Count -eq 0) { return $null }
    $s = $v | Sort-Object
    $m = [int][math]::Floor($s.Count / 2)
    if ($s.Count % 2) { return $s[$m] } else { return ($s[$m - 1] + $s[$m]) / 2 }
}

# Returns one row per (trace, criterion): Trace, Id, Metric, Op, Threshold,
# Value (aggregated over runs), Runs (per-run values), Pass ($true/$false/$null=skipped).
function Invoke-SzEvaluate([string]$AggregatePath, [string]$CriteriaPath) {
    $agg  = Get-Content $AggregatePath -Raw | ConvertFrom-Json
    $crit = Get-Content $CriteriaPath -Raw | ConvertFrom-Json
    $rows = @()
    foreach ($tp in $agg.traces.PSObject.Properties) {
        $runs = @($tp.Value.runs)
        foreach ($c in $crit.criteria) {
            $vals = @($runs | ForEach-Object { Get-SzMetric $_ $c.metric })
            $nonNull = @($vals | Where-Object { $null -ne $_ })
            $row = [ordered]@{ Trace = $tp.Name; Id = $c.id; Metric = $c.metric; Op = $c.op; Threshold = $c.value;
                               Value = $null; Runs = ($vals | ForEach-Object { if ($null -eq $_) { "null" } elseif ($_ -is [double]) { "{0:G4}" -f $_ } else { "$_" } }) -join " / "; Pass = $null }
            if ($runs.Count -eq 0 -or $nonNull.Count -eq 0) {
                if (-not $c.skip_if_null) { $row.Pass = $false; $row.Value = "missing" }
                $rows += [pscustomobject]$row; continue
            }
            $test = {
                param($v)
                switch ($c.op) {
                    "==" { $v -eq $c.value }
                    "<=" { [double]$v -le [double]$c.value }
                    ">=" { [double]$v -ge [double]$c.value }
                    default { throw "unknown op $($c.op)" }
                }
            }
            switch ($c.agg) {
                "all"    { $row.Value = if (@($nonNull | Where-Object { -not (& $test $_) }).Count) { "not all" } else { $c.value }
                           $row.Pass = (@($nonNull | Where-Object { -not (& $test $_) }).Count -eq 0) -and ($nonNull.Count -eq $runs.Count) }
                "max"    { $row.Value = ($nonNull | ForEach-Object { [double]$_ } | Measure-Object -Maximum).Maximum; $row.Pass = [bool](& $test $row.Value) }
                "median" { $row.Value = Get-SzMedian ($nonNull | ForEach-Object { [double]$_ }); $row.Pass = [bool](& $test $row.Value) }
                default  { throw "unknown agg $($c.agg)" }
            }
            $rows += [pscustomobject]$row
        }
    }
    return $rows
}

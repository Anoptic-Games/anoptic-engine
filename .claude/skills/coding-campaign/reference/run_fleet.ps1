#!/usr/bin/env pwsh
#Requires -Version 7
# run_fleet.ps1 — one bounded squad contest. Every IU generates and clears fitness under
# hard deadlines. Results persist per candidate; the first verified Sol pass starts one
# fixed group clock; only fully green candidates can become champion.
$ErrorActionPreference = 'Stop'

function Show-Usage { @'
usage:  $env:PROMPT_FILE = 'prompts\parser.txt'
        $env:FITNESS_CMD = '& C:\fitness\fit.ps1 $args[0]'
        $env:WORK = 'C:\contest\squad-1'; $env:NAME = 'parser'
        pwsh run_fleet.ps1

required (env)
  PROMPT_FILE       generation prompt; include exact contracts and required tests
  FITNESS_CMD       PowerShell snippet run once per candidate; path arrives as $args[0];
                    must print 'FITNESS pass=X/Y metric=N' or a failure status
  WORK              output root; candidates and incremental results live under WORK\NAME

optional (env)                                                   [default]
  NAME / EXT        round label / candidate extension            [round / rs]
  ROUND_TIMEOUT     group deadline when no Sol passes, seconds   [1200]
  FITNESS_TIMEOUT   independent fitness deadline, seconds        [180]
  CLAUDEX           backend CLI                                  [PATH or ~/.local/bin/claudex]

deprecated (ignored)
  SOL LUNA FLEET ALLOW_SMALLER_FLEET SOL_MODEL SOL_EFFORT LUNA_MODEL LUNA_EFFORT

The fleet is always 4 gpt-5.6-sol/high + 6 gpt-5.6-luna/xhigh.

All fleet units launch together. Each IU owns green tests on time. Only the first verified
Sol pass arms the fixed clock: warn all IUs and the SL at four minutes, cull unfinished IUs
at five minutes. Luna results never arm or alter the clock.
'@ }

if ($args[0] -in '-h', '-help', '--help') { Show-Usage; exit 0 }

function Req([string]$Name) {
  $value = [Environment]::GetEnvironmentVariable($Name)
  if ([string]::IsNullOrWhiteSpace($value)) { throw "run_fleet.ps1: `$env:$Name is not set (run_fleet.ps1 -h for usage)" }
  $value
}
function Opt([string]$Name, [string]$Default) {
  $value = [Environment]::GetEnvironmentVariable($Name)
  if ([string]::IsNullOrWhiteSpace($value)) { $Default } else { $value }
}
function Positive-Int([string]$Name, [string]$Value) {
  $parsed = 0
  if (-not [int]::TryParse($Value, [ref]$parsed) -or $parsed -le 0) { throw "run_fleet.ps1: $Name must be a positive integer" }
  $parsed
}
function Write-Result([string]$RoundDir, [string]$Idx, [string]$Fitness, [string]$Metric, [int]$Lines) {
  $path = Join-Path $RoundDir "result_$Idx.tsv"
  $tmp = "$path.tmp.$PID"
  "{0}`t{1}`tmetric={2}`t{3}L" -f $Idx, $Fitness, $Metric, $Lines | Set-Content -LiteralPath $tmp
  Move-Item -LiteralPath $tmp -Destination $path -Force
}

$Work           = Req 'WORK'
$PromptFile     = Req 'PROMPT_FILE'
$FitnessCmd     = Req 'FITNESS_CMD'
$RoundName      = Opt 'NAME' 'round'
$Ext            = Opt 'EXT' 'rs'
$Sol            = 4
$Luna           = 6
$RoundTimeout   = Positive-Int 'ROUND_TIMEOUT' (Opt 'ROUND_TIMEOUT' '1200')
$FitnessTimeout = Positive-Int 'FITNESS_TIMEOUT' (Opt 'FITNESS_TIMEOUT' '180')
$SolPassGrace = 300
$SolPassWarning = 240
if ((Opt 'CAMPAIGN_TEST_MODE' '0') -eq '1') {
  $SolPassGrace = Positive-Int 'TEST_SOL_PASS_GRACE' (Opt 'TEST_SOL_PASS_GRACE' '300')
  $SolPassWarning = Positive-Int 'TEST_SOL_PASS_WARNING' (Opt 'TEST_SOL_PASS_WARNING' '240')
  if ($SolPassWarning -ge $SolPassGrace) { throw 'run_fleet.ps1: TEST_SOL_PASS_WARNING must be earlier than TEST_SOL_PASS_GRACE' }
} elseif (-not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('TEST_SOL_PASS_GRACE')) -or
          -not [string]::IsNullOrWhiteSpace([Environment]::GetEnvironmentVariable('TEST_SOL_PASS_WARNING'))) {
  throw 'run_fleet.ps1: the five-minute Sol-pass clock is fixed; timing overrides are test-only'
}

$OnPath = Get-Command claudex -ErrorAction Ignore
$Claudex = Opt 'CLAUDEX' ($(if ($OnPath) { $OnPath.Source } else { Join-Path $HOME '.local\bin\claudex' }))
if (-not (Get-Command $Claudex -ErrorAction Ignore)) { throw "run_fleet.ps1: backend '$Claudex' not found — install claudex or set `$env:CLAUDEX" }
if (-not (Test-Path -LiteralPath $PromptFile)) { throw "run_fleet.ps1: cannot read PROMPT_FILE '$PromptFile'" }

$Fleet = @(
  1..4 | ForEach-Object { [pscustomobject]@{ Idx = '{0:d2}' -f $_; Model = 'gpt-5.6-sol'; Effort = 'high'; Role = 'Sol' } }
  1..6 | ForEach-Object { [pscustomobject]@{ Idx = '{0:d2}' -f (4 + $_); Model = 'gpt-5.6-luna'; Effort = 'xhigh'; Role = 'Luna' } }
)

$RoundDir = [System.IO.Path]::GetFullPath((Join-Path $Work $RoundName))
New-Item -ItemType Directory -Force -Path $RoundDir | Out-Null
Get-ChildItem -LiteralPath $RoundDir -File -ErrorAction Ignore | Where-Object {
  $_.Name -match '^(result_.*\.tsv|done_.*|pid_.*|cand_.*|response_.*\.txt|stderr_.*\.log|fitness_.*\.log|warning_.*\.txt|first_sol_pass|group_events\.tsv|results\.tsv)$'
} | Remove-Item -Force
$firstSolPassLock = Join-Path $RoundDir 'first_sol_pass.lock'
if (Test-Path -LiteralPath $firstSolPassLock) { Remove-Item -LiteralPath $firstSolPassLock -Recurse -Force }

$Prompt = (Get-Content -Raw -LiteralPath $PromptFile) + @'

CAMPAIGN IU LAW: You own this artifact through green completion. Run the required tests in your isolated environment; reasoning that they ought to pass is not testing. Repair your own work and reach the required passing tally inside the deadline. Never return partial or non-green work for the SL to fix. Return only the requested artifact.
IDENTICAL PROMPT LAW: Every IU in this workgroup receives these exact prompt bytes. Do not expect role-, index-, or model-specific instructions. Read your isolated output path from CAMPAIGN_CANDIDATE_PATH and the group warning path from CAMPAIGN_WARNING_PATH. Check CAMPAIGN_WARNING_PATH before every test/repair iteration; if the warning file appears, finish and return within the stated final minute.
INDEPENDENT FITNESS TEMPLATE (the runner will repeat it; substitute your candidate path for $args[0]):
'@ + $FitnessCmd

$unitScript = {
  param($Idx, $Model, $Effort, $Role, $Claudex, $Prompt, $RoundDir, $Ext, $FitnessCmd, $FitnessTimeout)
  $ErrorActionPreference = 'Stop'

  function Result([string]$Fitness, [string]$Metric, [int]$Lines) {
    $path = Join-Path $RoundDir "result_$Idx.tsv"
    $tmp = "$path.tmp.$PID"
    "{0}`t{1}`tmetric={2}`t{3}L" -f $Idx, $Fitness, $Metric, $Lines | Set-Content -LiteralPath $tmp
    Move-Item -LiteralPath $tmp -Destination $path -Force
    if ($Role -eq 'Sol' -and $Fitness -match '^pass=(\d+)/(\d+)$' -and [int]$Matches[1] -eq [int]$Matches[2] -and [int]$Matches[2] -gt 0) {
      try {
        New-Item -ItemType Directory -Path (Join-Path $RoundDir 'first_sol_pass.lock') -ErrorAction Stop | Out-Null
        "{0}`t{1}" -f [DateTimeOffset]::UtcNow.ToUnixTimeSeconds(), $Idx | Set-Content -LiteralPath (Join-Path $RoundDir 'first_sol_pass')
      } catch {}
    }
  }

  $out = Join-Path $RoundDir "cand_$Idx.$Ext"
  $raw = Join-Path $RoundDir "response_$Idx.txt"
  $log = Join-Path $RoundDir "stderr_$Idx.log"
  $warningPath = Join-Path $RoundDir "warning_$Idx.txt"
  $pidFile = Join-Path $RoundDir "pid_$Idx"
  $psi = [System.Diagnostics.ProcessStartInfo]::new()
  $psi.FileName = $Claudex
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.Environment['CAMPAIGN_CANDIDATE_PATH'] = $out
  $psi.Environment['CAMPAIGN_WARNING_PATH'] = $warningPath
  foreach ($argument in @('-p', '--model', $Model, '--effort', $Effort, $Prompt)) { [void]$psi.ArgumentList.Add($argument) }
  $process = [System.Diagnostics.Process]::new()
  $process.StartInfo = $psi
  [void]$process.Start()
  $process.Id | Set-Content -LiteralPath $pidFile
  $stdout = $process.StandardOutput.ReadToEndAsync()
  $stderr = $process.StandardError.ReadToEndAsync()
  $process.WaitForExit()

  $rawBody = $stdout.GetAwaiter().GetResult()
  $rawBody | Set-Content -LiteralPath $raw
  $stderr.GetAwaiter().GetResult() | Set-Content -LiteralPath $log
  $body = if ([string]::IsNullOrEmpty($rawBody)) { @() } else {
    $rawBody.TrimEnd("`r", "`n") -split "\r?\n" | Where-Object {
      $_ -notmatch 'connectors are disabled' -and $_ -notmatch '^```'
    }
  }
  if (-not (Test-Path -LiteralPath $out) -or (Get-Item -LiteralPath $out).Length -eq 0) {
    if (@($body).Count) { $body | Set-Content -LiteralPath $out } else { New-Item -ItemType File -Force -Path $out | Out-Null }
  }
  $lines = if ((Get-Item -LiteralPath $out).Length) { @(Get-Content -LiteralPath $out).Count } else { 0 }
  if ($process.ExitCode -ne 0) {
    Result 'CRASH' 'NA' $lines
    New-Item -ItemType File -Force -Path (Join-Path $RoundDir "done_$Idx") | Out-Null
    return
  }
  if ((Get-Item -LiteralPath $out).Length -eq 0) {
    Result 'NO_OUTPUT' 'NA' 0
    New-Item -ItemType File -Force -Path (Join-Path $RoundDir "done_$Idx") | Out-Null
    return
  }

  $fitnessScript = @'
$ErrorActionPreference = 'Stop'
$global:LASTEXITCODE = 0
& ([scriptblock]::Create($env:CAMPAIGN_FITNESS_CMD)) $env:CAMPAIGN_CANDIDATE
if (-not $?) { exit 1 }
if ($LASTEXITCODE -is [int]) { exit $LASTEXITCODE }
'@
  $fitPsi = [System.Diagnostics.ProcessStartInfo]::new()
  $fitPsi.FileName = (Get-Process -Id $PID).Path
  $fitPsi.UseShellExecute = $false
  $fitPsi.RedirectStandardOutput = $true
  $fitPsi.RedirectStandardError = $true
  [void]$fitPsi.ArgumentList.Add('-NoProfile')
  [void]$fitPsi.ArgumentList.Add('-EncodedCommand')
  [void]$fitPsi.ArgumentList.Add([Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($fitnessScript)))
  $fitPsi.Environment['CAMPAIGN_FITNESS_CMD'] = $FitnessCmd
  $fitPsi.Environment['CAMPAIGN_CANDIDATE'] = $out
  $fitProcess = [System.Diagnostics.Process]::new()
  $fitProcess.StartInfo = $fitPsi
  [void]$fitProcess.Start()
  $fitStdout = $fitProcess.StandardOutput.ReadToEndAsync()
  $fitStderr = $fitProcess.StandardError.ReadToEndAsync()
  if (-not $fitProcess.WaitForExit($FitnessTimeout * 1000)) {
    try { $fitProcess.Kill($true) } catch {}
    $fitProcess.WaitForExit()
    ($fitStdout.GetAwaiter().GetResult() + $fitStderr.GetAwaiter().GetResult()) | Set-Content -LiteralPath (Join-Path $RoundDir "fitness_$Idx.log")
    Result 'TIMEOUT' 'NA' $lines
    New-Item -ItemType File -Force -Path (Join-Path $RoundDir "done_$Idx") | Out-Null
    return
  }
  $fitOut = $fitStdout.GetAwaiter().GetResult()
  ($fitOut + $fitStderr.GetAwaiter().GetResult()) | Set-Content -LiteralPath (Join-Path $RoundDir "fitness_$Idx.log")

  $fitExit = $fitProcess.ExitCode
  $pass = $null
  if ($fitOut -match 'pass=(\d+)/(\d+)') { $pass = $Matches[0] }
  $metric = 'NA'
  if ($fitOut -match '(?:metric|reduction)=(\d+)') { $metric = $Matches[1] }
  if ($fitExit -ne 0) {
    Result 'CRASH' $metric $lines
  } elseif ($pass) {
    Result $pass $metric $lines
  } else {
    $status = ((($fitOut -split "\r?\n" | Where-Object { $_.Trim() } | Select-Object -First 1) ?? 'NO_OUTPUT') -split '\s+')[0]
    Result $status $metric $lines
  }
  New-Item -ItemType File -Force -Path (Join-Path $RoundDir "done_$Idx") | Out-Null
}

Write-Host "[$Work $RoundName] commissioning $($Fleet.Count) candidates (Sol=$Sol Luna=$Luna, round=${RoundTimeout}s fitness=${FitnessTimeout}s Sol-pass clock=${SolPassGrace}s) ..."
$jobs = @($Fleet | ForEach-Object {
  Start-Job -Name "fleet-$($_.Idx)" -ScriptBlock $unitScript -ArgumentList @(
    $_.Idx, $_.Model, $_.Effort, $_.Role, $Claudex, $Prompt, $RoundDir, $Ext, $FitnessCmd, $FitnessTimeout
  )
})

$deadline = $null
$warningAt = $null
$warningIssued = $false
$roundStarted = [DateTime]::UtcNow
while (@($jobs | Where-Object State -in @('NotStarted', 'Running')).Count) {
  if (-not $deadline -and (Test-Path -LiteralPath (Join-Path $RoundDir 'first_sol_pass'))) {
    $trigger = (Get-Content -LiteralPath (Join-Path $RoundDir 'first_sol_pass') -Raw).Trim() -split "`t"
    $epoch = [long]$trigger[0]
    $warningAt = [DateTimeOffset]::FromUnixTimeSeconds($epoch).UtcDateTime.AddSeconds($SolPassWarning)
    $deadline = [DateTimeOffset]::FromUnixTimeSeconds($epoch).UtcDateTime.AddSeconds($SolPassGrace)
    Write-Host "[$Work $RoundName] Sol $($trigger[1]) independently passed; group warning at $($warningAt.ToString('O')), cull at $($deadline.ToString('O'))"
  }
  $running = @($jobs | Where-Object State -in @('NotStarted', 'Running'))
  if ($deadline -and -not $warningIssued -and [DateTime]::UtcNow -ge $warningAt) {
    $message = "ONE-MINUTE WARNING: a Sol passed four minutes ago; every unfinished IU must complete within one minute or be culled."
    Write-Host "[$Work $RoundName] $message"
    "{0}`tSOL_PASS_ONE_MINUTE_WARNING`t{1}" -f [DateTimeOffset]::UtcNow.ToUnixTimeSeconds(), $message | Add-Content -LiteralPath (Join-Path $RoundDir 'group_events.tsv')
    foreach ($unit in $Fleet) {
      $warningPath = Join-Path $RoundDir "warning_$($unit.Idx).txt"
      $tmp = "$warningPath.tmp.$PID"
      $message | Set-Content -LiteralPath $tmp
      Move-Item -LiteralPath $tmp -Destination $warningPath -Force
    }
    $warningIssued = $true
  }
  $roundExpired = (-not $deadline -and ([DateTime]::UtcNow - $roundStarted).TotalSeconds -ge $RoundTimeout)
  if ($roundExpired -and $running) {
    Write-Host "[$Work $RoundName] no Sol passed before the group round deadline; culling $($running.Count) unfinished IUs"
  }
  if ((($deadline -and [DateTime]::UtcNow -ge $deadline) -or $roundExpired) -and $running) {
    if (-not $roundExpired) { Write-Host "[$Work $RoundName] five-minute Sol-pass deadline reached; culling $($running.Count) unfinished IUs" }
    foreach ($job in $running) {
      $idx = $job.Name -replace '^fleet-', ''
      $pidFile = Join-Path $RoundDir "pid_$idx"
      if (Test-Path -LiteralPath $pidFile) {
        try { [System.Diagnostics.Process]::GetProcessById([int](Get-Content -LiteralPath $pidFile -Raw)).Kill($true) } catch {}
      }
      Stop-Job -Job $job -ErrorAction Ignore
      if (-not (Test-Path -LiteralPath (Join-Path $RoundDir "result_$idx.tsv"))) { Write-Result $RoundDir $idx 'CULLED' 'NA' 0 }
    }
    break
  }
  if ($running) { Wait-Job -Job $running -Any -Timeout 1 | Out-Null }
}

$jobs | Wait-Job -Timeout 5 | Out-Null
$jobs | Receive-Job -ErrorAction Ignore | Out-Null
$jobs | Remove-Job -Force -ErrorAction Ignore

$results = foreach ($unit in $Fleet) {
  $resultPath = Join-Path $RoundDir "result_$($unit.Idx).tsv"
  if (-not (Test-Path -LiteralPath $resultPath)) { Write-Result $RoundDir $unit.Idx 'NO_RESULT' 'NA' 0 }
  $fields = (Get-Content -LiteralPath $resultPath -Raw).Trim() -split "`t"
  $passed = 0; $total = 0
  if ($fields[1] -match '^pass=(\d+)/(\d+)$') { $passed = [int]$Matches[1]; $total = [int]$Matches[2] }
  $metricN = 0
  if ($fields[2] -match '^metric=(\d+)$') { $metricN = [int]$Matches[1] }
  [pscustomobject]@{
    Idx = $fields[0]; Fitness = $fields[1]; Metric = $fields[2]
    Lines = [int]($fields[3] -replace 'L$', ''); PassN = $passed; Total = $total; MetricN = $metricN
    FullPass = ($total -gt 0 -and $passed -eq $total)
  }
}

$results | ForEach-Object { "{0}`t{1}`t{2}`t{3}L" -f $_.Idx, $_.Fitness, $_.Metric, $_.Lines } |
  Set-Content -LiteralPath (Join-Path $RoundDir 'results.tsv')

$best = $results | Where-Object FullPass | Sort-Object @{Expression='PassN';Descending=$true}, @{Expression='MetricN';Descending=$true}, @{Expression='Lines'} | Select-Object -First 1

Write-Host "[$Work $RoundName] ranked results:"
$results | Sort-Object Idx | ForEach-Object { Write-Host ('{0,-4} {1,-14} {2,-12} {3}L' -f $_.Idx, $_.Fitness, $_.Metric, $_.Lines) }
if (-not $best) { throw "[$Work $RoundName] no fully passing candidate; no champion" }

$champ = Join-Path $Work "champion_$RoundName.$Ext"
Copy-Item -LiteralPath (Join-Path $RoundDir "cand_$($best.Idx).$Ext") -Destination $champ -Force
Write-Host "PROVISIONAL_WINNER=$($best.Idx)  (champion at $champ)"

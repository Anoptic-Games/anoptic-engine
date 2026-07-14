#Requires -Version 7
$ErrorActionPreference = 'Stop'

$here = $PSScriptRoot
$runner = Join-Path (Split-Path $here -Parent) 'run_fleet.ps1'
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("fleet-test-" + [guid]::NewGuid())
try {
  if (-not ((& $runner -help | Out-String) -match 'deprecated \(ignored\)')) { throw 'deprecated fleet arguments missing from help' }

  $env:PROMPT_FILE = Join-Path $here 'prompt.txt'
  $env:FITNESS_CMD = "if ((Get-Content -Raw -LiteralPath `$args[0]).Trim() -eq 'PASS') { 'FITNESS pass=1/1 metric=1' } else { 'FITNESS pass=0/1 metric=0' }"
  $env:WORK = $work
  $env:NAME = 'self'
  $env:EXT = 'txt'
  $env:CLAUDEX = Join-Path $here 'fake_claudex.cmd'
  $env:ROUND_TIMEOUT = '20'
  $env:FITNESS_TIMEOUT = '5'
  $env:FAKE_SCENARIO = 'deadline'
  $env:CAMPAIGN_TEST_MODE = '1'
  $env:TEST_SOL_PASS_GRACE = '3'
  $env:TEST_SOL_PASS_WARNING = '2'

  $env:SOL = '99'
  $env:LUNA = '1'
  $env:FLEET = 'garbage'
  $env:ALLOW_SMALLER_FLEET = 'nonsense'
  $env:SOL_MODEL = 'ignored'
  $env:SOL_EFFORT = 'ignored'
  $env:LUNA_MODEL = 'ignored'
  $env:LUNA_EFFORT = 'ignored'
  & $runner

  $results = Get-Content -LiteralPath (Join-Path $work 'self\results.tsv')
  if ($results.Count -ne 10) { throw 'runner did not hard-fire ten units' }
  if (-not ($results -match '^01\tpass=1/1\t')) { throw 'passing Sol result missing' }
  if (-not ($results -match '^02\tpass=0/1\t')) { throw 'failing Sol result missing' }
  foreach ($idx in '03', '04', '05', '06', '07', '08', '09', '10') {
    if (-not ($results -match "^$idx\tCULLED\t")) { throw "culled result $idx missing" }
  }
  if ((Get-Content -Raw -LiteralPath (Join-Path $work 'champion_self.txt')).Trim() -ne 'PASS') { throw 'wrong champion' }
  if (-not (Test-Path -LiteralPath (Join-Path $work 'self\first_sol_pass'))) { throw 'passing Sol did not arm the deadline' }
  foreach ($idx in '01', '02', '03', '04', '05', '06', '07', '08', '09', '10') {
    if (-not (Test-Path -LiteralPath (Join-Path $work "self\warning_$idx.txt"))) { throw "IU $idx did not receive the group warning" }
  }
  if (-not ((Get-Content -LiteralPath (Join-Path $work 'self\group_events.tsv')) -match 'SOL_PASS_ONE_MINUTE_WARNING')) { throw 'SL-visible warning event missing' }

  $env:WORK = Join-Path $work 'luna-trigger'
  $env:FAKE_SCENARIO = 'luna-trigger'
  & $runner
  $results = Get-Content -LiteralPath (Join-Path $env:WORK 'self\results.tsv')
  if ($results.Count -ne 10) { throw 'Luna scenario did not use ten units' }
  if (-not ($results -match '^05\tpass=1/1\t')) { throw 'passing Luna result missing' }
  if (Test-Path -LiteralPath (Join-Path $env:WORK 'self\first_sol_pass')) { throw 'passing Luna armed the Sol-only deadline' }
  if (Test-Path -LiteralPath (Join-Path $env:WORK 'self\group_events.tsv')) { throw 'passing Luna emitted a deadline warning' }

  $env:WORK = Join-Path $work 'fitness'
  $env:FAKE_SCENARIO = 'all-pass'
  $env:FITNESS_CMD = "Start-Sleep -Seconds 8; 'FITNESS pass=1/1 metric=1'"
  $env:FITNESS_TIMEOUT = '1'
  $rejected = $false
  try { & $runner *> $null } catch { $rejected = $true }
  if (-not $rejected) { throw 'timed-out fitness command produced a champion' }
  $results = Get-Content -LiteralPath (Join-Path $env:WORK 'self\results.tsv')
  if (@($results -match '\tTIMEOUT\t').Count -ne 10) { throw 'fitness timeouts were not recorded for all ten units' }

  'run_fleet.ps1 self-test passed'
} finally {
  Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction Ignore
}

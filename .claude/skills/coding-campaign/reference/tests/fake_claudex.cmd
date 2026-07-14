@echo off
for %%F in ("%CAMPAIGN_CANDIDATE_PATH%") do set "idx=%%~nF"
set "idx=%idx:cand_=%"
if "%FAKE_SCENARIO%"=="" set "FAKE_SCENARIO=deadline"

if "%FAKE_SCENARIO%"=="deadline" (
  if "%idx%"=="01" (
    echo PASS
    exit /b 0
  )
  if "%idx%"=="02" (
    echo FAIL
    exit /b 0
  )
  ping -n 9 127.0.0.1 >nul
  echo PASS
  exit /b 0
)

if "%FAKE_SCENARIO%"=="luna-trigger" (
  if "%idx%"=="05" (
    echo PASS
    exit /b 0
  )
  echo FAIL
  exit /b 0
)

if "%FAKE_SCENARIO%"=="all-pass" (
  echo PASS
  exit /b 0
)
exit /b 3

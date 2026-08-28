@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

set "MAIN_REPO=E:\tools\Need\rathena"
set "WORK_REPO=E:\tools\Need\rathena-need-server"
set "BASE_BRANCH=need-server"
set "WORK_BRANCH=feature/update26b808"
set "REMOTE=origin"

echo.
echo ========================================================
echo  NEED update worktree sync
echo ========================================================
echo.

git -C "%MAIN_REPO%" rev-parse --is-inside-work-tree >nul 2>&1 || (
    echo [ERROR] Main worktree not found: %MAIN_REPO%
    pause
    exit /b 1
)

for /f "delims=" %%B in ('git -C "%MAIN_REPO%" branch --show-current') do set "MAIN_BRANCH=%%B"
if /I not "!MAIN_BRANCH!"=="%BASE_BRANCH%" (
    echo [ERROR] Main worktree branch is not %BASE_BRANCH%.
    echo         Current: !MAIN_BRANCH!
    echo         No changes were made.
    pause
    exit /b 1
)

for /f "delims=" %%S in ('git -C "%MAIN_REPO%" status --porcelain') do (
    echo [ERROR] Main worktree has uncommitted changes.
    git -C "%MAIN_REPO%" status --short
    pause
    exit /b 1
)

git -C "%WORK_REPO%" rev-parse --is-inside-work-tree >nul 2>&1 || (
    echo [ERROR] Update worktree not found: %WORK_REPO%
    pause
    exit /b 1
)

for /f "delims=" %%B in ('git -C "%WORK_REPO%" branch --show-current') do set "WORK_CURRENT=%%B"
if /I not "!WORK_CURRENT!"=="%WORK_BRANCH%" (
    echo [ERROR] Update worktree branch is not %WORK_BRANCH%.
    echo         Current: !WORK_CURRENT!
    echo         No changes were made.
    pause
    exit /b 1
)

for /f "delims=" %%S in ('git -C "%WORK_REPO%" status --porcelain') do (
    echo [ERROR] Update worktree has uncommitted changes.
    git -C "%WORK_REPO%" status --short
    pause
    exit /b 1
)

echo [1/4] Fetching origin...
git -C "%MAIN_REPO%" fetch %REMOTE%
if errorlevel 1 (
    echo [ERROR] git fetch failed.
    pause
    exit /b 1
)

echo [2/4] Updating local %BASE_BRANCH% with fast-forward only...
git -C "%MAIN_REPO%" pull --ff-only %REMOTE% %BASE_BRANCH%
if errorlevel 1 (
    echo [ERROR] Main branch could not be fast-forwarded.
    echo         Update worktree was not reset.
    pause
    exit /b 1
)

for /f "delims=" %%H in ('git -C "%MAIN_REPO%" rev-parse --short %BASE_BRANCH%') do set "BASE_HEAD=%%H"

echo [3/4] Resetting %WORK_BRANCH% to local %BASE_BRANCH% (!BASE_HEAD!)...
git -C "%WORK_REPO%" reset --hard %BASE_BRANCH%
if errorlevel 1 (
    echo [ERROR] Reset failed.
    pause
    exit /b 1
)

echo [4/4] Verification...
for /f "delims=" %%H in ('git -C "%WORK_REPO%" rev-parse --short HEAD') do set "WORK_HEAD=%%H"

echo.
echo Main   %BASE_BRANCH% : !BASE_HEAD!
echo Update %WORK_BRANCH% : !WORK_HEAD!
echo.

if /I not "!BASE_HEAD!"=="!WORK_HEAD!" (
    echo [ERROR] HEADs do not match. Remote feature was NOT touched.
    pause
    exit /b 1
)

echo [OK] Local update worktree now exactly matches %BASE_BRANCH%.
echo.

set /p "ANSWER=Also update origin/%WORK_BRANCH% to this state? [y/N]: "
if /I "!ANSWER!"=="Y" (
    git -C "%WORK_REPO%" push --force-with-lease %REMOTE% %WORK_BRANCH%
    if errorlevel 1 (
        echo [ERROR] Remote feature push failed.
        pause
        exit /b 1
    )
    echo [OK] Remote feature branch updated.
) else (
    echo [INFO] Remote feature branch was not changed.
)

echo.
git -C "%WORK_REPO%" status -sb
echo.
echo [DONE] Ready for the next update.
pause

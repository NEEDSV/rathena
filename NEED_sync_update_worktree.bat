@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

rem =========================================================
rem NEED rAthena update worktree sync
rem - Worktree path is kept as-is
rem - Branch name is kept as feature/update26b808
rem - Fetches origin and hard-resets the feature branch
rem   to origin's default branch (fallback: origin/need-server)
rem - Aborts if there are uncommitted changes
rem =========================================================

set "REPO=E:\tools\Need\rathena-need-server"
set "WORK_BRANCH=feature/update26b808"
set "REMOTE=origin"
set "FALLBACK_BASE=need-server"

echo.
echo [NEED] Update worktree sync
echo Repository: %REPO%
echo.

cd /d "%REPO%" || (
    echo [ERROR] Cannot open repository path.
    pause
    exit /b 1
)

git rev-parse --is-inside-work-tree >nul 2>&1 || (
    echo [ERROR] This folder is not a Git worktree.
    pause
    exit /b 1
)

for /f "delims=" %%B in ('git branch --show-current') do set "CURRENT_BRANCH=%%B"

if /I not "!CURRENT_BRANCH!"=="%WORK_BRANCH%" (
    echo [ERROR] Current branch is not %WORK_BRANCH%.
    echo Current branch: !CURRENT_BRANCH!
    echo No changes were made.
    pause
    exit /b 1
)

for /f "delims=" %%S in ('git status --porcelain') do (
    echo [ERROR] Uncommitted changes exist.
    echo Commit, stash, or discard them before syncing.
    echo.
    git status --short
    pause
    exit /b 1
)

echo [1/3] Fetching %REMOTE%...
git fetch %REMOTE%
if errorlevel 1 (
    echo [ERROR] git fetch failed.
    pause
    exit /b 1
)

set "BASE_REF="
for /f "delims=" %%R in ('git symbolic-ref --short refs/remotes/%REMOTE%/HEAD 2^>nul') do set "BASE_REF=%%R"

if not defined BASE_REF (
    set "BASE_REF=%REMOTE%/%FALLBACK_BASE%"
)

git rev-parse --verify "!BASE_REF!" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Base branch !BASE_REF! was not found.
    echo.
    echo Remote branches:
    git branch -r
    pause
    exit /b 1
)

echo [2/3] Resetting %WORK_BRANCH% to !BASE_REF!...
git reset --hard "!BASE_REF!"
if errorlevel 1 (
    echo [ERROR] git reset failed.
    pause
    exit /b 1
)

echo.
echo [3/3] Local worktree is now synced.
echo Base:    !BASE_REF!
echo Branch:  %WORK_BRANCH%
echo.

set /p "ANSWER=Also sync origin/%WORK_BRANCH% with the same state? [y/N]: "
if /I "!ANSWER!"=="Y" (
    echo.
    echo Updating remote feature branch with --force-with-lease...
    git push --force-with-lease %REMOTE% %WORK_BRANCH%
    if errorlevel 1 (
        echo [ERROR] Remote push failed.
        pause
        exit /b 1
    )
    echo Remote feature branch synced.
) else (
    echo Remote feature branch was not changed.
)

echo.
git status -sb
echo.
echo [DONE] Ready for the next update.
pause

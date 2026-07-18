@echo off
rem ================================================================
rem  LayerHeightSlider build script (x64)
rem  Auto-loads vcvars64.bat so you don't need a Developer Prompt.
rem ================================================================
setlocal

for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSDIR=%%i
if "%VSDIR%"=="" (
    echo Visual Studio not found.
    exit /b 1
)

call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul

cd /d "%~dp0"

rem /LD    : build as DLL
rem /O1    : optimize for size
rem /utf-8 : treat source as UTF-8
cl /nologo /LD /O1 /W3 /std:c++17 /EHsc /DUNICODE /D_UNICODE /utf-8 LayerHeightSlider.cpp /link /OUT:LayerHeightSlider.aux2

if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

echo.
echo Build OK: LayerHeightSlider.aux2
echo Copy it to C:\ProgramData\aviutl2\Plugin\
endlocal

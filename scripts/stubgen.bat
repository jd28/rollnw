@echo off
set PYTHON_API_DIR=docs\fake\rollnw
set STUBGEN_OUTPUT_DIR=%TEMP%\generated_stubs
set PYTHON_STUBS_DIR=rollnw-stubs

echo Running stubgen...

if not exist "%STUBGEN_OUTPUT_DIR%" (
    mkdir "%STUBGEN_OUTPUT_DIR%"
)

for %%f in ("%PYTHON_API_DIR%\*.py") do (
    stubgen "%%f" -o "%STUBGEN_OUTPUT_DIR%"
)

if not exist "%PYTHON_STUBS_DIR%" (
    mkdir "%PYTHON_STUBS_DIR%"
)

xcopy /Y /Q "%STUBGEN_OUTPUT_DIR%\rollnw\*.pyi" "%PYTHON_STUBS_DIR%\"

rmdir /S /Q "%STUBGEN_OUTPUT_DIR%"

echo Stub generation complete.
pause

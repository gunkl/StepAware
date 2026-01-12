@echo off
REM StepAware Python Virtual Environment Setup (CMD version)

echo ========================================
echo   StepAware Python venv Setup
echo ========================================
echo.

REM Check if Python is installed
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo X Python not found. Please install Python 3.11 or later.
    echo   Download from: https://www.python.org/downloads/
    exit /b 1
)

echo Checking Python version...
python --version

REM Create virtual environment if it doesn't exist
if not exist "venv" (
    echo.
    echo Creating virtual environment...
    python -m venv venv
    echo Virtual environment created
) else (
    echo.
    echo Virtual environment already exists
)

REM Activate virtual environment
echo.
echo Activating virtual environment...
call venv\Scripts\activate.bat

REM Upgrade pip
echo.
echo Upgrading pip...
python -m pip install --upgrade pip

REM Install dependencies
echo.
echo Installing dependencies from requirements.txt...
pip install -r requirements.txt

echo.
echo ========================================
echo   Setup Complete!
echo ========================================
echo.
echo Virtual environment is now active.
echo.
echo To activate it manually in the future:
echo   venv\Scripts\activate.bat
echo.
echo To deactivate:
echo   deactivate
echo.

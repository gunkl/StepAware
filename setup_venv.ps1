#!/usr/bin/env pwsh
# StepAware Python Virtual Environment Setup
# This script creates and configures a Python virtual environment for development

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  StepAware Python venv Setup" -ForegroundColor Cyan
Write-Host "========================================`n" -ForegroundColor Cyan

# Check if Python is installed
try {
    $pythonVersion = python --version 2>&1
    Write-Host "✓ Found: $pythonVersion" -ForegroundColor Green
} catch {
    Write-Host "✗ Python not found. Please install Python 3.11 or later." -ForegroundColor Red
    Write-Host "  Download from: https://www.python.org/downloads/" -ForegroundColor Yellow
    exit 1
}

# Create virtual environment if it doesn't exist
if (!(Test-Path "venv")) {
    Write-Host "`nCreating virtual environment..." -ForegroundColor Yellow
    python -m venv venv
    Write-Host "✓ Virtual environment created" -ForegroundColor Green
} else {
    Write-Host "`n✓ Virtual environment already exists" -ForegroundColor Green
}

# Activate virtual environment
Write-Host "`nActivating virtual environment..." -ForegroundColor Yellow
& .\venv\Scripts\Activate.ps1

# Upgrade pip
Write-Host "`nUpgrading pip..." -ForegroundColor Yellow
python -m pip install --upgrade pip

# Install dependencies
Write-Host "`nInstalling dependencies from requirements.txt..." -ForegroundColor Yellow
pip install -r requirements.txt

Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "  Setup Complete!" -ForegroundColor Green
Write-Host "========================================`n" -ForegroundColor Cyan

Write-Host "Virtual environment is now active." -ForegroundColor Green
Write-Host "`nTo activate it manually in the future:" -ForegroundColor Yellow
Write-Host "  PowerShell: .\venv\Scripts\Activate.ps1" -ForegroundColor Cyan
Write-Host "  CMD:        .\venv\Scripts\activate.bat" -ForegroundColor Cyan
Write-Host "  Bash:       source venv/Scripts/activate" -ForegroundColor Cyan
Write-Host "`nTo deactivate:" -ForegroundColor Yellow
Write-Host "  deactivate`n" -ForegroundColor Cyan

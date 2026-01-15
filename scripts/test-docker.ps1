# Run native tests in Docker (PowerShell)
# Usage:
#   .\scripts\test-docker.ps1              # Run all tests
#   .\scripts\test-docker.ps1 motion       # Run test_hal_motion_sensor
#   .\scripts\test-docker.ps1 ultrasonic   # Run test_hal_ultrasonic
#   .\scripts\test-docker.ps1 build        # Build ESP32 firmware

param(
    [string]$TestName = "all"
)

$ErrorActionPreference = "Stop"

Push-Location $PSScriptRoot\..

try {
    switch ($TestName) {
        "all" {
            Write-Host "Running all native tests..." -ForegroundColor Cyan
            docker-compose -f docker-compose.test.yml run --rm test
        }
        "build" {
            Write-Host "Building ESP32 firmware..." -ForegroundColor Cyan
            docker-compose -f docker-compose.test.yml run --rm build
        }
        default {
            Write-Host "Running test_$TestName..." -ForegroundColor Cyan
            docker-compose -f docker-compose.test.yml run --rm test-single `
                pio test -e native -f "test_$TestName" -v
        }
    }
    Write-Host "Done!" -ForegroundColor Green
}
finally {
    Pop-Location
}

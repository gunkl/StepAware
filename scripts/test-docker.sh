#!/bin/bash
# Run native tests in Docker
# Usage:
#   ./scripts/test-docker.sh              # Run all tests
#   ./scripts/test-docker.sh motion       # Run test_hal_motion_sensor
#   ./scripts/test-docker.sh ultrasonic   # Run test_hal_ultrasonic
#   ./scripts/test-docker.sh build        # Build ESP32 firmware

set -e

cd "$(dirname "$0")/.."

case "${1:-all}" in
    all)
        echo "Running all native tests..."
        docker-compose -f docker-compose.test.yml run --rm test
        ;;
    build)
        echo "Building ESP32 firmware..."
        docker-compose -f docker-compose.test.yml run --rm build
        ;;
    *)
        echo "Running test_$1..."
        docker-compose -f docker-compose.test.yml run --rm test-single \
            pio test -e native -f "test_$1" -v
        ;;
esac

echo "Done!"

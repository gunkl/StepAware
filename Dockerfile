# StepAware Docker Development Environment
# Provides PlatformIO, testing tools, and build environment

FROM python:3.11-slim

# Install system dependencies
RUN apt-get update && apt-get install -y \
    git \
    build-essential \
    gcc \
    g++ \
    make \
    curl \
    usbutils \
    && rm -rf /var/lib/apt/lists/*

# Install PlatformIO
RUN pip install --no-cache-dir platformio

# Create working directory
WORKDIR /workspace

# Pre-download PlatformIO platforms and tools (speeds up first build)
RUN pio platform install espressif32@6.9.0

# Set up USB device access (for hardware upload when needed)
# Note: Requires --device flag when running container

# Default command: bash shell
CMD ["/bin/bash"]

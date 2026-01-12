# Project AGENT PLAYBOOK

This repository expects any automation agent (Codex CLI, LLM assistants, scripted bots) to follow the rules below before delegating to folder-specific AGENTS guides. Treat this document as the global source of truth; per-directory `AGENTS.md` files (for example `agent/AGENTS.md`) extend these rules with additional constraints for their subtrees.

## Mission & Mindset

**Project Overview**: StepAware is an ESP32-C3 based motion-activated hazard warning system. See [README.md](README.md) for complete feature descriptions and user-facing documentation.

**Agent Mission**: Help developers build, test, and deploy this IoT safety device while maintaining code quality, security, and proper documentation.

## Project Structure

```
StepAware/
├── src/                  # Main source files
├── include/              # Public headers
├── lib/                  # Custom libraries (HAL, etc.)
│   ├── HAL/             # Hardware Abstraction Layer
│   └── StateMachine/    # State machine implementation
├── test/                 # Test framework
│   ├── test_*/          # Test suites
│   ├── reports/         # Test output (not committed)
│   └── analyze_results.py # Test analysis tools
├── data/                 # Web files, config templates
├── docs/                 # Documentation
│   ├── hardware/        # Wiring diagrams, schematics
│   ├── architecture/    # Flow charts, diagrams
│   └── api/             # API specifications
├── scripts/              # Utility scripts
├── datasheets/           # Hardware datasheets (not in git)
├── platformio.ini        # PlatformIO configuration
├── docker-compose.yml    # Docker development environment
└── Dockerfile            # Container build definition
```

## Development Workflow

### Building and Testing

**Using Docker (Recommended):**
```bash
# Build firmware
docker-compose run --rm stepaware-dev pio run -e esp32-devkitlipo

# Run C++ Unity tests (output to console)
docker-compose run --rm stepaware-dev pio test -e native

# Run Python logic tests with HTML reports (files appear in test/reports/ directory)
docker-compose run --rm stepaware-dev python3 test/run_tests.py

# Analyze test results from database
docker-compose run --rm stepaware-dev python3 test/analyze_results.py

# Upload to device (requires hardware connected)
docker-compose run --rm stepaware-dev pio run --target upload

# Monitor serial output
docker-compose run --rm stepaware-dev pio device monitor
```

**Note:** Docker volume mount (`.:/workspace`) ensures all test artifacts generated inside the container are immediately accessible in your local `test/reports/` directory. The container writes to `/workspace/test/reports/` which maps directly to your local filesystem.

**Native PlatformIO:**
```bash
# Build firmware
pio run -e esp32-devkitlipo

# Run C++ Unity tests
pio test -e native

# Run specific test suite
pio test -f test_state_machine

# Run Python tests with reports
python3 test/run_tests.py

# Analyze test history
python3 test/analyze_results.py

# Build with mock hardware
pio run -D MOCK_HARDWARE=1
```

### Implementation Phases

The project follows a 6-phase development plan:

1. **Phase 1**: MVP - Core motion detection ✅ (Current)
2. **Phase 2**: WiFi & Web interface
3. **Phase 3**: Testing infrastructure
4. **Phase 4**: Documentation & versioning
5. **Phase 5**: Power management
6. **Phase 6**: Advanced features (light sensing)

When implementing features, always check which phase they belong to and ensure prerequisites are met.

## Core Development Principles

### Code Quality
- **TESTING**: Create comprehensive automated tests for all features and workflows
  - Unit tests for individual components
  - Integration tests for subsystems
  - Assisted tests for hardware-dependent features
  - Mock hardware implementations for development without physical devices
- **CONFIGURABILITY**: All settings must be configurable via:
  1. Physical device controls (buttons)
  2. Configuration files on device
  3. Web interface (when WiFi enabled)
- **VERSIONING**: Maintain semantic versioning by component
  - Update versions on test runs, builds, or releases
  - Track version history in documentation
- **BUG PROCESS**: Follow systematic bug handling:
  1. Investigate and document the issue
  2. Create GitHub issue with analysis
  3. Implement fix with tests
  4. Validate fix passes all tests
  5. Close issue with commit reference

### Testing Infrastructure
- **TEST REPORTING**: All test outputs are accessible locally after Docker runs
  - Test artifacts stored in `test/reports/` directory:
    - `test_results.db` - SQLite database tracking all test runs
    - `report_*.html` - HTML reports with visual results
    - `report_latest.html` - Symlink to most recent report
  - The entire `test/reports/` directory is excluded from git via `.gitignore`
  - Docker volume mount (`.:/workspace`) ensures files appear in local filesystem
- **Automated Testing**: Maximize test automation coverage
  - Python logic tests via `test/run_tests.py`
  - C++ Unity tests via PlatformIO (`pio test -e native`)
  - Mock simulator for interactive testing (`test/mock_simulator.py`)
  - Prefer unit tests over manual verification
  - Use mocks/stubs for hardware dependencies
  - Maintain >80% code coverage where practical


### Input Handling
- **Debouncing**: All user inputs must be debounced
  - Physical inputs: buttons, sensors
  - Virtual inputs: web server submissions, API calls
  - Use consistent debounce timing across the codebase

### Logging and Diagnostics
- **Robust Logging**: Logs must persist even if process is interrupted
  - Write logs promptly, don't buffer excessively
  - Support Ctrl+C graceful shutdown
  - Include timestamps and severity levels
- **Log Limits**: Prevent unbounded log growth
  - Implement rotation or size limits
  - Provide configuration for log verbosity
- **Failure Investigation**: When runs fail, always:
  1. Check relevant log files
  2. Look for error patterns
  3. Provide actionable diagnostics
  4. Suggest fixes when possible

## Security & Safety Guardrails

- **Credential Protection**: NEVER log or commit sensitive data
  - No credentials, API tokens, passwords in logs
  - No customer/user data in version control
  - Redact sensitive info if it appears in output
  - Alert developer if sensitive data detected
- **Authentication**: All remote access must be authenticated
  - WiFi web interface requires auth
  - SSH/HTTPS must use proper credentials
  - Support first-run setup or config file auth
  - Generate proper security keys for TLS/SSH

## Documentation Standards

### Documentation Hierarchy
- **README.md**: User-facing documentation
  - Project overview and features
  - Hardware setup and requirements
  - Quick start guides
  - Troubleshooting
  - API reference
- **AGENTS.md** (this file): Developer/agent guidance
  - Development workflows
  - Build and test procedures
  - Code quality standards
  - Agent-specific instructions
- **docs/**: Detailed technical documentation
  - Hardware schematics and wiring diagrams
  - Software architecture and flow charts
  - API specifications
  - Test plans and reports
- **datasheets/**: Reference materials (not in git)
  - Component datasheets
  - Specifications
  - Vendor documentation

### Keeping Documentation Current
- When adding features, update:
  1. README.md with user-facing details
  2. This AGENTS.md if workflow changes
  3. Relevant docs/ files for technical details
  4. Code comments for complex logic
- Create diagrams for:
  - Hardware wiring (schematic diagrams)
  - Program flow (flow charts)
  - System architecture (architectural diagrams)
- Update version numbers when shipping features

## Agent Workflow Guidelines

### When in Doubt
1. Read this document to confirm behavior aligns with guardrails
2. Check folder-specific `AGENTS.md` for specialized instructions
3. Review README.md for user requirements
4. Ask clarifying questions if conflicts arise
5. Never guess when safety or data integrity is at stake

### Collaboration with Developers
- Provide normalized, reusable outputs
- Use harmonized report layouts across tools
- Close the feedback loop:
  1. Investigate issues thoroughly
  2. Scaffold solutions with proper structure
  3. Run and validate results
  4. Capture lessons learned for future automation
- Suggest improvements when patterns emerge

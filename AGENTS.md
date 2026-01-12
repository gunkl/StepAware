# Project AGENT PLAYBOOK

This repository expects any automation agent (Codex CLI, LLM assistants, scripted bots) to follow the rules below before delegating to folder-specific AGENTS guides. Treat this document as the global source of truth; per-directory `AGENTS.md` files (for example `agent/AGENTS.md`) extend these rules with additional constraints for their subtrees.

## Mission & Mindset

- The mission of this project is to use the ESP32 development board to develop and build a first of its kind electronic device.  
- PRIMARY USE CASE: Detect a human entering a specific area (ie hallway) and notify them of a hazard (a step down) that they might not see or might forget exists, avoiding them from tripping or worse, falling after missing the hazard or step.
- FEATURE: Use a motion sensor(s) to detect movement and then notify the moving person of the hazard ahead.
- FEATURE: Blink an LED brightly, visibily and repeatedly as a notification of the hazard exists.
- FEATURE: The notification methods should act for at least 15 seconds.
- FEATURE: When the battery is low (below 25%) we should provide the user with a notification to prompt them to recharge the battery.  Initially this could be by blinking with a different pattern when motion is detected.  We do not want to add additional battery usage in order to notify them.
- FEATURE: Provide an indication of battery charging and charge state.  This can be done with the device LED.
- FEATURE: Provide an input method using the device to allow the user to switch between OFF, Continuous ON, and Motion detection operational modes.  When the device is OFF it should still allow a button press to be switched between modes, but save the maximum possible power.   When the device is Continous ON, it will always flash the LED indicating a hazard.  When the device is in Motion detection mode, only when motion is detected will it flash the LED.
- FEATURE: If possible, add an additional set of operational mode(s) that is Motion and Light sensing mode.  This will make use of an ambient light sensor to detect if dark or light, and make the system only operate when darkness exists.  Multiple modes could exist within, for example, instead of flashing the LED, it could act as a low brightness night light, which still serves the purpose of warning of the hazard without flashing, both with and without motion sensing enabled (ie, without motion sensing enabled, it would always be on, but at a reduced power level, and with motion sensing enabled it would be on at a reduced level only when detecting motion).  It could be configured to be steady or flashing in any of these modes.
- FEATURE: If the device supports wifi, it can be configured by the user to connect to wifi.  We would want to have a simple web server on the device that allows easy configuration.  We would also want to provide a view that has device status and activity history.
- FEATURE: If a web server or ssh server are made available to the user, it must have authentication methods.  The user could configure these authentication methods at first use (first use, open access) or via configuration file.
- FEATURE: If possible the device should create proper security keys to allow full secured https/ssh access.
- CONFIGURABILITY: To the best of our ability we want all settings and operational modes to be phyiscally configurable on the device without programming the device remotely.  In addition the user should be able to easily modify simple configuration file(s) on the device to configure it.  In addition the user should be able to use a web browser to configure the device.
- TESTING: Create as much automated testing of all project features and workflows as possible.  If not possible to fully automate, also create an assisted test case that can guide the user through the various use cases and verification.  Create a markup report with the test cases and test output.  Create a lightweight database and record all testing, allowing the user to diagnose changes through different versions of the project and see a list of all prior test runs, allowing them to select a test run and regenerate the report for that test run as needed.
- VERSIONING: Create a versioning schema and maintain updated versioning of the project by component and subcomponent as necessary.  Versions can be updated whenever a new test run, build, or execution is run, it is not necessary to update versions on every single code change.
- BUG PROCESS: Follow a full systematic bug handling process; when bugs are found, investigate, then submit the bug to the repository, then resolve, fix, validate, test, close bug.

- Ensure all user inputs are debounced, all physical and virtual inputs. Virtual includes web server submissions, etc.
- Default to reusable, normalized outputs. Suggest harmonized report layouts across plugins and only diverge when the operator explicitly requests it.
- Close the loop: help the operator investigate, scaffold solutions, run them, validate results, and capture lessons so future automation can move faster.
- Whenever there is a bad run or the user indicates a run failed, always investigate using the logs from agent runs to see if more information can be gathered to troubleshoot and diagnose the problem more accurately.

## Global Guardrails

- **Safety:** Never log or commit credentials, API tokens, or customer data. If sensitive information appears in logs or artifacts, redact it and alert the operator.
- **Logging of program results:** Make sure that we log as much as possible, even if the user cancels the app/closes mid-run with control-c the logs should exist, we should not wait to write out the log information, do it as promptly as possible.
- **Always create limits:** Do not allow logs to exceed allowable usage or expand infinitely, always provide guidance to set sain limits.

## Documentation Map

- `AGENTS.md` (this file) — global principles for every automation agent touching the repo.
- Additional folders can include their own `AGENTS.md` when specialized instructions emerge.
- Datasheets for devices to be used will be stored in the datasheets/ folder, which is not stored in github.  This can be read to obtain specifications, details, and help with the design and implementation of the project.
- Create schematic diagrams of wiring of the various project physical hardware components.
- Create flow chart diagrams representing the program flow.
- Create architectural diagrams of the program.

## Working With Operators

- Keep README and `docs/` focused on human workflows; when you need deeper guidance, check the relevant `AGENTS.md`.
- When adding features, functions, and capabilities, update both this document (if the guidance is project-wide) and the relevant subfolder’s AGENT guide to avoid stale instructions, and especially update the README to keep the project tuned to where a new developer or user knows what it does and how it works.

## When in Doubt

1. Read this document to confirm the requested behavior aligns with global guardrails.
2. Drill into the folder-specific `AGENTS.md` for implementation details.
3. Ask clarifying questions if the request conflicts with these guardrails; never guess when safety or data integrity is at stake.

# `/device-logs` — Download and Display ESP32 Device Log

Downloads a debug log from a StepAware device and displays it, with key
diagnostic lines highlighted.  Useful for analysing sleep/wake behaviour,
crash recovery, and GPIO state after a flash without needing a serial monitor.

Supports an optional argument to select which log to fetch:
- `current` (default) — the log for the current boot cycle (LittleFS)
- `boot_1` — the log from the previous boot cycle
- `boot_2` — the log from two boots ago
- `ram` — the in-memory ring buffer (JSON; contains only the most recent entries
  and does **not** survive a reboot)

---

## Step 1: Confirm device IP

Read `secrets.env` in the project root to extract `DEVICE_IP`.  The file uses
simple `KEY=VALUE` format; parse with a plain text read.

- If `secrets.env` exists and contains `DEVICE_IP`, use that value as the
  default.  Show it to the user and ask for confirmation or override before
  continuing.
- If the file does not exist or `DEVICE_IP` is not set, show **no default** and
  require the user to supply the IP explicitly.  Do not hardcode any IP.

Do not proceed to Step 2 until a valid IP is confirmed.

---

## Step 2: Fetch the log

Map the user's argument to the correct endpoint:

| Argument | Endpoint | Content-Type |
|---|---|---|
| `current` (default) | `/api/debug/logs/current` | plaintext |
| `boot_1` | `/api/debug/logs/boot_1` | plaintext |
| `boot_2` | `/api/debug/logs/boot_2` | plaintext |
| `ram` | `/api/logs` | JSON |

```bash
curl -s -w "\n%{http_code}" http://<DEVICE_IP><endpoint> --connect-timeout 10
```

- HTTP 200 = success.  Continue to Step 3.
- HTTP 404 = log does not exist on device (e.g. no previous boot for `boot_1`).
  Tell the user and suggest alternatives.
- Any other status or connection failure = report error and stop.

---

## Step 3: Display the log

### Plaintext logs (`current`, `boot_1`, `boot_2`)

Print the full log.  Then, after the raw output, print a **Diagnostic Summary**
section that highlights lines matching these patterns (grep-style; show line
number and the full matching line):

| Pattern | Why it matters |
|---|---|
| `UART0 teardown` | Confirms UART0 driver was deleted at boot |
| `IO_MUX GPIO1` | Shows live MCU_SEL value — 1 = GPIO mode (correct) |
| `Light sleep GPIO` | Pre-sleep pin levels — GPIO1=1 here means sensor is HIGH before sleep |
| `Wake snapshot` | Pin levels at the exact moment of wake (captured before Serial re-init) |
| `Power:.*wake` | Wake-source routing result: which source was detected |
| `Spurious GPIO wake` | Confirms a glitch wake was detected and suppressed |
| `PIR motion` | Any line containing this (alerts, crash recovery, routing) |

If none of the diagnostic patterns match, note that in the summary.

### JSON log (`ram`)

Pretty-print the JSON.  Extract the `logs` array and display each entry.  Then
run the same diagnostic pattern scan across all log message strings.

---

## Step 4 (optional): Clear logs

If the user invoked `/device-logs clear`, also issue a POST to wipe all
LittleFS debug logs after displaying them:

```bash
curl -s -X POST http://<DEVICE_IP>/api/debug/logs/clear
```

Report the response.

---

## API reference (do not prompt for these)

| Endpoint | Method | Description |
|---|---|---|
| `/api/debug/logs/current` | GET | Current-boot LittleFS log (plaintext) |
| `/api/debug/logs/boot_1` | GET | Previous-boot log |
| `/api/debug/logs/boot_2` | GET | Two-boots-ago log |
| `/api/logs` | GET | RAM ring-buffer log (JSON) |
| `/api/debug/logs/clear` | POST | Wipe all LittleFS debug logs |

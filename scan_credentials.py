import re

CORE = r"C:\Users\David\Documents\VSCode Projects\ESP32\StepAware\coredump.bin"

print("=== CREDENTIAL SCAN ===\n")

with open(CORE, 'rb') as f:
    data = f.read()

# Look for common WiFi credential patterns
patterns = {
    'ssid': rb'ssid',
    'password': rb'password',
    'pass': rb'pass',
    'wifi': rb'wifi',
    'SSID': rb'SSID',
    'PASSWORD': rb'PASSWORD'
}

found_credentials = False
for name, pattern in patterns.items():
    pos = 0
    while True:
        pos = data.find(pattern, pos)
        if pos == -1:
            break

        # Extract surrounding context (but don't print the actual values)
        start = max(0, pos - 20)
        end = min(len(data), pos + 60)

        # Check if this looks like actual credential data (printable ASCII nearby)
        context = data[start:end]
        printable_count = sum(1 for b in context if 32 <= b < 127)

        if printable_count > len(context) * 0.5:  # If more than 50% printable
            print(f"[SECURITY] Potential credential pattern '{name}' at offset 0x{pos:04X} [REDACTED]")
            found_credentials = True

        pos += 1

if not found_credentials:
    print("[OK] No obvious credential patterns detected in core dump.")
else:
    print("\n[WARNING] Do not share this core dump file publicly.")

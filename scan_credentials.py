import re

data = open('coredump.bin', 'rb').read()

# Scan for WiFi credentials
wifi_pos = data.find(b'wifi')
ssid_patterns = [b'ssid', b'SSID']
pass_patterns = [b'password', b'pass', b'psk']

found_creds = []

# Search for credential-like strings
for pattern in ssid_patterns + pass_patterns:
    pos = 0
    while True:
        pos = data.find(pattern, pos)
        if pos == -1:
            break
        # Check if there's a string-like value nearby (within 64 bytes)
        for offset in range(-32, 64):
            check_pos = pos + offset
            if check_pos < 0 or check_pos >= len(data):
                continue
            # Look for printable ASCII sequences of 6+ chars
            if 32 <= data[check_pos] < 127:
                end = check_pos
                while end < len(data) and 32 <= data[end] < 127 and end - check_pos < 64:
                    end += 1
                if end - check_pos >= 6:
                    found_creds.append((pattern.decode(), check_pos, end - check_pos))
                    break
        pos += 1

if found_creds:
    print("[SECURITY] Potential WiFi credentials found in core dump (DRAM snapshot).")
    for name, offset, length in found_creds[:5]:  # Limit output
        print(f"  - Near '{name}' at offset 0x{offset:04X} ({length} bytes) [REDACTED]")
    print("  Do not share this core dump file publicly.")
else:
    print("[SECURITY] No obvious credential patterns detected in DRAM snapshot.")
    print("  (Note: credentials may still be present in encrypted or obfuscated form)")

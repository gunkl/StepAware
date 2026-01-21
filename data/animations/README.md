# Custom LED Matrix Animations

This directory contains custom animation definition files for the 8x8 LED Matrix display.

## File Format

Animation files are plain text with the following format:

```
# Comment lines start with #
name=AnimationName
loop=true

# Each frame: 8 binary bytes (rows) + delay in milliseconds
frame=11111111,10000001,10000001,10000001,10000001,10000001,10000001,11111111,100
frame=...
```

### Format Details

- **name**: Animation name (max 31 characters, used to play animation)
- **loop**: `true` or `false` - whether animation loops indefinitely
- **frame**: 8 binary bytes + delay
  - Each byte represents one row of 8 pixels (MSB = leftmost pixel)
  - Delay in milliseconds before showing next frame
  - Maximum 16 frames per animation

### Example

```
name=Blink
loop=true

# Frame 1: All on
frame=11111111,11111111,11111111,11111111,11111111,11111111,11111111,11111111,500

# Frame 2: All off
frame=00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,500
```

## Included Animations

| File | Name | Description | Frames | Loop |
|------|------|-------------|--------|------|
| [heart.txt](heart.txt) | Heart | Pulsing heart symbol | 5 | Yes |
| [spinner.txt](spinner.txt) | Spinner | Rotating line | 8 | Yes |
| [alert.txt](alert.txt) | Alert | Attention-grabbing X pattern | 4 | Yes |
| [wave.txt](wave.txt) | Wave | Smooth vertical wave | 8 | Yes |

## Usage

### Loading Animations

Animations must be uploaded to the ESP32's LittleFS filesystem under `/animations/` directory.

```cpp
// Load custom animation
if (ledMatrix->loadCustomAnimation("/animations/heart.txt")) {
    Serial.println("Animation loaded successfully");
}
```

### Playing Animations

```cpp
// Play animation (infinite loop)
ledMatrix->playCustomAnimation("Heart", 0);

// Play animation for 5 seconds
ledMatrix->playCustomAnimation("Spinner", 5000);

// Stop animation
ledMatrix->stopAnimation();
```

### Checking Loaded Animations

```cpp
uint8_t count = ledMatrix->getCustomAnimationCount();
Serial.printf("Loaded %d custom animations\n", count);
```

### Clearing Animations

```cpp
// Free memory used by custom animations
ledMatrix->clearCustomAnimations();
```

## Creating Custom Animations

### Design Tips

1. **Binary Format**: Each row is 8 bits, leftmost pixel = MSB
   - `11111111` = all 8 pixels on
   - `10000001` = left and right pixels on, middle off
   - `00000000` = all pixels off

2. **Frame Timing**: Balance smoothness vs power consumption
   - Fast: 50-100ms per frame (smooth, higher power)
   - Medium: 100-200ms per frame (balanced)
   - Slow: 200-500ms per frame (dramatic, lower power)

3. **Frame Count**: Maximum 16 frames
   - Simple: 2-4 frames (blink, pulse)
   - Medium: 5-8 frames (rotation, wave)
   - Complex: 9-16 frames (detailed sequences)

### Tools

**Binary to Decimal Converter**:
```
Row pattern:  █ ░ █ ░ █ ░ █ ░
Binary:       1 0 1 0 1 0 1 0
Value:        10101010 (binary) = 170 (decimal)
```

**Online Tools**:
- [Binary to Decimal Converter](https://www.rapidtables.com/convert/number/binary-to-decimal.html)
- LED Matrix designer (create visual pattern, export binary)

### Example: Simple Blink

```
name=SimpleBlink
loop=true

# All pixels on
frame=11111111,11111111,11111111,11111111,11111111,11111111,11111111,11111111,500

# All pixels off
frame=00000000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,500
```

### Example: Bouncing Dot

```
name=BouncingDot
loop=true

# Top
frame=00011000,00000000,00000000,00000000,00000000,00000000,00000000,00000000,100

# Moving down
frame=00000000,00011000,00000000,00000000,00000000,00000000,00000000,00000000,100
frame=00000000,00000000,00011000,00000000,00000000,00000000,00000000,00000000,100
frame=00000000,00000000,00000000,00011000,00000000,00000000,00000000,00000000,100
frame=00000000,00000000,00000000,00000000,00011000,00000000,00000000,00000000,100
frame=00000000,00000000,00000000,00000000,00000000,00011000,00000000,00000000,100
frame=00000000,00000000,00000000,00000000,00000000,00000000,00011000,00000000,100

# Bottom
frame=00000000,00000000,00000000,00000000,00000000,00000000,00000000,00011000,100

# Moving up
frame=00000000,00000000,00000000,00000000,00000000,00000000,00011000,00000000,100
frame=00000000,00000000,00000000,00000000,00000000,00011000,00000000,00000000,100
frame=00000000,00000000,00000000,00000000,00011000,00000000,00000000,00000000,100
frame=00000000,00000000,00000000,00011000,00000000,00000000,00000000,00000000,100
frame=00000000,00000000,00011000,00000000,00000000,00000000,00000000,00000000,100
frame=00000000,00011000,00000000,00000000,00000000,00000000,00000000,00000000,100
```

## Upload to ESP32

### Via Web UI (Recommended)

1. Navigate to Hardware tab in web dashboard
2. Scroll to "LED Matrix Display" section
3. Click "Upload Animation File"
4. Select `.txt` file
5. Animation automatically loads and appears in dropdown

### Via Serial/USB

```bash
# Upload using esptool or PlatformIO
pio run -t uploadfs
```

### Via Code

Place animation files in `data/animations/` directory before uploading filesystem.

## Memory Considerations

- Each custom animation uses ~260 bytes of RAM
- Maximum 8 custom animations can be loaded simultaneously
- Total custom animation memory: ~2KB

## Troubleshooting

### Animation Won't Load

**Check file format**:
- Name is set correctly
- Each frame has exactly 8 binary bytes + delay
- Binary values use only 0 and 1
- No extra commas or spaces

**Check file location**:
- File must be in `/animations/` directory on LittleFS
- Filename extension is `.txt`

### Animation Plays Incorrectly

**Frame timing**:
- Increase delay values if animation is too fast
- Decrease delay values if animation is too slow

**Visual appearance**:
- Check binary patterns are correct
- Verify MSB is leftmost pixel
- Test individual frames

### Out of Memory

**Clear unused animations**:
```cpp
ledMatrix->clearCustomAnimations();
```

**Reduce animation count**:
- Maximum 8 animations loaded at once
- Remove unused animations before loading new ones

## Advanced Features

### Combining Animations

You can switch between animations at runtime:

```cpp
// Load multiple animations
ledMatrix->loadCustomAnimation("/animations/heart.txt");
ledMatrix->loadCustomAnimation("/animations/spinner.txt");
ledMatrix->loadCustomAnimation("/animations/wave.txt");

// Play heart for 3 seconds
ledMatrix->playCustomAnimation("Heart", 3000);
delay(3000);

// Switch to spinner
ledMatrix->playCustomAnimation("Spinner", 0);  // Loop forever
```

### Animation Events

Trigger animations based on events:

```cpp
void onMotionDetected() {
    ledMatrix->playCustomAnimation("Alert", 5000);
}

void onBatteryLow() {
    ledMatrix->playCustomAnimation("Heart", 0);  // Pulsing heart warning
}

void onWiFiConnected() {
    ledMatrix->playCustomAnimation("Wave", 2000);
}
```

## References

- [Adafruit 8x8 LED Matrix Guide](https://learn.adafruit.com/adafruit-led-backpack)
- [Binary Number Tutorial](https://www.electronics-tutorials.ws/binary/bin_1.html)
- [HAL_LEDMatrix_8x8 API Documentation](../../include/hal_ledmatrix_8x8.h)

---

**Created**: 2026-01-20
**Last Updated**: 2026-01-20
**Issue**: #12 Phase 2 - Custom Animation Support

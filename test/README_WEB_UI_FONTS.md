# Web UI Font Consistency Tests

## Overview

This test suite (`test_web_ui_fonts.py`) prevents font size regressions in the web UI, specifically in the Hardware tab's sensor and display configuration sections.

## Problem History

**Issue #12 Web UI Fixes (2026-01-20)**

During implementation of 8x8 LED Matrix support, multiple font size inconsistencies were discovered:

1. **Compounding em units**: Nested `font-size:0.85em` declarations caused text to render at 0.614em (way too small)
   - Parent div: `font-size:0.85em`
   - Child div: `font-size:0.85em` → 0.7225em
   - Span: `font-size:0.85em` → 0.614em (38% smaller!)

2. **Monospace font size mismatch**: GPIO pin numbers and I2C addresses used `font-family:monospace` which rendered smaller than sans-serif at the same `font-size`

3. **Inconsistent label/value styling**: Sensor config used different patterns than display config

## Solution

All value text now uses:
- **Uniform font**: Default sans-serif (no monospace)
- **Single font-size level**: `0.85em` applied at div level only
- **Consistent structure**: Same pattern for sensors and displays

```html
<!-- Correct (single font-size level) -->
<div style="font-size:0.85em;">
  <span style="color:#64748b;">Label:</span> <span>Value</span>
</div>

<!-- Incorrect (nested font-size) -->
<div style="font-size:0.85em;">
  <span style="font-size:0.85em; color:#64748b;">Label:</span>
  <span style="font-size:0.85em;">Value</span>
</div>
```

## Test Coverage

The test suite includes 12 tests across 2 test classes:

### `TestWebUIFontConsistency`
- `test_no_nested_font_sizes_in_sensor_wiring` - No nested font-size in sensor wiring
- `test_no_nested_font_sizes_in_sensor_config` - No nested font-size in sensor config
- `test_no_nested_font_sizes_in_display_wiring` - No nested font-size in display wiring
- `test_no_nested_font_sizes_in_display_config` - No nested font-size in display config
- `test_no_monospace_font_in_gpio_values` - GPIO values don't use monospace
- `test_no_monospace_font_in_i2c_address` - I2C address doesn't use monospace
- `test_consistent_font_size_throughout` - All text uses consistent 0.85em
- `test_no_triple_nested_font_sizes` - Prevents 0.614em compounding
- `test_sensor_and_display_have_matching_structure` - Cards use identical CSS

### `TestFontSizeRegressionPreventions`
- `test_prevent_compounding_em_units` - Prevents multiple font-size per line
- `test_prevent_monospace_size_mismatch` - Prevents monospace font usage
- `test_prevent_inconsistent_label_value_styling` - Ensures consistent patterns

## Running the Tests

```bash
# Run font consistency tests only
python test/test_web_ui_fonts.py

# Run all tests
python test/run_tests.py
```

Expected output:
```
Ran 12 tests in 0.002s

OK
```

## When to Run

Run these tests:
1. **Before committing** any changes to `src/web_api.cpp`
2. **After modifying** HTML generation for sensor/display cards
3. **When adding** new configuration fields to Hardware tab
4. **During CI/CD** pipeline for every pull request

## What the Tests Validate

### HTML Structure Validation
- Font sizes only applied at appropriate levels
- No nesting of font-size declarations
- Consistent use of spans for labels and values

### Visual Consistency
- All value text at same size (0.85em)
- Headers slightly larger (0.9em)
- No monospace fonts causing size discrepancies

### Regression Prevention
- Specific checks for known issues (0.614em compounding)
- Validates sensor and display cards match
- Ensures labels and values use consistent structure

## Maintenance

When adding new configuration fields to sensor or display cards:

1. **Follow the pattern**:
   ```html
   <div style="font-size:0.85em;">
     <span style="color:#64748b;">New Field:</span> <span>Value</span>
   </div>
   ```

2. **Update test mock** in `test_web_ui_fonts.py`:
   - Add new field to `MockWebUIGenerator.generate_sensor_card_html()` or
   - Add new field to `MockWebUIGenerator.generate_display_card_html()`

3. **Run tests** to ensure consistency

## CI/CD Integration

Add to `.github/workflows/tests.yml` (if using GitHub Actions):

```yaml
- name: Run Web UI Font Tests
  run: python test/test_web_ui_fonts.py
```

## See Also

- [src/web_api.cpp](../src/web_api.cpp) - Web UI HTML generation
- [test/test_web_api.py](test_web_api.py) - REST API endpoint tests
- Issue #12 Phase 1 documentation files:
  - `WEBUI_FIXES_ISSUE12.md`
  - `IMPLEMENTATION_SUMMARY_ISSUE12.md`

---

**Last Updated**: 2026-01-20
**Regression Prevention for**: Issue #12 Web UI Font Size Inconsistencies

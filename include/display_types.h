#ifndef STEPAWARE_DISPLAY_TYPES_H
#define STEPAWARE_DISPLAY_TYPES_H

#include <stdint.h>

/**
 * @brief Display type enumeration
 */
enum DisplayType {
    DISPLAY_TYPE_SINGLE_LED = 0,
    DISPLAY_TYPE_MATRIX_8X8 = 1,
    DISPLAY_TYPE_NONE = 255
};

/**
 * @brief Display capabilities structure
 */
struct DisplayCapabilities {
    bool supportsText;
    bool supportsPixelControl;
    bool supportsAnimation;
    bool supportsGrayscale;
    uint8_t width;
    uint8_t height;
    const char* typeName;
};

#endif // STEPAWARE_DISPLAY_TYPES_H

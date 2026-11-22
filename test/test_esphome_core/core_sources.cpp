// Compiles the dependency-free ESPHome core sources into this native test suite.
// Relative includes keep the suite buildable from both [env:native] and
// [env:native_esphome] (test_build_src = no in both).
#include "../../esphome/components/stepaware/core/step_button.cpp"
#include "../../esphome/components/stepaware/core/step_direction_detector.cpp"
#include "../../esphome/components/stepaware/core/step_led_pattern.cpp"
#include "../../esphome/components/stepaware/core/step_state_machine.cpp"

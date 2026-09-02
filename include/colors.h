#include <Arduino.h>

struct Colour
{
    uint8_t r, g, b;
};

constexpr Colour BLACK = {0, 0, 0};
constexpr Colour GREEN = {0, 10, 0};
constexpr Colour RED = {10, 0, 0};
constexpr Colour BLUE = {0, 0, 10};
constexpr Colour AMBER = {10, 6, 0};
constexpr Colour PURPLE = {10, 0, 10};

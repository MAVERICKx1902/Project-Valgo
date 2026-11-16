#include "valgo/core/format.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace valgo {
namespace {

std::string FormatFloat(f64 value, int max_decimals) {
    if (std::isnan(value)) {
        return "nan";
    }
    if (std::isinf(value)) {
        return value > 0.0 ? "inf" : "-inf";
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*g", max_decimals, value);
    return std::string(buffer);
}

}  // namespace

std::string ToString(bool value) { return value ? "true" : "false"; }
std::string ToString(char value) { return std::string(1, value); }
std::string ToString(const char* value) { return value != nullptr ? std::string(value) : std::string("(null)"); }
std::string ToString(std::string_view value) { return std::string(value); }
std::string ToString(const std::string& value) { return value; }
std::string ToString(i8 value) { return std::to_string(static_cast<int>(value)); }
std::string ToString(i16 value) { return std::to_string(static_cast<int>(value)); }
std::string ToString(i32 value) { return std::to_string(value); }
std::string ToString(i64 value) { return std::to_string(value); }
std::string ToString(u8 value) { return std::to_string(static_cast<unsigned>(value)); }
std::string ToString(u16 value) { return std::to_string(static_cast<unsigned>(value)); }
std::string ToString(u32 value) { return std::to_string(value); }
std::string ToString(u64 value) { return std::to_string(value); }
std::string ToString(f32 value) { return FormatFloat(static_cast<f64>(value), 6); }
std::string ToString(f64 value) { return FormatFloat(value, 7); }

std::string ToString(const void* value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%p", value);
    return std::string(buffer);
}

}  // namespace valgo

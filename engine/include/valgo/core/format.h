// =============================================================================
// Project Valgo — engine core: string formatting.
//
// The engine does not take a dependency on {fmt} or <format> (the latter is not
// available in the minimum supported toolchains). This is a deliberately small
// formatter used for logging and diagnostics: "{}" placeholders are substituted
// left to right. There is no width/precision syntax; compose with helpers when
// more control is required.
// =============================================================================
#pragma once

#include <string>
#include <string_view>
#include <type_traits>

#include "valgo/core/types.h"

namespace valgo {

/// Converts a single value to text for logging. Extend with overloads.
std::string ToString(bool value);
std::string ToString(char value);
std::string ToString(const char* value);
std::string ToString(std::string_view value);
std::string ToString(const std::string& value);
std::string ToString(i8 value);
std::string ToString(i16 value);
std::string ToString(i32 value);
std::string ToString(i64 value);
std::string ToString(u8 value);
std::string ToString(u16 value);
std::string ToString(u32 value);
std::string ToString(u64 value);
std::string ToString(f32 value);
std::string ToString(f64 value);
std::string ToString(const void* value);

/// Formats with a single "{}" replacement per argument.
template <typename... Args>
std::string Format(std::string_view format, const Args&... args) {
    if constexpr (sizeof...(Args) == 0) {
        return std::string(format);
    } else {
        const std::string parts[] = {ToString(args)...};
        std::string out;
        out.reserve(format.size() + sizeof...(Args) * 8u);
        usize arg_index = 0;
        for (usize i = 0; i < format.size(); ++i) {
            const char c = format[i];
            const bool is_placeholder = (c == '{') && (i + 1 < format.size()) && (format[i + 1] == '}');
            if (is_placeholder) {
                if (arg_index < sizeof...(Args)) {
                    out += parts[arg_index++];
                } else {
                    out += "{}";
                }
                ++i;
                continue;
            }
            out += c;
        }
        return out;
    }
}

}  // namespace valgo

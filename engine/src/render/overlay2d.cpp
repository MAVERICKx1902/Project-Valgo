#include "valgo/render/overlay2d.h"

#include <array>

namespace valgo::render {
namespace {

// -----------------------------------------------------------------------------
// 5x7 bitmap font covering ASCII 32..95 (uppercase only; 64 glyphs).
// Each glyph is 7 rows; the low 5 bits of every row are the pixels, bit 4 is
// the left-most column.
// -----------------------------------------------------------------------------
constexpr u8 kGlyphCount = 64;
constexpr std::array<std::array<u8, 7>, kGlyphCount> kFont = {{
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},  // ' '
    {{0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},  // '!'
    {{0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00}},  // '"'
    {{0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},  // '#'
    {{0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04}},  // '$'
    {{0x19, 0x1A, 0x04, 0x08, 0x0B, 0x13, 0x00}},  // '%'
    {{0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}},  // '&'
    {{0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}},  // '\''
    {{0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},  // '('
    {{0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},  // ')'
    {{0x00, 0x0A, 0x04, 0x1F, 0x04, 0x0A, 0x00}},  // '*'
    {{0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},  // '+'
    {{0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08}},  // ','
    {{0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},  // '-'
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},  // '.'
    {{0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},  // '/'
    {{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},  // '0'
    {{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},  // '1'
    {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},  // '2'
    {{0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},  // '3'
    {{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},  // '4'
    {{0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},  // '5'
    {{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},  // '6'
    {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},  // '7'
    {{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},  // '8'
    {{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},  // '9'
    {{0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00}},  // ':'
    {{0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}},  // ';'
    {{0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},  // '<'
    {{0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},  // '='
    {{0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},  // '>'
    {{0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},  // '?'
    {{0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E}},  // '@'
    {{0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},  // 'A'
    {{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},  // 'B'
    {{0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},  // 'C'
    {{0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C}},  // 'D'
    {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},  // 'E'
    {{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},  // 'F'
    {{0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},  // 'G'
    {{0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},  // 'H'
    {{0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},  // 'I'
    {{0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},  // 'J'
    {{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},  // 'K'
    {{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},  // 'L'
    {{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},  // 'M'
    {{0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},  // 'N'
    {{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},  // 'O'
    {{0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},  // 'P'
    {{0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},  // 'Q'
    {{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},  // 'R'
    {{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},  // 'S'
    {{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},  // 'T'
    {{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},  // 'U'
    {{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},  // 'V'
    {{0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},  // 'W'
    {{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},  // 'X'
    {{0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},  // 'Y'
    {{0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},  // 'Z'
    {{0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},  // '['
    {{0x10, 0x08, 0x08, 0x04, 0x02, 0x02, 0x01}},  // '\\'
    {{0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},  // ']'
    {{0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00}},  // '^'
    {{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},  // '_'
}};

const std::array<u8, 7> kFallback = {0x1F, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1F};

const std::array<u8, 7>& Glyph(char character) {
    unsigned char code = static_cast<unsigned char>(character);
    if (code >= 'a' && code <= 'z') {
        code = static_cast<unsigned char>(code - 'a' + 'A');
    }
    if (code < 32 || code > 95) {
        return kFallback;
    }
    return kFont[static_cast<usize>(code - 32)];
}

}  // namespace

void Overlay2D::Clear() { triangles_.clear(); }

void Overlay2D::AddQuad(const Vec2& min, const Vec2& max, const Vec4& color) {
    const u32 packed = rhi::PackColor(color);
    const rhi::ScreenVertex a{{min.x, min.y}, {0.0f, 0.0f}, packed};
    const rhi::ScreenVertex b{{max.x, min.y}, {1.0f, 0.0f}, packed};
    const rhi::ScreenVertex c{{max.x, max.y}, {1.0f, 1.0f}, packed};
    const rhi::ScreenVertex d{{min.x, max.y}, {0.0f, 1.0f}, packed};
    triangles_.push_back(a);
    triangles_.push_back(b);
    triangles_.push_back(c);
    triangles_.push_back(a);
    triangles_.push_back(c);
    triangles_.push_back(d);
}

void Overlay2D::DrawRect(const Vec2& position, const Vec2& size, const Vec4& color) {
    if (size.x <= 0.0f || size.y <= 0.0f || color.w <= 0.0f) {
        return;
    }
    AddQuad(position, {position.x + size.x, position.y + size.y}, color);
}

void Overlay2D::DrawRectOutline(const Vec2& position, const Vec2& size, f32 thickness,
                                const Vec4& color) {
    if (thickness <= 0.0f) {
        return;
    }
    DrawRect(position, {size.x, thickness}, color);
    DrawRect({position.x, position.y + size.y - thickness}, {size.x, thickness}, color);
    DrawRect(position, {thickness, size.y}, color);
    DrawRect({position.x + size.x - thickness, position.y}, {thickness, size.y}, color);
}

void Overlay2D::DrawLine(const Vec2& from, const Vec2& to, f32 thickness, const Vec4& color) {
    const f32 delta_x = to.x - from.x;
    const f32 delta_y = to.y - from.y;
    const f32 length = std::sqrt(delta_x * delta_x + delta_y * delta_y);
    if (length < 1.0e-4f) {
        return;
    }
    // Axis-aligned fast paths keep HUD lines crisp.
    if (std::fabs(delta_x) <= 0.5f) {
        const f32 min_x = from.x - thickness * 0.5f;
        DrawRect({min_x, std::min(from.y, to.y)},
                 {thickness, std::fabs(delta_y) + thickness}, color);
        return;
    }
    if (std::fabs(delta_y) <= 0.5f) {
        const f32 min_y = from.y - thickness * 0.5f;
        DrawRect({std::min(from.x, to.x), min_y},
                 {std::fabs(delta_x) + thickness, thickness}, color);
        return;
    }

    const f32 normal_x = -delta_y / length * thickness * 0.5f;
    const f32 normal_y = delta_x / length * thickness * 0.5f;
    const u32 packed = rhi::PackColor(color);
    const rhi::ScreenVertex quad[6] = {
        {{from.x + normal_x, from.y + normal_y}, {0.0f, 0.0f}, packed},
        {{to.x + normal_x, to.y + normal_y}, {1.0f, 0.0f}, packed},
        {{to.x - normal_x, to.y - normal_y}, {1.0f, 1.0f}, packed},
        {{from.x + normal_x, from.y + normal_y}, {0.0f, 0.0f}, packed},
        {{to.x - normal_x, to.y - normal_y}, {1.0f, 1.0f}, packed},
        {{from.x - normal_x, from.y - normal_y}, {0.0f, 1.0f}, packed},
    };
    triangles_.insert(triangles_.end(), quad, quad + 6);
}

void Overlay2D::DrawText(const Vec2& position, std::string_view text, const Vec4& color, f32 scale) {
    if (scale <= 0.0f) {
        return;
    }
    f32 cursor_x = position.x;
    for (char character : text) {
        if (character == '\n') {
            cursor_x = position.x;
            continue;
        }
        const std::array<u8, 7>& glyph = Glyph(character);
        for (usize row = 0; row < 7; ++row) {
            const u8 bits = glyph[row];
            // Merge horizontal runs into single quads to cut overdraw.
            usize column = 0;
            while (column < 5) {
                if ((bits & (0x10u >> column)) == 0u) {
                    ++column;
                    continue;
                }
                usize run_end = column;
                while (run_end < 5 && (bits & (0x10u >> run_end)) != 0u) {
                    ++run_end;
                }
                const f32 quad_x = cursor_x + static_cast<f32>(column) * scale;
                const f32 quad_y = position.y + static_cast<f32>(row) * scale;
                AddQuad({quad_x, quad_y},
                        {quad_x + static_cast<f32>(run_end - column) * scale, quad_y + scale}, color);
                column = run_end;
            }
        }
        cursor_x += kFontAdvance * scale;
    }
}

void Overlay2D::DrawTextCentered(f32 center_x, f32 y, std::string_view text, const Vec4& color,
                                 f32 scale) {
    const Vec2 size = MeasureText(text, scale);
    DrawText({center_x - size.x * 0.5f, y}, text, color, scale);
}

Vec2 Overlay2D::MeasureText(std::string_view text, f32 scale) const {
    f32 max_width = 0.0f;
    f32 width = 0.0f;
    for (char character : text) {
        if (character == '\n') {
            max_width = std::max(max_width, width);
            width = 0.0f;
            continue;
        }
        width += kFontAdvance * scale;
    }
    max_width = std::max(max_width, width);
    return {max_width, kFontHeight * scale};
}

}  // namespace valgo::render

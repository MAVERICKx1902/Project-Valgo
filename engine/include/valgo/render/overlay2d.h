// =============================================================================
// Project Valgo — immediate-mode 2D overlay.
//
// Engine-level HUD/debug drawing in screen pixels. Games describe what to draw;
// the renderer submits it through the Screen2D shader program. Keeps game code
// free of vertex buffers and rasteriser details.
// =============================================================================
#pragma once

#include <string_view>
#include <vector>

#include "valgo/math/math.h"
#include "valgo/rhi/rhi.h"

namespace valgo::render {

class Overlay2D {
public:
    void Clear();
    bool empty() const { return triangles_.empty(); }

    void DrawRect(const Vec2& position, const Vec2& size, const Vec4& color);
    void DrawRectOutline(const Vec2& position, const Vec2& size, f32 thickness, const Vec4& color);
    void DrawLine(const Vec2& from, const Vec2& to, f32 thickness, const Vec4& color);

    /// Draws text with the built-in 5x7 bitmap font. Lowercase is folded to
    /// uppercase; unknown characters render as '_'.
    void DrawText(const Vec2& position, std::string_view text, const Vec4& color, f32 scale = 2.0f);
    void DrawTextCentered(f32 center_x, f32 y, std::string_view text, const Vec4& color, f32 scale = 2.0f);

    Vec2 MeasureText(std::string_view text, f32 scale = 2.0f) const;

    const std::vector<rhi::ScreenVertex>& triangles() const { return triangles_; }
    usize triangle_count() const { return triangles_.size() / 3; }

private:
    void AddQuad(const Vec2& min, const Vec2& max, const Vec4& color);

    std::vector<rhi::ScreenVertex> triangles_;
};

/// Advance width of one glyph in font pixels (5 wide + 1 spacing).
inline constexpr f32 kFontAdvance = 6.0f;
inline constexpr f32 kFontHeight = 7.0f;

}  // namespace valgo::render

// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/container/optional.hpp"
#include "foundation/utils/maths.hpp"

union UiSize {
    constexpr UiSize() : width(0), height(0) {}
    constexpr UiSize(u16 w, u16 h) : width(w), height(h) {}
    f32x2 ToFloat2() const { return {(f32)width, (f32)height}; }

    struct {
        u16 width;
        u16 height;
    };
    u16 e[2];
};

PUBLIC constexpr bool operator!=(UiSize a, UiSize b) { return a.width != b.width || a.height != b.height; }
PUBLIC constexpr bool operator==(UiSize const& a, UiSize const& b) {
    return a.width == b.width && a.height == b.height;
}

PUBLIC constexpr UiSize ReduceClampedToZero(UiSize size, UiSize reduction) {
    return {(size.width > reduction.width) ? (u16)(size.width - reduction.width) : (u16)0,
            (size.height > reduction.height) ? (u16)(size.height - reduction.height) : (u16)0};
}

PUBLIC constexpr UiSize ExpandChecked(UiSize size, UiSize expansion) {
    return {CheckedCast<u16>(size.width + expansion.width), CheckedCast<u16>(size.height + expansion.height)};
}

// NOTE: we have to be a bit careful with naming here. The name 'Rectangle' conflicts with a GDI
// header on Windows, and 'Rect' conflicts with an header on macOS
struct Rect {
    Rect(f32x2 _pos, f32 _w, f32 _h) : pos(_pos), w(_w), h(_h) {}
    Rect(f32 _x, f32 _y, f32x2 _size) : pos {_x, _y}, size(_size) {}
    Rect(f32x2 _pos, f32x2 _size) : pos(_pos), size(_size) {}
    Rect(f32 _x, f32 _y, f32 _w, f32 _h) : pos {_x, _y}, w(_w), h(_h) {}
    Rect() { x = y = w = h = 0; }

    Rect Up(f32 offset) const { return Rect(x, y - offset, w, h); }
    Rect Down(f32 offset) const { return Rect(x, y + offset, w, h); }
    Rect Left(f32 offset) const { return Rect(x - offset, y, w, h); }
    Rect Right(f32 offset) const { return Rect(x + offset, y, w, h); }

    Rect WithX(f32 _x) const { return Rect(_x, y, w, h); }
    Rect WithY(f32 _y) const { return Rect(x, _y, w, h); }
    Rect WithW(f32 _w) const { return Rect(x, y, _w, h); }
    Rect WithH(f32 _h) const { return Rect(x, y, w, _h); }

    Rect WithXW(f32 _x, f32 _w) const { return Rect(_x, y, _w, h); }
    Rect WithYH(f32 _y, f32 _h) const { return Rect(x, _y, w, _h); }

    Rect WithPos(f32x2 _pos) const { return Rect(_pos, size); }
    Rect WithSize(f32x2 _size) const { return Rect(pos, _size); }

    Rect CutLeft(f32 cut_amount) const { return Rect(x + cut_amount, y, w - cut_amount, h); }
    Rect CutTop(f32 cut_amount) const { return Rect(x, y - cut_amount, w, h - cut_amount); }
    Rect CutRight(f32 cut_amount) const { return Rect(x, y, w - cut_amount, h); }
    Rect CutBottom(f32 cut_amount) const { return Rect(x, y, w, h - cut_amount); }

    void SetBottomByResizing(f32 b) { h = b - pos.y; }
    void SetRightByResizing(f32 r) { w = r - pos.x; }
    void SetBottomByMoving(f32 b) { y = b - h; }
    void SetRightByMoving(f32 r) { x = r - w; }
    f32 Bottom() const { return pos.y + h; }
    f32 Right() const { return pos.x + w; }
    f32 CentreX() const { return pos.x + w / 2; }
    f32 CentreY() const { return pos.y + h / 2; }
    f32x2 Min() const { return pos; }
    f32x2 Max() const { return pos + size; }
    f32x2 Centre() const { return pos + (size * 0.5f); }
    f32x2 TopLeft() const { return pos; }
    f32x2 TopRight() const { return f32x2 {x + w, y}; }
    f32x2 BottomLeft() const { return f32x2 {x, y + h}; }
    f32x2 BottomRight() const { return Max(); }
    bool Contains(f32x2 p) const { return p.x >= pos.x && p.x < Right() && p.y >= pos.y && p.y < Bottom(); }

    bool operator==(Rect const& other) const {
        return other.x == x && other.y == y && other.w == w && other.h == h;
    }
    bool operator!=(Rect const& other) const { return !(*this == other); }

    Rect Reduced(f32 val) const {
        Rect result = *this;
        result.x += val;
        result.y += val;
        result.w -= val * 2;
        result.h -= val * 2;
        return result;
    }

    Rect ReducedVertically(f32 val) const {
        Rect result = *this;
        result.y += val;
        result.h -= val * 2;
        return result;
    }

    Rect ReducedHorizontally(f32 val) const {
        Rect result = *this;
        result.x += val;
        result.w -= val * 2;
        return result;
    }

    Rect Expanded(f32 val) const {
        Rect result = *this;
        result.x -= val;
        result.y -= val;
        result.w += val * 2;
        result.h += val * 2;
        return result;
    }

    static bool Intersection(Rect& a, Rect b) {
        auto const x1 = ::Max(a.x, b.x);
        auto const x2 = ::Min(a.x + a.w, b.x + b.w);
        if (x2 < x1) return false;
        auto const y1 = ::Max(a.y, b.y);
        auto const y2 = ::Min(a.y + a.h, b.y + b.h);
        if (y2 < y1) return false;

        a = Rect(x1, y1, x2 - x1, y2 - y1);
        return true;
    }

    static bool DoRectsIntersect(Rect a, Rect b) {
        return !((b.x > (a.x + a.w)) || ((b.x + b.w) < a.x) || (b.y > (a.y + a.h)) || (b.y + b.h) < a.y);
    }

    static Rect FromMinMax(f32x2 min, f32x2 max) { return Rect(min, max - min); }

    static Rect MakeRectThatEnclosesRects(Rect const& a, Rect const& b) {
        Rect result;
        result.x = ::Min(a.x, b.x);
        result.y = ::Min(a.y, b.y);
        result.SetRightByResizing(::Max(a.Right(), b.Right()));
        result.SetBottomByResizing(::Max(a.Bottom(), b.Bottom()));
        return result;
    }

    static Rect MakeInnerRect(Rect const& a, Rect const& b) {
        Rect result;
        result.x = ::Max(a.x, b.x);
        result.y = ::Max(a.y, b.y);
        result.SetRightByResizing(::Min(a.Right(), b.Right()));
        result.SetBottomByResizing(::Min(a.Bottom(), b.Bottom()));
        return result;
    }

    union {
        f32x2 pos;
        struct {
            f32 x;
            f32 y;
        };
    };
    union {
        f32x2 size;
        struct {
            f32 w;
            f32 h;
        };
    };
};

// y = mx + c
struct LineEquation {
    f32 m, c;
};

struct Line {
    Optional<LineEquation> LineEquation() const {
        auto const delta_x = b.x - a.x;
        if (delta_x == 0) return {};

        auto const m = (b.y - a.y) / delta_x;
        auto const c = a.y - (m * a.x);
        return ::LineEquation {m, c};
    }

    Optional<f32x2> IntersectionWithVerticalLine(f32 vertical_line_x) const {
        if (auto line_equation = this->LineEquation()) {
            auto& v = line_equation.Value();
            return f32x2 {vertical_line_x, v.m * vertical_line_x + v.c};
        }
        return {};
    }

    f32x2 a, b;
};

// https://halt.software/dead-simple-layouts/
namespace rect_cut {

PUBLIC inline Rect CutRight(Rect& r, f32 right_width) {
    auto const new_width = r.w - right_width;
    Rect const result {r.x + new_width, r.y, right_width, r.h};
    r.w = new_width;
    return result;
}

PUBLIC inline Rect CutLeft(Rect& r, f32 left_width) {
    Rect const result {r.x, r.y, left_width, r.h};
    r.x += left_width;
    r.w -= left_width;
    return result;
}

} // namespace rect_cut

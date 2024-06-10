// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "foundation/foundation.hpp"

#define LAY_FLOAT                     1
#define LAY_ASSERT                    ASSERT
#define LAY_REALLOC(_block, _size)    GpaRealloc(_block, _size)
#define LAY_FREE(_block)              GpaFree(_block)
#define LAY_MEMSET(_dst, _val, _size) FillMemory(_dst, _val, _size)
#include "layout/layout.h"

using LayID = lay_id;
using LayScalar = lay_scalar;
using LayVec4 = lay_vec4;

enum LayContain : uint32_t {
    LayContainRow = LAY_ROW, // left to right
    LayContainColumn = LAY_COLUMN, // top to bottom

    LayContainLayout = LAY_LAYOUT, // free layout
    LayContainFlex = LAY_FLEX, // flex model

    LayContainNoWrap = LAY_NOWRAP, // single-line
    LayContainWrap = LAY_WRAP, // multi-line, wrap left to right

    LayContainStart = LAY_START, // at start of row/column
    LayContainMiddle = LAY_MIDDLE, // at center of row/column
    LayContainEnd = LAY_END, // at end of row/column
    LayContainJustify = LAY_JUSTIFY, // insert spacing to stretch across whole row/column
};

enum LayBehave : uint32_t {
    LayBehaveLeft = LAY_LEFT, // anchor to left item or left side of parent
    LayBehaveTop = LAY_TOP, // anchor to top item or top side of parent
    LayBehaveRight = LAY_RIGHT, // anchor to right item or right side of parent
    LayBehaveBottom = LAY_BOTTOM, // anchor to bottom item or bottom side of parent

    LayBehaveHfill = LAY_HFILL, // anchor to both left and right item or parent borders
    LayBehaveVfill = LAY_VFILL, // anchor to both top and bottom item or parent borders
    LayBehaveFill = LAY_FILL, // anchor to all four directions

    LayBehaveHcentre = LAY_HCENTER, // center horizontally, with left margin as offset
    LayBehaveVcentre = LAY_VCENTER, // center vertically, with top margin as offset
    LayBehaveCentre = LAY_CENTER, // center in both directions, with left/top margin as offset
};

struct Layout {
    lay_context ctx;

    Layout() { lay_init_context(&ctx); }
    ~Layout() { lay_destroy_context(&ctx); }

    LayID CreateRootItem(LayScalar width, LayScalar height, uint32_t contain_flags) {
        LayID const item = lay_item(&ctx);
        lay_set_size_xy(&ctx, item, width, height);
        lay_set_contain(&ctx, item, contain_flags);
        return item;
    }
    LayID CreateParentItem(LayID parent,
                           LayScalar width,
                           LayScalar height,
                           uint32_t behave_flags,
                           uint32_t contain_flags) {
        LayID const item = lay_item(&ctx);
        lay_insert(&ctx, parent, item);
        lay_set_size_xy(&ctx, item, width, height);
        lay_set_behave(&ctx, item, behave_flags);
        lay_set_contain(&ctx, item, contain_flags);
        return item;
    }
    LayID CreateChildItem(LayID parent, LayScalar width, LayScalar height, uint32_t behave_flags) {
        LayID const item = lay_item(&ctx);
        lay_insert(&ctx, parent, item);
        lay_set_size_xy(&ctx, item, width, height);
        lay_set_behave(&ctx, item, behave_flags);
        return item;
    }

    void SetMargins(LayID id, LayScalar l, LayScalar t, LayScalar r, LayScalar b) {
        lay_set_margins_ltrb(&ctx, id, l, t, r, b);
    }

    void SetLeftMargin(LayID id, LayScalar val) {
        auto margins = lay_get_margins(&ctx, id);
        margins[0] = val;
        lay_set_margins(&ctx, id, margins);
    }

    void SetTopMargin(LayID id, LayScalar val) {
        auto margins = lay_get_margins(&ctx, id);
        margins[1] = val;
        lay_set_margins(&ctx, id, margins);
    }

    void SetRightMargin(LayID id, LayScalar val) {
        auto margins = lay_get_margins(&ctx, id);
        margins[2] = val;
        lay_set_margins(&ctx, id, margins);
    }

    void SetBottomMargin(LayID id, LayScalar val) {
        auto margins = lay_get_margins(&ctx, id);
        margins[3] = val;
        lay_set_margins(&ctx, id, margins);
    }

    void PerformLayout() { lay_run_context(&ctx); }

    void Reserve(int size) { lay_reserve_items_capacity(&ctx, (uint32_t)size); }

    void Reset() { lay_reset_context(&ctx); }

    LayVec4 GetLayRect(LayID id) { return lay_get_rect(&ctx, id); }

    Rect GetRect(LayID id) {
        auto r = GetLayRect(id);
        return {(f32)r[0], (f32)r[1], (f32)r[2], (f32)r[3]};
    }
};

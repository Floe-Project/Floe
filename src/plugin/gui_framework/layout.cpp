// Copyright 2024 Sam Windell
// Copyright (c) 2016 Andrew Richards randrew@gmail.com
// Blendish - Blender 2.5 UI based theming functions for NanoVG
// Copyright (c) 2014 Leonard Ritter leonard.ritter@duangle.com
// SPDX-License-Identifier: MIT

#include "layout.hpp"

namespace layout {

void ReserveItemsCapacity(Context& ctx, Id count) {
    if (count >= ctx.capacity) {
        ctx.capacity = count;
        auto const item_size = sizeof(Item) + sizeof(f32x4);
        ctx.items = (Item*)GpaRealloc(ctx.items, ctx.capacity * item_size);
        auto const* past_last = ctx.items + ctx.capacity;
        ctx.rects = (f32x4*)past_last;
    }
}

void DestroyContext(Context& ctx) {
    if (ctx.items != nullptr) {
        GpaFree(ctx.items);
        ctx.items = nullptr;
        ctx.rects = nullptr;
    }
}

void ResetContext(Context& ctx) { ctx.num_items = 0; }

static void CalcSize(Context& ctx, Id item, int dim);
static void Arrange(Context& ctx, Id item, int dim);

void RunContext(Context& ctx) {
    if (ctx.num_items > 0) RunItem(ctx, 0);
}

void RunItem(Context& ctx, Id item) {
    CalcSize(ctx, item, 0);
    Arrange(ctx, item, 0);
    CalcSize(ctx, item, 1);
    Arrange(ctx, item, 1);
}

void ClearItemBreak(Context& ctx, Id item) {
    Item* pitem = GetItem(ctx, item);
    pitem->flags = pitem->flags & ~flags::LineBreak;
}

Id ItemsCount(Context& ctx) { return ctx.num_items; }

Id ItemsCapacity(Context& ctx) { return ctx.capacity; }

Id CreateItem(Context& ctx) {
    Id idx = ctx.num_items++;

    if (idx >= ctx.capacity) {
        ctx.capacity = ctx.capacity < 1 ? 32 : (ctx.capacity * 4);
        auto const item_size = sizeof(Item) + sizeof(f32x4);
        ctx.items = (Item*)GpaRealloc(ctx.items, ctx.capacity * item_size);
        auto const* past_last = ctx.items + ctx.capacity;
        ctx.rects = (f32x4*)past_last;
    }

    auto* item = GetItem(ctx, idx);
    // We can either do this here, or when creating/resetting buffer
    *item = {};
    item->first_child = k_invalid_id;
    item->next_sibling = k_invalid_id;
    // hmm
    ctx.rects[idx] = {};
    return idx;
}

static ALWAYS_INLINE void AppendByPtr(Item* __restrict pearlier, Id later, Item* __restrict plater) {
    plater->next_sibling = pearlier->next_sibling;
    plater->flags |= flags::ItemInserted;
    pearlier->next_sibling = later;
}

Id LastChild(Context const& ctx, Id parent) {
    auto pparent = GetItem(ctx, parent);
    auto child = pparent->first_child;
    if (child == k_invalid_id) return k_invalid_id;
    auto pchild = GetItem(ctx, child);
    auto result = child;
    for (;;) {
        auto next = pchild->next_sibling;
        if (next == k_invalid_id) break;
        result = next;
        pchild = GetItem(ctx, next);
    }
    return result;
}

void Append(Context& ctx, Id earlier, Id later) {
    ASSERT(later != 0); // Must not be root item
    ASSERT(earlier != later); // Must not be same item id
    Item* __restrict pearlier = GetItem(ctx, earlier);
    Item* __restrict plater = GetItem(ctx, later);
    AppendByPtr(pearlier, later, plater);
}

void Insert(Context& ctx, Id parent, Id child) {
    ASSERT(child != 0); // Must not be root item
    ASSERT(parent != child); // Must not be same item id
    Item* __restrict pparent = GetItem(ctx, parent);
    Item* __restrict pchild = GetItem(ctx, child);
    ASSERT(!(pchild->flags & flags::ItemInserted));
    if (pparent->first_child == k_invalid_id) {
        // Parent has no existing children, make inserted item the first child.
        pparent->first_child = child;
        pchild->flags |= flags::ItemInserted;
    } else {
        // Parent has existing items, iterate to find the last child and append the
        // inserted item after it.
        auto next = pparent->first_child;
        Item* __restrict pnext = GetItem(ctx, next);
        for (;;) {
            next = pnext->next_sibling;
            if (next == k_invalid_id) break;
            pnext = GetItem(ctx, next);
        }
        AppendByPtr(pnext, child, pchild);
    }
}

void Push(Context& ctx, Id parent, Id new_child) {
    ASSERT(new_child != 0); // Must not be root item
    ASSERT(parent != new_child); // Must not be same item id
    Item* __restrict pparent = GetItem(ctx, parent);
    auto old_child = pparent->first_child;
    Item* __restrict pchild = GetItem(ctx, new_child);
    ASSERT(!(pchild->flags & flags::ItemInserted));
    pparent->first_child = new_child;
    pchild->flags |= flags::ItemInserted;
    pchild->next_sibling = old_child;
}

// TODO restrict item ptrs correctly
static ALWAYS_INLINE f32 CalcOverlayedSize(Context& ctx, Id item, int dim) {
    auto const wdim = dim + 2;
    Item* __restrict pitem = GetItem(ctx, item);
    auto need_size = 0.0f;
    auto child = pitem->first_child;
    while (child != k_invalid_id) {
        auto pchild = GetItem(ctx, child);
        auto rect = ctx.rects[child];
        // width = start margin + calculated width + end margin
        auto child_size = rect[dim] + rect[2 + dim] + pchild->margins[wdim];
        need_size = Max(need_size, child_size);
        child = pchild->next_sibling;
    }
    return need_size;
}

static ALWAYS_INLINE f32 CalcStackedSize(Context& ctx, Id item, int dim) {
    auto const wdim = dim + 2;
    Item* __restrict pitem = GetItem(ctx, item);
    auto need_size = 0.0f;
    auto child = pitem->first_child;
    while (child != k_invalid_id) {
        auto pchild = GetItem(ctx, child);
        auto rect = ctx.rects[child];
        need_size += rect[dim] + rect[2 + dim] + pchild->margins[wdim];
        child = pchild->next_sibling;
    }
    return need_size;
}

static ALWAYS_INLINE f32 CalcWrappedOverlayedSize(Context& ctx, Id item, int dim) {
    auto const wdim = dim + 2;
    Item* __restrict pitem = GetItem(ctx, item);
    auto need_size = 0.0f;
    auto need_size2 = 0.0f;
    auto child = pitem->first_child;
    while (child != k_invalid_id) {
        auto pchild = GetItem(ctx, child);
        auto rect = ctx.rects[child];
        if (pchild->flags & flags::LineBreak) {
            need_size2 += need_size;
            need_size = 0;
        }
        auto child_size = rect[dim] + rect[2 + dim] + pchild->margins[wdim];
        need_size = Max(need_size, child_size);
        child = pchild->next_sibling;
    }
    return need_size2 + need_size;
}

// Equivalent to uiComputeWrappedStackedSize
static ALWAYS_INLINE f32 CalcWrappedStackedSize(Context& ctx, Id item, int dim) {
    auto const wdim = dim + 2;
    Item* __restrict pitem = GetItem(ctx, item);
    auto need_size = 0.0f;
    auto need_size2 = 0.0f;
    auto child = pitem->first_child;
    while (child != k_invalid_id) {
        auto pchild = GetItem(ctx, child);
        auto rect = ctx.rects[child];
        if (pchild->flags & flags::LineBreak) {
            need_size2 = Max(need_size2, need_size);
            need_size = 0;
        }
        need_size += rect[dim] + rect[2 + dim] + pchild->margins[wdim];
        child = pchild->next_sibling;
    }
    return Max(need_size2, need_size);
}

static void CalcSize(Context& ctx, Id item, int dim) {
    auto pitem = GetItem(ctx, item);

    auto child = pitem->first_child;
    while (child != k_invalid_id) {
        // NOTE: this is recursive and will run out of stack space if items are
        // nested too deeply.
        CalcSize(ctx, child, dim);
        auto pchild = GetItem(ctx, child);
        child = pchild->next_sibling;
    }

    // Set the mutable rect output data to the starting input data
    ctx.rects[item][dim] = pitem->margins[dim];

    // If we have an explicit input size, just set our output size (which other
    // calc_size and arrange procedures will use) to it.
    if (pitem->size[dim] != 0) {
        ctx.rects[item][2 + dim] = pitem->size[dim];
        return;
    }

    // Calculate our size based on children items. Note that we've already
    // called CalcSize on our children at this point.
    f32 cal_size;
    switch (pitem->flags & flags::ItemBoxModelMask) {
        case flags::Column | flags::Wrap:
            // flex model
            if (dim) // direction
                cal_size = CalcStackedSize(ctx, item, 1);
            else
                cal_size = CalcOverlayedSize(ctx, item, 0);
            break;
        case flags::Row | flags::Wrap:
            // flex model
            if (!dim) // direction
                cal_size = CalcWrappedStackedSize(ctx, item, 0);
            else
                cal_size = CalcWrappedOverlayedSize(ctx, item, 1);
            break;
        case flags::Column:
        case flags::Row:
            // flex model
            if ((pitem->flags & 1) == (u32)dim) // direction
                cal_size = CalcStackedSize(ctx, item, dim);
            else
                cal_size = CalcOverlayedSize(ctx, item, dim);
            break;
        default:
            // layout model
            cal_size = CalcOverlayedSize(ctx, item, dim);
            break;
    }

    // Set our output data size. Will be used by parent calc_size procedures.,
    // and by arrange procedures.
    ctx.rects[item][2 + dim] = cal_size;
}

static ALWAYS_INLINE void ArrangeStacked(Context& ctx, Id item, int dim, bool wrap) {
    auto const wdim = dim + 2;
    auto pitem = GetItem(ctx, item);

    auto const item_flags = pitem->flags;
    auto rect = ctx.rects[item];
    auto space = rect[2 + dim];

    auto max_x2 = rect[dim] + space;

    auto start_child = pitem->first_child;
    while (start_child != k_invalid_id) {
        f32 used = 0;
        u32 count = 0; // count of fillers
        [[maybe_unused]] u32 squeezed_count = 0; // count of squeezable elements
        u32 total = 0;
        bool hardbreak = false;
        // first pass: count items that need to be expanded,
        // and the space that is used
        auto child = start_child;
        auto end_child = k_invalid_id;
        while (child != k_invalid_id) {
            auto* pchild = GetItem(ctx, child);
            auto const child_flags = pchild->flags;
            auto const flags = (child_flags & flags::ItemLayoutMask) >> dim;
            auto const fflags = (child_flags & flags::ItemFixedMask) >> dim;
            auto const child_margins = pchild->margins;
            auto child_rect = ctx.rects[child];
            auto extend = used;
            if ((flags & flags::FillHorizontal) == flags::FillHorizontal) {
                ++count;
                extend += child_rect[dim] + child_margins[wdim];
            } else {
                if ((fflags & flags::ItemHorizontalFixed) != flags::ItemHorizontalFixed) ++squeezed_count;
                extend += child_rect[dim] + child_rect[2 + dim] + child_margins[wdim];
            }
            // wrap on end of line or manual flag
            if (wrap && (total && ((extend > space) || (child_flags & flags::LineBreak)))) {
                end_child = child;
                hardbreak = (child_flags & flags::LineBreak) == flags::LineBreak;
                // add marker for subsequent queries
                pchild->flags = child_flags | flags::LineBreak;
                break;
            } else {
                used = extend;
                child = pchild->next_sibling;
            }
            ++total;
        }

        auto extra_space = space - used;
        auto filler = 0.0f;
        auto spacer = 0.0f;
        auto extra_margin = 0.0f;

        if (extra_space > 0) {
            if (count > 0)
                filler = extra_space / (f32)count;
            else if (total > 0) {
                switch (item_flags & flags::Justify) {
                    case flags::Justify:
                        // justify when not wrapping or not in last line,
                        // or not manually breaking
                        if (!wrap || ((end_child != k_invalid_id) && !hardbreak))
                            spacer = extra_space / (f32)(total - 1);
                        break;
                    case flags::Start: break;
                    case flags::End: extra_margin = extra_space; break;
                    default: extra_margin = extra_space / 2.0f; break;
                }
            }
        }
        // In f32ing point, it's possible to end up with some small negative
        // value for extra_space, while also have a 0.0 squeezed_count. This
        // would cause divide by zero. Instead, we'll check to see if
        // squeezed_count is > 0. I believe this produces the same results as
        // the original oui int-only code. However, I don't have any tests for
        // it, so I'll leave it if-def'd for now.
        else if (!wrap && (squeezed_count > 0))
            ;

        // distribute width among items
        auto x = rect[dim];
        f32 x1;
        // second pass: distribute and rescale
        child = start_child;
        while (child != end_child) {
            f32 ix0;
            f32 ix1;
            auto* pchild = GetItem(ctx, child);
            auto const child_flags = pchild->flags;
            auto const flags = (child_flags & flags::ItemLayoutMask) >> dim;
            auto const fflags = (child_flags & flags::ItemFixedMask) >> dim;
            auto const child_margins = pchild->margins;
            auto child_rect = ctx.rects[child];

            x += child_rect[dim] + extra_margin;
            if ((flags & flags::FillHorizontal) == flags::FillHorizontal) // grow
                x1 = x + filler;
            else if ((fflags & flags::ItemHorizontalFixed) == flags::ItemHorizontalFixed)
                x1 = x + child_rect[2 + dim];
            else // squeeze
                x1 = x + Max(0.0f, child_rect[2 + dim] /* + eater */);

            // NOTE(Sam): I have removed the eater addition in the squeeze calculations,
            // so that when passing 0 as a width or height, the compenent will fit to the
            // size of it's children, even if it overruns the parent size. This is not fully
            // tested if it does what I expect all the time

            ix0 = x;
            if (wrap)
                ix1 = Min(max_x2 - child_margins[wdim], x1);
            else
                ix1 = x1;
            child_rect[dim] = ix0; // pos
            child_rect[dim + 2] = ix1 - ix0; // size
            ctx.rects[child] = child_rect;
            x = x1 + child_margins[wdim];
            child = pchild->next_sibling;
            extra_margin = spacer;
        }

        start_child = end_child;
    }
}

static ALWAYS_INLINE void ArrangeOverlay(Context& ctx, Id item, int dim) {
    auto const wdim = dim + 2;
    auto* pitem = GetItem(ctx, item);
    auto const rect = ctx.rects[item];
    auto const offset = rect[dim];
    auto const space = rect[2 + dim];

    auto child = pitem->first_child;
    while (child != k_invalid_id) {
        auto* pchild = GetItem(ctx, child);
        auto const b_flags = (pchild->flags & flags::ItemLayoutMask) >> dim;
        auto const child_margins = pchild->margins;
        auto child_rect = ctx.rects[child];

        switch (b_flags & flags::FillHorizontal) {
            case flags::CentreHorizontal:
                child_rect[dim] += (space - child_rect[2 + dim]) / 2 - child_margins[wdim];
                break;
            case flags::Right:
                child_rect[dim] += space - child_rect[2 + dim] - child_margins[dim] - child_margins[wdim];
                break;
            case flags::FillHorizontal:
                child_rect[2 + dim] = Max(0.0f, space - child_rect[dim] - child_margins[wdim]);
                break;
            default: break;
        }

        child_rect[dim] += offset;
        ctx.rects[child] = child_rect;
        child = pchild->next_sibling;
    }
}

static ALWAYS_INLINE void
ArrangeOverlaySqueezedRange(Context& ctx, int dim, Id start_item, Id end_item, f32 offset, f32 space) {
    auto wdim = dim + 2;
    auto item = start_item;
    while (item != end_item) {
        auto* pitem = GetItem(ctx, item);
        auto const b_flags = (pitem->flags & flags::ItemLayoutMask) >> dim;
        auto const margins = pitem->margins;
        auto rect = ctx.rects[item];
        auto min_size = Max(0.0f, space - rect[dim] - margins[wdim]);
        switch (b_flags & flags::FillHorizontal) {
            case flags::CentreHorizontal:
                rect[2 + dim] = Min(rect[2 + dim], min_size);
                rect[dim] += (space - rect[2 + dim]) / 2 - margins[wdim];
                break;
            case flags::Right:
                rect[2 + dim] = Min(rect[2 + dim], min_size);
                rect[dim] = space - rect[2 + dim] - margins[wdim];
                break;
            case flags::FillHorizontal: rect[2 + dim] = min_size; break;
            default: rect[2 + dim] = Min(rect[2 + dim], min_size); break;
        }
        rect[dim] += offset;
        ctx.rects[item] = rect;
        item = pitem->next_sibling;
    }
}

static ALWAYS_INLINE f32 ArrangeWrappedOverlaySqueezed(Context& ctx, Id item, int dim) {
    auto const wdim = dim + 2;
    auto* pitem = GetItem(ctx, item);
    auto offset = ctx.rects[item][dim];
    auto need_size = 0.0f;
    auto child = pitem->first_child;
    auto start_child = child;
    while (child != k_invalid_id) {
        Item* pchild = GetItem(ctx, child);
        if (pchild->flags & flags::LineBreak) {
            ArrangeOverlaySqueezedRange(ctx, dim, start_child, child, offset, need_size);
            offset += need_size;
            start_child = child;
            need_size = 0;
        }
        f32x4 const rect = ctx.rects[child];
        f32 child_size = rect[dim] + rect[2 + dim] + pchild->margins[wdim];
        need_size = Max(need_size, child_size);
        child = pchild->next_sibling;
    }
    ArrangeOverlaySqueezedRange(ctx, dim, start_child, k_invalid_id, offset, need_size);
    offset += need_size;
    return offset;
}

static void Arrange(Context& ctx, Id item, int dim) {
    auto* pitem = GetItem(ctx, item);

    auto const flags = pitem->flags;
    switch (flags & flags::ItemBoxModelMask) {
        case flags::Column | flags::Wrap:
            if (dim != 0) {
                ArrangeStacked(ctx, item, 1, true);
                f32 offset = ArrangeWrappedOverlaySqueezed(ctx, item, 0);
                ctx.rects[item][2 + 0] = offset - ctx.rects[item][0];
            }
            break;
        case flags::Row | flags::Wrap:
            if (dim == 0)
                ArrangeStacked(ctx, item, 0, true);
            else
                // discard return value
                ArrangeWrappedOverlaySqueezed(ctx, item, 1);
            break;
        case flags::Column:
        case flags::Row:
            if ((flags & 1) == (u32)dim) {
                ArrangeStacked(ctx, item, dim, false);
            } else {
                f32x4 const rect = ctx.rects[item];
                ArrangeOverlaySqueezedRange(ctx,
                                            dim,
                                            pitem->first_child,
                                            k_invalid_id,
                                            rect[dim],
                                            rect[2 + dim]);
            }
            break;
        default: ArrangeOverlay(ctx, item, dim); break;
    }
    auto child = pitem->first_child;
    while (child != k_invalid_id) {
        // NOTE: this is recursive and will run out of stack space if items are
        // nested too deeply.
        Arrange(ctx, child, dim);
        auto* pchild = GetItem(ctx, child);
        child = pchild->next_sibling;
    }
}

} // namespace layout

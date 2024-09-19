// Copyright 2024 Sam Windell
// Copyright (c) 2016 Andrew Richards randrew@gmail.com
// Blendish - Blender 2.5 UI based theming functions for NanoVG
// Copyright (c) 2014 Leonard Ritter leonard.ritter@duangle.com
// SPDX-License-Identifier: MIT

#include "layout.hpp"

#include "os/filesystem.hpp"
#include "tests/framework.hpp"

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

static void CalcSize(Context& ctx, Id item, u32 dim, f32 item_gap);
static void Arrange(Context& ctx, Id item, u32 dim);

void RunContext(Context& ctx) {
    if (ctx.num_items > 0) RunItem(ctx, 0);
}

void RunItem(Context& ctx, Id id) {
    CalcSize(ctx, id, 0, 0);
    Arrange(ctx, id, 0);
    CalcSize(ctx, id, 1, 0);
    Arrange(ctx, id, 1);
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

static ALWAYS_INLINE void AppendByPtr(Item* __restrict earlier, Id later_id, Item* __restrict later) {
    later->next_sibling = earlier->next_sibling;
    later->flags |= flags::ItemInserted;
    earlier->next_sibling = later_id;
}

Id LastChild(Context const& ctx, Id parent_id) {
    auto parent = GetItem(ctx, parent_id);
    auto child_id = parent->first_child;
    if (child_id == k_invalid_id) return k_invalid_id;
    auto child = GetItem(ctx, child_id);
    auto result = child_id;
    for (;;) {
        auto next = child->next_sibling;
        if (next == k_invalid_id) break;
        result = next;
        child = GetItem(ctx, next);
    }
    return result;
}

void Append(Context& ctx, Id earlier_id, Id later_id) {
    ASSERT(later_id != 0); // Must not be root item
    ASSERT(earlier_id != later_id); // Must not be same item id
    Item* __restrict earlier = GetItem(ctx, earlier_id);
    Item* __restrict later = GetItem(ctx, later_id);
    AppendByPtr(earlier, later_id, later);
}

void Insert(Context& ctx, Id parent_id, Id child_id) {
    ASSERT(child_id != 0); // Must not be root item
    ASSERT(parent_id != child_id); // Must not be same item id
    Item* __restrict parent = GetItem(ctx, parent_id);
    Item* __restrict child = GetItem(ctx, child_id);
    ASSERT(!(child->flags & flags::ItemInserted));
    if (parent->first_child == k_invalid_id) {
        // Parent has no existing children, make inserted item the first child.
        parent->first_child = child_id;
        child->flags |= flags::ItemInserted;
    } else {
        // Parent has existing items, iterate to find the last child and append the inserted item after it.
        auto next_id = parent->first_child;
        Item* __restrict next = GetItem(ctx, next_id);
        for (;;) {
            next_id = next->next_sibling;
            if (next_id == k_invalid_id) break;
            next = GetItem(ctx, next_id);
        }
        AppendByPtr(next, child_id, child);
    }
}

void Push(Context& ctx, Id parent_id, Id new_child_id) {
    ASSERT(new_child_id != 0); // Must not be root item
    ASSERT(parent_id != new_child_id); // Must not be same item id
    Item* __restrict parent = GetItem(ctx, parent_id);
    auto old_child = parent->first_child;
    Item* __restrict child = GetItem(ctx, new_child_id);
    ASSERT(!(child->flags & flags::ItemInserted));
    parent->first_child = new_child_id;
    child->flags |= flags::ItemInserted;
    child->next_sibling = old_child;
}

static ALWAYS_INLINE f32 MaxChildSize(Context& ctx, Id id, u32 dim) {
    auto const size_dim = dim + 2;
    Item const* __restrict item = GetItem(ctx, id);
    auto max_child_size = 0.0f;
    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        auto const child = GetItem(ctx, child_id);
        // rect[dim] will contain the start margin already
        auto const rect = ctx.rects[child_id];
        auto const child_size = rect[dim] + rect[size_dim] + child->margins_ltrb[size_dim];
        max_child_size = Max(max_child_size, child_size);
        child_id = child->next_sibling;
    }
    return max_child_size;
}

static ALWAYS_INLINE f32 TotalChildSize(Context& ctx, Id id, u32 dim) {
    auto const size_dim = dim + 2;
    Item const* __restrict item = GetItem(ctx, id);
    auto need_size = 0.0f;
    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        auto const child = GetItem(ctx, child_id);
        auto const rect = ctx.rects[child_id];
        // rect[dim] will contain the start margin already
        need_size += rect[dim] + rect[size_dim] + child->margins_ltrb[size_dim];
        child_id = child->next_sibling;
    }
    return need_size;
}

static ALWAYS_INLINE f32 MaxChildSizeWrapped(Context& ctx, Id id, u32 dim) {
    auto const size_dim = dim + 2;
    Item const* __restrict item = GetItem(ctx, id);
    auto max_child_size = 0.0f;
    auto max_child_size2 = 0.0f;
    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        auto const child = GetItem(ctx, child_id);
        auto const rect = ctx.rects[child_id];
        if (child->flags & flags::LineBreak) {
            max_child_size2 += max_child_size;
            max_child_size = 0;
        }
        auto const child_size = rect[dim] + rect[size_dim] + child->margins_ltrb[size_dim];
        max_child_size = Max(max_child_size, child_size);
        child_id = child->next_sibling;
    }
    return max_child_size2 + max_child_size;
}

static ALWAYS_INLINE f32 TotalChildSizeWrapped(Context& ctx, Id id, u32 dim) {
    auto const size_dim = dim + 2;
    Item const* __restrict item = GetItem(ctx, id);
    auto max_child_size = 0.0f;
    auto max_child_size2 = 0.0f;
    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        auto const child = GetItem(ctx, child_id);
        auto const rect = ctx.rects[child_id];
        if (child->flags & flags::LineBreak) {
            max_child_size2 = Max(max_child_size2, max_child_size);
            max_child_size = 0;
        }
        max_child_size += rect[dim] + rect[size_dim] + child->margins_ltrb[size_dim];
        child_id = child->next_sibling;
    }
    return Max(max_child_size2, max_child_size);
}

static void CalcSize(Context& ctx, Id id, u32 dim, f32 item_gap) {
    auto item = GetItem(ctx, id);

    auto const item_layout_dim = item->flags & 1;

    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        // NOTE: this is recursive and will run out of stack space if items are nested too deeply.
        CalcSize(ctx, child_id, dim, dim == item_layout_dim ? item->gap[dim] : 0);
        auto child = GetItem(ctx, child_id);
        child_id = child->next_sibling;
    }

    if (item->next_sibling != k_invalid_id) item->margins_ltrb[dim + 2] += item_gap;

    // Set the mutable rect output data to the starting input data
    ctx.rects[id][dim] = item->margins_ltrb[dim];

    // If we have an explicit input size, just set our output size (which other calc_size and arrange
    // procedures will use) to it.
    if (item->size[dim] != 0) {
        ctx.rects[id][2 + dim] = item->size[dim];
        return;
    }

    // Calculate our size based on children items. Note that we've already called CalcSize on our children at
    // this point.
    f32 cal_size;
    switch (item->flags & flags::LayoutModeMask) {
        case flags::Column | flags::Wrap:
            if (dim) // direction
                cal_size = TotalChildSize(ctx, id, 1);
            else
                cal_size = MaxChildSize(ctx, id, 0);
            break;
        case flags::Row | flags::Wrap:
            if (!dim) // direction
                cal_size = TotalChildSizeWrapped(ctx, id, 0);
            else
                cal_size = MaxChildSizeWrapped(ctx, id, 1);
            break;
        case flags::Column:
        case flags::Row:
            if (item_layout_dim == dim) // direction
                cal_size = TotalChildSize(ctx, id, dim);
            else
                cal_size = MaxChildSize(ctx, id, dim);
            break;
        default:
            // NoLayout
            cal_size = MaxChildSize(ctx, id, dim);
            break;
    }

    // Set our output data size. Will be used by parent calc_size procedures., and by arrange procedures.
    ctx.rects[id][2 + dim] = cal_size;
}

static ALWAYS_INLINE void ArrangeStacked(Context& ctx, Id id, u32 dim, bool wrap) {
    auto const size_dim = dim + 2;
    auto item = GetItem(ctx, id);

    auto const item_flags = item->flags;
    auto rect = ctx.rects[id];
    auto space = rect[2 + dim];

    auto max_x2 = rect[dim] + space;

    auto start_child_id = item->first_child;
    while (start_child_id != k_invalid_id) {
        f32 used = 0;
        u32 count = 0; // count of fillers
        [[maybe_unused]] u32 squeezed_count = 0; // count of squeezable elements
        u32 total = 0;
        bool hardbreak = false;
        // first pass: count items that need to be expanded, and the space that is used
        auto child_id = start_child_id;
        auto end_child_id = k_invalid_id;
        while (child_id != k_invalid_id) {
            auto* child = GetItem(ctx, child_id);
            auto const child_flags = child->flags;
            auto const behaviour_flags = (child_flags & flags::ChildBehaviourMask) >> dim;
            auto const fixed_size_flags = (child_flags & flags::FixedSizeMask) >> dim;
            auto const child_margins = child->margins_ltrb;
            auto child_rect = ctx.rects[child_id];
            auto extend = used;
            if ((behaviour_flags & flags::AnchorLeftAndRight) == flags::AnchorLeftAndRight) {
                ++count;
                extend += child_rect[dim] + child_margins[size_dim];
            } else {
                if ((fixed_size_flags & flags::HorizontalSizeFixed) != flags::HorizontalSizeFixed)
                    ++squeezed_count;
                extend += child_rect[dim] + child_rect[2 + dim] + child_margins[size_dim];
            }
            // wrap on end of line or manual flag
            if (wrap && (total && ((extend > space) || (child_flags & flags::LineBreak)))) {
                end_child_id = child_id;
                hardbreak = (child_flags & flags::LineBreak) == flags::LineBreak;
                // add marker for subsequent queries
                child->flags = child_flags | flags::LineBreak;
                break;
            } else {
                used = extend;
                child_id = child->next_sibling;
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
                        // justify when not wrapping or not in last line, or not manually breaking
                        if (!wrap || ((end_child_id != k_invalid_id) && !hardbreak))
                            spacer = extra_space / (f32)(total - 1);
                        break;
                    case flags::Start: break;
                    case flags::End: extra_margin = extra_space; break;
                    default: extra_margin = extra_space / 2.0f; break;
                }
            }
        }

        // distribute width among items
        auto x = rect[dim];
        f32 x1;
        // second pass: distribute and rescale
        child_id = start_child_id;
        while (child_id != end_child_id) {
            f32 ix0;
            f32 ix1;
            auto* child = GetItem(ctx, child_id);
            auto const child_flags = child->flags;
            auto const behaviour_flags = (child_flags & flags::ChildBehaviourMask) >> dim;
            auto const fixed_size_flags = (child_flags & flags::FixedSizeMask) >> dim;
            auto const child_margins = child->margins_ltrb;
            auto child_rect = ctx.rects[child_id];

            x += child_rect[dim] + extra_margin;
            if ((behaviour_flags & flags::AnchorLeftAndRight) == flags::AnchorLeftAndRight) // grow
                x1 = x + filler;
            else if ((fixed_size_flags & flags::HorizontalSizeFixed) == flags::HorizontalSizeFixed)
                x1 = x + child_rect[2 + dim];
            else // squeeze
                // NOTE(Sam): I have removed the eater addition in the squeeze calculations, so that when
                // passing 0 as a width or height, the compenent will fit to the size of it's children, even
                // if it overruns the parent size. This is not fully tested if it does what I expect all the
                // time
                x1 = x + Max(0.0f, child_rect[2 + dim] /* + eater */);

            ix0 = x;
            if (wrap)
                ix1 = Min(max_x2 - child_margins[size_dim], x1);
            else
                ix1 = x1;
            child_rect[dim] = ix0; // pos
            child_rect[dim + 2] = ix1 - ix0; // size
            ctx.rects[child_id] = child_rect;
            x = x1 + child_margins[size_dim];
            child_id = child->next_sibling;
            extra_margin = spacer;
        }

        start_child_id = end_child_id;
    }
}

static ALWAYS_INLINE void ArrangeOverlay(Context& ctx, Id id, u32 dim) {
    auto const size_dim = dim + 2;
    auto* item = GetItem(ctx, id);
    auto const rect = ctx.rects[id];
    auto const offset = rect[dim];
    auto const space = rect[2 + dim];

    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        auto* child = GetItem(ctx, child_id);
        auto const behaviour_flags = (child->flags & flags::ChildBehaviourMask) >> dim;
        auto const child_margins = child->margins_ltrb;
        auto child_rect = ctx.rects[child_id];

        switch (behaviour_flags & flags::AnchorLeftAndRight) {
            case flags::CentreHorizontal:
                child_rect[dim] += (space - child_rect[2 + dim]) / 2 - child_margins[size_dim];
                break;
            case flags::AnchorRight:
                child_rect[dim] += space - child_rect[2 + dim] - child_margins[dim] - child_margins[size_dim];
                break;
            case flags::AnchorLeftAndRight:
                child_rect[2 + dim] = Max(0.0f, space - child_rect[dim] - child_margins[size_dim]);
                break;
            default: break;
        }

        child_rect[dim] += offset;
        ctx.rects[child_id] = child_rect;
        child_id = child->next_sibling;
    }
}

static ALWAYS_INLINE void
ArrangeOverlaySqueezedRange(Context& ctx, u32 dim, Id start_item_id, Id end_item_id, f32 offset, f32 space) {
    auto size_dim = dim + 2;
    auto item_id = start_item_id;
    while (item_id != end_item_id) {
        auto* item = GetItem(ctx, item_id);
        auto const behaviour_flags = (item->flags & flags::ChildBehaviourMask) >> dim;
        auto const margins = item->margins_ltrb;
        auto rect = ctx.rects[item_id];
        auto min_size = Max(0.0f, space - rect[dim] - margins[size_dim]);
        switch (behaviour_flags & flags::AnchorLeftAndRight) {
            case flags::CentreHorizontal:
                rect[2 + dim] = Min(rect[2 + dim], min_size);
                rect[dim] += (space - rect[2 + dim]) / 2 - margins[size_dim];
                break;
            case flags::AnchorRight:
                rect[2 + dim] = Min(rect[2 + dim], min_size);
                rect[dim] = space - rect[2 + dim] - margins[size_dim];
                break;
            case flags::AnchorLeftAndRight: rect[2 + dim] = min_size; break;
            default: rect[2 + dim] = Min(rect[2 + dim], min_size); break;
        }
        rect[dim] += offset;
        ctx.rects[item_id] = rect;
        item_id = item->next_sibling;
    }
}

static ALWAYS_INLINE f32 ArrangeWrappedOverlaySqueezed(Context& ctx, Id id, u32 dim) {
    auto const size_dim = dim + 2;
    auto* item = GetItem(ctx, id);
    auto offset = ctx.rects[id][dim];
    auto need_size = 0.0f;
    auto child_id = item->first_child;
    auto start_child_id = child_id;
    while (child_id != k_invalid_id) {
        Item* child = GetItem(ctx, child_id);
        if (child->flags & flags::LineBreak) {
            ArrangeOverlaySqueezedRange(ctx, dim, start_child_id, child_id, offset, need_size);
            offset += need_size;
            start_child_id = child_id;
            need_size = 0;
        }
        f32x4 const rect = ctx.rects[child_id];
        f32 child_size = rect[dim] + rect[2 + dim] + child->margins_ltrb[size_dim];
        need_size = Max(need_size, child_size);
        child_id = child->next_sibling;
    }
    ArrangeOverlaySqueezedRange(ctx, dim, start_child_id, k_invalid_id, offset, need_size);
    offset += need_size;
    return offset;
}

static void Arrange(Context& ctx, Id id, u32 dim) {
    auto* item = GetItem(ctx, id);

    auto const flags = item->flags;
    switch (flags & flags::LayoutModeMask) {
        case flags::Column | flags::Wrap:
            if (dim != 0) {
                ArrangeStacked(ctx, id, 1, true);
                f32 offset = ArrangeWrappedOverlaySqueezed(ctx, id, 0);
                ctx.rects[id][2 + 0] = offset - ctx.rects[id][0];
            }
            break;
        case flags::Row | flags::Wrap:
            if (dim == 0)
                ArrangeStacked(ctx, id, 0, true);
            else
                // discard return value
                ArrangeWrappedOverlaySqueezed(ctx, id, 1);
            break;
        case flags::Column:
        case flags::Row:
            if ((flags & 1) == dim) {
                ArrangeStacked(ctx, id, dim, false);
            } else {
                f32x4 const rect = ctx.rects[id];
                ArrangeOverlaySqueezedRange(ctx,
                                            dim,
                                            item->first_child,
                                            k_invalid_id,
                                            rect[dim],
                                            rect[2 + dim]);
            }
            break;
        default: ArrangeOverlay(ctx, id, dim); break;
    }
    auto child_id = item->first_child;
    while (child_id != k_invalid_id) {
        // NOTE: this is recursive and will run out of stack space if items are nested too deeply.
        Arrange(ctx, child_id, dim);
        auto* child = GetItem(ctx, child_id);
        child_id = child->next_sibling;
    }
}

} // namespace layout

struct Bitmap {
    u32 width;
    u32 height;
    u8* rgba;
};

ErrorCodeOr<void> WriteTga(File& file, Bitmap bitmap) {
    // TGA wants bgra instead of rgba
    for (usize i = 0; i < (size_t)bitmap.width * (size_t)bitmap.height; i++)
        Swap(bitmap.rgba[4 * i + 0], bitmap.rgba[4 * i + 2]);

    constexpr u8 k_uncompressed_true_color_image = 2;
    constexpr u8 k_bits_per_pixel = 32;
    u8 tga_header[] = {
        0,
        0,
        k_uncompressed_true_color_image,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        (u8)(bitmap.width & 0xFF),
        (u8)((bitmap.width >> 8) & 0xFF),
        (u8)(bitmap.height & 0xFF),
        (u8)((bitmap.height >> 8) & 0xFF),
        k_bits_per_pixel,
        8 | 0x20, // 32-bit image with top-left origin
    };
    TRY(file.Write({tga_header, sizeof(tga_header)}));
    TRY(file.Write({bitmap.rgba, 4 * bitmap.width * bitmap.height}));
    return k_success;
}

static void FillRect(Bitmap bitmap, Rect rect, u32 rgb_hex_colour) {
    auto const x = (u32)rect.x;
    auto const y = (u32)rect.y;
    auto const width = (u32)rect.w;
    auto const height = (u32)rect.h;

    u8* rgba = bitmap.rgba;
    auto r = (u8)((rgb_hex_colour >> 16) & 0xFF);
    auto g = (u8)((rgb_hex_colour >> 8) & 0xFF);
    auto b = (u8)(rgb_hex_colour & 0xFF);
    for (u32 j = 0; j < height; ++j) {
        for (u32 i = 0; i < width; ++i) {
            auto const index = 4 * ((y + j) * bitmap.width + (x + i));
            ASSERT(index + 3 < 4 * bitmap.width * bitmap.height);
            rgba[index + 0] = r;
            rgba[index + 1] = g;
            rgba[index + 2] = b;
            rgba[index + 3] = 0xFF;
        }
    }
}

enum Colours : u32 {
    Rosewater = 0xdc8a78,
    Flamingo = 0xdd7878,
    Pink = 0xea76cb,
    Mauve = 0x8839ef,
    Red = 0xd20f39,
    Maroon = 0xe64553,
    Peach = 0xfe640b,
    Yellow = 0xdf8e1d,
    Green = 0x40a02b,
    Teal = 0x179299,
    Sky = 0x04a5e5,
    Sapphire = 0x209fb5,
    Blue = 0x1e66f5,
    Lavender = 0x7287fd,
    Text = 0x4c4f69,
    Subtext1 = 0x5c5f77,
    Subtext0 = 0x6c6f85,
    Overlay2 = 0x7c7f93,
    Overlay1 = 0x8c8fa1,
    Overlay0 = 0x9ca0b0,
    Surface2 = 0xacb0be,
    Surface1 = 0xbcc0cc,
    Surface0 = 0xccd0da,
    Base = 0xeff1f5,
    Mantle = 0xe6e9ef,
    Crust = 0xdce0e8,
};

struct LayoutImageArgs {
    layout::ItemOptions root_options;
    Array<layout::ItemOptions, 3> child_options;
};

// TODO: use SVG instead of TGA, it will be quicker, smaller and we can easily combine images into a single
// document with text labels.
static ErrorCodeOr<void>
GenerateLayoutImage(String filename, ArenaAllocator& arena, String folder, LayoutImageArgs args) {
    layout::Context ctx;
    DEFER { layout::DestroyContext(ctx); };

    auto root = layout::CreateItem(ctx, args.root_options);
    for (auto& child_options : args.child_options)
        child_options.parent = root;

    Array<layout::Id, args.child_options.size> children;
    for (auto const i : Range(children.size))
        children[i] = layout::CreateItem(ctx, args.child_options[i]);

    layout::RunContext(ctx);

    Bitmap bitmap {
        .width = CheckedCast<u32>(args.root_options.size.x),
        .height = CheckedCast<u32>(args.root_options.size.y),
    };
    bitmap.rgba = arena.AllocateExactSizeUninitialised<u8>(4 * bitmap.width * bitmap.height).data;
    ZeroMemory(bitmap.rgba, 4 * bitmap.width * bitmap.height);
    FillRect(bitmap, layout::GetRect(ctx, root), Base);

    auto const colours = Array {Red, Green, Blue, Yellow, Peach, Pink, Mauve, Flamingo, Rosewater};
    for (auto const i : Range(children.size))
        FillRect(bitmap, layout::GetRect(ctx, children[i]), colours[i]);

    auto f = TRY(OpenFile(CombineStrings(arena, Array {folder, path::k_dir_separator_str, filename, ".tga"}),
                          FileMode::Write));
    TRY(WriteTga(f, bitmap));

    return k_success;
}

static String DirectionName(layout::Direction direction) {
    switch (direction) {
        case layout::Direction::Row: return "row";
        case layout::Direction::Column: return "column";
    }
    PanicIfReached();
    return {};
}

static String JustifyContentName(layout::JustifyContent j) {
    switch (j) {
        case layout::JustifyContent::Start: return "start";
        case layout::JustifyContent::Middle: return "middle";
        case layout::JustifyContent::End: return "end";
        case layout::JustifyContent::Justify: return "justify";
    }
    PanicIfReached();
    return {};
}

static String AnchorName(layout::Anchor a) {
    switch (a) {
        case layout::Anchor::None: return "none";
        case layout::Anchor::Left: return "left";
        case layout::Anchor::Right: return "right";
        case layout::Anchor::Top: return "top";
        case layout::Anchor::Bottom: return "bottom";
    }
    PanicIfReached();
    return {};
}

TEST_CASE(TestLayout) {
    using namespace layout;

    auto const output_dir = path::Join(
        tester.arena,
        Array {String(KnownDirectory(tester.arena, KnownDirectoryType::UserData, {.create = true})),
               "Floe",
               "layout-tests"});
    TRY(CreateDirectory(output_dir, {.create_intermediate_directories = true}));
    tester.log.Info({}, "Layout output directory: {}", output_dir);

    auto const basic_child = layout::ItemOptions {
        .size = 20,
    };

    for (auto const contents_direction : Array {Direction::Row, Direction::Column}) {
        for (auto const contents_align : Array {JustifyContent::Start,
                                                JustifyContent::Middle,
                                                JustifyContent::End,
                                                JustifyContent::Justify}) {
            for (auto const middle_item_anchor :
                 Array {Anchor::None, Anchor::Left, Anchor::Right, Anchor::Top, Anchor::Bottom}) {
                auto const filename = fmt::Format(tester.arena,
                                                  "{}-{}-{}",
                                                  DirectionName(contents_direction),
                                                  JustifyContentName(contents_align),
                                                  AnchorName(middle_item_anchor));
                LayoutImageArgs args {
                    .root_options =
                        {
                            .size = 128,
                            .gap = 8,
                            .contents_direction = contents_direction,
                            .contents_align = contents_align,
                        },
                    .child_options = {basic_child, basic_child, basic_child},
                };
                args.child_options[1].anchor = middle_item_anchor;
                TRY(GenerateLayoutImage(filename, tester.arena, output_dir, args));
            }
        }
    }

    TRY(GenerateLayoutImage(
        "0-flags",
        tester.arena,
        output_dir,
        {
            .root_options =
                {
                    .size = 128,
                    .anchor = Anchor(0),
                    .contents_direction = Direction(0),
                    .contents_align = JustifyContent(0),
                },
            .child_options =
                {
                    ItemOptions {.size = 20, .anchor = Anchor::Top},
                    ItemOptions {.size = 20, .anchor = Anchor::Top | Anchor::Bottom | Anchor::Left},
                    ItemOptions {.size = 20, .anchor = Anchor::Bottom | Anchor::Left | Anchor::Right},
                },
        }));

    return k_success;
}

TEST_REGISTRATION(RegisterLayoutTests) { REGISTER_TEST(TestLayout); }

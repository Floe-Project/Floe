// Copyright 2024 Sam Windell
// Copyright (c) 2016 Andrew Richards randrew@gmail.com
// Blendish - Blender 2.5 UI based theming functions for NanoVG
// Copyright (c) 2014 Leonard Ritter leonard.ritter@duangle.com
// SPDX-License-Identifier: MIT

// Layout - Simple 2D stacking boxes calculations
// Customised for Floe:
// - Standardised style
// - C++
// - Remove config options and just use what we need
// - Add a higher-level API for creating items

#pragma once
#include "foundation/foundation.hpp"

namespace layout {

using Id = u32;

constexpr Id k_invalid_id = ~(u32)0;

struct Item {
    u32 flags;
    Id first_child;
    Id next_sibling;
    f32x4 margins;
    f32x2 size;
};

struct Context {
    Item* items {};
    f32x4* rects {};
    Id capacity {};
    Id num_items {};
};

namespace flags {

// Container flags to pass to SetContainer()
enum : u32 {
    // flex-direction (bit 0+1)
    Row = 0x002, // left to right
    Column = 0x003, // top to bottom

    // model (bit 1)
    FreeLayout = 0x000, // free layout
    Flex = 0x002, // flex model

    // flex-wrap (bit 2)
    NoWrap = 0x000, // single-line
    Wrap = 0x004, // multi-line, wrap left to right

    // justify-content (start, end, center, space-between)
    Start = 0x008, // at start of row/column
    Middle = 0x000, // at center of row/column
    End = 0x010, // at end of row/column
    Justify = Start | End, // insert spacing to stretch across whole row/column

    // CSS align-items can be implemented by putting a flex container in a layout container, then using
    // LAY_TOP, LAY_BOTTOM, LAY_VFILL, LAY_VCENTER, etc. FILL is equivalent to stretch/grow

    // CSS align-content (start, end, center, stretch) can be implemented by putting a flex container in a
    // layout container, then using LAY_TOP, LAY_BOTTOM, LAY_VFILL, LAY_VCENTER, etc. FILL is equivalent to
    // stretch; space-between is not supported.
};

// child layout flags to pass to SetBehave()
enum : u32 {
    // attachments (bit 5-8)
    // fully valid when parent uses LAY_LAYOUT model
    // partially valid when in LAY_FLEX model

    CentreHorizontal = 0x000, // center horizontally, with left margin as offset
    CentreVertical = 0x000, // center vertically, with top margin as offset
    Centre = 0x000, // center in both directions, with left/top margin as offset

    Left = 0x020, // anchor to left item or left side of parent
    Top = 0x040, // anchor to top item or top side of parent
    Right = 0x080, // anchor to right item or right side of parent
    Bottom = 0x100, // anchor to bottom item or bottom side of parent
                    //
    FillHorizontal = Left | Right, // anchor to both left and right item or parent borders
    FillVertical = Top | Bottom, // anchor to both top and bottom item or parent borders
    Fill = Left | Top | Right | Bottom, // anchor to all four directions

    // When in a wrapping container, put this element on a new line. Wrapping layout code auto-inserts
    // LAY_BREAK flags as needed.
    //
    // Drawing routines can read this via item pointers as needed after performing layout calculations.
    LineBreak = 0x200
};

enum : u32 {
    // these bits, starting at bit 16, can be safely assigned by the application, e.g. as item types, other
    // event types, drop targets, etc. this is not yet exposed via API functions, you'll need to get/set these
    // by directly accessing item pointers.
    UserMask = 0x7fff0000,
};

// extra item flags
enum : u32 {
    ItemBoxModelMask = 0x000007, // bit 0-2
    ItemBoxMask = 0x00001F, // bit 0-4
    ItemLayoutMask = 0x0003E0, // bit 5-9
    ItemInserted = 0x400, // item has been inserted (bit 10)
    ItemHorizontalFixed = 0x800, // horizontal size has been explicitly set (bit 11)
    ItemVerticalFixed = 0x1000, // vertical size has been explicitly set (bit 12)

    ItemFixedMask = ItemHorizontalFixed | ItemVerticalFixed,

    // which flag bits will be compared
    ItemCompareMask = ItemBoxModelMask | (ItemLayoutMask & ~LineBreak) | UserMask,
};

} // namespace flags

// Reserve enough heap memory to contain `count` items without needing to reallocate. The initial
// InitContext() call does not allocate any heap memory, so if you init a context and then call this once with
// a large enough number for the number of items you'll create, there will not be any further reallocations.
void ReserveItemsCapacity(Context& ctx, Id count);

// Frees any heap allocated memory used by a context. Don't call this on a context that did not have
// InitContext() call on it. To reuse a context after destroying it, you will need to set it to {}.
void DestroyContext(Context& ctx);

// Clears all of the items in a context, setting its count to 0. Use this when you want to re-declare your
// layout starting from the root item. This does not free any memory or perform allocations. It's safe to use
// the context again after calling this. You should probably use this instead of init/destroy if you are
// recalculating your layouts in a loop.
void ResetContext(Context& ctx);

// Performs the layout calculations, starting at the root item (id 0). After calling this, you can use
// GetRect() to query for an item's calculated rectangle. If you use procedures such as Append() or Insert()
// after calling this, your calculated data may become invalid if a reallocation occurs.
//
// You should prefer to recreate your items starting from the root instead of doing fine-grained updates to
// the existing context.
//
// However, it's safe to use SetSize on an item, and then re-run RunContext. This might be useful if you are
// doing a resizing animation on items in a layout without any contents changing.
void RunContext(Context& ctx);

// Like RunContext(), this procedure will run layout calculations -- however, it lets you specify which item
// you want to start from. RunContext() always starts with item 0, the first item, as the root. Running the
// layout calculations from a specific item is useful if you want need to iteratively re-run parts of your
// layout hierarchy, or if you are only interested in updating certain subsets of it. Be careful when using
// this -- it's easy to generated bad output if the parent items haven't yet had their output rectangles
// calculated, or if they've been invalidated (e.g. due to re-allocation).
void RunItem(Context& ctx, Id item);

// Performing a layout on items where wrapping is enabled in the parent container can cause flags to be
// modified during the calculations. If you plan to call RunContext or RunItem multiple times without calling
// Reset, and if you have a container that uses wrapping, and if the width or height of the container may have
// changed, you should call ClearItemBreak on all of the children of a container before calling RunContext or
// RunItem again. If you don't, the layout calculations may perform unnecessary wrapping.
//
// This requirement may be changed in the future.
//
//
// Calling this will also reset any manually-specified breaking. You will need to set the manual breaking
// again, or simply not call this on any items that you know you wanted to break manually.
//
// If you clear your context every time you calculate your layout, or if you don't use wrapping, you don't
// need to call this.
void ClearItemBreak(Context& ctx, Id item);

// Returns the number of items that have been created in a context.
Id ItemsCount(Context& ctx);

// Returns the number of items the context can hold without performing a reallocation.
Id ItemsCapacity(Context& ctx);

// Create a new item, which can just be thought of as a rectangle. Returns the id (handle) used to identify
// the item.
Id CreateItem(Context& ctx);

// Inserts an item into another item, forming a parent - child relationship. An item can contain any number of
// child items. Items inserted into a parent are put at the end of the ordering, after any existing siblings.
void Insert(Context& ctx, Id parent, Id child);

// Append inserts an item as a sibling after another item. This allows inserting an item into the middle of an
// existing list of items within a parent. It's also more efficient than repeatedly using Insert(ctx, parent,
// new_child) in a loop to create a list of items in a parent, because it does not need to traverse the
// parent's children each time. So if you're creating a long list of children inside of a parent, you might
// prefer to use this after using Insert to insert the first child.
void Append(Context& ctx, Id earlier, Id later);

// Like Insert, but puts the new item as the first child in a parent instead of as the last.
void Push(Context& ctx, Id parent, Id child);

// Get the pointer to an item in the buffer by its id. Don't keep this around -- it will become invalid as
// soon as any reallocation occurs. Just store the id instead (it's smaller, anyway, and the lookup cost will
// be nothing.)
ALWAYS_INLINE inline Item* GetItem(Context const& ctx, Id id) {
    ASSERT(id != k_invalid_id && id < ctx.num_items);
    return ctx.items + id;
}

// Get the id of first child of an item, if any. Returns k_invalid_id if there is no child.
ALWAYS_INLINE inline Id FirstChild(Context const& ctx, Id id) {
    Item const* pitem = GetItem(ctx, id);
    return pitem->first_child;
}

// Get the id of the next sibling of an item, if any. Returns k_invalid_id if there is no next sibling.
ALWAYS_INLINE inline Id NextSibling(Context const& ctx, Id id) {
    Item const* pitem = GetItem(ctx, id);
    return pitem->next_sibling;
}

// Returns the calculated rectangle of an item. This is only valid after calling RunContext and before any
// other reallocation occurs. Otherwise, the result will be undefined. The vector components are: 0: x
// starting position, 1: y starting position 2: width, 3: height
ALWAYS_INLINE inline f32x4 GetRectXywh(Context const& ctx, Id id) {
    ASSERT(id != k_invalid_id && id < ctx.num_items);
    return ctx.rects[id];
}
ALWAYS_INLINE inline Rect GetRect(Context const& ctx, Id id) {
    auto const xywh = GetRectXywh(ctx, id);
    return {.xywh = xywh};
}

ALWAYS_INLINE inline f32x2 GetSize(Context& ctx, Id item) { return GetItem(ctx, item)->size; }

ALWAYS_INLINE inline void SetItemSize(Item& item, f32x2 size) {
    item.size = size;
    u32 flags = item.flags;
    if (size[0] == 0)
        flags &= ~flags::ItemHorizontalFixed;
    else
        flags |= flags::ItemHorizontalFixed;
    if (size[1] == 0)
        flags &= ~flags::ItemVerticalFixed;
    else
        flags |= flags::ItemVerticalFixed;
    item.flags = flags;
}
ALWAYS_INLINE inline void SetSize(Context& ctx, Id id, f32x2 size) { SetItemSize(*GetItem(ctx, id), size); }

// Set the flags on an item which determines how it behaves as a child inside of a parent item. For example,
// setting LAY_VFILL will make an item try to fill up all available vertical space inside of its parent.
ALWAYS_INLINE inline void SetBehave(Item& item, u32 flags) {
    ASSERT((flags & flags::ItemLayoutMask) == flags);
    item.flags = (item.flags & ~flags::ItemLayoutMask) | flags;
}
ALWAYS_INLINE inline void SetBehave(Context& ctx, Id id, u32 flags) { SetBehave(*GetItem(ctx, id), flags); }

// Set the flags on an item which determines how it behaves as a parent. For example, setting LAY_COLUMN will
// make an item behave as if it were a column -- it will lay out its children vertically.
ALWAYS_INLINE inline void SetContain(Item& item, u32 flags) {
    ASSERT((flags & flags::ItemBoxMask) == flags);
    item.flags = (item.flags & ~flags::ItemBoxMask) | flags;
}
ALWAYS_INLINE inline void SetContain(Context& ctx, Id id, u32 flags) { SetContain(*GetItem(ctx, id), flags); }

// Set the margins on an item. The components of the vector are:
// 0: left, 1: top, 2: right, 3: bottom.
ALWAYS_INLINE inline void SetMargins(Item& item, f32x4 ltrb) { item.margins = ltrb; }
ALWAYS_INLINE inline void SetMargins(Context& ctx, Id item, f32x4 ltrb) {
    SetMargins(*GetItem(ctx, item), ltrb);
}

// Get the margins that were set by SetMargins.
// 0: left, 1: top, 2: right, 3: bottom.
ALWAYS_INLINE inline f32x4& GetMarginsLtrb(Context& ctx, Id item) { return GetItem(ctx, item)->margins; }

// =========================================================================================================
// Higher-level API focused on creating items using designated initialisers
// =========================================================================================================

// Lots of unions/structs here so that designated initialiser can be used really effectively to just set the
// value you want
struct Margins {
    union {
        struct {
            union {
                struct {
                    f32 l;
                    f32 r;
                };
                f32x2 lr;
            };
            union {
                struct {
                    f32 t;
                    f32 b;
                };
                f32x2 tb;
            };
        };
        f32x4 lrtb {};
    };
};

enum class Anchor : u16 {
    None = 0, // no anchor, item will be in the centre
    Left = flags::Left,
    Top = flags::Top,
    Right = flags::Right,
    Bottom = flags::Bottom,
    LeftAndRight = Left | Right, // AKA Fill horizontally
    TopAndBottom = Top | Bottom, // AKA Fill vertically
    All = LeftAndRight | TopAndBottom, // AKA Fill
};

inline Anchor operator|(Anchor a, Anchor b) { return (Anchor)(ToInt(a) | ToInt(b)); }
inline Anchor operator&(Anchor a, Anchor b) { return (Anchor)(ToInt(a) & ToInt(b)); }

enum class Direction : u8 {
    Row = flags::Row,
    Column = flags::Column,
};

enum class Model : u8 {
    Layout = flags::FreeLayout,
    Flex = flags::Flex,
};

enum class JustifyContent : u8 {
    Start = flags::Start,
    Middle = flags::Middle,
    End = flags::End,
    Justify = flags::Justify,
};

struct ItemOptions {
    Optional<Id> parent {};
    f32x2 size {};
    Margins margins {};
    Anchor anchor {Anchor::None};
    bool line_break {false};
    Direction contents_direction {Direction::Row};
    Model contents_model {Model::Layout};
    bool contents_multiline {false};
    JustifyContent contents_align {JustifyContent::Middle};
};

PUBLIC void SetMargins(Item& item, Margins m) {
    ASSERT(All(m.lrtb >= 0 && m.lrtb < 10000));
    auto const ltrb = __builtin_shufflevector(m.lrtb, m.lrtb, 0, 2, 1, 3);
    SetMargins(item, ltrb);
}
PUBLIC void SetMargins(Context& ctx, Id id, Margins m) { SetMargins(*GetItem(ctx, id), m); }

PUBLIC Margins GetMargins(Context& ctx, Id id) {
    auto const ltrb = GetMarginsLtrb(ctx, id);
    return {.lrtb = __builtin_shufflevector(ltrb, ltrb, 0, 2, 1, 3)};
}

PUBLIC Id CreateItem(Context& ctx, ItemOptions options) {
    auto const id = CreateItem(ctx);
    auto& item = *GetItem(ctx, id);
    SetItemSize(item, options.size);
    SetMargins(item, options.margins);
    item.flags = ToInt(options.anchor) | (options.line_break ? flags::LineBreak : 0) |
                 ToInt(options.contents_direction) | ToInt(options.contents_model) |
                 ToInt(options.contents_align) | (options.contents_multiline ? flags::Wrap : flags::NoWrap);
    if (options.parent) Insert(ctx, *options.parent, id);
    return id;
}

} // namespace layout

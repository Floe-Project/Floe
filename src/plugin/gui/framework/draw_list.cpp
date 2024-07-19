// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file is based on modified code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

#include "draw_list.hpp"

#include <float.h>

#include "foundation/foundation.hpp"

namespace graphics {

constexpr u32 k_max_u16_codepoint = 0xFFFF;

static inline f32 InvLength(f32x2 const& lhs, f32 fail_value) {
    f32 const d = lhs.x * lhs.x + lhs.y * lhs.y;
    if (d > 0.0f) return 1.0f / Sqrt(d);
    return fail_value;
}

void DrawContext::ScaleClipRects(DrawData draw_data, f32 display_ratio) {
    for (auto const& list : draw_data.draw_lists) {
        for (int i = 0; i < list->cmd_buffer.size; i++) {
            auto& cmd = list->cmd_buffer[i];
            cmd.clip_rect = f32x4 {cmd.clip_rect.x * display_ratio,
                                   cmd.clip_rect.y * display_ratio,
                                   cmd.clip_rect.z * display_ratio,
                                   cmd.clip_rect.w * display_ratio};
        }
    }
}

void DrawContext::PushDefaultFont() {
    ASSERT(!fonts.fonts.Empty()); // no default font!
    PushFont(fonts.fonts[0]);
}

void DrawContext::PushFont(Font* font) {
    ASSERT(font != nullptr);
    font_stack.PushBack(font);
}

void DrawContext::PopFont() { font_stack.PopBack(); }

constexpr f32x4 k_null_clip_rect {-8192.0f,
                                  -8192.0f,
                                  +8192.0f,
                                  +8192.0f}; // Large values that are easy to encode in a few bits+shift

void DrawList::Clear() {
    cmd_buffer.Resize(0);
    idx_buffer.Resize(0);
    vtx_buffer.Resize(0);
    vtx_current_idx = 0;
    vtx_write_ptr = nullptr;
    idx_write_ptr = nullptr;
    clip_rect_stack.Resize(0);
    texture_id_stack.Resize(0);
    path.Resize(0);
    channels_current = 0;
    channels_count = 1;
    // NB: Do not clear channels so our allocations are re-used after the first frame.
}

void DrawList::ClearFreeMemory() {
    cmd_buffer.Clear();
    idx_buffer.Clear();
    vtx_buffer.Clear();
    vtx_current_idx = 0;
    vtx_write_ptr = nullptr;
    idx_write_ptr = nullptr;
    clip_rect_stack.Clear();
    texture_id_stack.Clear();
    path.Clear();
    channels_current = 0;
    channels_count = 1;
    for (int i = 0; i < channels.size; i++) {
        if (i == 0)
            ZeroMemory(
                &channels[0],
                sizeof(channels[0])); // channel 0 is a copy of CmdBuffer/IdxBuffer, don't destruct again
        channels[i].cmd_buffer.Clear();
        channels[i].idx_buffer.Clear();
    }
    channels.Clear();
}

ALWAYS_INLINE f32x4 GetCurrentClipRect(DrawList const& l) {
    return l.clip_rect_stack.size ? l.clip_rect_stack.data[l.clip_rect_stack.size - 1] : k_null_clip_rect;
}

ALWAYS_INLINE TextureHandle GetCurrentTextureId(DrawList const& l) {
    return l.texture_id_stack.size ? l.texture_id_stack.data[l.texture_id_stack.size - 1] : nullptr;
}

void DrawList::AddDrawCmd() {
    DrawCmd draw_cmd;
    draw_cmd.clip_rect = GetCurrentClipRect(*this);
    draw_cmd.texture_id = GetCurrentTextureId(*this);

    ASSERT(draw_cmd.clip_rect.x <= draw_cmd.clip_rect.z && draw_cmd.clip_rect.y <= draw_cmd.clip_rect.w);
    cmd_buffer.PushBack(draw_cmd);
}

void DrawList::AddCallback(DrawCallback callback, void* callback_data) {
    DrawCmd* current_cmd = cmd_buffer.size ? &cmd_buffer.Back() : nullptr;
    if (!current_cmd || current_cmd->elem_count != 0 || current_cmd->user_callback != nullptr) {
        AddDrawCmd();
        current_cmd = &cmd_buffer.Back();
    }
    current_cmd->user_callback = callback;
    current_cmd->user_callback_data = callback_data;

    AddDrawCmd(); // Force a new command after us (see comment below)
}

// Our scheme may appears a bit unusual, basically we want the most-common calls AddLine AddRect etc. to not
// have to perform any check so we always have a command ready in the stack. The cost of figuring out if a new
// command has to be added or if we can merge is paid in those Update** functions only.
void DrawList::UpdateClipRect() {
    // If current command is used with different settings we need to add a new command
    f32x4 const curr_clip_rect = GetCurrentClipRect(*this);
    DrawCmd* curr_cmd = cmd_buffer.size > 0 ? &cmd_buffer.data[cmd_buffer.size - 1] : nullptr;
    if (!curr_cmd ||
        (curr_cmd->elem_count != 0 && !MemoryIsEqual(&curr_cmd->clip_rect, &curr_clip_rect, sizeof(f32x4))) ||
        curr_cmd->user_callback != nullptr) {
        AddDrawCmd();
        return;
    }

    // Try to merge with previous command if it matches, else use current command
    DrawCmd* prev_cmd = cmd_buffer.size > 1 ? curr_cmd - 1 : nullptr;
    if (curr_cmd->elem_count == 0 && prev_cmd &&
        MemoryIsEqual(&prev_cmd->clip_rect, &curr_clip_rect, sizeof(f32x4)) &&
        prev_cmd->texture_id == GetCurrentTextureId(*this) && prev_cmd->user_callback == nullptr)
        cmd_buffer.PopBack();
    else
        curr_cmd->clip_rect = curr_clip_rect;
}

void DrawList::UpdateTexturePtr() {
    // If current command is used with different settings we need to add a new command
    TextureHandle const curr_texture_id = GetCurrentTextureId(*this);
    DrawCmd* curr_cmd = cmd_buffer.size ? &cmd_buffer.Back() : nullptr;
    if (!curr_cmd || (curr_cmd->elem_count != 0 && curr_cmd->texture_id != curr_texture_id) ||
        curr_cmd->user_callback != nullptr) {
        AddDrawCmd();
        return;
    }

    // Try to merge with previous command if it matches, else use current command
    DrawCmd* prev_cmd = cmd_buffer.size > 1 ? curr_cmd - 1 : nullptr;
    auto const curr_clip_rect = GetCurrentClipRect(*this);
    if (prev_cmd && prev_cmd->texture_id == curr_texture_id &&
        MemoryIsEqual(&prev_cmd->clip_rect, &curr_clip_rect, sizeof(f32x4)) &&
        prev_cmd->user_callback == nullptr)
        cmd_buffer.PopBack();
    else
        curr_cmd->texture_id = curr_texture_id;
}

// Render-level scissoring. This is passed down to your render function but not used for CPU-side coarse
// clipping. Prefer using higher-level Gui::PushClipRect() to affect logic (hit-testing and widget culling)
void DrawList::PushClipRect(f32x2 cr_min, f32x2 cr_max, bool intersect_with_current_clip_rect) {
    f32x4 cr {cr_min.x, cr_min.y, cr_max.x, cr_max.y};
    if (intersect_with_current_clip_rect && clip_rect_stack.size) {
        f32x4 const current = clip_rect_stack.data[clip_rect_stack.size - 1];
        if (cr.x < current.x) cr.x = current.x;
        if (cr.y < current.y) cr.y = current.y;
        if (cr.z > current.z) cr.z = current.z;
        if (cr.w > current.w) cr.w = current.w;
    }
    cr.z = Max(cr.x, cr.z);
    cr.w = Max(cr.y, cr.w);

    clip_rect_stack.PushBack(cr);
    UpdateClipRect();
}

void DrawList::SetClipRect(f32x2 cr_min, f32x2 cr_max) {
    f32x4 cr {cr_min.x, cr_min.y, cr_max.x, cr_max.y};
    cr.z = Max(cr.x, cr.z);
    cr.w = Max(cr.y, cr.w);

    auto& rect = clip_rect_stack.Back();
    rect = cr;
    UpdateClipRect();
}

void DrawList::SetClipRectFullscreen() {
    SetClipRect(f32x2 {k_null_clip_rect.x, k_null_clip_rect.y},
                f32x2 {k_null_clip_rect.z, k_null_clip_rect.w});
}

void DrawList::PushClipRectFullScreen() {
    PushClipRect(f32x2 {k_null_clip_rect.x, k_null_clip_rect.y},
                 f32x2 {k_null_clip_rect.z, k_null_clip_rect.w});
}

void DrawList::PopClipRect() {
    ASSERT(clip_rect_stack.size > 0);
    clip_rect_stack.PopBack();
    UpdateClipRect();
}

void DrawList::PushTextureHandle(TextureHandle const& texture_id) {
    texture_id_stack.PushBack(texture_id);
    UpdateTexturePtr();
}

void DrawList::PopTextureHandle() {
    ASSERT(texture_id_stack.size > 0);
    texture_id_stack.PopBack();
    UpdateTexturePtr();
}

void DrawList::ChannelsSplit(int chans) {
    ASSERT(channels_current == 0 && chans == 1);
    int const old_channels_count = channels.size;
    if (old_channels_count < chans) channels.Resize(chans);
    channels_count = chans;

    // _Channels[] (24 bytes each) hold storage that we'll swap with this->_CmdBuffer/_IdxBuffer
    // The content of _Channels[0] at this point doesn't matter. We clear it to make state tidy in a debugger
    // but we don't strictly need to. When we switch to the next channel, we'll copy _CmdBuffer/_IdxBuffer
    // into _Channels[0] and then _Channels[1] into _CmdBuffer/_IdxBuffer
    ZeroMemory(&channels[0], sizeof(DrawChannel));
    for (int i = 1; i < channels_count; i++) {
        if (i >= old_channels_count) {
            PLACEMENT_NEW(&channels[i]) DrawChannel();
        } else {
            channels[i].cmd_buffer.Resize(0);
            channels[i].idx_buffer.Resize(0);
        }
        if (channels[i].cmd_buffer.size == 0) {
            DrawCmd draw_cmd;
            draw_cmd.clip_rect = clip_rect_stack.Back();
            draw_cmd.texture_id = texture_id_stack.Back();
            channels[i].cmd_buffer.PushBack(draw_cmd);
        }
    }
}

void DrawList::ChannelsMerge() {
    // Note that we never use or rely on channels.Size because it is merely a buffer that we never shrink back
    // to 0 to keep all sub-buffers ready for use.
    if (channels_count <= 1) return;

    ChannelsSetCurrent(0);
    if (cmd_buffer.size && cmd_buffer.Back().elem_count == 0) cmd_buffer.PopBack();

    int new_cmd_buffer_count = 0;
    int new_idx_buffer_count = 0;
    for (int i = 1; i < channels_count; i++) {
        DrawChannel& ch = channels[i];
        if (ch.cmd_buffer.size && ch.cmd_buffer.Back().elem_count == 0) ch.cmd_buffer.PopBack();
        new_cmd_buffer_count += ch.cmd_buffer.size;
        new_idx_buffer_count += ch.idx_buffer.size;
    }
    cmd_buffer.Resize(cmd_buffer.size + new_cmd_buffer_count);
    idx_buffer.Resize(idx_buffer.size + new_idx_buffer_count);

    DrawCmd* cmd_write = cmd_buffer.data + cmd_buffer.size - new_cmd_buffer_count;
    idx_write_ptr = idx_buffer.data + idx_buffer.size - new_idx_buffer_count;
    for (int i = 1; i < channels_count; i++) {
        DrawChannel const& ch = channels[i];
        if (int const sz = ch.cmd_buffer.size) {
            CopyMemory(cmd_write, ch.cmd_buffer.data, (usize)sz * sizeof(DrawCmd));
            cmd_write += sz;
        }
        if (int const sz = ch.idx_buffer.size) {
            CopyMemory(idx_write_ptr, ch.idx_buffer.data, (usize)sz * sizeof(DrawIdx));
            idx_write_ptr += sz;
        }
    }
    AddDrawCmd();
    channels_count = 1;
}

void DrawList::ChannelsSetCurrent(int idx) {
    ASSERT(idx < channels_count);
    if (channels_current == idx) return;
    CopyMemory(&channels.data[channels_current].cmd_buffer,
               &cmd_buffer,
               sizeof(cmd_buffer)); // copy 12 bytes, four times
    CopyMemory(&channels.data[channels_current].idx_buffer, &idx_buffer, sizeof(idx_buffer));
    channels_current = idx;
    CopyMemory(&cmd_buffer, &channels.data[channels_current].cmd_buffer, sizeof(cmd_buffer));
    CopyMemory(&idx_buffer, &channels.data[channels_current].idx_buffer, sizeof(idx_buffer));
    idx_write_ptr = idx_buffer.data + idx_buffer.size;
}

// NB: this can be called with negative count for removing primitives (as long as the result does not
// underflow)
void DrawList::PrimReserve(int idx_count, int vtx_count) {
    DrawCmd& draw_cmd = cmd_buffer.data[cmd_buffer.size - 1];
    draw_cmd.elem_count += (unsigned)idx_count;

    int const vtx_buffer_size = vtx_buffer.size;
    vtx_buffer.Resize(vtx_buffer_size + vtx_count);
    vtx_write_ptr = vtx_buffer.data + vtx_buffer_size;

    int const idx_buffer_size = idx_buffer.size;
    idx_buffer.Resize(idx_buffer_size + idx_count);
    idx_write_ptr = idx_buffer.data + idx_buffer_size;
}

// Fully unrolled with inline call to keep our debug builds decently fast.
void DrawList::PrimRect(f32x2 const& a, f32x2 const& c, u32 col) {
    f32x2 const b {c.x, a.y};
    f32x2 const d {a.x, c.y};
    f32x2 const uv(context->fonts.tex_uv_white_pixel);
    auto idx = (DrawIdx)vtx_current_idx;
    idx_write_ptr[0] = idx;
    idx_write_ptr[1] = (DrawIdx)(idx + 1);
    idx_write_ptr[2] = (DrawIdx)(idx + 2);
    idx_write_ptr[3] = idx;
    idx_write_ptr[4] = (DrawIdx)(idx + 2);
    idx_write_ptr[5] = (DrawIdx)(idx + 3);
    vtx_write_ptr[0].pos = a;
    vtx_write_ptr[0].uv = uv;
    vtx_write_ptr[0].col = col;
    vtx_write_ptr[1].pos = b;
    vtx_write_ptr[1].uv = uv;
    vtx_write_ptr[1].col = col;
    vtx_write_ptr[2].pos = c;
    vtx_write_ptr[2].uv = uv;
    vtx_write_ptr[2].col = col;
    vtx_write_ptr[3].pos = d;
    vtx_write_ptr[3].uv = uv;
    vtx_write_ptr[3].col = col;
    vtx_write_ptr += 4;
    vtx_current_idx += 4;
    idx_write_ptr += 6;
}

void DrawList::PrimRectUV(f32x2 const& a, f32x2 const& c, f32x2 const& uv_a, f32x2 const& uv_c, u32 col) {
    f32x2 const b {c.x, a.y};
    f32x2 const d {a.x, c.y};
    f32x2 const uv_b {uv_c.x, uv_a.y};
    f32x2 const uv_d {uv_a.x, uv_c.y};
    auto idx = (DrawIdx)vtx_current_idx;
    idx_write_ptr[0] = idx;
    idx_write_ptr[1] = (DrawIdx)(idx + 1);
    idx_write_ptr[2] = (DrawIdx)(idx + 2);
    idx_write_ptr[3] = idx;
    idx_write_ptr[4] = (DrawIdx)(idx + 2);
    idx_write_ptr[5] = (DrawIdx)(idx + 3);
    vtx_write_ptr[0].pos = a;
    vtx_write_ptr[0].uv = uv_a;
    vtx_write_ptr[0].col = col;
    vtx_write_ptr[1].pos = b;
    vtx_write_ptr[1].uv = uv_b;
    vtx_write_ptr[1].col = col;
    vtx_write_ptr[2].pos = c;
    vtx_write_ptr[2].uv = uv_c;
    vtx_write_ptr[2].col = col;
    vtx_write_ptr[3].pos = d;
    vtx_write_ptr[3].uv = uv_d;
    vtx_write_ptr[3].col = col;
    vtx_write_ptr += 4;
    vtx_current_idx += 4;
    idx_write_ptr += 6;
}

void DrawList::PrimQuadUV(f32x2 const& a,
                          f32x2 const& b,
                          f32x2 const& c,
                          f32x2 const& d,
                          f32x2 const& uv_a,
                          f32x2 const& uv_b,
                          f32x2 const& uv_c,
                          f32x2 const& uv_d,
                          u32 col) {
    auto idx = (DrawIdx)vtx_current_idx;
    idx_write_ptr[0] = idx;
    idx_write_ptr[1] = (DrawIdx)(idx + 1);
    idx_write_ptr[2] = (DrawIdx)(idx + 2);
    idx_write_ptr[3] = idx;
    idx_write_ptr[4] = (DrawIdx)(idx + 2);
    idx_write_ptr[5] = (DrawIdx)(idx + 3);
    vtx_write_ptr[0].pos = a;
    vtx_write_ptr[0].uv = uv_a;
    vtx_write_ptr[0].col = col;
    vtx_write_ptr[1].pos = b;
    vtx_write_ptr[1].uv = uv_b;
    vtx_write_ptr[1].col = col;
    vtx_write_ptr[2].pos = c;
    vtx_write_ptr[2].uv = uv_c;
    vtx_write_ptr[2].col = col;
    vtx_write_ptr[3].pos = d;
    vtx_write_ptr[3].uv = uv_d;
    vtx_write_ptr[3].col = col;
    vtx_write_ptr += 4;
    vtx_current_idx += 4;
    idx_write_ptr += 6;
}

constexpr u32 k_red_shift = 0;
constexpr u32 k_green_shift = 8;
constexpr u32 k_blue_shift = 16;
constexpr u32 k_alpha_shift = 24;
constexpr u32 k_alpha_mask = 0xFF000000;
constexpr u32 ColU32(u8 r, u8 g, u8 b, u8 a) {
    return (u32)a << k_alpha_shift | (u32)b << k_blue_shift | (u32)g << k_green_shift | (u32)r << k_red_shift;
}

// IMPROVE: Thickness anti-aliased lines cap are missing their AA fringe.
void DrawList::AddPolyline(f32x2 const* points,
                           int const points_count,
                           u32 col,
                           bool closed,
                           f32 thickness,
                           bool anti_aliased) {
    if (points_count < 2) return;

    f32x2 const uv = context->fonts.tex_uv_white_pixel;
    anti_aliased &= context->anti_aliased_lines;

    int count = points_count;
    if (!closed) count = points_count - 1;

    bool const thick_line = thickness > 1.0f;
    if (anti_aliased) {
        // Anti-aliased stroke
        // const f32 AA_SIZE = 1.0f;
        f32 const aa_size = context->stroke_anti_alias;
        u32 const col_trans = col & ColU32(255, 255, 255, 0);

        int const idx_count = thick_line ? count * 18 : count * 12;
        int const vtx_count = thick_line ? points_count * 4 : points_count * 3;
        PrimReserve(idx_count, vtx_count);

        // Temporary buffer
        auto* temp_normals =
            (f32x2*)__builtin_alloca((unsigned)points_count * (thick_line ? 5 : 3) * sizeof(f32x2));
        f32x2* temp_points = temp_normals + points_count;

        for (int i1 = 0; i1 < count; i1++) {
            int const i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
            f32x2 diff = points[i2] - points[i1];
            diff *= InvLength(diff, 1.0f);
            temp_normals[i1].x = diff.y;
            temp_normals[i1].y = -diff.x;
        }
        if (!closed) temp_normals[points_count - 1] = temp_normals[points_count - 2];

        if (!thick_line) {
            if (!closed) {
                temp_points[0] = points[0] + temp_normals[0] * aa_size;
                temp_points[1] = points[0] - temp_normals[0] * aa_size;
                temp_points[(points_count - 1) * 2 + 0] =
                    points[points_count - 1] + temp_normals[points_count - 1] * aa_size;
                temp_points[(points_count - 1) * 2 + 1] =
                    points[points_count - 1] - temp_normals[points_count - 1] * aa_size;
            }

            // FIXME-OPT: Merge the different loops, possibly remove the temporary buffer.
            unsigned int idx1 = vtx_current_idx;
            for (int i1 = 0; i1 < count; i1++) {
                int const i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
                unsigned int const idx2 = (i1 + 1) == points_count ? vtx_current_idx : idx1 + 3;

                // Average normals
                f32x2 dm = (temp_normals[i1] + temp_normals[i2]) * 0.5f;
                f32 const dmr2 = dm.x * dm.x + dm.y * dm.y;
                if (dmr2 > 0.000001f) {
                    f32 scale = 1.0f / dmr2;
                    if (scale > 100.0f) scale = 100.0f;
                    dm *= scale;
                }
                dm *= aa_size;
                temp_points[i2 * 2 + 0] = points[i2] + dm;
                temp_points[i2 * 2 + 1] = points[i2] - dm;

                // Add indexes
                idx_write_ptr[0] = (DrawIdx)(idx2 + 0);
                idx_write_ptr[1] = (DrawIdx)(idx1 + 0);
                idx_write_ptr[2] = (DrawIdx)(idx1 + 2);
                idx_write_ptr[3] = (DrawIdx)(idx1 + 2);
                idx_write_ptr[4] = (DrawIdx)(idx2 + 2);
                idx_write_ptr[5] = (DrawIdx)(idx2 + 0);
                idx_write_ptr[6] = (DrawIdx)(idx2 + 1);
                idx_write_ptr[7] = (DrawIdx)(idx1 + 1);
                idx_write_ptr[8] = (DrawIdx)(idx1 + 0);
                idx_write_ptr[9] = (DrawIdx)(idx1 + 0);
                idx_write_ptr[10] = (DrawIdx)(idx2 + 0);
                idx_write_ptr[11] = (DrawIdx)(idx2 + 1);
                idx_write_ptr += 12;

                idx1 = idx2;
            }

            // Add vertices
            for (int i = 0; i < points_count; i++) {
                vtx_write_ptr[0].pos = points[i];
                vtx_write_ptr[0].uv = uv;
                vtx_write_ptr[0].col = col;
                vtx_write_ptr[1].pos = temp_points[i * 2 + 0];
                vtx_write_ptr[1].uv = uv;
                vtx_write_ptr[1].col = col_trans;
                vtx_write_ptr[2].pos = temp_points[i * 2 + 1];
                vtx_write_ptr[2].uv = uv;
                vtx_write_ptr[2].col = col_trans;
                vtx_write_ptr += 3;
            }
        } else {
            f32 const half_inner_thickness = (thickness - aa_size) * 0.5f;
            if (!closed) {
                temp_points[0] = points[0] + temp_normals[0] * (half_inner_thickness + aa_size);
                temp_points[1] = points[0] + temp_normals[0] * (half_inner_thickness);
                temp_points[2] = points[0] - temp_normals[0] * (half_inner_thickness);
                temp_points[3] = points[0] - temp_normals[0] * (half_inner_thickness + aa_size);
                temp_points[(points_count - 1) * 4 + 0] =
                    points[points_count - 1] +
                    temp_normals[points_count - 1] * (half_inner_thickness + aa_size);
                temp_points[(points_count - 1) * 4 + 1] =
                    points[points_count - 1] + temp_normals[points_count - 1] * (half_inner_thickness);
                temp_points[(points_count - 1) * 4 + 2] =
                    points[points_count - 1] - temp_normals[points_count - 1] * (half_inner_thickness);
                temp_points[(points_count - 1) * 4 + 3] =
                    points[points_count - 1] -
                    temp_normals[points_count - 1] * (half_inner_thickness + aa_size);
            }

            // FIXME-OPT: Merge the different loops, possibly remove the temporary buffer.
            unsigned int idx1 = vtx_current_idx;
            for (int i1 = 0; i1 < count; i1++) {
                int const i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
                unsigned int const idx2 = (i1 + 1) == points_count ? vtx_current_idx : idx1 + 4;

                // Average normals
                f32x2 dm = (temp_normals[i1] + temp_normals[i2]) * 0.5f;
                f32 const dmr2 = dm.x * dm.x + dm.y * dm.y;
                if (dmr2 > 0.000001f) {
                    f32 scale = 1.0f / dmr2;
                    if (scale > 100.0f) scale = 100.0f;
                    dm *= scale;
                }
                f32x2 const dm_out = dm * (half_inner_thickness + aa_size);
                f32x2 const dm_in = dm * half_inner_thickness;
                temp_points[i2 * 4 + 0] = points[i2] + dm_out;
                temp_points[i2 * 4 + 1] = points[i2] + dm_in;
                temp_points[i2 * 4 + 2] = points[i2] - dm_in;
                temp_points[i2 * 4 + 3] = points[i2] - dm_out;

                // Add indexes
                idx_write_ptr[0] = (DrawIdx)(idx2 + 1);
                idx_write_ptr[1] = (DrawIdx)(idx1 + 1);
                idx_write_ptr[2] = (DrawIdx)(idx1 + 2);
                idx_write_ptr[3] = (DrawIdx)(idx1 + 2);
                idx_write_ptr[4] = (DrawIdx)(idx2 + 2);
                idx_write_ptr[5] = (DrawIdx)(idx2 + 1);
                idx_write_ptr[6] = (DrawIdx)(idx2 + 1);
                idx_write_ptr[7] = (DrawIdx)(idx1 + 1);
                idx_write_ptr[8] = (DrawIdx)(idx1 + 0);
                idx_write_ptr[9] = (DrawIdx)(idx1 + 0);
                idx_write_ptr[10] = (DrawIdx)(idx2 + 0);
                idx_write_ptr[11] = (DrawIdx)(idx2 + 1);
                idx_write_ptr[12] = (DrawIdx)(idx2 + 2);
                idx_write_ptr[13] = (DrawIdx)(idx1 + 2);
                idx_write_ptr[14] = (DrawIdx)(idx1 + 3);
                idx_write_ptr[15] = (DrawIdx)(idx1 + 3);
                idx_write_ptr[16] = (DrawIdx)(idx2 + 3);
                idx_write_ptr[17] = (DrawIdx)(idx2 + 2);
                idx_write_ptr += 18;

                idx1 = idx2;
            }

            // Add vertices
            for (int i = 0; i < points_count; i++) {
                vtx_write_ptr[0].pos = temp_points[i * 4 + 0];
                vtx_write_ptr[0].uv = uv;
                vtx_write_ptr[0].col = col_trans;
                vtx_write_ptr[1].pos = temp_points[i * 4 + 1];
                vtx_write_ptr[1].uv = uv;
                vtx_write_ptr[1].col = col;
                vtx_write_ptr[2].pos = temp_points[i * 4 + 2];
                vtx_write_ptr[2].uv = uv;
                vtx_write_ptr[2].col = col;
                vtx_write_ptr[3].pos = temp_points[i * 4 + 3];
                vtx_write_ptr[3].uv = uv;
                vtx_write_ptr[3].col = col_trans;
                vtx_write_ptr += 4;
            }
        }
        vtx_current_idx += (DrawIdx)vtx_count;
    } else {
        // Non Anti-aliased Stroke
        int const idx_count = count * 6;
        int const vtx_count = count * 4; // FIXME-OPT: Not sharing edges
        PrimReserve(idx_count, vtx_count);

        for (int i1 = 0; i1 < count; i1++) {
            int const i2 = (i1 + 1) == points_count ? 0 : i1 + 1;
            f32x2 const& p1 = points[i1];
            f32x2 const& p2 = points[i2];
            f32x2 diff = p2 - p1;
            f32 const inv = InvLength(diff, 1.0f);
            diff *= inv;

            f32 const dx = diff.x * (thickness * 0.5f);
            f32 const dy = diff.y * (thickness * 0.5f);
            vtx_write_ptr[0].pos.x = p1.x + dy;
            vtx_write_ptr[0].pos.y = p1.y - dx;
            vtx_write_ptr[0].uv = uv;
            vtx_write_ptr[0].col = col;
            vtx_write_ptr[1].pos.x = p2.x + dy;
            vtx_write_ptr[1].pos.y = p2.y - dx;
            vtx_write_ptr[1].uv = uv;
            vtx_write_ptr[1].col = col;
            vtx_write_ptr[2].pos.x = p2.x - dy;
            vtx_write_ptr[2].pos.y = p2.y + dx;
            vtx_write_ptr[2].uv = uv;
            vtx_write_ptr[2].col = col;
            vtx_write_ptr[3].pos.x = p1.x - dy;
            vtx_write_ptr[3].pos.y = p1.y + dx;
            vtx_write_ptr[3].uv = uv;
            vtx_write_ptr[3].col = col;

            // sw_print("Before: (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f)\n",
            //         _VtxWritePtr[0].pos.x, _VtxWritePtr[0].pos.y,
            //         _VtxWritePtr[1].pos.x, _VtxWritePtr[1].pos.y,
            //         _VtxWritePtr[2].pos.x, _VtxWritePtr[2].pos.y,
            //         _VtxWritePtr[3].pos.x, _VtxWritePtr[3].pos.y
            //         );

            // if (_VtxWritePtr[0].pos.x < 50) _VtxWritePtr[0].pos.x = (f32)(int)_VtxWritePtr[0].pos.x;
            // else _VtxWritePtr[0].pos.x = ceilf(_VtxWritePtr[0].pos.x);
            // if (_VtxWritePtr[0].pos.y < 50) _VtxWritePtr[0].pos.y = (f32)(int)_VtxWritePtr[0].pos.y;
            // else _VtxWritePtr[0].pos.y = ceilf(_VtxWritePtr[0].pos.y);

            // if (_VtxWritePtr[1].pos.x < 50) _VtxWritePtr[1].pos.x = (f32)(int)_VtxWritePtr[1].pos.x;
            // else _VtxWritePtr[1].pos.x = ceilf(_VtxWritePtr[1].pos.x);
            // if (_VtxWritePtr[1].pos.y < 50) _VtxWritePtr[1].pos.y = (f32)(int)_VtxWritePtr[1].pos.y;
            // else _VtxWritePtr[1].pos.y = ceilf(_VtxWritePtr[1].pos.y);

            // if (_VtxWritePtr[2].pos.x < 50) _VtxWritePtr[2].pos.x = (f32)(int)_VtxWritePtr[2].pos.x;
            // else _VtxWritePtr[2].pos.x = ceilf(_VtxWritePtr[2].pos.x);
            // if (_VtxWritePtr[2].pos.y < 50) _VtxWritePtr[2].pos.y = (f32)(int)_VtxWritePtr[2].pos.y;
            // else _VtxWritePtr[2].pos.y = ceilf(_VtxWritePtr[2].pos.y);

            // if (_VtxWritePtr[3].pos.x < 50) _VtxWritePtr[3].pos.x = (f32)(int)_VtxWritePtr[3].pos.x;
            // else _VtxWritePtr[3].pos.x = ceilf(_VtxWritePtr[3].pos.x);
            // if (_VtxWritePtr[3].pos.y < 50) _VtxWritePtr[3].pos.y = (f32)(int)_VtxWritePtr[3].pos.y;
            // else _VtxWritePtr[3].pos.y = ceilf(_VtxWritePtr[3].pos.y);

            // _VtxWritePtr[0].pos.x = roundf(_VtxWritePtr[0].pos.x); _VtxWritePtr[0].pos.y =
            // roundf(_VtxWritePtr[0].pos.y); _VtxWritePtr[1].pos.x = roundf(_VtxWritePtr[1].pos.x);
            // _VtxWritePtr[1].pos.y = roundf(_VtxWritePtr[1].pos.y); _VtxWritePtr[2].pos.x =
            // roundf(_VtxWritePtr[2].pos.x); _VtxWritePtr[2].pos.y = roundf(_VtxWritePtr[2].pos.y);
            // _VtxWritePtr[3].pos.x = roundf(_VtxWritePtr[3].pos.x); _VtxWritePtr[3].pos.y =
            // roundf(_VtxWritePtr[3].pos.y);

            // sw_print("After: (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f), (%.2f, %.2f)\n",
            //         _VtxWritePtr[0].pos.x, _VtxWritePtr[0].pos.y,
            //         _VtxWritePtr[1].pos.x, _VtxWritePtr[1].pos.y,
            //         _VtxWritePtr[2].pos.x, _VtxWritePtr[2].pos.y,
            //         _VtxWritePtr[3].pos.x, _VtxWritePtr[3].pos.y
            //         );

            vtx_write_ptr += 4;

            idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
            idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + 1);
            idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + 2);
            idx_write_ptr[3] = (DrawIdx)(vtx_current_idx);
            idx_write_ptr[4] = (DrawIdx)(vtx_current_idx + 2);
            idx_write_ptr[5] = (DrawIdx)(vtx_current_idx + 3);
            idx_write_ptr += 6;
            vtx_current_idx += 4;
        }
    }
}

void DrawList::AddConvexPolyFilled(f32x2 const* points, int const points_count, u32 col, bool anti_aliased) {
    f32x2 const uv = context->fonts.tex_uv_white_pixel;
    anti_aliased &= context->anti_aliased_shapes;
    // if (Gui::GetIO().KeyCtrl) anti_aliased = false; // Debug

    if (anti_aliased) {
        // Anti-aliased Fill
        f32 const aa_size = context->fill_anti_alias;
        u32 const col_trans = col & ColU32(255, 255, 255, 0);
        int const idx_count = (points_count - 2) * 3 + points_count * 6;
        int const vtx_count = (points_count * 2);
        PrimReserve(idx_count, vtx_count);

        // Add indexes for fill
        unsigned int const vtx_inner_idx = vtx_current_idx;
        unsigned int const vtx_outer_idx = vtx_current_idx + 1;
        for (int i = 2; i < points_count; i++) {
            idx_write_ptr[0] = (DrawIdx)(vtx_inner_idx);
            idx_write_ptr[1] = (DrawIdx)(vtx_inner_idx + (((unsigned)i - 1) << 1));
            idx_write_ptr[2] = (DrawIdx)(vtx_inner_idx + ((unsigned)i << 1));
            idx_write_ptr += 3;
        }

        // Compute normals
        auto* temp_normals = (f32x2*)__builtin_alloca((unsigned)points_count * sizeof(f32x2));
        for (int i0 = points_count - 1, i1 = 0; i1 < points_count; i0 = i1++) {
            f32x2 const& p0 = points[i0];
            f32x2 const& p1 = points[i1];
            f32x2 diff = p1 - p0;
            diff *= InvLength(diff, 1.0f);
            temp_normals[i0].x = diff.y;
            temp_normals[i0].y = -diff.x;
        }

        for (int i0 = points_count - 1, i1 = 0; i1 < points_count; i0 = i1++) {
            // Average normals
            f32x2 const& n0 = temp_normals[i0];
            f32x2 const& n1 = temp_normals[i1];
            f32x2 dm = (n0 + n1) * 0.5f;
            f32 const dmr2 = dm.x * dm.x + dm.y * dm.y;
            if (dmr2 > 0.000001f) {
                f32 scale = 1.0f / dmr2;
                if (scale > 100.0f) scale = 100.0f;
                dm *= scale;
            }
            dm *= aa_size * 0.5f;

            // Add vertices
            vtx_write_ptr[0].pos = (points[i1] - dm);
            vtx_write_ptr[0].uv = uv;
            vtx_write_ptr[0].col = col; // Inner
            vtx_write_ptr[1].pos = (points[i1] + dm);
            vtx_write_ptr[1].uv = uv;
            vtx_write_ptr[1].col = col_trans; // Outer
            vtx_write_ptr += 2;

            // Add indexes for fringes
            idx_write_ptr[0] = (DrawIdx)(vtx_inner_idx + ((unsigned)i1 << 1));
            idx_write_ptr[1] = (DrawIdx)(vtx_inner_idx + ((unsigned)i0 << 1));
            idx_write_ptr[2] = (DrawIdx)(vtx_outer_idx + ((unsigned)i0 << 1));
            idx_write_ptr[3] = (DrawIdx)(vtx_outer_idx + ((unsigned)i0 << 1));
            idx_write_ptr[4] = (DrawIdx)(vtx_outer_idx + ((unsigned)i1 << 1));
            idx_write_ptr[5] = (DrawIdx)(vtx_inner_idx + ((unsigned)i1 << 1));
            idx_write_ptr += 6;
        }
        vtx_current_idx += (DrawIdx)vtx_count;
    } else {
        // Non Anti-aliased Fill
        int const idx_count = (points_count - 2) * 3;
        int const vtx_count = points_count;
        PrimReserve(idx_count, vtx_count);
        for (int i = 0; i < vtx_count; i++) {
            vtx_write_ptr[0].pos = points[i];
            vtx_write_ptr[0].uv = uv;
            vtx_write_ptr[0].col = col;
            vtx_write_ptr++;
        }
        for (int i = 2; i < points_count; i++) {
            idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
            idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + (unsigned)i - 1);
            idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + (unsigned)i);
            idx_write_ptr += 3;
        }
        vtx_current_idx += (DrawIdx)vtx_count;
    }
}

void DrawList::PathArcToFast(f32x2 const& centre, f32 radius, int amin, int amax) {
    static f32x2 circle_vtx[24];
    amin *= 2;
    amax *= 2;
    static bool circle_vtx_builds = false;
    auto const circle_vtx_count = (int)ArraySize(circle_vtx);
    if (!circle_vtx_builds) {
        for (int i = 0; i < circle_vtx_count; i++) {
            f32 const a = ((f32)i / (f32)circle_vtx_count) * 2 * maths::k_pi<>;
            circle_vtx[i].x = Cos(a);
            circle_vtx[i].y = Sin(a);
        }
        circle_vtx_builds = true;
    }

    if (amin > amax) return;
    if (radius == 0.0f) {
        path.PushBack(centre);
    } else {
        path.Reserve(path.size + (amax - amin + 1));
        for (int a = amin; a <= amax; a++) {
            f32x2 const& c = circle_vtx[a % circle_vtx_count];
            path.PushBack(f32x2 {centre.x + c.x * radius, centre.y + c.y * radius});
        }
    }
}

void DrawList::PathArcTo(f32x2 const& centre, f32 radius, f32 amin, f32 amax, int num_segments) {
    if (radius == 0.0f) path.PushBack(centre);
    path.Reserve(path.size + (num_segments + 1));
    for (int i = 0; i <= num_segments; i++) {
        f32 const a = amin + ((f32)i / (f32)num_segments) * (amax - amin);
        path.PushBack(f32x2 {centre.x + Cos(a) * radius, centre.y + Sin(a) * radius});
    }
}

static void PathBezierToCasteljau(Vector<f32x2>* path,
                                  f32 x1,
                                  f32 y1,
                                  f32 x2,
                                  f32 y2,
                                  f32 x3,
                                  f32 y3,
                                  f32 x4,
                                  f32 y4,
                                  f32 tess_tol,
                                  int level) {
    f32 const dx = x4 - x1;
    f32 const dy = y4 - y1;
    f32 d2 = ((x2 - x4) * dy - (y2 - y4) * dx);
    f32 d3 = ((x3 - x4) * dy - (y3 - y4) * dx);
    d2 = (d2 >= 0) ? d2 : -d2;
    d3 = (d3 >= 0) ? d3 : -d3;
    if ((d2 + d3) * (d2 + d3) < tess_tol * (dx * dx + dy * dy)) {
        path->PushBack(f32x2 {x4, y4});
    } else if (level < 10) {
        f32 const x12 = (x1 + x2) * 0.5f;
        f32 const y12 = (y1 + y2) * 0.5f;
        f32 const x23 = (x2 + x3) * 0.5f;
        f32 const y23 = (y2 + y3) * 0.5f;
        f32 const x34 = (x3 + x4) * 0.5f;
        f32 const y34 = (y3 + y4) * 0.5f;
        f32 const x123 = (x12 + x23) * 0.5f;
        f32 const y123 = (y12 + y23) * 0.5f;
        f32 const x234 = (x23 + x34) * 0.5f;
        f32 const y234 = (y23 + y34) * 0.5f;
        f32 const x1234 = (x123 + x234) * 0.5f;
        f32 const y1234 = (y123 + y234) * 0.5f;

        PathBezierToCasteljau(path, x1, y1, x12, y12, x123, y123, x1234, y1234, tess_tol, level + 1);
        PathBezierToCasteljau(path, x1234, y1234, x234, y234, x34, y34, x4, y4, tess_tol, level + 1);
    }
}

void DrawList::PathBezierCurveTo(f32x2 const& p2, f32x2 const& p3, f32x2 const& p4, int num_segments) {
    f32x2 const p1 = path.Back();
    if (num_segments == 0) {
        // Auto-tessellated
        PathBezierToCasteljau(&path,
                              p1.x,
                              p1.y,
                              p2.x,
                              p2.y,
                              p3.x,
                              p3.y,
                              p4.x,
                              p4.y,
                              context->curve_tessellation_tol,
                              0);
    } else {
        f32 const t_step = 1.0f / (f32)num_segments;
        for (int i_step = 1; i_step <= num_segments; i_step++) {
            f32 const t = t_step * (f32)i_step;
            f32 const u = 1.0f - t;
            f32 const w1 = u * u * u;
            f32 const w2 = 3 * u * u * t;
            f32 const w3 = 3 * u * t * t;
            f32 const w4 = t * t * t;
            path.PushBack(f32x2 {w1 * p1.x + w2 * p2.x + w3 * p3.x + w4 * p4.x,
                                 w1 * p1.y + w2 * p2.y + w3 * p3.y + w4 * p4.y});
        }
    }
}

void DrawList::PathRect(f32x2 const& a, f32x2 const& b, f32 rounding, int rounding_corners) {
    f32 r = rounding;
    r = Min(r,
            Fabs(b.x - a.x) *
                    (((rounding_corners & (1 | 2)) == (1 | 2)) || ((rounding_corners & (4 | 8)) == (4 | 8))
                         ? 0.5f
                         : 1.0f) -
                1.0f);
    r = Min(r,
            Fabs(b.y - a.y) *
                    (((rounding_corners & (1 | 8)) == (1 | 8)) || ((rounding_corners & (2 | 4)) == (2 | 4))
                         ? 0.5f
                         : 1.0f) -
                1.0f);

    if (r <= 0.0f || rounding_corners == 0) {
        PathLineTo(a);
        PathLineTo(f32x2 {b.x + 1, a.y});
        PathLineTo(f32x2 {b.x, a.y});
        PathLineTo(b);
        PathLineTo(f32x2 {a.x, b.y});
    } else {
        f32 const r0 = (rounding_corners & 1) ? r : 0.0f;
        f32 const r1 = (rounding_corners & 2) ? r : 0.0f;
        f32 const r2 = (rounding_corners & 4) ? r : 0.0f;
        f32 const r3 = (rounding_corners & 8) ? r : 0.0f;
        PathArcToFast(f32x2 {a.x + r0, a.y + r0}, r0, 6, 9);
        PathArcToFast(f32x2 {b.x - r1, a.y + r1}, r1, 9, 12);
        PathArcToFast(f32x2 {b.x - r2, b.y - r2}, r2, 0, 3);
        PathArcToFast(f32x2 {a.x + r3, b.y - r3}, r3, 3, 6);
    }
}

void DrawList::AddLine(f32x2 const& a, f32x2 const& b, u32 col, f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;
    PathLineTo(a + f32x2 {0.5f, 0.5f});
    PathLineTo(b + f32x2 {0.5f, 0.5f});
    PathStroke(col, false, thickness);
}

void DrawList::AddNonAABox(f32x2 const& a, f32x2 const& b, u32 col, f32 thickness) {

    f32x2 const& p1 = a;
    auto const p2 = f32x2 {b.x, a.y};
    f32x2 const& p3 = b;
    auto const p4 = f32x2 {a.x, b.y};

    f32x2 const uv = context->fonts.tex_uv_white_pixel;

    // Non Anti-aliased Stroke
    int const idx_count = 4 * 6;
    int const vtx_count = 4 * 4; // FIXME-OPT: Not sharing edges
    PrimReserve(idx_count, vtx_count);

    {
        vtx_write_ptr[0].pos.x = p1.x + thickness;
        vtx_write_ptr[0].pos.y = p1.y;

        vtx_write_ptr[1].pos.x = p2.x;
        vtx_write_ptr[1].pos.y = p2.y;

        vtx_write_ptr[2].pos.x = p2.x;
        vtx_write_ptr[2].pos.y = p2.y + thickness;

        vtx_write_ptr[3].pos.x = p1.x + thickness;
        vtx_write_ptr[3].pos.y = p1.y + thickness;

        vtx_write_ptr[0].uv = uv;
        vtx_write_ptr[0].col = col;
        vtx_write_ptr[1].uv = uv;
        vtx_write_ptr[1].col = col;
        vtx_write_ptr[2].uv = uv;
        vtx_write_ptr[2].col = col;
        vtx_write_ptr[3].uv = uv;
        vtx_write_ptr[3].col = col;

        vtx_write_ptr += 4;

        idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + 1);
        idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + 2);

        idx_write_ptr[3] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[4] = (DrawIdx)(vtx_current_idx + 2);
        idx_write_ptr[5] = (DrawIdx)(vtx_current_idx + 3);

        idx_write_ptr += 6;
        vtx_current_idx += 4;
    }

    {
        vtx_write_ptr[0].pos.x = p2.x;
        vtx_write_ptr[0].pos.y = p2.y + thickness;

        vtx_write_ptr[1].pos.x = p3.x;
        vtx_write_ptr[1].pos.y = p3.y;

        vtx_write_ptr[2].pos.x = p3.x - thickness;
        vtx_write_ptr[2].pos.y = p3.y;

        vtx_write_ptr[3].pos.x = p2.x - thickness;
        vtx_write_ptr[3].pos.y = p2.y + thickness;

        vtx_write_ptr[0].uv = uv;
        vtx_write_ptr[0].col = col;
        vtx_write_ptr[1].uv = uv;
        vtx_write_ptr[1].col = col;
        vtx_write_ptr[2].uv = uv;
        vtx_write_ptr[2].col = col;
        vtx_write_ptr[3].uv = uv;
        vtx_write_ptr[3].col = col;

        vtx_write_ptr += 4;

        idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + 1);
        idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + 2);

        idx_write_ptr[3] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[4] = (DrawIdx)(vtx_current_idx + 2);
        idx_write_ptr[5] = (DrawIdx)(vtx_current_idx + 3);

        idx_write_ptr += 6;
        vtx_current_idx += 4;
    }

    {
        vtx_write_ptr[0].pos.x = p4.x;
        vtx_write_ptr[0].pos.y = p4.y - thickness;

        vtx_write_ptr[1].pos.x = p3.x - thickness;
        vtx_write_ptr[1].pos.y = p3.y - thickness;

        vtx_write_ptr[2].pos.x = p3.x - thickness;
        vtx_write_ptr[2].pos.y = p3.y;

        vtx_write_ptr[3].pos.x = p4.x;
        vtx_write_ptr[3].pos.y = p4.y;

        vtx_write_ptr[0].uv = uv;
        vtx_write_ptr[0].col = col;
        vtx_write_ptr[1].uv = uv;
        vtx_write_ptr[1].col = col;
        vtx_write_ptr[2].uv = uv;
        vtx_write_ptr[2].col = col;
        vtx_write_ptr[3].uv = uv;
        vtx_write_ptr[3].col = col;

        vtx_write_ptr += 4;

        idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + 1);
        idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + 2);

        idx_write_ptr[3] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[4] = (DrawIdx)(vtx_current_idx + 2);
        idx_write_ptr[5] = (DrawIdx)(vtx_current_idx + 3);

        idx_write_ptr += 6;
        vtx_current_idx += 4;
    }

    {
        vtx_write_ptr[0].pos.x = p1.x + thickness;
        vtx_write_ptr[0].pos.y = p1.y;

        vtx_write_ptr[1].pos.x = p4.x + thickness;
        vtx_write_ptr[1].pos.y = p4.y - thickness;

        vtx_write_ptr[2].pos.x = p4.x;
        vtx_write_ptr[2].pos.y = p4.y - thickness;

        vtx_write_ptr[3].pos.x = p1.x;
        vtx_write_ptr[3].pos.y = p1.y;

        vtx_write_ptr[0].uv = uv;
        vtx_write_ptr[0].col = col;
        vtx_write_ptr[1].uv = uv;
        vtx_write_ptr[1].col = col;
        vtx_write_ptr[2].uv = uv;
        vtx_write_ptr[2].col = col;
        vtx_write_ptr[3].uv = uv;
        vtx_write_ptr[3].col = col;

        vtx_write_ptr += 4;

        idx_write_ptr[0] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[1] = (DrawIdx)(vtx_current_idx + 1);
        idx_write_ptr[2] = (DrawIdx)(vtx_current_idx + 2);

        idx_write_ptr[3] = (DrawIdx)(vtx_current_idx);
        idx_write_ptr[4] = (DrawIdx)(vtx_current_idx + 2);
        idx_write_ptr[5] = (DrawIdx)(vtx_current_idx + 3);

        idx_write_ptr += 6;
        vtx_current_idx += 4;
    }
}

// a: upper-left, b: lower-right. we don't render 1 px sized rectangles properly.
void DrawList::AddRect(f32x2 const& a,
                       f32x2 const& b,
                       u32 col,
                       f32 rounding,
                       int rounding_corners_flags,
                       f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;
    PathRect(a + f32x2 {0.5f, 0.5f}, b - f32x2 {0.5f, 0.5f}, rounding, rounding_corners_flags);
    PathStroke(col, true, thickness);
}

void DrawList::AddRectFilled(f32x2 const& a,
                             f32x2 const& b,
                             u32 col,
                             f32 rounding,
                             int rounding_corners_flags) {
    if ((col & k_alpha_mask) == 0) return;
    if (rounding > 0.0f) {
        PathRect(a, b, rounding, rounding_corners_flags);
        PathFill(col);
    } else {
        PrimReserve(6, 4);
        PrimRect(a, b, col);
    }
}

void DrawList::AddRectFilledMultiColor(f32x2 const& a,
                                       f32x2 const& c,
                                       u32 col_upr_left,
                                       u32 col_upr_right,
                                       u32 col_bot_right,
                                       u32 col_bot_left) {
    if (((col_upr_left | col_upr_right | col_bot_right | col_bot_left) & k_alpha_mask) == 0) return;

    f32x2 const uv = context->fonts.tex_uv_white_pixel;
    PrimReserve(6, 4);
    PrimWriteIdx((DrawIdx)(vtx_current_idx));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 1));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 2));
    PrimWriteIdx((DrawIdx)(vtx_current_idx));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 2));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 3));
    PrimWriteVtx(a, uv, col_upr_left);
    PrimWriteVtx(f32x2 {c.x, a.y}, uv, col_upr_right);
    PrimWriteVtx(c, uv, col_bot_right);
    PrimWriteVtx(f32x2 {a.x, c.y}, uv, col_bot_left);
}

void DrawList::AddQuadFilledMultiColor(f32x2 const& upr_left,
                                       f32x2 const& upr_right,
                                       f32x2 const& bot_right,
                                       f32x2 const& bot_left,
                                       u32 col_upr_left,
                                       u32 col_upr_right,
                                       u32 col_bot_right,
                                       u32 col_bot_left) {
    if (((col_upr_left | col_upr_right | col_bot_right | col_bot_left) & k_alpha_mask) == 0) return;

    f32x2 const uv = context->fonts.tex_uv_white_pixel;
    PrimReserve(6, 4);
    PrimWriteIdx((DrawIdx)(vtx_current_idx));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 1));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 2));
    PrimWriteIdx((DrawIdx)(vtx_current_idx));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 2));
    PrimWriteIdx((DrawIdx)(vtx_current_idx + 3));
    PrimWriteVtx(upr_left, uv, col_upr_left);
    PrimWriteVtx(upr_right, uv, col_upr_right);
    PrimWriteVtx(bot_right, uv, col_bot_right);
    PrimWriteVtx(bot_left, uv, col_bot_left);
}

void DrawList::AddQuad(f32x2 const& a,
                       f32x2 const& b,
                       f32x2 const& c,
                       f32x2 const& d,
                       u32 col,
                       f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathLineTo(d);
    PathStroke(col, true, thickness);
}

void DrawList::AddQuadFilled(f32x2 const& a, f32x2 const& b, f32x2 const& c, f32x2 const& d, u32 col) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathLineTo(d);
    PathFill(col);
}

void DrawList::AddTriangle(f32x2 const& a, f32x2 const& b, f32x2 const& c, u32 col, f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathStroke(col, true, thickness);
}

void DrawList::AddTriangleFilled(f32x2 const& a, f32x2 const& b, f32x2 const& c, u32 col) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(a);
    PathLineTo(b);
    PathLineTo(c);
    PathFill(col);
}

void DrawList::AddCircle(f32x2 const& centre, f32 radius, u32 col, int num_segments, f32 thickness) {
    if ((col & k_alpha_mask) == 0) return;

    f32 const a_max = maths::k_pi<> * 2.0f * ((f32)num_segments - 1.0f) / (f32)num_segments;
    PathArcTo(centre, radius - 0.5f, 0.0f, a_max, num_segments);
    PathStroke(col, true, thickness);
}

void DrawList::AddCircleFilled(f32x2 const& centre, f32 radius, u32 col, int num_segments) {
    if ((col & k_alpha_mask) == 0) return;

    f32 const a_max = maths::k_pi<> * 2.0f * ((f32)num_segments - 1.0f) / (f32)num_segments;
    PathArcTo(centre, radius, 0.0f, a_max, num_segments);
    PathFill(col);
}

void DrawList::AddBezierCurve(f32x2 const& pos0,
                              f32x2 const& cp0,
                              f32x2 const& cp1,
                              f32x2 const& pos1,
                              u32 col,
                              f32 thickness,
                              int num_segments) {
    if ((col & k_alpha_mask) == 0) return;

    PathLineTo(pos0);
    PathBezierCurveTo(cp0, cp1, pos1, num_segments);
    PathStroke(col, false, thickness);
}

static f32x2 GetTextPosition(Font* font,
                             f32 font_size,
                             f32x2 r_min,
                             f32x2 r_max,
                             TextJustification justification,
                             String text,
                             f32x2* calculated_size) {
    f32x2 pos = r_min;
    if (justification != TextJustification::TopLeft) {
        Optional<f32x2> size = {};
        auto const height = font_size;
        if (justification & TextJustification::Left) {
            pos.x = r_min.x;
        } else {
            auto const width = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text).x;
            size = f32x2 {width, height};
            if (justification & TextJustification::Right)
                pos.x = r_max.x - width;
            else if (justification & TextJustification::HorizontallyCentred)
                pos.x = r_min.x + ((r_max.x - r_min.x) / 2) - (width / 2);
        }

        if (justification & TextJustification::Baseline)
            pos.y = r_max.y - height + (-font->descent);
        else if (justification & TextJustification::Top)
            pos.y = r_min.y;
        else if (justification & TextJustification::Bottom)
            pos.y = r_max.y - height;
        else if (justification & TextJustification::VerticallyCentred)
            pos.y = r_min.y + ((r_max.y - r_min.y) / 2) - (height / 2);
        if (calculated_size && size) *calculated_size = *size;
    }
    return pos;
}

void DrawList::AddTextJustified(Rect r,
                                String str,
                                u32 col,
                                TextJustification justification,
                                TextOverflowType overflow_type,
                                f32 font_scaling) {
    auto font = context->CurrentFont();
    auto const display_scale = font->font_size_no_scale / font->font_size;
    auto font_size = context->CurrentFontSize() * font_scaling;

    ArenaAllocatorWithInlineStorage<1000> temp_allocator;
    DynamicArray<char> buffer(temp_allocator);
    String const dots {".."};

    f32x2 text_size {-1, -1};
    auto text_pos = GetTextPosition(font, font_size, r.Min(), r.Max(), justification, str, &text_size);
    if (overflow_type != TextOverflowType::AllowOverflow) {
        if (text_size.x == -1) text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, str);
        if (text_size.x > r.w) {
            f32 const dots_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, dots).x;
            f32 line_width = 0;

            if (overflow_type == TextOverflowType::ShowDotsOnRight) {
                char const* s = str.data;
                auto const end = End(str);
                while (s < end) {
                    auto prev_s = s;
                    auto c = (u32)*s;
                    if (c < 0x80) {
                        s += 1;
                    } else {
                        s += Utf8CharacterToUtf32(&c, s, end, k_max_u16_codepoint);
                        if (c == 0) break;
                    }

                    if (c < 32) {
                        if (c == '\n' || c == '\r') continue;
                    }

                    f32 const char_width =
                        ((int)c < font->index_x_advance.size ? font->index_x_advance[(int)c]
                                                             : font->fallback_x_advance) *
                        font_scaling * display_scale;

                    line_width += char_width;

                    if ((line_width + dots_size) > r.w) {
                        dyn::Assign(buffer, str.SubSpan(0, (usize)(prev_s - str.data)));
                        dyn::AppendSpan(buffer, dots);
                        str = buffer;
                        break;
                    }
                }
            } else if (overflow_type == TextOverflowType::ShowDotsOnLeft) {
                auto get_char_previous_to_end = [](char const* start, char const* end) {
                    char const* prev_s = start;
                    for (auto s = start; s < end && *s != '\0';) {
                        s = IncrementUTF8Characters(s, 1);
                        if (s >= end) return prev_s;
                        prev_s = s;
                    }
                    return start;
                };

                char const* start = str.data;
                char const* end = End(str);
                char const* s = get_char_previous_to_end(start, end);
                while (s > start) {
                    auto prev_s = s;
                    auto c = (u32)*s;
                    if (c < 0x80) {
                    } else {
                        Utf8CharacterToUtf32(&c, s, end, k_max_u16_codepoint);
                        if (c == 0) break;
                    }

                    if (c < 32) {
                        if (c == '\n' || c == '\r') continue;
                    }

                    f32 const char_width =
                        ((int)c < font->index_x_advance.size ? font->index_x_advance[(int)c]
                                                             : font->fallback_x_advance) *
                        font_scaling * display_scale;

                    line_width += char_width;

                    if ((line_width + dots_size) > r.w) {
                        dyn::Assign(buffer, dots);
                        dyn::AppendSpan(buffer, String(prev_s, (usize)(end - prev_s)));
                        str = buffer;
                        text_pos.x = r.Right() - (line_width + dots_size);
                        break;
                    }

                    s = get_char_previous_to_end(start, s);
                }
            }
        }
    }

    AddText(font, font_size, text_pos, col, str);
}

void DrawList::AddText(Font const* font,
                       f32 font_size,
                       f32x2 const& pos,
                       u32 col,
                       String str,
                       f32 wrap_width,
                       f32x4 const* cpu_fine_clip_rect) {
    if ((col & k_alpha_mask) == 0) return;
    if (str.size == 0) return;

    f32x4 clip_rect = clip_rect_stack.Back();
    if (cpu_fine_clip_rect) {
        clip_rect.x = Max(clip_rect.x, cpu_fine_clip_rect->x);
        clip_rect.y = Max(clip_rect.y, cpu_fine_clip_rect->y);
        clip_rect.z = Min(clip_rect.z, cpu_fine_clip_rect->z);
        clip_rect.w = Min(clip_rect.w, cpu_fine_clip_rect->w);
    }
    font->RenderText(this,
                     font_size,
                     pos,
                     col,
                     clip_rect,
                     str.data,
                     End(str),
                     wrap_width,
                     cpu_fine_clip_rect != nullptr);
}

void DrawList::AddText(f32x2 const& pos, u32 col, String str) {
    AddText(context->CurrentFont(), context->CurrentFont()->font_size_no_scale, pos, col, str);
}

static inline f32x2 Mul(f32x2 const& lhs, f32x2 const& rhs) { return f32x2 {lhs.x * rhs.x, lhs.y * rhs.y}; }
static inline f32 LengthSqr(f32x2 const& lhs) { return (lhs.x * lhs.x) + (lhs.y * lhs.y); }
static inline f32 Dot(f32x2 const& a, f32x2 const& b) { return a.x * b.x + a.y * b.y; }

static void ShadeVertsLinearUV(DrawList* draw_list,
                               int vert_start_idx,
                               int vert_end_idx,
                               f32x2 const& a,
                               f32x2 const& b,
                               f32x2 const& uv_a,
                               f32x2 const& uv_b,
                               bool clamp) {
    f32x2 const size = b - a;
    f32x2 const uv_size = uv_b - uv_a;
    auto const scale =
        f32x2 {size.x != 0.0f ? (uv_size.x / size.x) : 0.0f, size.y != 0.0f ? (uv_size.y / size.y) : 0.0f};

    DrawVert* vert_start = draw_list->vtx_buffer.data + vert_start_idx;
    DrawVert* vert_end = draw_list->vtx_buffer.data + vert_end_idx;
    if (clamp) {
        f32x2 const min = Min(uv_a, uv_b);
        f32x2 const max = Max(uv_a, uv_b);
        for (DrawVert* vertex = vert_start; vertex < vert_end; ++vertex)
            vertex->uv = Clamp(uv_a + Mul(f32x2 {vertex->pos.x, vertex->pos.y} - a, scale), min, max);
    } else {
        for (DrawVert* vertex = vert_start; vertex < vert_end; ++vertex)
            vertex->uv = uv_a + Mul(f32x2 {vertex->pos.x, vertex->pos.y} - a, scale);
    }
}

void DrawList::ShadeVertsLinearColorGradientSetAlpha(DrawList* draw_list,
                                                     int vert_start_idx,
                                                     int vert_end_idx,
                                                     f32x2 gradient_p0,
                                                     f32x2 gradient_p1,
                                                     u32 col0,
                                                     u32 col1) {
    auto const gradient_extent = gradient_p1 - gradient_p0;
    auto const gradient_inv_length2 = 1.0f / LengthSqr(gradient_extent);
    auto const vert_start = draw_list->vtx_buffer.data + vert_start_idx;
    auto const vert_end = draw_list->vtx_buffer.data + vert_end_idx;
    auto const col0_r = (f32)((int)(col0 >> k_red_shift) & 0xFF);
    auto const col0_g = (f32)((int)(col0 >> k_green_shift) & 0xFF);
    auto const col0_b = (f32)((int)(col0 >> k_blue_shift) & 0xFF);
    auto const col0_a = (f32)((int)(col0 >> k_alpha_shift) & 0xFF);
    auto const col_delta_r = ((f32)((int)(col1 >> k_red_shift) & 0xFF) - col0_r);
    auto const col_delta_g = ((f32)((int)(col1 >> k_green_shift) & 0xFF) - col0_g);
    auto const col_delta_b = ((f32)((int)(col1 >> k_blue_shift) & 0xFF) - col0_b);
    auto const col_delta_a = ((f32)((int)(col1 >> k_alpha_shift) & 0xFF) - col0_a);
    for (auto vert = vert_start; vert < vert_end; vert++) {
        auto const d = Dot(vert->pos - gradient_p0, gradient_extent);
        auto const t = Clamp(d * gradient_inv_length2, 0.0f, 1.0f);
        auto const r = (u32)(col0_r + col_delta_r * t);
        auto const g = (u32)(col0_g + col_delta_g * t);
        auto const b = (u32)(col0_b + col_delta_b * t);
        auto const a = (u32)(col0_a + col_delta_a * t);
        vert->col = (r << k_red_shift) | (g << k_green_shift) | (b << k_blue_shift) | (a << k_alpha_shift);
    }
}

void DrawList::AddImage(TextureHandle user_texture_id,
                        f32x2 const& a,
                        f32x2 const& b,
                        f32x2 const& uv0,
                        f32x2 const& uv1,
                        u32 col) {
    if ((col & k_alpha_mask) == 0) return;
    if (user_texture_id == nullptr) return;

    // FIXME-OPT: This is wasting draw calls.
    bool const push_texture_id = texture_id_stack.Empty() || user_texture_id != texture_id_stack.Back();
    if (push_texture_id) PushTextureHandle(user_texture_id);

    PrimReserve(6, 4);
    PrimRectUV(a, b, uv0, uv1, col);

    if (push_texture_id) PopTextureHandle();
}

void DrawList::AddImageRounded(TextureHandle user_texture_id,
                               f32x2 const& p_min,
                               f32x2 const& p_max,
                               f32x2 const& uv_min,
                               f32x2 const& uv_max,
                               u32 col,
                               f32 rounding,
                               int rounding_corners) {
    if ((col & k_alpha_mask) == 0) return;
    if (user_texture_id == nullptr) return;

    if (rounding <= 0.0f || (rounding_corners & 0xf) == 0) {
        AddImage(user_texture_id, p_min, p_max, uv_min, uv_max, col);
        return;
    }

    bool const push_texture_id = texture_id_stack.Empty() || user_texture_id != texture_id_stack.Back();
    if (push_texture_id) PushTextureHandle(user_texture_id);

    int const vert_start_idx = vtx_buffer.size;
    PathRect(p_min, p_max, rounding, rounding_corners);
    PathFillConvex(col);
    int const vert_end_idx = vtx_buffer.size;
    ShadeVertsLinearUV(this, vert_start_idx, vert_end_idx, p_min, p_max, uv_min, uv_max, true);

    if (push_texture_id) PopTextureHandle();
}

#define STBTT_ifloor(x)    ((int)Floor(x))
#define STBTT_iceil(x)     ((int)Ceil(x))
#define STBTT_sqrt(x)      Sqrt(x)
#define STBTT_fabs(x)      Fabs(x)
#define STBTT_malloc(x, u) ((void)(u), GpaAlloc(x))
#define STBTT_free(x, u)   ((void)(u), GpaFree(x))
#define STBTT_assert(x)    ASSERT(x)
#define STBTT_strlen(x)    NullTerminatedSize(x)
#define STBTT_memcpy       CopyMemory
#define STBTT_memset       FillMemory

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"
#pragma clang diagnostic pop

void FontAtlas::ClearInputData() {
    for (int i = 0; i < config_data.size; i++)
        if (!config_data[i].font_data_reference_only && config_data[i].font_data &&
            config_data[i].font_data_owned_by_atlas) {
            GpaFree(config_data[i].font_data);
            config_data[i].font_data = nullptr;
        }

    // When clearing this we lose access to the font name and other information used to build the font.
    for (int i = 0; i < fonts.size; i++)
        if (fonts[i]->config_data >= config_data.data &&
            fonts[i]->config_data < config_data.data + config_data.size) {
            fonts[i]->config_data = nullptr;
        }
    config_data.Clear();
}

void FontAtlas::ClearTexData() {
    if (tex_pixels_alpha8) GpaFree(tex_pixels_alpha8);
    if (tex_pixels_rgb_a32) GpaFree(tex_pixels_rgb_a32);
    tex_pixels_alpha8 = nullptr;
    tex_pixels_rgb_a32 = nullptr;
}

void FontAtlas::ClearFonts() {
    for (int i = 0; i < fonts.size; i++) {
        fonts[i]->~Font();
        GpaFree(fonts[i]);
    }
    fonts.Clear();
}

void FontAtlas::Clear() {
    ClearInputData();
    ClearTexData();
    ClearFonts();
}

void FontAtlas::GetTexDataAsAlpha8(unsigned char** out_pixels,
                                   int* out_width,
                                   int* out_height,
                                   int* out_bytes_per_pixel) {
    // Build atlas on demand
    if (tex_pixels_alpha8 == nullptr) {
        if (config_data.Empty()) AddFontDefault();
        Build();
    }

    *out_pixels = tex_pixels_alpha8;
    if (out_width) *out_width = tex_width;
    if (out_height) *out_height = tex_height;
    if (out_bytes_per_pixel) *out_bytes_per_pixel = 1;
}

void FontAtlas::GetTexDataAsRGBA32(unsigned char** out_pixels,
                                   int* out_width,
                                   int* out_height,
                                   int* out_bytes_per_pixel) {
    // Convert to RGBA32 format on demand
    // Although it is likely to be the most commonly used format, our font rendering is 1 channel / 8 bpp
    if (!tex_pixels_rgb_a32) {
        unsigned char* pixels;
        GetTexDataAsAlpha8(&pixels, nullptr, nullptr);
        tex_pixels_rgb_a32 = (unsigned int*)GpaAlloc((usize)(tex_width * tex_height * 4));
        unsigned char const* src = pixels;
        unsigned int* dst = tex_pixels_rgb_a32;
        for (int n = tex_width * tex_height; n > 0; n--)
            *dst++ = ColU32(255, 255, 255, (u8)(*src++));
    }

    *out_pixels = (unsigned char*)tex_pixels_rgb_a32;
    if (out_width) *out_width = tex_width;
    if (out_height) *out_height = tex_height;
    if (out_bytes_per_pixel) *out_bytes_per_pixel = 4;
}

Font* FontAtlas::AddFont(FontConfig const* font_cfg) {
    ASSERT(font_cfg->font_data != nullptr && font_cfg->font_data_size > 0);
    ASSERT(font_cfg->size_pixels > 0.0f);

    // Create new font
    if (!font_cfg->merge_mode) {
        auto* font = (Font*)GpaAlloc(sizeof(Font));
        PLACEMENT_NEW(font) Font();
        fonts.PushBack(font);
    }

    config_data.PushBack(*font_cfg);
    FontConfig& new_font_cfg = config_data.Back();
    if (!new_font_cfg.dst_font) new_font_cfg.dst_font = fonts.Back();
    if (!new_font_cfg.font_data_reference_only && !new_font_cfg.font_data_owned_by_atlas) {
        new_font_cfg.font_data = GpaAlloc((usize)new_font_cfg.font_data_size);
        new_font_cfg.font_data_owned_by_atlas = true;
        CopyMemory(new_font_cfg.font_data, font_cfg->font_data, (usize)new_font_cfg.font_data_size);
    }

    // Invalidate texture
    ClearTexData();
    return new_font_cfg.dst_font;
}

// Default font TTF is compressed with stb_compress then base85 encoded (see
// extra_fonts/binary_to_compressed_c.cpp for encoder)
// NOLINTNEXTLINE(readability-identifier-naming)
static unsigned int stb_decompress_length(unsigned char* input);
// NOLINTNEXTLINE(readability-identifier-naming)
static unsigned int stb_decompress(unsigned char* output, unsigned char* i, unsigned int length);
static char const* GetDefaultCompressedFontDataTTFBase85();
static unsigned int Decode85Byte(unsigned char c) { return c >= '\\' ? c - 36 : c - 35; }
static void Decode85(unsigned char const* src, unsigned char* dst) {
    while (*src) {
        unsigned int const tmp =
            Decode85Byte(src[0]) +
            85 * (Decode85Byte(src[1]) +
                  85 * (Decode85Byte(src[2]) + 85 * (Decode85Byte(src[3]) + 85 * Decode85Byte(src[4]))));
        dst[0] = ((tmp >> 0) & 0xFF);
        dst[1] = ((tmp >> 8) & 0xFF);
        dst[2] = ((tmp >> 16) & 0xFF);
        dst[3] = ((tmp >> 24) & 0xFF); // We can't assume little-endianness.
        src += 5;
        dst += 4;
    }
}

// Load embedded ProggyClean.ttf at size 13, disable oversampling
Font* FontAtlas::AddFontDefault(FontConfig const* font_cfg_template) {
    FontConfig font_cfg = font_cfg_template ? *font_cfg_template : FontConfig();
    if (!font_cfg_template) {
        font_cfg.oversample_h = font_cfg.oversample_v = 1;
        font_cfg.pixel_snap_h = true;
    }
    if (font_cfg.name[0] == '\0') CopyStringIntoBufferWithNullTerm(font_cfg.name, "<default>");

    char const* ttf_compressed_base85 = GetDefaultCompressedFontDataTTFBase85();
    Font* font = AddFontFromMemoryCompressedBase85TTF(ttf_compressed_base85,
                                                      13.0f,
                                                      &font_cfg,
                                                      GetGlyphRangesDefault());
    return font;
}

// NBM Transfer ownership of 'ttf_data' to FontAtlas, unless font_cfg_template->FontDataOwnedByAtlas == false.
// Or, you can specific FontDataReferenceOnly = true in which case you must keep the memory valid until
// Build(). Owned TTF buffer will be deleted after Build().
Font* FontAtlas::AddFontFromMemoryTTF(void* ttf_data,
                                      int ttf_size,
                                      f32 size_pixels,
                                      FontConfig const* font_cfg_template,
                                      Span<GlyphRange const> glyph_ranges) {
    FontConfig font_cfg = font_cfg_template ? *font_cfg_template : FontConfig();
    ASSERT(glyph_ranges.size < font_cfg.glyph_ranges.Capacity());
    ASSERT(font_cfg.font_data == nullptr);
    font_cfg.font_data = ttf_data;
    font_cfg.font_data_size = ttf_size;
    font_cfg.size_pixels = size_pixels;
    if (glyph_ranges.size) font_cfg.glyph_ranges = glyph_ranges;
    return AddFont(&font_cfg);
}

Font* FontAtlas::AddFontFromMemoryCompressedTTF(void const* compressed_ttf_data,
                                                int compressed_ttf_size,
                                                f32 size_pixels,
                                                FontConfig const* font_cfg_template,
                                                Span<GlyphRange const> glyph_ranges) {
    unsigned int const buf_decompressed_size = stb_decompress_length((unsigned char*)compressed_ttf_data);
    auto* buf_decompressed_data = (unsigned char*)GpaAlloc(buf_decompressed_size);
    stb_decompress(buf_decompressed_data,
                   (unsigned char*)compressed_ttf_data,
                   (unsigned int)compressed_ttf_size);

    FontConfig font_cfg = font_cfg_template ? *font_cfg_template : FontConfig();
    ASSERT(font_cfg.font_data == nullptr);
    font_cfg.font_data_owned_by_atlas = true;
    return AddFontFromMemoryTTF(buf_decompressed_data,
                                (int)buf_decompressed_size,
                                size_pixels,
                                &font_cfg,
                                glyph_ranges);
}

Font* FontAtlas::AddFontFromMemoryCompressedBase85TTF(char const* compressed_ttf_data_base85,
                                                      f32 size_pixels,
                                                      FontConfig const* font_cfg,
                                                      Span<GlyphRange const> glyph_ranges) {
    int const compressed_ttf_size = (((int)NullTerminatedSize(compressed_ttf_data_base85) + 4) / 5) * 4;
    void* compressed_ttf = GpaAlloc((usize)compressed_ttf_size);
    Decode85((unsigned char const*)compressed_ttf_data_base85, (unsigned char*)compressed_ttf);
    Font* font = AddFontFromMemoryCompressedTTF(compressed_ttf,
                                                compressed_ttf_size,
                                                size_pixels,
                                                font_cfg,
                                                glyph_ranges);
    GpaFree(compressed_ttf);
    return font;
}

bool FontAtlas::Build() {
    ASSERT(config_data.size > 0);

    tex_id = nullptr;
    tex_width = tex_height = 0;
    tex_uv_white_pixel = f32x2 {0, 0};
    ClearTexData();

    struct FontTempBuildData {
        stbtt_fontinfo font_info;
        stbrp_rect* rects;
        stbtt_pack_range* ranges;
        int ranges_count;
    };
    auto* tmp_array = (FontTempBuildData*)GpaAlloc((usize)config_data.size * sizeof(FontTempBuildData));

    // Initialize font information early (so we can error without any cleanup) + count glyphs
    int total_glyph_count = 0;
    int total_glyph_range_count = 0;
    for (int input_i = 0; input_i < config_data.size; input_i++) {
        FontConfig& cfg = config_data[input_i];
        FontTempBuildData& tmp = tmp_array[input_i];

        ASSERT(cfg.dst_font && (!cfg.dst_font->IsLoaded() || cfg.dst_font->container_atlas == this));
        int const font_offset = stbtt_GetFontOffsetForIndex((unsigned char*)cfg.font_data, cfg.font_no);
        ASSERT(font_offset >= 0);
        if (!stbtt_InitFont(&tmp.font_info, (unsigned char*)cfg.font_data, font_offset)) return false;

        // Count glyphs
        if (!cfg.glyph_ranges.size) cfg.glyph_ranges = GetGlyphRangesDefault();
        for (auto const& glyph_range : cfg.glyph_ranges) {
            total_glyph_count += (glyph_range.end - glyph_range.start) + 1;
            total_glyph_range_count++;
        }
    }

    // Start packing. We need a known width for the skyline algorithm. Using a cheap heuristic here to decide
    // of width. User can override TexDesiredWidth if they wish. After packing is done, width shouldn't matter
    // much, but some API/GPU have texture size limitations and increasing width can decrease height.
    tex_width = (tex_desired_width > 0)      ? tex_desired_width
                : (total_glyph_count > 4000) ? 4096
                : (total_glyph_count > 2000) ? 2048
                : (total_glyph_count > 1000) ? 1024
                                             : 512;
    tex_height = 0;
    int const max_tex_height = 1024 * 32;
    stbtt_pack_context spc {};
    stbtt_PackBegin(&spc, nullptr, tex_width, max_tex_height, 0, 1, nullptr);

    // Pack our extra data rectangles first, so it will be on the upper-left corner of our texture (UV will
    // have small values).
    Vector<stbrp_rect> extra_rects;
    RenderCustomTexData(0, &extra_rects);
    stbtt_PackSetOversampling(&spc, 1, 1);
    stbrp_pack_rects((stbrp_context*)spc.pack_info, &extra_rects[0], extra_rects.size);
    for (int i = 0; i < extra_rects.size; i++)
        if (extra_rects[i].was_packed) tex_height = Max(tex_height, extra_rects[i].y + extra_rects[i].h);

    // Allocate packing character data and flag packed characters buffer as non-packed (x0=y0=x1=y1=0)
    int buf_packedchars_n = 0;
    int buf_rects_n = 0;
    int buf_ranges_n = 0;
    auto* buf_packedchars = (stbtt_packedchar*)GpaAlloc((usize)total_glyph_count * sizeof(stbtt_packedchar));
    auto* buf_rects = (stbrp_rect*)GpaAlloc((usize)total_glyph_count * sizeof(stbrp_rect));
    auto* buf_ranges = (stbtt_pack_range*)GpaAlloc((usize)total_glyph_range_count * sizeof(stbtt_pack_range));
    ZeroMemory(buf_packedchars, (usize)total_glyph_count * sizeof(stbtt_packedchar));
    ZeroMemory(buf_rects,
               (usize)total_glyph_count *
                   sizeof(stbrp_rect)); // Unnecessary but let's clear this for the sake of sanity.
    ZeroMemory(buf_ranges, (usize)total_glyph_range_count * sizeof(stbtt_pack_range));

    // First font pass: pack all glyphs (no rendering at this point, we are working with rectangles in an
    // infinitely tall texture at this point)
    for (int input_i = 0; input_i < config_data.size; input_i++) {
        FontConfig const& cfg = config_data[input_i];
        FontTempBuildData& tmp = tmp_array[input_i];

        // Setup ranges
        int glyph_count = 0;
        int glyph_ranges_count = 0;
        for (auto const& glyph_range : cfg.glyph_ranges) {
            glyph_count += (glyph_range.end - glyph_range.start) + 1;
            glyph_ranges_count++;
        }
        tmp.ranges = buf_ranges + buf_ranges_n;
        tmp.ranges_count = glyph_ranges_count;
        buf_ranges_n += glyph_ranges_count;
        for (int i = 0; i < glyph_ranges_count; i++) {
            auto const in_range = cfg.glyph_ranges[(u32)i];
            stbtt_pack_range& range = tmp.ranges[i];
            range.font_size = cfg.size_pixels;
            range.first_unicode_codepoint_in_range = in_range.start;
            range.num_chars = (in_range.end - in_range.start) + 1;
            range.chardata_for_range = buf_packedchars + buf_packedchars_n;
            buf_packedchars_n += range.num_chars;
        }

        // Pack
        tmp.rects = buf_rects + buf_rects_n;
        buf_rects_n += glyph_count;
        stbtt_PackSetOversampling(&spc, (unsigned)cfg.oversample_h, (unsigned)cfg.oversample_v);
        int const n =
            stbtt_PackFontRangesGatherRects(&spc, &tmp.font_info, tmp.ranges, tmp.ranges_count, tmp.rects);
        stbrp_pack_rects((stbrp_context*)spc.pack_info, tmp.rects, n);

        // Extend texture height
        for (int i = 0; i < n; i++)
            if (tmp.rects[i].was_packed) tex_height = Max(tex_height, tmp.rects[i].y + tmp.rects[i].h);
    }
    ASSERT(buf_rects_n == total_glyph_count);
    ASSERT(buf_packedchars_n == total_glyph_count);
    ASSERT(buf_ranges_n == total_glyph_range_count);

    // Create texture
    tex_height = (int)NextPowerOf2((u32)tex_height);
    tex_pixels_alpha8 = (unsigned char*)GpaAlloc((usize)(tex_width * tex_height));
    ZeroMemory(tex_pixels_alpha8, (usize)(tex_width * tex_height));
    spc.pixels = tex_pixels_alpha8;
    spc.height = tex_height;

    // Second pass: render characters
    for (int input_i = 0; input_i < config_data.size; input_i++) {
        FontConfig const& cfg = config_data[input_i];
        FontTempBuildData& tmp = tmp_array[input_i];
        stbtt_PackSetOversampling(&spc, (unsigned)cfg.oversample_h, (unsigned)cfg.oversample_v);
        stbtt_PackFontRangesRenderIntoRects(&spc, &tmp.font_info, tmp.ranges, tmp.ranges_count, tmp.rects);
        tmp.rects = nullptr;
    }

    // End packing
    stbtt_PackEnd(&spc);
    GpaFree(buf_rects);
    buf_rects = nullptr;

    // Third pass: setup Font and glyphs for runtime
    for (int input_i = 0; input_i < config_data.size; input_i++) {
        FontConfig& cfg = config_data[input_i];
        FontTempBuildData const& tmp = tmp_array[input_i];
        Font* dst_font = cfg.dst_font;

        f32 const font_scale = stbtt_ScaleForPixelHeight(&tmp.font_info, cfg.size_pixels);
        int unscaled_ascent;
        int unscaled_descent;
        int unscaled_line_gap;
        stbtt_GetFontVMetrics(&tmp.font_info, &unscaled_ascent, &unscaled_descent, &unscaled_line_gap);

        f32 const ascent = (f32)unscaled_ascent * font_scale;
        f32 const descent = (f32)unscaled_descent * font_scale;
        if (!cfg.merge_mode) {
            dst_font->container_atlas = this;
            dst_font->config_data = &cfg;
            dst_font->font_size = cfg.size_pixels;
            dst_font->ascent = ascent;
            dst_font->descent = descent;
            dst_font->glyphs.Resize(0);
        }
        f32 const off_y =
            (cfg.merge_mode && cfg.merge_glyph_center_v) ? (ascent - dst_font->ascent) * 0.5f : 0.0f;

        dst_font->fallback_glyph = nullptr; // Always clear fallback so FindGlyph can return nullptr. It will
                                            // be set again in BuildLookupTable()
        for (int i = 0; i < tmp.ranges_count; i++) {
            stbtt_pack_range const& range = tmp.ranges[i];
            for (int char_idx = 0; char_idx < range.num_chars; char_idx += 1) {
                stbtt_packedchar const& pc = range.chardata_for_range[char_idx];
                if (!pc.x0 && !pc.x1 && !pc.y0 && !pc.y1) continue;

                int const codepoint = range.first_unicode_codepoint_in_range + char_idx;
                if (cfg.merge_mode && dst_font->FindGlyph((Char16)codepoint)) continue;

                stbtt_aligned_quad q;
                f32 dummy_x = 0.0f;
                f32 dummy_y = 0.0f;
                stbtt_GetPackedQuad(range.chardata_for_range,
                                    tex_width,
                                    tex_height,
                                    char_idx,
                                    &dummy_x,
                                    &dummy_y,
                                    &q,
                                    0);

                dst_font->glyphs.Resize(dst_font->glyphs.size + 1);
                Font::Glyph& glyph = dst_font->glyphs.Back();
                glyph.codepoint = (Char16)codepoint;
                glyph.x0 = q.x0;
                glyph.y0 = q.y0;
                glyph.x1 = q.x1;
                glyph.y1 = q.y1;
                glyph.u0 = q.s0;
                glyph.v0 = q.t0;
                glyph.u1 = q.s1;
                glyph.v1 = q.t1;
                glyph.y0 += (f32)(int)(dst_font->ascent + off_y + 0.5f);
                glyph.y1 += (f32)(int)(dst_font->ascent + off_y + 0.5f);
                glyph.x_advance = (pc.xadvance + cfg.glyph_extra_spacing.x); // Bake spacing into XAdvance
                if (cfg.pixel_snap_h) glyph.x_advance = (f32)(int)(glyph.x_advance + 0.5f);
            }
        }
        cfg.dst_font->BuildLookupTable();
    }

    // Cleanup temporaries
    GpaFree(buf_packedchars);
    GpaFree(buf_ranges);
    GpaFree(tmp_array);

    // Render into our custom data block
    RenderCustomTexData(1, &extra_rects);

    return true;
}

void FontAtlas::RenderCustomTexData(int pass, void* p_rects) {
    // The white texels on the top left are the ones we'll use everywhere in Gui to render filled shapes.
    int const tex_data_w = 2;
    int const tex_data_h = 2;
    char const texture_data[tex_data_w * tex_data_h + 1] = {".."
                                                            ".."};

    Vector<stbrp_rect>& rects = *(Vector<stbrp_rect>*)p_rects;
    if (pass == 0) {
        // Request rectangles
        stbrp_rect r;
        ZeroMemory(&r, sizeof(r));
        r.w = (tex_data_w * 2) + 1;
        r.h = tex_data_h + 1;
        rects.PushBack(r);
    } else if (pass == 1) {
        // Render/copy pixels
        stbrp_rect const& r = rects[0];
        for (int y = 0, n = 0; y < tex_data_h; y++)
            for (int x = 0; x < tex_data_w; x++, n++) {
                int const offset0 = (int)(r.x + x) + (int)(r.y + y) * tex_width;
                int const offset1 = offset0 + 1 + tex_data_w;
                tex_pixels_alpha8[offset0] = texture_data[n] == '.' ? 0xFF : 0x00;
                tex_pixels_alpha8[offset1] = texture_data[n] == 'X' ? 0xFF : 0x00;
            }
        f32x2 const tex_uv_scale {1.0f / (f32)tex_width, 1.0f / (f32)tex_height};
        tex_uv_white_pixel = f32x2 {((f32)r.x + 0.5f) * tex_uv_scale.x, ((f32)r.y + 0.5f) * tex_uv_scale.y};
    }
}

GlyphRanges FontAtlas::GetGlyphRangesDefaultAudioPlugin() {
    GlyphRanges ranges;
    dyn::Assign(ranges,
                Array {
                    GlyphRange {0x0020, 0x00FF}, // Basic Latin + Latin Supplement
                    GlyphRange {0x221E, 0x221E}, // Infinity
                    GlyphRange {0x2019, 0x2019}, // Apostrophe
                });
    return ranges;
}

// Retrieve list of range (2 int per range, values are inclusive)
GlyphRanges FontAtlas::GetGlyphRangesDefault() {
    GlyphRanges ranges;
    dyn::Assign(ranges,
                Array {
                    GlyphRange {0x0020, 0x00FF}, // Basic Latin + Latin Supplement
                });
    return ranges;
}

//-----------------------------------------------------------------------------
// Font
//-----------------------------------------------------------------------------

void Font::BuildLookupTable() {
    int max_codepoint = 0;
    for (int i = 0; i != glyphs.size; i++)
        max_codepoint = Max(max_codepoint, (int)glyphs[i].codepoint);

    ASSERT(glyphs.size < 0xFFFF);
    index_x_advance.Clear();
    index_lookup.Clear();
    GrowIndex(max_codepoint + 1);
    for (int i = 0; i < glyphs.size; i++) {
        auto const codepoint = glyphs[i].codepoint;
        index_x_advance[codepoint] = glyphs[i].x_advance;
        index_lookup[codepoint] = (Char16)i;
    }

    // Create a glyph to handle TAB
    // FIXME: Needs proper TAB handling but it needs to be contextualized (or we could arbitrary say that each
    // string starts at "column 0" ?)
    if (FindGlyph(L' ')) {
        if (glyphs.Back().codepoint != '\t') // So we can call this function multiple times
            glyphs.Resize(glyphs.size + 1);
        Font::Glyph& tab_glyph = glyphs.Back();
        tab_glyph = *FindGlyph(L' ');
        tab_glyph.codepoint = '\t';
        tab_glyph.x_advance *= 4;
        index_x_advance[(int)tab_glyph.codepoint] = tab_glyph.x_advance;
        index_lookup[(int)tab_glyph.codepoint] = (Char16)(glyphs.size - 1);
    }

    fallback_glyph = nullptr;
    fallback_glyph = FindGlyph(k_fallback_char);
    fallback_x_advance = fallback_glyph ? fallback_glyph->x_advance : 0.0f;
    for (int i = 0; i < max_codepoint + 1; i++)
        if (index_x_advance[i] < 0.0f) index_x_advance[i] = fallback_x_advance;
}

void Font::GrowIndex(int new_size) {
    ASSERT(index_x_advance.size == index_lookup.size);
    int const old_size = index_lookup.size;
    if (new_size <= old_size) return;
    index_x_advance.Resize(new_size);
    index_lookup.Resize(new_size);
    for (int i = old_size; i < new_size; i++) {
        index_x_advance[i] = -1.0f;
        index_lookup[i] = k_invalid_codepoint;
    }
}

void Font::AddRemapChar(Char16 dst, Char16 src, bool overwrite_dst) {
    ASSERT(index_lookup.size > 0); // Currently this can only be called AFTER the font has been built, aka
                                   // after calling FontAtlas::GetTexDataAs*() function.
    int const index_size = index_lookup.size;

    if (dst < index_size && index_lookup.data[dst] == k_invalid_codepoint &&
        !overwrite_dst) // 'dst' already exists
        return;
    if (src >= index_size && dst >= index_size) // both 'dst' and 'src' don't exist -> no-op
        return;

    GrowIndex(dst + 1);
    index_lookup[dst] = (src < index_size) ? index_lookup.data[src] : k_invalid_codepoint;
    index_x_advance[dst] = (src < index_size) ? index_x_advance.data[src] : 1.0f;
}

Font::Glyph const* Font::FindGlyph(Char16 c) const {
    if (c < index_lookup.size) {
        auto const i = index_lookup[c];
        if (i != k_invalid_codepoint) return &glyphs.data[i];
    }
    return fallback_glyph;
}

char const*
Font::CalcWordWrapPositionA(f32 scale, char const* text, char const* text_end, f32 wrap_width) const {
    // Simple word-wrapping for English, not full-featured. Please submit failing cases!
    // FIXME: Much possible improvements (don't cut things like "word !", "word!!!" but cut within "word,,,,",
    // more sensible support for punctuations, support for Unicode punctuations, etc.)

    // For references, possible wrap point marked with ^
    //  "aaa bbb, ccc,ddd. eee   fff. ggg!"
    //      ^    ^    ^   ^   ^__    ^    ^

    // List of hardcoded separators: .,;!?'"

    // Skip extra blanks after a line returns (that includes not counting them in width computation)
    // e.g. "Hello    world" --> "Hello" "World"

    // Cut words that cannot possibly fit within one line.
    // e.g.: "The tropical fish" with ~5 characters worth of width --> "The tr" "opical" "fish"

    f32 line_width = 0.0f;
    f32 word_width = 0.0f;
    f32 blank_width = 0.0f;

    char const* word_end = text;
    char const* prev_word_end = nullptr;
    bool inside_word = true;

    char const* s = text;
    while (s < text_end) {
        auto c = (unsigned int)*s;
        char const* next_s;
        if (c < 0x80)
            next_s = s + 1;
        else
            next_s = s + Utf8CharacterToUtf32(&c, s, text_end, k_max_u16_codepoint);
        if (c == 0) break;

        if (c < 32) {
            if (c == '\n') {
                line_width = word_width = blank_width = 0.0f;
                inside_word = true;
                s = next_s;
                continue;
            }
            if (c == '\r') {
                s = next_s;
                continue;
            }
        }

        f32 const char_width =
            ((int)c < index_x_advance.size ? index_x_advance[(int)c] : fallback_x_advance) * scale;
        if (IsSpaceU32(c)) {
            if (inside_word) {
                line_width += blank_width;
                blank_width = 0.0f;
            }
            blank_width += char_width;
            inside_word = false;
        } else {
            word_width += char_width;
            if (inside_word) {
                word_end = next_s;
            } else {
                prev_word_end = word_end;
                line_width += word_width + blank_width;
                word_width = blank_width = 0.0f;
            }

            // Allow wrapping after punctuation.
            inside_word = !(c == '.' || c == ',' || c == ';' || c == '!' || c == '?' || c == '\"');
        }

        // We ignore blank width at the end of the line (they can be skipped)
        if (line_width + word_width >= wrap_width) {
            // Words that cannot possibly fit within an entire line will be cut anywhere.
            if (word_width < wrap_width) s = prev_word_end ? prev_word_end : word_end;
            break;
        }

        s = next_s;
    }

    return s;
}

f32x2 Font::CalcTextSizeA(f32 size, f32 max_width, f32 wrap_width, String str, char const** remaining) const {
    auto text_begin = str.data;
    auto text_end = End(str);

    f32 const line_height = size;
    f32 const scale = size / font_size;

    auto text_size = f32x2 {0, 0};
    f32 line_width = 0.0f;

    bool const word_wrap_enabled = (wrap_width > 0.0f);
    char const* word_wrap_eol = nullptr;

    char const* s = text_begin;
    while (s < text_end) {
        if (word_wrap_enabled) {
            // Calculate how far we can render. Requires two passes on the string data but keeps the code
            // simple and not intrusive for what's essentially an uncommon feature.
            if (!word_wrap_eol) {
                word_wrap_eol = CalcWordWrapPositionA(scale, s, text_end, wrap_width - line_width);
                if (word_wrap_eol == s) // Wrap_width is too small to fit anything. Force displaying 1
                                        // character to minimize the height discontinuity.
                    word_wrap_eol++; // +1 may not be a character start point in UTF-8 but it's ok because we
                                     // use s >= word_wrap_eol below
            }

            if (s >= word_wrap_eol) {
                if (text_size.x < line_width) text_size.x = line_width;
                text_size.y += line_height;
                line_width = 0.0f;
                word_wrap_eol = nullptr;

                // Wrapping skips upcoming blanks
                while (s < text_end) {
                    char const c = *s;
                    if (IsSpaceU32((unsigned)c)) {
                        s++;
                    } else if (c == '\n') {
                        s++;
                        break;
                    } else {
                        break;
                    }
                }
                continue;
            }
        }

        // Decode and advance source
        char const* prev_s = s;
        auto c = (unsigned int)*s;
        if (c < 0x80) {
            s += 1;
        } else {
            s += Utf8CharacterToUtf32(&c, s, text_end, k_max_u16_codepoint);
            if (c == 0) break;
        }

        if (c < 32) {
            if (c == '\n') {
                text_size.x = Max(text_size.x, line_width);
                text_size.y += line_height;
                line_width = 0.0f;
                continue;
            }
            if (c == '\r') continue;
        }

        f32 const char_width =
            ((int)c < index_x_advance.size ? index_x_advance[(int)c] : fallback_x_advance) * scale;
        if (line_width + char_width >= max_width) {
            s = prev_s;
            break;
        }

        line_width += char_width;
    }

    if (text_size.x < line_width) text_size.x = line_width;

    if (line_width > 0 || text_size.y == 0.0f) text_size.y += line_height;

    if (remaining) *remaining = s;

    return text_size;
}

void Font::RenderChar(DrawList* draw_list, f32 size, f32x2 pos, u32 col, Char16 c) const {
    if (c == ' ' || c == '\t' || c == '\n' ||
        c == '\r') // Match behavior of RenderText(), those 4 codepoints are hard-coded.
        return;
    if (Glyph const* glyph = FindGlyph(c)) {
        f32 const scale = (size >= 0.0f) ? (size / font_size) : 1.0f;
        pos.x = (f32)(int)pos.x + display_offset.x;
        pos.y = (f32)(int)pos.y + display_offset.y;
        f32x2 const pos_tl {pos.x + glyph->x0 * scale, pos.y + glyph->y0 * scale};
        f32x2 const pos_br {pos.x + glyph->x1 * scale, pos.y + glyph->y1 * scale};
        draw_list->PrimReserve(6, 4);
        draw_list->PrimRectUV(pos_tl,
                              pos_br,
                              f32x2 {glyph->u0, glyph->v0},
                              f32x2 {glyph->u1, glyph->v1},
                              col);
    }
}

void Font::RenderText(DrawList* draw_list,
                      f32 size,
                      f32x2 pos,
                      u32 col,
                      f32x4 const& clip_rect,
                      char const* text_begin,
                      char const* text_end,
                      f32 wrap_width,
                      bool cpu_fine_clip) const {
    if (!text_end) text_end = text_begin + NullTerminatedSize(text_begin);

    // Align to be pixel perfect
    pos.x = (f32)(int)pos.x + display_offset.x;
    pos.y = (f32)(int)pos.y + display_offset.y;
    f32 x = pos.x;
    f32 y = pos.y;
    if (y > clip_rect.w) return;

    f32 const scale = size / font_size;
    f32 const line_height = font_size * scale;
    bool const word_wrap_enabled = (wrap_width > 0.0f);
    char const* word_wrap_eol = nullptr;

    // Skip non-visible lines
    char const* s = text_begin;
    if (!word_wrap_enabled && y + line_height < clip_rect.y)
        while (s < text_end && *s != '\n') // Fast-forward to next line
            s++;

    // Reserve vertices for remaining worse case (over-reserving is useful and easily amortized)
    int const vtx_count_max = (int)(text_end - s) * 4;
    int const idx_count_max = (int)(text_end - s) * 6;
    int const idx_expected_size = draw_list->idx_buffer.size + idx_count_max;
    draw_list->PrimReserve(idx_count_max, vtx_count_max);

    DrawVert* vtx_write = draw_list->vtx_write_ptr;
    DrawIdx* idx_write = draw_list->idx_write_ptr;
    unsigned int vtx_current_idx = draw_list->vtx_current_idx;

    while (s < text_end) {
        if (word_wrap_enabled) {
            // Calculate how far we can render. Requires two passes on the string data but keeps the code
            // simple and not intrusive for what's essentially an uncommon feature.
            if (!word_wrap_eol) {
                word_wrap_eol = CalcWordWrapPositionA(scale, s, text_end, wrap_width - (x - pos.x));
                if (word_wrap_eol == s) // Wrap_width is too small to fit anything. Force displaying 1
                                        // character to minimize the height discontinuity.
                    word_wrap_eol++; // +1 may not be a character start point in UTF-8 but it's ok because we
                                     // use s >= word_wrap_eol below
            }

            if (s >= word_wrap_eol) {
                x = pos.x;
                y += line_height;
                word_wrap_eol = nullptr;

                // Wrapping skips upcoming blanks
                while (s < text_end) {
                    char const c = *s;
                    if (IsSpaceU32((unsigned)c)) {
                        s++;
                    } else if (c == '\n') {
                        s++;
                        break;
                    } else {
                        break;
                    }
                }
                continue;
            }
        }

        // Decode and advance source
        auto c = (unsigned int)*s;
        if (c < 0x80) {
            s += 1;
        } else {
            s += Utf8CharacterToUtf32(&c, s, text_end, k_max_u16_codepoint);
            if (c == 0) break;
        }

        if (c < 32) {
            if (c == '\n') {
                x = pos.x;
                y += line_height;

                if (y > clip_rect.w) break;
                if (!word_wrap_enabled && y + line_height < clip_rect.y)
                    while (s < text_end && *s != '\n') // Fast-forward to next line
                        s++;
                continue;
            }
            if (c == '\r') continue;
        }

        f32 char_width = 0.0f;
        if (Glyph const* glyph = FindGlyph((Char16)c)) {
            char_width = glyph->x_advance * scale;

            // Arbitrarily assume that both space and tabs are empty glyphs as an optimization
            if (c != ' ' && c != '\t') {
                // We don't do a second finer clipping test on the Y axis as we've already skipped anything
                // before clip_rect.y and exit once we pass clip_rect.w
                f32 x1 = x + glyph->x0 * scale;
                f32 x2 = x + glyph->x1 * scale;
                f32 y1 = y + glyph->y0 * scale;
                f32 y2 = y + glyph->y1 * scale;
                if (x1 <= clip_rect.z && x2 >= clip_rect.x) {
                    // Render a character
                    f32 u1 = glyph->u0;
                    f32 v1 = glyph->v0;
                    f32 u2 = glyph->u1;
                    f32 v2 = glyph->v1;

                    // CPU side clipping used to fit text in their frame when the frame is too small. Only
                    // does clipping for axis aligned quads.
                    if (cpu_fine_clip) {
                        if (x1 < clip_rect.x) {
                            u1 = u1 + (1.0f - (x2 - clip_rect.x) / (x2 - x1)) * (u2 - u1);
                            x1 = clip_rect.x;
                        }
                        if (y1 < clip_rect.y) {
                            v1 = v1 + (1.0f - (y2 - clip_rect.y) / (y2 - y1)) * (v2 - v1);
                            y1 = clip_rect.y;
                        }
                        if (x2 > clip_rect.z) {
                            u2 = u1 + ((clip_rect.z - x1) / (x2 - x1)) * (u2 - u1);
                            x2 = clip_rect.z;
                        }
                        if (y2 > clip_rect.w) {
                            v2 = v1 + ((clip_rect.w - y1) / (y2 - y1)) * (v2 - v1);
                            y2 = clip_rect.w;
                        }
                        if (y1 >= y2) {
                            x += char_width;
                            continue;
                        }
                    }

                    // We are NOT calling PrimRectUV() here because non-inlined causes too much overhead in a
                    // debug build. Inlined here:
                    {
                        idx_write[0] = (DrawIdx)(vtx_current_idx);
                        idx_write[1] = (DrawIdx)(vtx_current_idx + 1);
                        idx_write[2] = (DrawIdx)(vtx_current_idx + 2);
                        idx_write[3] = (DrawIdx)(vtx_current_idx);
                        idx_write[4] = (DrawIdx)(vtx_current_idx + 2);
                        idx_write[5] = (DrawIdx)(vtx_current_idx + 3);
                        vtx_write[0].pos.x = x1;
                        vtx_write[0].pos.y = y1;
                        vtx_write[0].col = col;
                        vtx_write[0].uv.x = u1;
                        vtx_write[0].uv.y = v1;
                        vtx_write[1].pos.x = x2;
                        vtx_write[1].pos.y = y1;
                        vtx_write[1].col = col;
                        vtx_write[1].uv.x = u2;
                        vtx_write[1].uv.y = v1;
                        vtx_write[2].pos.x = x2;
                        vtx_write[2].pos.y = y2;
                        vtx_write[2].col = col;
                        vtx_write[2].uv.x = u2;
                        vtx_write[2].uv.y = v2;
                        vtx_write[3].pos.x = x1;
                        vtx_write[3].pos.y = y2;
                        vtx_write[3].col = col;
                        vtx_write[3].uv.x = u1;
                        vtx_write[3].uv.y = v2;
                        vtx_write += 4;
                        vtx_current_idx += 4;
                        idx_write += 6;
                    }
                }
            }
        }

        x += char_width;
    }

    // Give back unused vertices
    draw_list->vtx_buffer.Resize((int)(vtx_write - draw_list->vtx_buffer.data));
    draw_list->idx_buffer.Resize((int)(idx_write - draw_list->idx_buffer.data));
    draw_list->cmd_buffer[draw_list->cmd_buffer.size - 1].elem_count -=
        (unsigned)(idx_expected_size - draw_list->idx_buffer.size);
    draw_list->vtx_write_ptr = vtx_write;
    draw_list->idx_write_ptr = idx_write;
    draw_list->vtx_current_idx = (unsigned int)draw_list->vtx_buffer.size;
}

//-----------------------------------------------------------------------------
// DEFAULT FONT DATA
//-----------------------------------------------------------------------------
// Compressed with stb_compress() then converted to a C array.
// Use the program in extra_fonts/binary_to_compressed_c.cpp to create the array from a TTF file.
// Decompression from stb.h (public domain) by Sean Barrett https://github.com/nothings/stb/blob/master/stb.h
//-----------------------------------------------------------------------------

static unsigned int stb_decompress_length(unsigned char* input) {
    return ((unsigned)input[8] << 24) + ((unsigned)input[9] << 16) + ((unsigned)input[10] << 8) + input[11];
}

static unsigned char *stb__barrier, *stb__barrier2, *stb__barrier3, *stb__barrier4;
static unsigned char* stb__dout;
static void stb__match(unsigned char* data, unsigned int length) { // NOLINT(readability-identifier-naming)
    // INVERSE of memmove... write each byte before copying the next...
    ASSERT(stb__dout + length <= stb__barrier);
    if (stb__dout + length > stb__barrier) {
        stb__dout += length;
        return;
    }
    if (data < stb__barrier4) {
        stb__dout = stb__barrier + 1;
        return;
    }
    while (length--)
        *stb__dout++ = *data++;
}

// NOLINTNEXTLINE(readability-identifier-naming)
static void stb__lit(unsigned char* data, unsigned int length) {
    ASSERT(stb__dout + length <= stb__barrier);
    if (stb__dout + length > stb__barrier) {
        stb__dout += length;
        return;
    }
    if (data < stb__barrier2) {
        stb__dout = stb__barrier + 1;
        return;
    }
    CopyMemory(stb__dout, data, length);
    stb__dout += length;
}

#define stb__in2(x) (((unsigned)i[x] << 8u) + (unsigned)i[(x) + 1u])
#define stb__in3(x) (((unsigned)i[x] << 16u) + stb__in2((x) + 1u))
#define stb__in4(x) (((unsigned)i[x] << 24u) + stb__in3((x) + 1u))

// NOLINTNEXTLINE(readability-identifier-naming)
static unsigned char* stb_decompress_token(unsigned char* i) {
    if (*i >= 0x20) { // use fewer if's for cases that expand small
        if (*i >= 0x80)
            stb__match(stb__dout - i[1] - 1, i[0] - 0x80 + 1), i += 2;
        else if (*i >= 0x40)
            stb__match(stb__dout - (stb__in2(0) - 0x4000 + 1), i[2] + 1), i += 3;
        else /* *i >= 0x20 */
            stb__lit(i + 1, i[0] - 0x20 + 1), i += 1 + (i[0] - 0x20 + 1);
    } else { // more ifs for cases that expand large, since overhead is amortized
        if (*i >= 0x18)
            stb__match(stb__dout - (stb__in3(0) - 0x180000 + 1), i[3] + 1), i += 4;
        else if (*i >= 0x10)
            stb__match(stb__dout - (stb__in3(0) - 0x100000 + 1), stb__in2(3) + 1), i += 5;
        else if (*i >= 0x08)
            stb__lit(i + 2, stb__in2(0) - 0x0800 + 1), i += 2 + (stb__in2(0) - 0x0800 + 1);
        else if (*i == 0x07)
            stb__lit(i + 3, stb__in2(1) + 1), i += 3 + (stb__in2(1) + 1);
        else if (*i == 0x06)
            stb__match(stb__dout - (stb__in3(1) + 1), i[4] + 1), i += 5;
        else if (*i == 0x04)
            stb__match(stb__dout - (stb__in3(1) + 1), stb__in2(4) + 1), i += 6;
    }
    return i;
}

// NOLINTNEXTLINE(readability-identifier-naming)
static unsigned int stb_adler32(unsigned int adler32, unsigned char* buffer, unsigned int buflen) {
    unsigned long const adler_mod = 65521;
    unsigned long s1 = adler32 & 0xffff;
    unsigned long s2 = adler32 >> 16;
    unsigned long blocklen;
    unsigned long i;

    blocklen = buflen % 5552;
    while (buflen) {
        for (i = 0; i + 7 < blocklen; i += 8) {
            s1 += buffer[0], s2 += s1;
            s1 += buffer[1], s2 += s1;
            s1 += buffer[2], s2 += s1;
            s1 += buffer[3], s2 += s1;
            s1 += buffer[4], s2 += s1;
            s1 += buffer[5], s2 += s1;
            s1 += buffer[6], s2 += s1;
            s1 += buffer[7], s2 += s1;

            buffer += 8;
        }

        for (; i < blocklen; ++i)
            s1 += *buffer++, s2 += s1;

        s1 %= adler_mod, s2 %= adler_mod;
        buflen -= blocklen;
        blocklen = 5552;
    }
    return (unsigned int)(s2 << 16) + (unsigned int)s1;
}

static unsigned int stb_decompress(unsigned char* output, unsigned char* i, unsigned int length) {
    unsigned int olen;
    if (stb__in4(0) != 0x57bC0000) return 0;
    if (stb__in4(4) != 0) return 0; // error! stream is > 4GB
    olen = stb_decompress_length(i);
    stb__barrier2 = i;
    stb__barrier3 = i + length;
    stb__barrier = output + olen;
    stb__barrier4 = output;
    i += 16;

    stb__dout = output;
    for (;;) {
        unsigned char* old_i = i;
        i = stb_decompress_token(i);
        if (i == old_i) {
            if (*i == 0x05 && i[1] == 0xfa) {
                ASSERT(stb__dout == output + olen);
                if (stb__dout != output + olen) return 0;
                if (stb_adler32(1, output, olen) != (unsigned int)stb__in4(2)) return 0;
                return olen;
            } else {
                PanicIfReached(); /* NOTREACHED */
                return 0;
            }
        }
        ASSERT(stb__dout <= output + olen);
        if (stb__dout > output + olen) return 0;
    }
}

//-----------------------------------------------------------------------------
// ProggyClean.ttf
// Copyright (c) 2004, 2005 Tristan Grimmer
// MIT license (see License.txt in http://www.upperbounds.net/download/ProggyClean.ttf.zip)
// Download and more information at http://upperbounds.net
//-----------------------------------------------------------------------------
// File: 'ProggyClean.ttf' (41208 bytes)
// Exported using binary_to_compressed_c.cpp
//-----------------------------------------------------------------------------
static char const proggy_clean_ttf_compressed_data_base85[11980 + 1] =
    "7])#######hV0qs'/###[),##/l:$#Q6>##5[n42>c-TH`->>#/"
    "e>11NNV=Bv(*:.F?uu#(gRU.o0XGH`$vhLG1hxt9?W`#,5LsCp#-i>.r$<$6pD>Lb';9Crc6tgXmKVeU2cD4Eo3R/"
    "2*>]b(MC;$jPfY.;h^`IWM9<Lh2TlS+f-s$o6Q<BWH`YiU.xfLq$N;$0iR/GX:U(jcW2p/"
    "W*q?-qmnUCI;jHSAiFWM.R*kU@C=GH?a9wp8f$e.-4^Qg1)Q-GL(lf(r/7GrRgwV%MS=C#"
    "`8ND>Qo#t'X#(v#Y9w0#1D$CIf;W'#pWUPXOuxXuU(H9M(1<q-UE31#^-V'8IRUo7Qf./"
    "L>=Ke$$'5F%)]0^#0X@U.a<r:QLtFsLcL6##lOj)#.Y5<-R&KgLwqJfLgN&;Q?gI^#DY2uL"
    "i@^rMl9t=cWq6##weg>$FBjVQTSDgEKnIS7EM9>ZY9w0#L;>>#Mx&4Mvt//"
    "L[MkA#W@lK.N'[0#7RL_&#w+F%HtG9M#XL`N&.,GM4Pg;-<nLENhvx>-VsM.M0rJfLH2eTM`*oJMHRC`N"
    "kfimM2J,W-jXS:)r0wK#@Fge$U>`w'N7G#$#fB#$E^$#:9:hk+eOe--6x)F7*E%?76%^GMHePW-Z5l'&GiF#$956:rS?dA#fiK:)Yr+`"
    "&#0j@'DbG&#^$PG.Ll+DNa<XCMKEV*N)LN/N"
    "*b=%Q6pia-Xg8I$<MR&,VdJe$<(7G;Ckl'&hF;;$<_=X(b.RS%%)###MPBuuE1V:v&cX&#2m#(&cV]`k9OhLMbn%s$G2,B$BfD3X*"
    "sp5#l,$R#]x_X1xKX%b5U*[r5iMfUo9U`N99hG)"
    "tm+/Us9pG)XPu`<0s-)WTt(gCRxIg(%6sfh=ktMKn3j)<6<b5Sk_/0(^]AaN#(p/L>&VZ>1i%h1S9u5o@YaaW$e+b<TWFn/"
    "Z:Oh(Cx2$lNEoN^e)#CFY@@I;BOQ*sRwZtZxRcU7uW6CX"
    "ow0i(?$Q[cjOd[P4d)]>ROPOpxTO7Stwi1::iB1q)C_=dV26J;2,]7op$]uQr@_V7$q^%lQwtuHY]=DX,n3L#0PHDO4f9>dC@O>"
    "HBuKPpP*E,N+b3L#lpR/MrTEH.IAQk.a>D[.e;mc."
    "x]Ip.PH^'/aqUO/$1WxLoW0[iLA<QT;5HKD+@qQ'NQ(3_PLhE48R.qAPSwQ0/WK?Z,[x?-J;jQTWA0X@KJ(_Y8N-:/M74:/"
    "-ZpKrUss?d#dZq]DAbkU*JqkL+nwX@@47`5>w=4h(9.`G"
    "CRUxHPeR`5Mjol(dUWxZa(>STrPkrJiWx`5U7F#.g*jrohGg`cg:lSTvEY/"
    "EV_7H4Q9[Z%cnv;JQYZ5q.l7Zeas:HOIZOB?G<Nald$qs]@]L<J7bR*>gv:[7MI2k).'2($5FNP&EQ(,)"
    "U]W]+fh18.vsai00);D3@4ku5P?DP8aJt+;qUM]=+b'8@;mViBKx0DE[-auGl8:PJ&Dj+M6OC]O^((##]`0i)drT;-7X`=-H3["
    "igUnPG-NZlo.#k@h#=Ork$m>a>$-?Tm$UV(?#P6YY#"
    "'/###xe7q.73rI3*pP/$1>s9)W,JrM7SN]'/"
    "4C#v$U`0#V.[0>xQsH$fEmPMgY2u7Kh(G%siIfLSoS+MK2eTM$=5,M8p`A.;_R%#u[K#$x4AG8.kK/HSB==-'Ie/QTtG?-.*^N-4B/ZM"
    "_3YlQC7(p7q)&](`6_c)$/*JL(L-^(]$wIM`dPtOdGA,U3:w2M-0<q-]L_?^)1vw'.,MRsqVr.L;aN&#/"
    "EgJ)PBc[-f>+WomX2u7lqM2iEumMTcsF?-aT=Z-97UEnXglEn1K-bnEO`gu"
    "Ft(c%=;Am_Qs@jLooI&NX;]0#j4#F14;gl8-GQpgwhrq8'=l_f-b49'UOqkLu7-##oDY2L(te+Mch&gLYtJ,MEtJfLh'x'M=$CS-ZZ%"
    "P]8bZ>#S?YY#%Q&q'3^Fw&?D)UDNrocM3A76/"
    "/oL?#h7gl85[qW/"
    "NDOk%16ij;+:1a'iNIdb-ou8.P*w,v5#EI$TWS>Pot-R*H'-SEpA:g)f+O$%%`kA#G=8RMmG1&O`>to8bC]T&$,n.LoO>29sp3dt-"
    "52U%VM#q7'DHpg+#Z9%H[K<L"
    "%a2E-grWVM3@2=-k22tL]4$##6We'8UJCKE[d_=%wI;'6X-GsLX4j^SgJ$##R*w,vP3wK#iiW&#*h^D&R?jp7+/"
    "u&#(AP##XU8c$fSYW-J95_-Dp[g9wcO&#M-h1OcJlc-*vpw0xUX&#"
    "OQFKNX@QI'IoPp7nb,QU//"
    "MQ&ZDkKP)X<WSVL(68uVl&#c'[0#(s1X&xm$Y%B7*K:eDA323j998GXbA#pwMs-jgD$9QISB-A_(aN4xoFM^@C58D0+Q+q3n0#"
    "3U1InDjF682-SjMXJK)("
    "h$hxua_K]ul92%'BOU&#BRRh-slg8KDlr:%L71Ka:.A;%YULjDPmL<LYs8i#XwJOYaKPKc1h:'9Ke,g)b),78=I39B;xiY$bgGw-&."
    "Zi9InXDuYa%G*f2Bq7mn9^#p1vv%#(Wi-;/Z5h"
    "o;#2:;%d&#x9v68C5g?ntX0X)pT`;%pB3q7mgGN)3%(P8nTd5L7GeA-GL@+%J3u2:(Yf>et`e;)f#Km8&+DC$I46>#Kr]]u-[="
    "99tts1.qb#q72g1WJO81q+eN'03'eM>&1XxY-caEnO"
    "j%2n8)),?ILR5^.Ibn<-X-Mq7[a82Lq:F&#ce+S9wsCK*x`569E8ew'He]h:sI[2LM$[guka3ZRd6:t%IG:;$%YiJ:Nq=?eAw;/"
    ":nnDq0(CYcMpG)qLN4$##&J<j$UpK<Q4a1]MupW^-"
    "sj_$%[HK%'F####QRZJ::Y3EGl4'@%FkiAOg#p[##O`gukTfBHagL<LHw%q&OV0##F=6/"
    ":chIm0@eCP8X]:kFI%hl8hgO@RcBhS-@Qb$%+m=hPDLg*%K8ln(wcf3/'DW-$.lR?n[nCH-"
    "eXOONTJlh:.RYF%3'p6sq:UIMA945&^HFS87@$EP2iG<-lCO$%c`uKGD3rC$x0BL8aFn--`ke%#HMP'vh1/"
    "R&O_J9'um,.<tx[@%wsJk&bUT2`0uMv7gg#qp/ij.L56'hl;.s5CUrxjO"
    "M7-##.l+Au'A&O:-T72L]P`&=;ctp'XScX*rU.>-XTt,%OVU4)S1+R-#dg0/"
    "Nn?Ku1^0f$B*P:Rowwm-`0PKjYDDM'3]d39VZHEl4,.j']Pk-M.h^&:0FACm$maq-&sgw0t7/6(^xtk%"
    "LuH88Fj-ekm>GA#_>568x6(OFRl-IZp`&b,_P'$M<Jnq79VsJW/mWS*PUiq76;]/NM_>hLbxfc$mj`,O;&%W2m`Zh:/"
    ")Uetw:aJ%]K9h:TcF]u_-Sj9,VK3M.*'&0D[Ca]J9gp8,kAW]"
    "%(?A%R$f<->Zts'^kn=-^@c4%-pY6qI%J%1IGxfLU9CP8cbPlXv);C=b),<2mOvP8up,UVf3839acAWAW-W?#ao/"
    "^#%KYo8fRULNd2.>%m]UK:n%r$'sw]J;5pAoO_#2mO3n,'=H5(et"
    "Hg*`+RLgv>=4U8guD$I%D:W>-r5V*%j*W:Kvej.Lp$<M-SGZ':+Q_k+uvOSLiEo(<aD/"
    "K<CCc`'Lx>'?;++O'>()jLR-^u68PHm8ZFWe+ej8h:9r6L*0//c&iH&R8pRbA#Kjm%upV1g:"
    "a_#Ur7FuA#(tRh#.Y5K+@?3<-8m0$PEn;J:rh6?I6uG<-`wMU'ircp0LaE_OtlMb&1#6T.#FDKu#1Lw%u%+GM+X'e?YLfjM["
    "VO0MbuFp7;>Q&#WIo)0@F%q7c#4XAXN-U&VB<HFF*qL("
    "$/V,;(kXZejWO`<[5?\?ewY(*9=%wDc;,u<'9t3W-(H1th3+G]ucQ]kLs7df($/"
    "*JL]@*t7Bu_G3_7mp7<iaQjO@.kLg;x3B0lqp7Hf,^Ze7-##@/c58Mo(3;knp0%)A7?-W+eI'o8)b<"
    "nKnw'Ho8C=Y>pqB>0ie&jhZ[?iLR@@_AvA-iQC(=ksRZRVp7`.=+NpBC%rh&3]R:8XDmE5^V8O(x<<aG/"
    "1N$#FX$0V5Y6x'aErI3I$7x%E`v<-BY,)%-?Psf*l?%C3.mM(=/M0:JxG'?"
    "7WhH%o'a<-80g0NBxoO(GH<dM]n.+%q@jH?f.UsJ2Ggs&4<-e47&Kl+f//"
    "9@`b+?.TeN_&B8Ss?v;^Trk;f#YvJkl&w$]>-+k?'(<S:68tq*WoDfZu';mM?8X[ma8W%*`-=;D.(nc7/;"
    ")g:T1=^J$&BRV(-lTmNB6xqB[@0*o.erM*<SWF]u2=st-*(6v>^](H.aREZSi,#1:[IXaZFOm<-ui#qUq2$##Ri;u75OK#(RtaW-K-F`"
    "S+cF]uN`-KMQ%rP/Xri.LRcB##=YL3BgM/3M"
    "D?@f&1'BW-)Ju<L25gl8uhVm1hL$##*8###'A3/LkKW+(^rWX?5W_8g)a(m&K8P>#bmmWCMkk&#TR`C,5d>g)F;t,4:@_l8G/"
    "5h4vUd%&%950:VXD'QdWoY-F$BtUwmfe$YqL'8(PWX("
    "P?^@Po3$##`MSs?DWBZ/S>+4%>fX,VWv/w'KD`LP5IbH;rTV>n3cEK8U#bX]l-/"
    "V+^lj3;vlMb&[5YQ8#pekX9JP3XUC72L,,?+Ni&co7ApnO*5NK,((W-i:$,kp'UDAO(G0Sq7MVjJs"
    "bIu)'Z,*[>br5fX^:FPAWr-m2KgL<LUN098kTF&#lvo58=/vjDo;.;)Ka*hLR#/"
    "k=rKbxuV`>Q_nN6'8uTG&#1T5g)uLv:873UpTLgH+#FgpH'_o1780Ph8KmxQJ8#H72L4@768@Tm&Q"
    "h4CB/5OvmA&,Q&QbUoi$a_%3M01H)4x7I^&KQVgtFnV+;[Pc>[m4k//"
    ",]1?#`VY[Jr*3&&slRfLiVZJ:]?=K3Sw=[$=uRB?3xk48@aeg<Z'<$#4H)6,>e0jT6'N#(q%.O=?2S]u*(m<-"
    "V8J'(1)G][68hW$5'q[GC&5j`TE?m'esFGNRM)j,ffZ?-qx8;->g4t*:CIP/[Qap7/"
    "9'#(1sao7w-.qNUdkJ)tCF&#B^;xGvn2r9FEPFFFcL@.iFNkTve$m%#QvQS8U@)2Z+3K:AKM5i"
    "sZ88+dKQ)W6>J%CL<KE>`.d*(B`-n8D9oK<Up]c$X$(,)M8Zt7/"
    "[rdkqTgl-0cuGMv'?>-XV1q['-5k'cAZ69e;D_?$ZPP&s^+7])$*$#@QYi9,5P&#9r+$%CE=68>K8r0=dSC%%(@p7"
    ".m7jilQ02'0-VWAg<a/''3u.=4L$Y)6k/K:_[3=&jvL<L0C/"
    "2'v:^;-DIBW,B4E68:kZ;%?8(Q8BH=kO65BW?xSG&#@uU,DS*,?.+(o(#1vCS8#CHF>TlGW'b)Tq7VT9q^*^$$.:&N@@"
    "$&)WHtPm*5_rO0&e%K&#-30j(E4#'Zb.o/"
    "(Tpm$>K'f@[PvFl,hfINTNU6u'0pao7%XUp9]5.>%h`8_=VYbxuel.NTSsJfLacFu3B'lQSu/m6-Oqem8T+oE--$0a/"
    "k]uj9EwsG>%veR*"
    "hv^BFpQj:K'#SJ,sB-'#](j.Lg92rTw-*n%@/;39rrJF,l#qV%OrtBeC6/"
    ",;qB3ebNW[?,Hqj2L.1NP&GjUR=1D8QaS3Up&@*9wP?+lo7b?@%'k4`p0Z$22%K3+iCZj?XJN4Nm&+YF]u"
    "@-W$U%VEQ/,,>>#)D<h#`)h0:<Q6909ua+&VU%n2:cG3FJ-%@Bj-DgLr`Hw&HAKjKjseK</"
    "xKT*)B,N9X3]krc12t'pgTV(Lv-tL[xg_%=M_q7a^x?7Ubd>#%8cY#YZ?=,`Wdxu/ae&#"
    "w6)R89tI#6@s'(6Bf7a&?S=^ZI_kS&ai`&=tE72L_D,;^R)7[$s<Eh#c&)q.MXI%#v9ROa5FZO%sF7q7Nwb&#ptUJ:aqJe$Sl68%.D##"
    "#EC><?-aF&#RNQv>o8lKN%5/$(vdfq7+ebA#"
    "u1p]ovUKW&Y%q]'>$1@-[xfn$7ZTp7mM,G,Ko7a&Gu%G[RMxJs[0MM%wci.LFDK)(<c`Q8N)jEIF*+?P2a8g%)$q]o2aH8C&<SibC/"
    "q,(e:v;-b#6[$NtDZ84Je2KNvB#$P5?tQ3nt(0"
    "d=j.LQf./"
    "Ll33+(;q3L-w=8dX$#WF&uIJ@-bfI>%:_i2B5CsR8&9Z&#=mPEnm0f`<&c)QL5uJ#%u%lJj+D-r;BoF&#4DoS97h5g)E#o:&S4weDF,"
    "9^Hoe`h*L+_a*NrLW-1pG_&2UdB8"
    "6e%B/:=>)N4xeW.*wft-;$'58-ESqr<b?UI(_%@[P46>#U`'6AQ]m&6/"
    "`Z>#S?YY#Vc;r7U2&326d=w&H####?TZ`*4?&.MK?LP8Vxg>$[QXc%QJv92.(Db*B)gb*BM9dM*hJMAo*c&#"
    "b0v=Pjer]$gG&JXDf->'StvU7505l9$AFvgYRI^&<^b68?j#q9QX4SM'RO#&sL1IM.rJfLUAj221]d##DW=m83u5;'bYx,*Sl0hL(W;;"
    "$doB&O/TQ:(Z^xBdLjL<Lni;''X.`$#8+1GD"
    ":k$YUWsbn8ogh6rxZ2Z9]%nd+>V#*8U_72Lh+2Q8Cj0i:6hp&$C/:p(HK>T8Y[gHQ4`4)'$Ab(Nof%V'8hL&#<NEdtg(n'=S1A(Q1/"
    "I&4([%dM`,Iu'1:_hL>SfD07&6D<fp8dHM7/g+"
    "tlPN9J*rKaPct&?'uBCem^jn%9_K)<,C5K3s=5g&GmJb*[SYq7K;TRLGCsM-$$;S%:Y@r7AK0pprpL<Lrh,q7e/"
    "%KWK:50I^+m'vi`3?%Zp+<-d+$L-Sv:@.o19n$s0&39;kn;S%BSq*"
    "$3WoJSCLweV[aZ'MQIjO<7;X-X;&+dMLvu#^UsGEC9WEc[X(wI7#2.(F0jV*eZf<-Qv3J-c+J5AlrB#$p(H68LvEA'q3n0#m,[`*8Ft)"
    "FcYgEud]CWfm68,(aLA$@EFTgLXoBq/UPlp7"
    ":d[/;r_ix=:TF`S5H-b<LI&HY(K=h#)]Lk$K14lVfm:x$H<3^Ql<M`$OhapBnkup'D#L$Pb_`N*g]2e;X/"
    "Dtg,bsj&K#2[-:iYr'_wgH)NUIR8a1n#S?Yej'h8^58UbZd+^FKD*T@;6A"
    "7aQC[K8d-(v6GI$x:T<&'Gp5Uf>@M.*J:;$-rv29'M]8qMv-tLp,'886iaC=Hb*YJoKJ,(j%K=H`K.v9HggqBIiZu'QvBT.#=)"
    "0ukruV&.)3=(^1`o*Pj4<-<aN((^7('#Z0wK#5GX@7"
    "u][`*S^43933A4rl][`*O4CgLEl]v$1Q3AeF37dbXk,.)vj#x'd`;qgbQR%FW,2(?LO=s%Sc68%NP'##Aotl8x=BE#j1UD([3$M(]"
    "UI2LX3RpKN@;/#f'f/&_mt&F)XdF<9t4)Qa.*kT"
    "LwQ'(TTB9.xH'>#MJ+gLq9-##@HuZPN0]u:h7.T..G:;$/"
    "Usj(T7`Q8tT72LnYl<-qx8;-HV7Q-&Xdx%1a,hC=0u+HlsV>nuIQL-5<N?)NBS)QN*_I,?&)2'IM%L3I)X((e/dl2&8'<M"
    ":^#M*Q+[T.Xri.LYS3v%fF`68h;b-X[/En'CR.q7E)p'/"
    "kle2HM,u;^%OKC-N+Ll%F9CF<Nf'^#t2L,;27W:0O@6##U6W7:$rJfLWHj$#)woqBefIZ.PK<b*t7ed;p*_m;4ExK#h@&]>"
    "_>@kXQtMacfD.m-VAb8;IReM3$wf0''hra*so568'Ip&vRs849'MRYSp%:t:h5qSgwpEr$B>Q,;s(C#$)`svQuF$##-D,##,g68@2[T;"
    ".XSdN9Qe)rpt._K-#5wF)sP'##p#C0c%-Gb%"
    "hd+<-j'Ai*x&&HMkT]C'OSl##5RG[JXaHN;d'uA#x._U;.`PU@(Z3dt4r152@:v,'R.Sj'w#0<-;kPI)FfJ&#AYJ&#//"
    ")>-k=m=*XnK$>=)72L]0I%>.G690a:$##<,);?;72#?x9+d;"
    "^V'9;jY@;)br#q^YQpx:X#Te$Z^'=-=bGhLf:D6&bNwZ9-ZD#n^9HhLMr5G;']d&6'wYmTFmL<LD)F^%[tC'8;+9E#C$g%#5Y>q9wI>"
    "P(9mI[>kC-ekLC/R&CH+s'B;K-M6$EB%is00:"
    "+A4[7xks.LrNk0&E)wILYF@2L'0Nb$+pv<(2.768/"
    "FrY&h$^3i&@+G%JT'<-,v`3;_)I9M^AE]CN?Cl2AZg+%4iTpT3<n-&%H%b<FDj2M<hH=&Eh<2Len$b*aTX=-8QxN)k11IM1c^j%"
    "9s<L<NFSo)B?+<-(GxsF,^-Eh@$4dXhN$+#rxK8'je'D7k`e;)2pYwPA'_p9&@^18ml1^[@g4t*[JOa*[=Qp7(qJ_oOL^('7fB&Hq-:"
    "sf,sNj8xq^>$U4O]GKx'm9)b@p7YsvK3w^YR-"
    "CdQ*:Ir<($u&)#(&?L9Rg3H)4fiEp^iI9O8KnTj,]H?D*r7'M;PwZ9K0E^k&-cpI;.p/6_vwoFMV<->#%Xi.LxVnrU(4&8/"
    "P+:hLSKj$#U%]49t'I:rgMi'FL@a:0Y-uA[39',(vbma*"
    "hU%<-SRF`Tt:542R_VV$p@[p8DV[A,?1839FWdF<TddF<9Ah-6&9tWoDlh]&1SpGMq>Ti1O*H&#(AL8[_P%.M>v^-))qOT*F5Cq0`Ye%"
    "+$B6i:7@0IX<N+T+0MlMBPQ*Vj>SsD<U4JHY"
    "8kD2)2fU/M#$e.)T4,_=8hLim[&);?UkK'-x?'(:siIfL<$pFM`i<?%W(mGDHM%>iWP,##P`%/L<eXi:@Z9C.7o=@(pXdAO/"
    "NLQ8lPl+HPOQa8wD8=^GlPa8TKI1CjhsCTSLJM'/Wl>-"
    "S(qw%sf/@%#B6;/"
    "U7K]uZbi^Oc^2n<bhPmUkMw>%t<)'mEVE''n`WnJra$^TKvX5B>;_aSEK',(hwa0:i4G?.Bci.(X[?b*($,=-n<.Q%`(X=?+@Am*Js0&"
    "=3bh8K]mL<LoNs'6,'85`"
    "0?t/'_U59@]ddF<#LdF<eWdF<OuN/45rY<-L@&#+fm>69=Lb,OcZV/);TTm8VI;?%OtJ<(b4mq7M6:u?KRdF<gR@2L=FNU-<b[(9c/"
    "ML3m;Z[$oF3g)GAWqpARc=<ROu7cL5l;-[A]%/"
    "+fsd;l#SafT/"
    "f*W]0=O'$(Tb<[)*@e775R-:Yob%g*>l*:xP?Yb.5)%w_I?7uk5JC+FS(m#i'k.'a0i)9<7b'fs'59hq$*5Uhv##pi^8+hIEBF`nvo`;"
    "'l0.^S1<-wUK2/Coh58KKhLj"
    "M=SO*rfO`+qC`W-On.=AJ56>>i2@2LH6A:&5q`?9I3@@'04&p2/"
    "LVa*T-4<-i3;M9UvZd+N7>b*eIwg:CC)c<>nO&#<IGe;__.thjZl<%w(Wk2xmp4Q@I#I9,DF]u7-P=.-_:YJ]aS@V"
    "?6*C()dOp7:WL,b&3Rg/"
    ".cmM9&r^>$(>.Z-I&J(Q0Hd5Q%7Co-b`-c<N(6r@ip+AurK<m86QIth*#v;-OBqi+L7wDE-Ir8K['m+DDSLwK&/"
    ".?-V%U_%3:qKNu$_b*B-kp7NaD'QdWQPK"
    "Yq[@>P)hI;*_F]u`Rb[.j8_Q/<&>uu+VsH$sM9TA%?)(vmJ80),P7E>)tjD%2L=-t#fK[%`v=Q8<FfNkgg^oIbah*#8/Qt$F&:K*-(N/"
    "'+1vMB,u()-a.VUU*#[e%gAAO(S>WlA2);Sa"
    ">gXm8YB`1d@K#n]76-a$U,mF<fX]idqd)<3,]J7JmW4`6]uks=4-72L(jEk+:bJ0M^q-8Dm_Z?0olP1C9Sa&H[d&c$ooQUj]Exd*3ZM@"
    "-WGW2%s',B-_M%>%Ul:#/'xoFM9QX-$.QN'>"
    "[%$Z$uF6pA6Ki2O5:8w*vP1<-1`[G,)-m#>0`P&#eb#.3i)rtB61(o'$?X3B</"
    "R90;eZ]%Ncq;-Tl]#F>2Qft^ae_5tKL9MUe9b*sLEQ95C&`=G?@Mj=wh*'3E>=-<)Gt*Iw)'QG:`@I"
    "wOf7&]1i'S01B+Ev/Nac#9S;=;YQpg_6U`*kVY39xK,[/"
    "6Aj7:'1Bm-_1EYfa1+o&o4hp7KN_Q(OlIo@S%;jVdn0'1<Vc52=u`3^o-n1'g4v58Hj&6_t7$##?M)c<$bgQ_'SY((-xkA#"
    "Y(,p'H9rIVY-b,'%bCPF7.J<Up^,(dU1VY*5#WkTU>h19w,WQhLI)3S#f$2(eb,jr*b;3Vw]*7NH%$c4Vs,eD9>XW8?N]o+(*pgC%/"
    "72LV-u<Hp,3@e^9UB1J+ak9-TN/mhKPg+AJYd$"
    "MlvAF_jCK*.O-^(63adMT->W%iewS8W6m2rtCpo'RS1R84=@paTKt)>=%&1[)*vp'u+x,VrwN;&]kuO9JDbg=pO$J*.jVe;u'm0dr9l,"
    "<*wMK*Oe=g8lV_KEBFkO'oU]^=[-792#ok,)"
    "i]lR8qQ2oA8wcRCZ^7w/Njh;?.stX?Q1>S1q4Bn$)K1<-rGdO'$Wr.Lc.CG)$/*JL4tNR/"
    ",SVO3,aUw'DJN:)Ss;wGn9A32ijw%FL+Z0Fn.U9;reSq)bmI32U==5ALuG&#Vf1398/pVo"
    "1*c-(aY168o<`JsSbk-,1N;$>0:OUas(3:8Z972LSfF8eb=c-;>SPw7.6hn3m`9^Xkn(r.qS[0;T%&Qc=+STRxX'q1BNk3&*eu2;&8q$"
    "&x>Q#Q7^Tf+6<(d%ZVmj2bDi%.3L2n+4W'$P"
    "iDDG)g,r%+?,$@?uou5tSe2aN_AQU*<h`e-GI7)?OK2A.d7_c)?wQ5AS@DL3r#7fSkgl6-++D:'A,uq7SvlB$pcpH'q3n0#_%dY#"
    "xCpr-l<F0NR@-##FEV6NTF6##$l84N1w?AO>'IAO"
    "URQ##V^Fv-XFbGM7Fl(N<3DhLGF%q.1rC$#:T__&Pi68%0xi_&[qFJ(77j_&JWoF.V735&T,[R*:xFR*K5>>#`bW-?4Ne_&6Ne_&6Ne_"
    "&n`kr-#GJcM6X;uM6X;uM(.a..^2TkL%oR(#"
    ";u.T%fAr%4tJ8&><1=GHZ_+m9/#H1F^R#SC#*N=BA9(D?v[UiFY>>^8p,KKF.W]L29uLkLlu/"
    "+4T<XoIB&hx=T1PcDaB&;HH+-AFr?(m9HZV)FKS8JCw;SD=6[^/DZUL`EUDf]GGlG&>"
    "w$)F./^n3+rlo+DB;5sIYGNk+i1t-69Jg--0pao7Sm#K)pdHW&;LuDNH@H>#/"
    "X-TI(;P>#,Gc>#0Su>#4`1?#8lC?#<xU?#@.i?#D:%@#HF7@#LRI@#P_[@#Tkn@#Xw*A#]-=A#a9OA#"
    "d<F&#*;G##.GY##2Sl##6`($#:l:$#>xL$#B.`$#F:r$#JF.%#NR@%#R_R%#Vke%#Zww%#_-4&#3^Rh%Sflr-k'MS.o?.5/sWel/"
    "wpEM0%3'/1)K^f1-d>G21&v(35>V`39V7A4=onx4"
    "A1OY5EI0;6Ibgr6M$HS7Q<)58C5w,;WoA*#[%T*#`1g*#d=#+#hI5+#lUG+#pbY+#tnl+#x$),#&1;,#*=M,#.I`,#2Ur,#6b.-#;w["
    "H#iQtA#m^0B#qjBB#uvTB##-hB#'9$C#+E6C#"
    "/QHC#3^ZC#7jmC#;v)D#?,<D#C8ND#GDaD#KPsD#O]/"
    "E#g1A5#KA*1#gC17#MGd;#8(02#L-d3#rWM4#Hga1#,<w0#T.j<#O#'2#CYN1#qa^:#_4m3#o@/=#eG8=#t8J5#`+78#4uI-#"
    "m3B2#SB[8#Q0@8#i[*9#iOn8#1Nm;#^sN9#qh<9#:=x-#P;K2#$%X9#bC+.#Rg;<#mN=.#MTF.#RZO.#2?)4#Y#(/#[)1/#b;L/#dAU/"
    "#0Sv;#lY$0#n`-0#sf60#(F24#wrH0#%/e0#"
    "TmD<#%JSMFove:CTBEXI:<eh2g)B,3h2^G3i;#d3jD>)4kMYD4lVu`4m`:&5niUA5@(A5BA1]PBB:xlBCC=2CDLXMCEUtiCf&0g2'tN?"
    "PGT4CPGT4CPGT4CPGT4CPGT4CPGT4CPGT4CP"
    "GT4CPGT4CPGT4CPGT4CPGT4CPGT4CP-qekC`.9kEg^+F$kwViFJTB&5KTB&5KTB&5KTB&5KTB&5KTB&5KTB&5KTB&5KTB&5KTB&5KTB&"
    "5KTB&5KTB&5KTB&5KTB&5o,^<-28ZI'O?;xp"
    "O?;xpO?;xpO?;xpO?;xpO?;xpO?;xpO?;xpO?;xpO?;xpO?;xpO?;xpO?;xpO?;xp;7q-#lLYI:xvD=#";

static char const* GetDefaultCompressedFontDataTTFBase85() { return proggy_clean_ttf_compressed_data_base85; }

} // namespace graphics

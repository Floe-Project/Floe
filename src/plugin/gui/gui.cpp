// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#define STBI_NO_STDIO

#include "gui.hpp"

#include <IconsFontAwesome5.h>
#include <stb_image.h>
#include <stb_image_resize2.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
#include "utils/debug/debug.hpp"
#include "utils/logger/logger.hpp"

#include "build_resources/embedded_files.h"
#include "common/common_errors.hpp"
#include "common/constants.hpp"
#include "framework/gui_live_edit.hpp"
#include "gui/framework/draw_list.hpp"
#include "gui/framework/gui_imgui.hpp"
#include "gui_editor_widgets.hpp"
#include "gui_editors.hpp"
#include "gui_standalone_popups.hpp"
#include "gui_widget_helpers.hpp"
#include "plugin.hpp"
#include "plugin_instance.hpp"
#include "sample_library_server.hpp"
#include "settings/settings_filesystem.hpp"
#include "settings/settings_gui.hpp"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#define LAY_IMPLEMENTATION
#include "layout/layout.h"
#undef LAY_IMPLEMENTATION
#pragma clang diagnostic pop

static f32 PixelsPerPoint(Gui* g) {
    constexpr auto k_points_in_width = 1000.0f; // 1000 just because it's easy to work with
    return (f32)g->settings.settings.gui.window_width / k_points_in_width;
}

static ErrorCodeOr<ImagePixelsRgba> DecodeJpgOrPng(Span<u8 const> image_data) {
    ASSERT(image_data.size);
    ImagePixelsRgba result {};
    int actual_number_channels;
    int width;
    int height;
    constexpr int k_output_channels = 4;
    // always returns rgba because we specify 4 as the output channels
    result.data = stbi_load_from_memory(image_data.data,
                                        (int)image_data.size,
                                        &width,
                                        &height,
                                        &actual_number_channels,
                                        k_output_channels);
    if (!result.data) return ErrorCode(CommonError::FileFormatIsInvalid);
    result.size = {CheckedCast<u16>(width), CheckedCast<u16>(height)};
    result.free_data = [](u8* data) { stbi_image_free(data); };
    DebugLn("Decoded jpg/png {}x{} px", width, height);

#if _WIN32
    if (auto f = result.data; f)
        for (int i = 0; i < width * height * 4; i += 4)
            Swap(f[i + 0], f[i + 2]); // swap r and b
#endif

    return result;
}

static ErrorCodeOr<ImagePixelsRgba> DecodeImage(Span<u8 const> image_data) {
    return DecodeJpgOrPng(image_data);
}

static ErrorCodeOr<ImagePixelsRgba> DecodeImageFromFile(String filename) {
    PageAllocator allocator;
    auto const file_data = TRY(ReadEntireFile(filename, allocator));
    DEFER { allocator.Free(file_data.ToByteSpan()); };
    return DecodeImage(file_data.ToByteSpan());
}

enum class LibraryImageType { Icon, Background };
Optional<ImagePixelsRgba>
ImagePixelsFromLibrary(Gui* g, sample_lib::Library const& lib, LibraryImageType type) {
    String const filename = ({
        String s;
        switch (type) {
            case LibraryImageType::Icon: s = "icon.png"; break;
            case LibraryImageType::Background: s = "background.jpg"; break;
        }
        s;
    });

    if (lib.file_format_specifics.tag == sample_lib::FileFormat::Mdata) {
        // Back in the Mirage days, some libraries didn't embed their own images, but instead got them from a
        // shared pool. We replicate that behaviour here.
        auto mirage_compat_lib =
            sample_lib_server::FindLibraryRetained(g->plugin.shared_data.sample_library_server,
                                                   sample_lib::k_mirage_compat_library_id);
        DEFER { mirage_compat_lib.Release(); };

        if (mirage_compat_lib) {
            if (auto const dir = path::Directory(mirage_compat_lib->path); dir) {
                String const library_subdir = lib.name == "Wraith Demo" ? "Wraith" : lib.name;
                auto const path =
                    path::Join(g->scratch_arena, Array {*dir, "images"_s, library_subdir, filename});
                auto outcome = DecodeImageFromFile(path);
                if (outcome.HasValue()) return outcome.ReleaseValue();
            }
        }
    }

    Optional<String> const path_in_lib = ({
        Optional<String> p;
        switch (type) {
            case LibraryImageType::Icon: p = lib.icon_image_path; break;
            case LibraryImageType::Background: p = lib.background_image_path; break;
        }
        p;
    });

    auto err = [&](String middle) {
        DebugLn("{} {} {}", lib.name, middle, filename);
        return Optional<ImagePixelsRgba> {};
    };

    if (!path_in_lib) return err("does not have");

    auto open_outcome = lib.create_file_reader(lib, *path_in_lib);
    if (open_outcome.HasError()) return err("error opening");

    ArenaAllocator arena {PageAllocator::Instance()};
    auto const file_outcome = open_outcome.Value().ReadOrFetchAll(arena);
    if (file_outcome.HasError()) return err("error reading");

    auto image_outcome = DecodeImage(file_outcome.Value());
    if (image_outcome.HasError()) return err("error decoding");

    return image_outcome.ReleaseValue();
}

graphics::ImageID CopyPixelsToGpuLoadedImage(Gui* g, ImagePixelsRgba const& px) {
    ASSERT(px.data);
    auto const id = ({
        auto const outcome = g->frame_input.graphics_ctx->CreateImageID(px.data, px.size, 4);
        if (outcome.HasError()) {
            g->logger.ErrorLn("Failed to create a texture (size {}x{}): {}",
                              px.size.width,
                              px.size.height,
                              outcome.Error());
            return {};
        }
        outcome.Value();
    });
    return id;
}

inline f32x4 SimdRead4Bytes(u8 const* in) {
    auto const byte_vec = LoadUnalignedToType<u8x4>(in);
    return ConvertVector(byte_vec, f32x4);
}

inline void SimdWrite4Bytes(f32x4 data, u8* out) {
    auto const byte_vec = ConvertVector(data, u8x4);
    StoreToUnaligned(out, byte_vec);
}

//
// You can do a box blur by first blurring in one direction, and then in the other. This is quicker because we
// only need to work in 1 dimension at a time, and the memory access is probably more sequential and therefore
// more cache friendly.
// Rather than calculate the average for every pixel, we can just keep a running average. For each pixel we
// just add to the running average the next pixel in view, and subtract the pixel that went out of view. This
// means the performance will not be worse for larger radii.
//

static void BoxBlur(u8 const* in, u8* out, int width, int height, int radius) {
    constexpr int k_channels = 4;
    if (radius <= 0) return;
    radius = Min(radius, width / 2, height / 2);
    CopyMemory(out, in, (usize)(width * height * k_channels));

    f32x4 const box_width = (f32)(radius * 2 + 1);
    int const max_col_index = width - 1;
    int const max_row_index = height - 1;
    int const width_bytes = width * k_channels;
    int const max_col_bytes = max_col_index * k_channels;
    int const radius_p1 = radius + 1;

    {
        // Blur horizontally
        int const radius_bytes_left = radius * k_channels;
        int const radius_bytes_right = radius_p1 * k_channels;
        int pixel_index = 0;
        for ([[maybe_unused]] auto const row : Range(height)) {
            f32x4 avg = 0;
            {
                // calculate the initial average so that we can do a moving average from here onwards
                for (int i = -radius; i <= radius; ++i) {
                    auto const ptr = in + ((pixel_index + Clamp(i, 0, max_col_index)) * k_channels);
                    avg += SimdRead4Bytes(ptr);
                }
            }

            auto write_ptr = out + (pixel_index * k_channels);
            auto const row_start_read_ptr = in + (pixel_index * k_channels);

            // So as to avoid doing the Min/Max check for the edges, we break the loop into 3 sections, where
            // the presumably largest middle section does not have to use the checks.
            auto write_checked = [&](int start, int end) {
                for (int col_bytes = start; col_bytes < end; col_bytes += k_channels) {
                    SimdWrite4Bytes(avg / box_width, write_ptr);
                    write_ptr += k_channels;

                    auto const lhs_ptr = row_start_read_ptr + Max(col_bytes - radius_bytes_left, 0);
                    auto const rhs_ptr =
                        row_start_read_ptr + Min(col_bytes + radius_bytes_right, max_col_bytes);
                    avg += SimdRead4Bytes(rhs_ptr) - SimdRead4Bytes(lhs_ptr);
                }
            };

            write_checked(0, radius_bytes_left);

            // We don't have to check the edge cases for this middle section - which works out much faster
            auto lhs_ptr = row_start_read_ptr;
            auto rhs_ptr = row_start_read_ptr + radius_bytes_left + radius_bytes_right;
            for (int col_bytes = radius_bytes_left; col_bytes < width_bytes - radius_bytes_right;
                 col_bytes += k_channels) {
                SimdWrite4Bytes(avg / box_width, write_ptr);
                avg += SimdRead4Bytes(rhs_ptr) - SimdRead4Bytes(lhs_ptr);

                write_ptr += k_channels;
                lhs_ptr += k_channels;
                rhs_ptr += k_channels;
            }

            write_checked(width_bytes - radius_bytes_right, width_bytes);

            pixel_index += width;
        }
    }

    {
        // Blur vertically
        for (auto const col : Range(width)) {
            f32x4 avg = 0;
            for (int i = -radius; i <= radius; ++i) {
                auto const ptr = out + ((col + Clamp(i, 0, max_row_index) * width) * k_channels);
                avg += SimdRead4Bytes(ptr);
            }

            auto write_ptr = out + (col * k_channels);
            auto const col_start_read_ptr = write_ptr;

            auto write_checked = [&](int start, int end) {
                for (int row = start; row < end; ++row) {
                    SimdWrite4Bytes(avg / box_width, write_ptr);
                    write_ptr += width_bytes;

                    int const top_row_index = Max(row - radius, 0);
                    int const bot_row_index = Min(row + radius_p1, max_row_index);
                    auto const lhs_ptr = col_start_read_ptr + (top_row_index * width_bytes);
                    auto const rhs_ptr = col_start_read_ptr + (bot_row_index * width_bytes);
                    avg += SimdRead4Bytes(rhs_ptr) - SimdRead4Bytes(lhs_ptr);
                }
            };

            write_checked(0, radius);

            // We don't have to check the edge cases for this middle section - which works out much faster
            auto top_ptr = col_start_read_ptr;
            auto bot_ptr = col_start_read_ptr + ((radius + radius_p1) * width_bytes);
            for (int row = radius; row < height - radius_p1; ++row) {
                SimdWrite4Bytes(avg / box_width, write_ptr);
                avg += SimdRead4Bytes(bot_ptr) - SimdRead4Bytes(top_ptr);

                write_ptr += width_bytes;
                top_ptr += width_bytes;
                bot_ptr += width_bytes;
            }

            write_checked(height - radius_p1, height);
        }
    }
}

static void SampleLibraryChanged(Gui* g, sample_lib::LibraryIdRef library_id) {
    auto opt_index =
        FindIf(g->library_images, [&](LibraryImages const& l) { return l.library_id == library_id; });
    if (opt_index) {
        auto& ctx = g->frame_input.graphics_ctx;
        auto& imgs = g->library_images[*opt_index];
        imgs.icon_missing = false;
        imgs.background_missing = false;
        if (imgs.icon) ctx->DestroyImageID(*imgs.icon);
        if (imgs.background) ctx->DestroyImageID(*imgs.background);
        if (imgs.blurred_background) ctx->DestroyImageID(*imgs.blurred_background);
    }
}

void CreateLibraryBackgroundImageTextures(Gui* g,
                                          LibraryImages& imgs,
                                          ImagePixelsRgba const& bg_pixels,
                                          sample_lib::LibraryIdRef lib_id,
                                          bool reload_background,
                                          bool reload_blurred_background) {
    u8* background_rgba = bg_pixels.data;
    UiSize background_size {bg_pixels.size};
    constexpr u16 k_channels = 4;

    ArenaAllocator arena(PageAllocator::Instance());

    // If the image is quite a lot larger than we need, resize it down to avoid storing a huge image
    // on the GPU
    if (background_size.width > (u16)(g->frame_input.window_size.width * 1.3f)) {
        auto const desired_width = (int)g->frame_input.window_size.width;
        auto const desired_height = (int)((f32)background_size.height *
                                          (g->frame_input.window_size.width / (f32)background_size.width));
        auto background_buf = arena.AllocateBytesForTypeOversizeAllowed<u8>(
            (usize)(desired_width * desired_height * k_channels));

        DebugLn("Resizing background for {} from {}x{} to {}x{}",
                lib_id,
                background_size.width,
                background_size.height,
                desired_width,
                desired_height);

        stbir_resize_uint8_linear(background_rgba,
                                  background_size.width,
                                  background_size.height,
                                  0,
                                  background_buf.data,
                                  desired_width,
                                  desired_height,
                                  0,
                                  (stbir_pixel_layout)k_channels);

        background_rgba = background_buf.data;
        background_size = {CheckedCast<u16>(desired_width), CheckedCast<u16>(desired_height)};
    }

    if (reload_background) {
        DebugLn("reloading background for {}", lib_id);
        imgs.background =
            g->frame_input.graphics_ctx->CreateImageID(background_rgba, background_size, k_channels)
                .OrElse([g](ErrorCode error) {
                    g->logger.ErrorLn("Failed to create background image texture: {}", error);
                    return graphics::ImageID {};
                });
    }

    if (reload_blurred_background) {
        auto const original_image_num_bytes = background_size.width * background_size.height * k_channels;

        Span<u8> blurred_image_buffer {};
        auto blur_img_channels = k_channels;
        auto blur_img_size = background_size;
        auto blurred_image_num_bytes = usize(blur_img_size.width * blur_img_size.height * k_channels);

        auto const window_size = g->frame_input.window_size.ToFloat2() * 0.5f;
        if ((int)window_size.x < background_size.width && (int)window_size.x < background_size.height) {
            auto const downscale_factor =
                LiveSize(g->imgui, UiSizeId::BackgroundBlurringDownscaleFactor) / 100.0f;
            ASSERT(downscale_factor > 0.0f && downscale_factor <= 1.0f);

            blur_img_size = {(u16)(window_size.x * downscale_factor),
                             (u16)(window_size.y * downscale_factor)};
            blurred_image_num_bytes = (usize)(blur_img_size.width * blur_img_size.height * k_channels);
            blurred_image_buffer = arena.AllocateBytesForTypeOversizeAllowed<u8>(blurred_image_num_bytes);

            DebugLn("Resizing blurred background for {} from {}x{} to {}x{}",
                    lib_id,
                    background_size.width,
                    background_size.height,
                    blur_img_size.width,
                    blur_img_size.height);

            for (auto i : Range(blurred_image_num_bytes))
                blurred_image_buffer[i] = 0;

            for (auto i : Range(background_size.width * background_size.height * k_channels))
                background_rgba[i] = background_rgba[i];

            stbir_resize_uint8_linear(background_rgba,
                                      (int)background_size.width,
                                      (int)background_size.height,
                                      0,
                                      blurred_image_buffer.data,
                                      blur_img_size.width,
                                      blur_img_size.height,
                                      0,
                                      (stbir_pixel_layout)k_channels);

        } else {
            blurred_image_buffer = arena.Clone(Span<u8> {background_rgba, (usize)original_image_num_bytes});
        }

        // Make the blurred image a more of a mid-brightness, instead of very light or very dark
        f32 brightness_percent;
        f32 brightness_scaling;
        {
            u64 brightness {};
            for (usize i = 0; i < blurred_image_num_bytes; i += k_channels) {
                brightness += blurred_image_buffer[i + 0];
                brightness += blurred_image_buffer[i + 1];
                brightness += blurred_image_buffer[i + 2];
            }
            brightness /= (u64)blurred_image_num_bytes;
            brightness_percent = (f32)brightness / 255.0f;
            f32 const max_exponent =
                LiveSize(g->imgui, UiSizeId::BackgroundBlurringBrightnessExponent) / 100.0f;
            brightness_scaling = Pow(2.0f, MapFrom01(1 - brightness_percent, -max_exponent, max_exponent));

            f32x4 const scaling = brightness_scaling;
            for (usize i = 0; i < blurred_image_num_bytes; i += k_channels) {
                static f32x4 const max = 255.0f;
                f32x4 v {(f32)blurred_image_buffer[i + 0],
                         (f32)blurred_image_buffer[i + 1],
                         (f32)blurred_image_buffer[i + 2],
                         0};
                v = Min(max, v * scaling);

                alignas(16) f32 results[4];
                StoreToAligned(results, v);
                blurred_image_buffer[i + 0] = (u8)results[0];
                blurred_image_buffer[i + 1] = (u8)results[1];
                blurred_image_buffer[i + 2] = (u8)results[2];
            }
        }

        // Blend on top a dark colour to achieve a more consistently dark background
        {
            f32x4 const overlay_colour =
                Clamp(LiveSize(g->imgui, UiSizeId::BackgroundBlurringOverlayColour), 0.0f, 255.0f);
            f32x4 const overlay_intensity =
                Clamp(LiveSize(g->imgui, UiSizeId::BackgroundBlurringOverlayIntensity) / 100.0f, 0.0f, 1.0f);

            for (usize i = 0; i < blurred_image_num_bytes; i += k_channels) {
                f32x4 v {(f32)blurred_image_buffer[i + 0],
                         (f32)blurred_image_buffer[i + 1],
                         (f32)blurred_image_buffer[i + 2],
                         0};
                v = v + (overlay_colour - v) * overlay_intensity;

                alignas(16) f32 results[4];
                StoreToAligned(results, v);
                blurred_image_buffer[i + 0] = (u8)results[0];
                blurred_image_buffer[i + 1] = (u8)results[1];
                blurred_image_buffer[i + 2] = (u8)results[2];
            }
        }

        {
            // 1. Do a subtle blur into a new buffer
            auto const subtle_blur_radius =
                (int)(LiveSize(g->imgui, UiSizeId::BackgroundBlurringBlurringSmall) *
                      ((f32)blur_img_size.width / 700.0f));
            auto subtle_blur_buffer = arena.AllocateBytesForTypeOversizeAllowed<u8>(blurred_image_num_bytes);
            if (subtle_blur_radius) {
                BoxBlur(blurred_image_buffer.data,
                        subtle_blur_buffer.data,
                        blur_img_size.width,
                        blur_img_size.height,
                        subtle_blur_radius);
            } else {
                CopyMemory(subtle_blur_buffer.data, blurred_image_buffer.data, blurred_image_num_bytes);
            }

            // 2. Do a huge blur into a different buffer
            auto huge_blur_buffer = arena.AllocateBytesForTypeOversizeAllowed<u8>(blurred_image_num_bytes);
            auto const huge_blur_radius = LiveSize(g->imgui, UiSizeId::BackgroundBlurringBlurring) *
                                          ((f32)blur_img_size.width / 700.0f);
            BoxBlur(blurred_image_buffer.data,
                    huge_blur_buffer.data,
                    blur_img_size.width,
                    blur_img_size.height,
                    (int)huge_blur_radius);

            // 3. Blend the 2 blurs together with a given opacity
            auto s = subtle_blur_buffer.data;
            auto h = huge_blur_buffer.data;
            auto const opacity_of_subtle_blur_on_top_huge_blur =
                LiveSize(g->imgui, UiSizeId::BackgroundBlurringBlurringSmallOpacity) / 100.0f;

            for (usize i = 0; i < blurred_image_num_bytes; i += k_channels) {
                f32x4 const s_v {(f32)s[i + 0], (f32)s[i + 1], (f32)s[i + 2], 0};
                f32x4 h_v {(f32)h[i + 0], (f32)h[i + 1], (f32)h[i + 2], 0};
                h_v = h_v + (s_v - h_v) * opacity_of_subtle_blur_on_top_huge_blur;

                alignas(16) f32 results[4];
                StoreToAligned(results, h_v);
                blurred_image_buffer[i + 0] = (u8)results[0];
                blurred_image_buffer[i + 1] = (u8)results[1];
                blurred_image_buffer[i + 2] = (u8)results[2];
            }
        }

        imgs.blurred_background =
            g->frame_input.graphics_ctx
                ->CreateImageID(blurred_image_buffer.data, blur_img_size, blur_img_channels)
                .OrElse([g](ErrorCode error) {
                    g->logger.ErrorLn("Failed to create blurred background texture: {}", error);
                    return graphics::ImageID {};
                });
    }
}

static LibraryImages LoadDefaultBackgroundIfNeeded(Gui* g) {
    auto& ctx = g->frame_input.graphics_ctx;
    auto opt_index = FindIf(g->library_images, [&](LibraryImages const& l) {
        return l.library_id == k_default_background_lib_id;
    });
    if (!opt_index) {
        dyn::Append(g->library_images, {k_default_background_lib_id});
        opt_index = g->library_images.size - 1;
    }
    auto& imgs = g->library_images[*opt_index];

    auto const reload_background = !ctx->ImageIdIsValid(imgs.background) && !imgs.background_missing;
    auto const reload_blurred_background =
        !ctx->ImageIdIsValid(imgs.blurred_background) && !imgs.background_missing;

    if (reload_background || reload_blurred_background) {
        auto image_data = EmbeddedDefaultBackground();
        auto outcome = DecodeImage({image_data.data, image_data.size});
        ASSERT(!outcome.HasError());
        auto const bg_pixels = outcome.ReleaseValue();
        CreateLibraryBackgroundImageTextures(g,
                                             imgs,
                                             bg_pixels,
                                             k_default_background_lib_id,
                                             reload_background,
                                             reload_blurred_background);
    }

    return imgs;
}

static LibraryImages LoadLibraryBackgroundAndIconIfNeeded(Gui* g, sample_lib::Library const& lib) {
    auto& ctx = g->frame_input.graphics_ctx;
    auto const lib_id = lib.Id();

    auto opt_index =
        FindIf(g->library_images, [&](LibraryImages const& l) { return l.library_id == lib_id; });
    if (!opt_index) {
        dyn::Append(g->library_images, {lib_id});
        opt_index = g->library_images.size - 1;
    }
    auto& imgs = g->library_images[*opt_index];

    auto const reload_icon = !ctx->ImageIdIsValid(imgs.icon) && !imgs.icon_missing;
    auto const reload_background = !ctx->ImageIdIsValid(imgs.background) && !imgs.background_missing;
    auto const reload_blurred_background =
        !ctx->ImageIdIsValid(imgs.blurred_background) && !imgs.background_missing;

    if (reload_icon) {
        if (auto icon_pixels = ImagePixelsFromLibrary(g, lib, LibraryImageType::Icon))
            imgs.icon = CopyPixelsToGpuLoadedImage(g, icon_pixels.Value());
        else
            imgs.icon_missing = true;
    }

    if (reload_background || reload_blurred_background) {
        ImagePixelsRgba const bg_pixels = ({
            Optional<ImagePixelsRgba> opt = ImagePixelsFromLibrary(g, lib, LibraryImageType::Background);
            if (!opt) {
                DebugLn("Failed to file background image for {}", lib_id);
                imgs.background_missing = true;
                return imgs;
            }
            opt.ReleaseValue();
        });

        CreateLibraryBackgroundImageTextures(g,
                                             imgs,
                                             bg_pixels,
                                             lib_id,
                                             reload_background,
                                             reload_blurred_background);
    }

    return imgs;
}

Optional<LibraryImages> LibraryImagesFromLibraryId(Gui* g, sample_lib::LibraryIdRef library_id) {
    if (library_id == k_default_background_lib_id) return LoadDefaultBackgroundIfNeeded(g);

    auto background_lib =
        sample_lib_server::FindLibraryRetained(g->plugin.shared_data.sample_library_server, library_id);
    DEFER { background_lib.Release(); };
    if (!background_lib) return nullopt;

    return LoadLibraryBackgroundAndIconIfNeeded(g, *background_lib);
}

static void CreateFontsIfNeeded(Gui* g) {
    g->imgui.SetPixelsPerPoint(PixelsPerPoint(g));

    //
    // Fonts
    //
    auto& graphics_ctx = g->frame_input.graphics_ctx;

    if (graphics_ctx->fonts.tex_id == nullptr) {
        graphics_ctx->fonts.Clear();
        auto def = graphics_ctx->fonts.AddFontDefault();
        def->font_size_no_scale = 13;

        auto const fira_sans_size = g->imgui.PointsToPixels(16);
        auto const roboto_small_size = g->imgui.PointsToPixels(16);
        auto const mada_big_size = g->imgui.PointsToPixels(23);
        auto const mada_size = g->imgui.PointsToPixels(18);

        auto const def_ranges = graphics_ctx->fonts.GetGlyphRangesDefaultAudioPlugin();

        auto const load_font = [&](Span<u8 const> data, f32 size, Span<graphics::GlyphRange const> ranges) {
            graphics::FontConfig config {};
            config.font_data_reference_only = true; // we handle it in font_arena
            auto font = graphics_ctx->fonts.AddFontFromMemoryTTF((void*)data.data,
                                                                 (int)data.size,
                                                                 size * g->frame_input.draw_scale_factor,
                                                                 &config,
                                                                 ranges);
            ASSERT(font != nullptr);
            font->font_size_no_scale = size;

            return font;
        };

        {
            auto const data = EmbeddedFontAwesome();
            g->icons = load_font({data.data, data.size},
                                 mada_size,
                                 Array {graphics::GlyphRange {ICON_MIN_FA, ICON_MAX_FA}});
            g->icons->font_size_no_scale = mada_size;
            ASSERT(g->icons != nullptr);
        }

        {
            auto const data = EmbeddedFiraSans();
            g->fira_sans = load_font({data.data, data.size}, fira_sans_size, def_ranges);
        }

        {
            auto const data = EmbeddedRoboto();
            g->roboto_small = load_font({data.data, data.size}, roboto_small_size, def_ranges);
        }

        {
            auto const data = EmbeddedMada();
            g->mada = load_font({data.data, data.size}, mada_size, def_ranges);
            g->mada_big = load_font({data.data, data.size}, mada_big_size, def_ranges);
        }

        auto const outcome = graphics_ctx->CreateFontTexture();
        if (outcome.HasError()) g->logger.ErrorLn("Failed to create font texture: {}", outcome.Error());
    }
}

static ErrorCodeOr<void> OpenDialog(Gui* g, DialogType type) {
    switch (type) {
        case DialogType::AddNewLibraryScanFolder: {
            Optional<String> default_folder {};
            if (auto extra_paths = g->settings.settings.filesystem.extra_libraries_scan_folders;
                extra_paths.size)
                default_folder = extra_paths[0];

            auto const opt_path = TRY(FilesystemDialog({
                .type = DialogOptions::Type::SelectFolder,
                .allocator = g->scratch_arena,
                .title = "Select Floe Library Folder",
                .default_path = default_folder,
                .filters = {},
                .parent_window = g->frame_input.native_window,
            }));
            if (opt_path) {
                auto const path = *opt_path;
                filesystem_settings::AddScanFolder(g->settings, ScanFolderType::Libraries, path);
            }
            break;
        }
        case DialogType::AddNewPresetsScanFolder: {
            Optional<String> default_folder {};
            if (auto extra_paths = g->settings.settings.filesystem.extra_presets_scan_folders;
                extra_paths.size)
                default_folder = extra_paths[0];

            auto const opt_path = TRY(FilesystemDialog({
                .type = DialogOptions::Type::SelectFolder,
                .allocator = g->scratch_arena,
                .title = "Select Floe Presets Folder",
                .default_path = default_folder,
                .filters = {},
                .parent_window = g->frame_input.native_window,
            }));
            if (opt_path) {
                auto const path = *opt_path;
                filesystem_settings::AddScanFolder(g->settings, ScanFolderType::Presets, path);
            }
            break;
        }
        case DialogType::LoadPreset:
        case DialogType::SavePreset: {
            Optional<String> default_path {};
            auto& preset_scan_folders =
                g->plugin.shared_data.settings.settings.filesystem.extra_presets_scan_folders;
            if (preset_scan_folders.size) {
                default_path =
                    path::Join(g->scratch_arena,
                               Array {preset_scan_folders[0], "untitled" FLOE_PRESET_FILE_EXTENSION});
            }

            auto const filters = Array<DialogOptions::FileFilter, 1> {{{
                .description = "Floe Preset"_s,
                .wildcard_filter = "*.floe-*"_s,
            }}};

            if (type == DialogType::LoadPreset) {
                auto const opt_path = TRY(FilesystemDialog({
                    .type = DialogOptions::Type::OpenFile,
                    .allocator = g->scratch_arena,
                    .title = "Load Floe Preset",
                    .default_path = default_path,
                    .filters = filters.Items(),
                    .parent_window = g->frame_input.native_window,
                }));
                if (opt_path) LoadPresetFromFile(g->plugin, *opt_path);
            } else if (type == DialogType::SavePreset) {
                auto const opt_path = TRY(FilesystemDialog({
                    .type = DialogOptions::Type::SaveFile,
                    .allocator = g->scratch_arena,
                    .title = "Save Floe Preset",
                    .default_path = default_path,
                    .filters = filters.Items(),
                    .parent_window = g->frame_input.native_window,
                }));
                if (opt_path) SaveCurrentStateToFile(g->plugin, *opt_path);
            } else {
                PanicIfReached();
            }
            break;
        }
    }

    return k_success;
}

void Gui::OpenDialog(DialogType type) {
    auto const outcome = ::OpenDialog(this, type);
    if (outcome.HasError()) logger.ErrorLn("Failed to create dialog: {}", outcome.Error());
}

Gui::Gui(GuiFrameInput& frame_input, PluginInstance& plugin)
    : frame_input(frame_input)
    , plugin(plugin)
    , logger(plugin.shared_data.logger)
    , settings(plugin.shared_data.settings)
    , imgui(frame_input, frame_output)
    , sample_lib_server_async_channel(sample_lib_server::OpenAsyncCommsChannel(
          plugin.shared_data.sample_library_server,
          {
              .error_notifications = plugin.error_notifications,
              .result_added_callback = []() {},
              .library_changed_callback =
                  [gui = this](sample_lib::LibraryIdRef library_id_ref) {
                      sample_lib::LibraryId lib_id {library_id_ref};
                      gui->main_thread_callbacks.Push([gui, lib_id]() { SampleLibraryChanged(gui, lib_id); });
                      gui->frame_input.request_update.Store(true, StoreMemoryOrder::Relaxed);
                  },
          })) {
    g_log_file.TraceLn();

    editor.imgui = &imgui;
    imgui.user_callback_data = this;

    layout.Reserve(2048);

    m_window_size_listener_id =
        plugin.shared_data.settings.tracking.window_size_change_listeners.Add([this]() {
            auto const& host = this->plugin.host;
            auto const host_gui = (clap_host_gui const*)host.get_extension(&host, CLAP_EXT_GUI);
            if (host_gui) {
                auto const size = gui_settings::WindowSize(this->plugin.shared_data.settings.settings.gui);
                host_gui->resize_hints_changed(&host);
                host_gui->request_resize(&host, size.width, size.height);
            }
        });
}

Gui::~Gui() {
    sample_lib_server::CloseAsyncCommsChannel(plugin.shared_data.sample_library_server,
                                              sample_lib_server_async_channel);
    g_log_file.TraceLn();
    if (midi_keyboard_note_held_with_mouse) {
        plugin.processor.events_for_audio_thread.Push(
            GuiNoteClickReleased {.key = midi_keyboard_note_held_with_mouse.Value()});
        plugin.host.request_process(&plugin.host);
    }

    plugin.shared_data.settings.tracking.window_size_change_listeners.Remove(m_window_size_listener_id);
}

bool Tooltip(Gui* g, imgui::Id id, Rect r, char const* fmt, ...);

f32x2 GetMaxUVToMaintainAspectRatio(graphics::ImageID img, f32x2 container_size) {
    auto const img_w = (f32)img.size.width;
    auto const img_h = (f32)img.size.height;
    auto const window_ratio = container_size.x / container_size.y;
    auto const image_ratio = img_w / img_h;

    f32x2 uv {1, 1};
    if (image_ratio > window_ratio)
        uv.x = window_ratio / image_ratio;
    else
        uv.y = image_ratio / window_ratio;
    return uv;
}

GuiFrameResult GuiUpdate(Gui* g) {
    ZoneScoped;
    ASSERT(IsMainThread(g->plugin.host));

    g->frame_output = {};

    live_edit::g_high_contrast_gui = g->settings.settings.gui.high_contrast_gui; // IMRPOVE: hacky
    g->scratch_arena.ResetCursorAndConsolidateRegions();

    while (auto f = g->main_thread_callbacks.TryPop(g->scratch_arena))
        (*f)();

    CreateFontsIfNeeded(g);
    auto& imgui = g->imgui;
    imgui.SetPixelsPerPoint(PixelsPerPoint(g));

    g->waveforms.StartFrame();
    DEFER { g->waveforms.EndFrame(*g->frame_input.graphics_ctx); };

    auto whole_window_sets = imgui::DefMainWindow();
    whole_window_sets.draw_routine_window_background = [&](IMGUI_DRAW_WINDOW_BG_ARGS_TYPES) {};
    imgui.Begin(whole_window_sets);

    g->frame_input.graphics_ctx->PushFont(g->fira_sans);
    DEFER { g->frame_input.graphics_ctx->PopFont(); };

    auto& settings = g->settings.settings.gui;
    auto const top_h = LiveSize(imgui, UiSizeId::Top2Height);
    auto const bot_h = settings.show_keyboard ? gui_settings::KeyboardHeight(g->settings.settings.gui) : 0;
    auto const mid_h = (f32)g->frame_input.window_size.height - (top_h + bot_h);

    auto draw_top_window = [](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;
        auto const top = LiveCol(imgui, UiColMap::TopPanelBackTop);
        auto const bot = LiveCol(imgui, UiColMap::TopPanelBackBot);
        imgui.graphics->AddRectFilledMultiColor(r.Min(), r.Max(), top, top, bot, bot);
    };
    auto draw_mid_window = [&](IMGUI_DRAW_WINDOW_BG_ARGS) {
        bool has_image_bg = false;
        auto r = window->unpadded_bounds;

        if (!settings.high_contrast_gui) {
            auto overall_library = LibraryForOverallBackground(g->plugin);
            if (overall_library) {
                auto imgs = LibraryImagesFromLibraryId(g, *overall_library);
                if (imgs->background) {
                    auto tex = g->frame_input.graphics_ctx->GetTextureFromImage(*imgs->background);
                    if (tex) {
                        imgui.graphics->AddImage(*tex,
                                                 r.Min(),
                                                 r.Max(),
                                                 {0, 0},
                                                 GetMaxUVToMaintainAspectRatio(*imgs->background, r.size));
                        has_image_bg = true;
                    }
                }
            }
        }

        if (!has_image_bg)
            imgui.graphics->AddRectFilled(r.Min(), r.Max(), LiveCol(imgui, UiColMap::MidPanelBack));
        imgui.graphics->AddLine(r.TopLeft(), r.TopRight(), LiveCol(imgui, UiColMap::MidPanelTopLine));
    };
    auto draw_bot_window = [](IMGUI_DRAW_WINDOW_BG_ARGS) {
        auto r = window->unpadded_bounds;
        imgui.graphics->AddRectFilled(r.Min(), r.Max(), LiveCol(imgui, UiColMap::BotPanelBack));
    };

    {
        auto mid_settings = imgui::DefWindow();
        mid_settings.pad_top_left = {};
        mid_settings.pad_bottom_right = {};
        mid_settings.draw_routine_window_background = draw_mid_window;
        mid_settings.flags = 0;

        auto mid_panel_r = Rect {0, top_h, imgui.Width(), mid_h};
        imgui.BeginWindow(mid_settings, mid_panel_r, "MidPanel");
        MidPanel(g);
        imgui.EndWindow();

        PresetBrowser preset_browser {g, g->preset_browser_data, false};
        preset_browser.DoPresetBrowserPanel(mid_panel_r);
    }

    {
        auto sets = imgui::DefWindow();
        sets.draw_routine_window_background = draw_top_window;
        sets.pad_top_left = {LiveSize(imgui, UiSizeId::Top2PadLR), LiveSize(imgui, UiSizeId::Top2PadT)};
        sets.pad_bottom_right = {LiveSize(imgui, UiSizeId::Top2PadLR), LiveSize(imgui, UiSizeId::Top2PadB)};
        imgui.BeginWindow(sets, {0, 0, imgui.Width(), top_h}, "TopPanel");
        TopPanel(g);
        imgui.EndWindow();
    }

    if (settings.show_keyboard) {
        auto bot_settings = imgui::DefWindow();
        bot_settings.pad_top_left = {8, 8};
        bot_settings.pad_bottom_right = {8, 8};
        bot_settings.draw_routine_window_background = draw_bot_window;
        imgui.BeginWindow(bot_settings, {0, top_h + mid_h, imgui.Width(), bot_h}, "BotPanel");
        BotPanel(g);
        imgui.EndWindow();
    }

    if (!PRODUCTION_BUILD && !NullTermStringsEqual(g->plugin.host.name, k_floe_standalone_host_name))
        DoStandaloneErrorGUI(g);

    bool show_error_popup = false;
    for (auto& errs : Array {&g->plugin.error_notifications, &g->plugin.shared_data.error_notifications}) {
        for (auto& e : errs->items) {
            if (e.TryRetain()) {
                e.Release();
                show_error_popup = true;
                break;
            }
        }
    }

    if (show_error_popup && !imgui.IsPopupOpen(GetStandaloneID(StandaloneWindowsLoadError))) {
        imgui.ClosePopupToLevel(0);
        imgui.OpenPopup(GetStandaloneID(StandaloneWindowsLoadError));
    }

    if (IsAnyStandloneOpen(imgui)) DoOverlayClickableBackground(g);

    DoErrorsStandalone(g);
    DoMetricsStandalone(g);
    DoAboutStandalone(g);
    DoLicencesStandalone(g);
    DoInstrumentInfoStandalone(g);
    DoSettingsStandalone(g);

    DoLoadingOverlay(g);

    DoWholeEditor(g);
    imgui.End(g->scratch_arena);

    auto outcome = WriteSettingsFileIfChanged(g->settings);
    if (outcome.HasError()) {
        auto item = g->plugin.error_notifications.NewError();
        item->value = {
            .title = "Failed to save settings file"_s,
            .message = g->settings.paths.settings_write_path,
            .error_code = outcome.Error(),
            .id = U64FromChars("savesets"),
        };
        g->plugin.error_notifications.AddOrUpdateError(item);
    }

    return g->frame_output;
}

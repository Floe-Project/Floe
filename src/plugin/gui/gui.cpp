// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
#define STBI_NO_STDIO

#include "gui.hpp"

#include <IconsFontAwesome5.h>
#include <stb_image.h>
#include <stb_image_resize2.h>

#include "foundation/foundation.hpp"
#include "os/filesystem.hpp"
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

static String FilenameForLibraryImageType(LibraryImageType type) {
    switch (type) {
        case LibraryImageType::Icon: return "icon.png";
        case LibraryImageType::Background: return "background.jpg";
    }
    PanicIfReached();
    return {};
}

static Optional<String> PathInLibraryForImageType(sample_lib::Library const& lib, LibraryImageType type) {
    switch (type) {
        case LibraryImageType::Icon: return lib.icon_image_path;
        case LibraryImageType::Background: return lib.background_image_path;
    }
    PanicIfReached();
    return {};
}

Optional<ImagePixelsRgba>
ImagePixelsFromLibrary(Gui* g, sample_lib::Library const& lib, LibraryImageType type) {
    auto const filename = FilenameForLibraryImageType(type);

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

    auto const path_in_lib = PathInLibraryForImageType(lib, type);

    auto err = [&](String middle, LogLevel severity) {
        g_log.Ln(severity, "{} {} {}", lib.name, middle, filename);
        return Optional<ImagePixelsRgba> {};
    };

    if (!path_in_lib) return err("does not have", LogLevel::Debug);

    auto open_outcome = lib.create_file_reader(lib, *path_in_lib);
    if (open_outcome.HasError()) return err("error opening", LogLevel::Warning);

    ArenaAllocator arena {PageAllocator::Instance()};
    auto const file_outcome = open_outcome.Value().ReadOrFetchAll(arena);
    if (file_outcome.HasError()) return err("error reading", LogLevel::Warning);

    auto image_outcome = DecodeImage(file_outcome.Value());
    if (image_outcome.HasError()) return err("error decoding", LogLevel::Warning);

    return image_outcome.ReleaseValue();
}

graphics::ImageID CopyPixelsToGpuLoadedImage(Gui* g, ImagePixelsRgba const& px) {
    ASSERT(px.data);
    auto const outcome = g->frame_input.graphics_ctx->CreateImageID(px.data, px.size, 4);
    if (outcome.HasError()) {
        g->logger.ErrorLn("Failed to create a texture (size {}x{}): {}",
                          px.size.width,
                          px.size.height,
                          outcome.Error());
        return {};
    }
    return outcome.Value();
}

// IMPROVE: with these algorithms for blurring and overlaying, we should probably just use floats instead of
// constantly converting to and from bytes. I suspect it would be faster and the extra memory usage wouldn't
// be a problem because we shrink the image size anyway. All functions can then just work with arrays of
// f32x4.

ALWAYS_INLINE inline f32x4 SimdRead4Bytes(u8 const* in) {
    auto const byte_vec = LoadUnalignedToType<u8x4>(in);
    return ConvertVector(byte_vec, f32x4);
}

ALWAYS_INLINE inline void SimdWrite4Bytes(f32x4 data, u8* out) {
    auto const byte_vec = ConvertVector(data, u8x4);
    StoreToUnaligned(out, byte_vec);
}

// You can do a box blur by first blurring in one direction, and then in the other. This is quicker because we
// only need to work in 1 dimension at a time, and the memory access is probably more sequential and therefore
// more cache friendly.
// Rather than calculate the average for every pixel, we can just keep a running average. For each pixel we
// just add to the running average the next pixel in view, and subtract the pixel that went out of view. This
// means the performance will not be worse for larger radii.

constexpr u16 k_rgba_channels = 4;

struct BlurAxisArgs {
    f32x4 const* in;
    f32x4* out;
    usize data_size;
    int radius;
    f32x4 box_size;
    int line_stride;
    int element_stride;
    int num_lines;
    int line_size;
};
static inline void BlurAxis(BlurAxisArgs args) {
    auto const radius_p1 = args.radius + 1;
    auto const largest_line_index = args.line_size - 1;
    auto const rhs_edge_begin_index = args.line_size - radius_p1;
    for (auto const line_number : Range(args.num_lines)) {
        auto const line_start_index = line_number * args.line_stride;

        f32x4 avg = 0;
        {
            // calculate the initial average so that we can do a moving average from here onwards
            for (int i = -args.radius; i <= args.radius; ++i)
                avg += args.in[line_start_index + Clamp(i, 0, largest_line_index)];
        }

        auto write_ptr = args.out + line_start_index;
        auto const line_start_read_ptr = args.in + line_start_index;

        // So as to avoid doing the Min/Max check for the edges, we break the loop into 3 sections, where
        // the presumably largest middle section does not have to use the checks.

        auto write_checked = [&](int start, int end) {
            for (int element_index = start; element_index < end; ++element_index) {
                ASSERT(write_ptr >= args.out && write_ptr < args.out + args.data_size);
                *write_ptr = Clamp01<f32x4>(avg / args.box_size);
                write_ptr += args.element_stride;

                auto const lhs_ptr =
                    line_start_read_ptr + Max(element_index - args.radius, 0) * args.element_stride;
                auto const rhs_ptr = line_start_read_ptr +
                                     Min(element_index + radius_p1, largest_line_index) * args.element_stride;
                avg += *rhs_ptr - *lhs_ptr;
            }
        };

        write_checked(0, args.radius);

        // write_unchecked:
        // We don't have to check the edge cases for this middle section - which works out much faster
        auto lhs_ptr = line_start_read_ptr;
        auto rhs_ptr = line_start_read_ptr + args.radius + radius_p1;
        for (int element_index = args.radius; element_index < rhs_edge_begin_index; ++element_index) {
            ASSERT(write_ptr >= args.out && write_ptr < args.out + args.data_size);
            *write_ptr = Clamp01<f32x4>(avg / args.box_size);
            write_ptr += args.element_stride;
            avg += *rhs_ptr - *lhs_ptr;
            lhs_ptr += args.element_stride;
            rhs_ptr += args.element_stride;
        }

        write_checked(rhs_edge_begin_index, args.line_size);
    }
}

static bool BoxBlur(f32x4 const* in, f32x4* out, int width, int height, int radius) {
    radius = Min(radius, width / 2, height / 2);
    if (radius <= 0) return false;

    Stopwatch stopwatch;
    DEFER {
        g_log.DebugLn("Box blur {}x{}, radius {} took {} ms",
                      width,
                      height,
                      radius,
                      stopwatch.MillisecondsElapsed());
    };

    BlurAxisArgs args {
        .in = in,
        .out = out,
        .data_size = (usize)(width * height),
        .radius = radius,
        .box_size = (f32)(radius * 2 + 1),
    };

    // horizontal blur, a 'line' is a row
    args.num_lines = height;
    args.line_size = width;
    args.line_stride = width;
    args.element_stride = 1;
    BlurAxis(args);

    args.in = out;

    // vertical blur, a 'line' is a column
    args.num_lines = width;
    args.line_size = height;
    args.line_stride = 1;
    args.element_stride = width;
    BlurAxis(args);

    return true;
}

struct ImageBytes {
    usize NumPixels() const { return (usize)(size.width * size.height); }
    usize NumBytes() const { return NumPixels() * k_rgba_channels; }
    u8* rgba;
    UiSize size;
};

struct ImageF32 {
    usize NumPixels() const { return (usize)(size.width * size.height); }
    usize NumBytes() const { return NumPixels() * sizeof(f32x4); }
    Span<f32x4> rgba;
    UiSize size;
};

static ImageBytes ShrinkImage(ImageBytes image,
                              u16 bounding_width,
                              u16 shrinked_to_width,
                              ArenaAllocator& arena,
                              bool always_allocate) {
    if (image.size.width <= bounding_width) {
        if (!always_allocate)
            return image;
        else {
            auto const num_bytes = image.NumBytes();
            auto rgba = arena.AllocateExactSizeUninitialised<u8>(num_bytes).data;
            CopyMemory(rgba, image.rgba, num_bytes);
            return {.rgba = rgba, .size = image.size};
        }
    }

    // maintain aspect ratio
    auto const shrunk_height =
        CheckedCast<u16>((f32)image.size.height * ((f32)shrinked_to_width / (f32)image.size.width));

    Stopwatch stopwatch;
    DEFER {
        g_log.DebugLn("Shrinking image {}x{} to {}x{} took {} ms",
                      image.size.width,
                      image.size.height,
                      shrinked_to_width,
                      shrunk_height,
                      stopwatch.MillisecondsElapsed());
    };

    ImageBytes result {
        .rgba =
            arena.AllocateBytesForTypeOversizeAllowed<u8>(shrinked_to_width * shrunk_height * k_rgba_channels)
                .data,
        .size = {shrinked_to_width, shrunk_height},
    };

    stbir_resize_uint8_linear(image.rgba,
                              image.size.width,
                              image.size.height,
                              0,
                              result.rgba,
                              result.size.width,
                              result.size.height,
                              0,
                              STBIR_RGBA);
    return result;
}

static ImageF32 CreateImageF32(ImageBytes const image, ArenaAllocator& arena) {
    auto const result = arena.AllocateExactSizeUninitialised<f32x4>(image.NumPixels());
    for (auto [pixel_index, pixel] : Enumerate(result)) {
        auto const bytes = LoadUnalignedToType<u8x4>(image.rgba + pixel_index * k_rgba_channels);
        pixel = ConvertVector(bytes, f32x4) / 255.0f;
    }
    return {.rgba = result, .size = image.size};
}

static void WriteImageF32AsBytes(ImageF32 const image, u8* out) {
    for (auto [pixel_index, pixel] : Enumerate(image.rgba)) {
        auto bytes = ConvertVector(pixel * 255.0f, u8x4);
        bytes[3] = 255; // ignore alpha
        StoreToUnaligned(out + pixel_index * k_rgba_channels, bytes);
    }
}

static Optional<graphics::ImageID> TryCreateImageOnGpu(graphics::DrawContext& ctx, ImageBytes const image) {
    return ctx.CreateImageID(image.rgba, image.size, k_rgba_channels).OrElse([](ErrorCode error) {
        g_log.ErrorLn("Failed to create image texture: {}", error);
        return graphics::ImageID {};
    });
}

static ImageBytes
CreateBlurredBackground(imgui::Context const& imgui, ImageBytes const image, ArenaAllocator& arena) {
    Stopwatch stopwatch;
    DEFER { g_log.DebugLn("Blurred image generation took {} ms", stopwatch.MillisecondsElapsed()); };

    // Shrink the image down even further for better speed. We are about to blur it, we don't need
    // detail.
    auto const bounding_width = CheckedCast<u16>(
        image.size.width * Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringDownscaleFactor) / 100.0f));

    auto const result = ShrinkImage(image, bounding_width, bounding_width, arena, true);
    auto const num_pixels = result.NumPixels();

    // For ease of use and performance, we convert the image to f32x4 format
    auto const pixels = CreateImageF32(result, arena);

    // Make the blurred image more of a mid-brightness, instead of very light or very dark
    {
        f32x4 brightness_sum {};

        for (auto const& pixel : pixels.rgba)
            brightness_sum += pixel;

        f32 brightness_percent = 0;
        brightness_percent += brightness_sum[0];
        brightness_percent += brightness_sum[1];
        brightness_percent += brightness_sum[2];
        brightness_percent /= (f32)num_pixels * 3;

        f32 const max_exponent = LiveSize(imgui, UiSizeId::BackgroundBlurringBrightnessExponent) / 100.0f;
        f32 const brightness_scaling =
            Pow(2.0f, MapFrom01(1 - brightness_percent, -max_exponent, max_exponent));

        for (auto& pixel : pixels.rgba)
            pixel = Clamp01<f32x4>(pixel * brightness_scaling);
    }

    // Blend on top a dark colour to achieve a more consistently dark background
    {
        f32x4 const overlay_colour =
            Clamp(LiveSize(imgui, UiSizeId::BackgroundBlurringOverlayColour) / 100.0f, 0.0f, 1.0f);
        f32x4 const overlay_intensity =
            Clamp(LiveSize(imgui, UiSizeId::BackgroundBlurringOverlayIntensity) / 100.0f, 0.0f, 1.0f);

        for (auto& pixel : pixels.rgba)
            pixel = pixel + (overlay_colour - pixel) * overlay_intensity;
    }

    // Do a pair of blurs with different radii, and blend them together
    {
        // 1. Do a subtle blur into a new buffer
        auto const subtle_blur_pixels = arena.AllocateExactSizeUninitialised<f32x4>(num_pixels).data;
        auto const subtle_blur_radius =
            LiveSize(imgui, UiSizeId::BackgroundBlurringBlurringSmall) * ((f32)result.size.width / 700.0f);
        BoxBlur(pixels.rgba.data,
                subtle_blur_pixels,
                result.size.width,
                result.size.height,
                (int)subtle_blur_radius);

        // 2. Do a huge blur into a different buffer
        auto const huge_blur_pixels = arena.AllocateExactSizeUninitialised<f32x4>(num_pixels).data;
        auto const huge_blur_radius =
            LiveSize(imgui, UiSizeId::BackgroundBlurringBlurring) * ((f32)result.size.width / 700.0f);
        if (!BoxBlur(pixels.rgba.data,
                     huge_blur_pixels,
                     result.size.width,
                     result.size.height,
                     (int)huge_blur_radius)) {
            CopyMemory(huge_blur_pixels, pixels.rgba.data, pixels.NumBytes());
        }

        // 3. Blend the 2 blurs together with a given opacity
        f32x4 const opacity_of_subtle_blur_on_top_huge_blur =
            Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringBlurringSmallOpacity) / 100.0f);
        for (auto const pixel_index : Range(num_pixels)) {
            auto const new_pixel = huge_blur_pixels[pixel_index] +
                                   (subtle_blur_pixels[pixel_index] - huge_blur_pixels[pixel_index]) *
                                       opacity_of_subtle_blur_on_top_huge_blur;
            pixels.rgba[pixel_index] = new_pixel;
        }
    }

    // Convert the f32x4 back to bytes
    WriteImageF32AsBytes(pixels, result.rgba);
    return result;
}

static void CreateLibraryBackgroundImageTextures(Gui* g,
                                                 LibraryImages& imgs,
                                                 ImagePixelsRgba const& background_image,
                                                 bool reload_background,
                                                 bool reload_blurred_background) {
    ArenaAllocator arena {PageAllocator::Instance()};

    // If the image is quite a lot larger than we need, resize it down to avoid storing a huge image on the
    // GPU
    auto const scaled_background = ShrinkImage({.rgba = background_image.data, .size = background_image.size},
                                               CheckedCast<u16>(g->frame_input.window_size.width * 1.3f),
                                               g->frame_input.window_size.width,
                                               arena,
                                               false);
    if (reload_background)
        imgs.background = TryCreateImageOnGpu(*g->frame_input.graphics_ctx, scaled_background);

    if (reload_blurred_background) {
        imgs.blurred_background =
            TryCreateImageOnGpu(*g->frame_input.graphics_ctx,
                                CreateBlurredBackground(g->imgui, scaled_background, arena));
    }
}

static LibraryImages LoadDefaultLibraryImagesIfNeeded(Gui* g) {
    auto& ctx = g->frame_input.graphics_ctx;
    auto opt_index = FindIf(g->library_images, [&](LibraryImages const& images) {
        return images.library_id == k_default_background_lib_id;
    });
    if (!opt_index) {
        dyn::Append(g->library_images, {k_default_background_lib_id});
        opt_index = g->library_images.size - 1;
    }
    auto& images = g->library_images[*opt_index];

    static TimePoint last_time = TimePoint::Now();
    auto const now = TimePoint::Now();
    if ((now - last_time) > 0.5) {
        last_time = now;
        if (images.background) ctx->DestroyImageID(*images.background);
        if (images.blurred_background) ctx->DestroyImageID(*images.blurred_background);
    }

    auto const reload_background = !ctx->ImageIdIsValid(images.background) && !images.background_missing;
    auto const reload_blurred_background =
        !ctx->ImageIdIsValid(images.blurred_background) && !images.background_missing;

    if (reload_background || reload_blurred_background) {
        auto image_data = EmbeddedDefaultBackground();
        auto outcome = DecodeImage({image_data.data, image_data.size});
        ASSERT(!outcome.HasError());
        auto const bg_pixels = outcome.ReleaseValue();
        CreateLibraryBackgroundImageTextures(g,
                                             images,
                                             bg_pixels,
                                             reload_background,
                                             reload_blurred_background);
    }

    return images;
}

static LibraryImages LoadLibraryImagesIfNeeded(Gui* g, sample_lib::Library const& lib) {
    auto& ctx = g->frame_input.graphics_ctx;
    auto const lib_id = lib.Id();

    auto opt_index =
        FindIf(g->library_images, [&](LibraryImages const& l) { return l.library_id == lib_id; });
    if (!opt_index) {
        dyn::Append(g->library_images, {lib_id});
        opt_index = g->library_images.size - 1;
    }
    auto& imgs = g->library_images[*opt_index];

    static TimePoint last_time = TimePoint::Now();
    auto const now = TimePoint::Now();
    if ((now - last_time) > 0.5) {
        last_time = now;
        if (imgs.background) ctx->DestroyImageID(*imgs.background);
        if (imgs.blurred_background) ctx->DestroyImageID(*imgs.blurred_background);
    }

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
                imgs.background_missing = true;
                return imgs;
            }
            opt.ReleaseValue();
        });

        CreateLibraryBackgroundImageTextures(g,
                                             imgs,
                                             bg_pixels,
                                             reload_background,
                                             reload_blurred_background);
    }

    return imgs;
}

Optional<LibraryImages> LibraryImagesFromLibraryId(Gui* g, sample_lib::LibraryIdRef library_id) {
    if (library_id == k_default_background_lib_id) return LoadDefaultLibraryImagesIfNeeded(g);

    auto background_lib =
        sample_lib_server::FindLibraryRetained(g->plugin.shared_data.sample_library_server, library_id);
    DEFER { background_lib.Release(); };
    if (!background_lib) return nullopt;

    return LoadLibraryImagesIfNeeded(g, *background_lib);
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

static void CreateFontsIfNeeded(Gui* g) {
    g->imgui.SetPixelsPerPoint(PixelsPerPoint(g));

    //
    // Fonts
    //
    auto& graphics_ctx = g->frame_input.graphics_ctx;

    if (graphics_ctx->fonts.tex_id == nullptr) {
        graphics_ctx->fonts.Clear();

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

    while (auto function = g->main_thread_callbacks.TryPop(g->scratch_arena))
        (*function)();

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
    for (auto& err_notifications :
         Array {&g->plugin.error_notifications, &g->plugin.shared_data.error_notifications}) {
        for (auto& error : err_notifications->items) {
            if (error.TryRetain()) {
                error.Release();
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

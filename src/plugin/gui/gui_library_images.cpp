// Copyright 2025 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui_library_images.hpp"

#include "build_resources/embedded_files.h"
#include "engine/engine.hpp"
#include "gui_framework/image.hpp"

static Optional<graphics::ImageID> TryCreateImageOnGpu(graphics::DrawContext& ctx, ImageBytes const image) {
    return ctx.CreateImageID(image.rgba, image.size, k_rgba_channels).OrElse([](ErrorCode error) {
        LogError(ModuleName::Gui, "Failed to create image texture: {}", error);
        return graphics::ImageID {};
    });
}

static void CreateLibraryBackgroundImageTextures(imgui::Context const& imgui,
                                                 LibraryImages& imgs,
                                                 ImageBytesManaged const& background_image,
                                                 bool reload_background,
                                                 bool reload_blurred_background) {
    ArenaAllocator arena {PageAllocator::Instance()};

    auto const scaled_width = CheckedCast<u16>(imgui.frame_input.window_size.width * 1.3f);
    if (!scaled_width) return;

    // If the image is quite a lot larger than we need, resize it down to avoid storing a huge image on the
    // GPU
    auto const scaled_background = ShrinkImageIfNeeded(background_image,
                                                       scaled_width,
                                                       imgui.frame_input.window_size.width,
                                                       arena,
                                                       false);
    if (reload_background)
        imgs.background = TryCreateImageOnGpu(*imgui.frame_input.graphics_ctx, scaled_background);

    if (reload_blurred_background) {
        imgs.blurred_background = TryCreateImageOnGpu(
            *imgui.frame_input.graphics_ctx,
            CreateBlurredLibraryBackground(
                scaled_background,
                arena,
                {
                    .downscale_factor =
                        Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringDownscaleFactor) / 100.0f),
                    .brightness_scaling_exponent =
                        LiveSize(imgui, UiSizeId::BackgroundBlurringBrightnessExponent) / 100.0f,
                    .overlay_value =
                        Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringOverlayColour) / 100.0f),
                    .overlay_alpha =
                        Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringOverlayIntensity) / 100.0f),
                    .blur1_radius_percent = LiveSize(imgui, UiSizeId::BackgroundBlurringBlur1Radius) / 100,
                    .blur2_radius_percent = LiveSize(imgui, UiSizeId::BackgroundBlurringBlur2Radius) / 100,
                    .blur2_alpha = Clamp01(LiveSize(imgui, UiSizeId::BackgroundBlurringBlur2Alpha) / 100.0f),
                }));
    }
}

static LibraryImages& FindOrCreateLibraryImages(LibraryImagesArray& library_images,
                                                sample_lib::LibraryIdRef library_id) {
    auto opt_index =
        FindIf(library_images, [&](LibraryImages const& l) { return l.library_id == library_id; });
    if (opt_index) return library_images[*opt_index];

    dyn::Append(library_images, {library_id});
    return library_images[library_images.size - 1];
}

struct CheckLibraryImagesResult {
    bool reload_icon = false;
    bool reload_background = false;
    bool reload_blurred_background = false;
};

static CheckLibraryImagesResult CheckLibraryImages(graphics::DrawContext& ctx, LibraryImages& images) {
    CheckLibraryImagesResult result {};

    if (Exchange(images.reload, false)) {
        if (images.icon) ctx.DestroyImageID(*images.icon);
        if (images.background) ctx.DestroyImageID(*images.background);
        if (images.blurred_background) ctx.DestroyImageID(*images.blurred_background);
        result.reload_icon = true;
        result.reload_background = true;
        result.reload_blurred_background = true;
        return result;
    }

    if (!ctx.ImageIdIsValid(images.icon) && !images.icon_missing) result.reload_icon = true;
    if (!ctx.ImageIdIsValid(images.background) && !images.background_missing) result.reload_background = true;
    if (!ctx.ImageIdIsValid(images.blurred_background) && !images.background_missing)
        result.reload_blurred_background = true;

    return result;
}

static LibraryImages LoadDefaultLibraryImagesIfNeeded(LibraryImagesArray& library_images,
                                                      imgui::Context& imgui) {
    auto& images = FindOrCreateLibraryImages(library_images, k_default_background_lib_id);
    auto const reloads = CheckLibraryImages(*imgui.frame_input.graphics_ctx, images);

    if (reloads.reload_background || reloads.reload_blurred_background) {
        auto image_data = EmbeddedDefaultBackground();
        auto outcome = DecodeImage({image_data.data, image_data.size});
        ASSERT(!outcome.HasError());
        auto const bg_pixels = outcome.ReleaseValue();
        CreateLibraryBackgroundImageTextures(imgui,
                                             images,
                                             bg_pixels,
                                             reloads.reload_background,
                                             reloads.reload_blurred_background);
    }

    return images;
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

static Optional<sample_lib::LibraryPath> PathInLibraryForImageType(sample_lib::Library const& lib,
                                                                   LibraryImageType type) {
    switch (type) {
        case LibraryImageType::Icon: return lib.icon_image_path;
        case LibraryImageType::Background: return lib.background_image_path;
    }
    PanicIfReached();
    return {};
}

Optional<ImageBytesManaged> ImagePixelsFromLibrary(sample_lib::Library const& lib,
                                                   LibraryImageType type,
                                                   sample_lib_server::Server& server,
                                                   ArenaAllocator& scratch_arena) {
    auto const filename = FilenameForLibraryImageType(type);

    if (lib.file_format_specifics.tag == sample_lib::FileFormat::Mdata) {
        // Back in the Mirage days, some libraries didn't embed their own images, but instead got them from a
        // shared pool. We replicate that behaviour here.
        auto mirage_compat_lib =
            sample_lib_server::FindLibraryRetained(server, sample_lib::k_mirage_compat_library_id);
        DEFER { mirage_compat_lib.Release(); };

        if (mirage_compat_lib) {
            if (auto const dir = path::Directory(mirage_compat_lib->path); dir) {
                String const library_subdir = lib.name == "Wraith Demo" ? "Wraith" : lib.name;
                auto const path =
                    path::Join(scratch_arena, Array {*dir, "Images"_s, library_subdir, filename});
                auto outcome = DecodeImageFromFile(path);
                if (outcome.HasValue()) return outcome.ReleaseValue();
            }
        }
    }

    auto const path_in_lib = PathInLibraryForImageType(lib, type);

    auto err = [&](String middle, LogLevel severity) {
        Log(ModuleName::Gui, severity, "{} {} {}", lib.name, middle, filename);
        return Optional<ImageBytesManaged> {};
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

static LibraryImages LoadLibraryImagesIfNeeded(LibraryImagesArray& array,
                                               imgui::Context& imgui,
                                               sample_lib::Library const& lib,
                                               sample_lib_server::Server& server,
                                               ArenaAllocator& scratch_arena) {
    auto& images = FindOrCreateLibraryImages(array, lib.Id());
    auto const reloads = CheckLibraryImages(*imgui.frame_input.graphics_ctx, images);

    if (reloads.reload_icon) {
        if (auto icon_pixels = ImagePixelsFromLibrary(lib, LibraryImageType::Icon, server, scratch_arena))
            images.icon = CreateImageIdChecked(*imgui.frame_input.graphics_ctx, icon_pixels.Value());
        else
            images.icon_missing = true;
    }

    if (reloads.reload_background || reloads.reload_blurred_background) {
        ImageBytesManaged const bg_pixels = ({
            Optional<ImageBytesManaged> opt =
                ImagePixelsFromLibrary(lib, LibraryImageType::Background, server, scratch_arena);
            if (!opt) {
                images.background_missing = true;
                return images;
            }
            opt.ReleaseValue();
        });

        CreateLibraryBackgroundImageTextures(imgui,
                                             images,
                                             bg_pixels,
                                             reloads.reload_background,
                                             reloads.reload_blurred_background);
    }

    return images;
}

Optional<LibraryImages> LibraryImagesFromLibraryId(LibraryImagesArray& array,
                                                   imgui::Context& imgui,
                                                   sample_lib::LibraryIdRef const& library_id,
                                                   sample_lib_server::Server& server,
                                                   ArenaAllocator& scratch_arena) {
    if (library_id == k_default_background_lib_id) return LoadDefaultLibraryImagesIfNeeded(array, imgui);

    auto lib = sample_lib_server::FindLibraryRetained(server, library_id);
    DEFER { lib.Release(); };
    if (!lib) return k_nullopt;

    return LoadLibraryImagesIfNeeded(array, imgui, *lib, server, scratch_arena);
}

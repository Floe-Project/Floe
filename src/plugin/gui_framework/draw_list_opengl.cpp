// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later
//
// This file contains modified code from dear imgui:
// Copyright (c) 2014-2024 Omar Cornut
// SPDX-License-Identifier: MIT

#include "foundation/foundation.hpp"
#include "utils/logger/logger.hpp"

#if __APPLE__
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#else
// NOTE: on windows this includes windows.h
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#include "os/undef_windows_macros.h"
#include "utils/debug/tracy_wrapped.hpp"

#include "draw_list.hpp"

namespace graphics {

static constexpr ErrorCodeCategory k_gl_error_category {
    .category_id = "GL",
    .message = [](Writer const& writer, ErrorCode code) -> ErrorCodeOr<void> {
        String str {"unknown"};
        switch (code.code) {
            case GL_NO_ERROR: PanicIfReached(); break;
            case GL_INVALID_ENUM: str = "invalid enum"; break;
            case GL_INVALID_VALUE: str = "invalid value"; break;
            case GL_INVALID_OPERATION: str = "invalid operation"; break;
            case GL_STACK_OVERFLOW: str = "stack overflow"; break;
            case GL_STACK_UNDERFLOW: str = "stack underflow"; break;
            case GL_OUT_OF_MEMORY: str = "out of memory"; break;
            case GL_TABLE_TOO_LARGE: str = "table too large"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: str = "invalid framebuffer operation"; break;
            default: {
                PanicIfReached();
            }
        }
        return writer.WriteChars(str);
    },
};

ErrorCodeOr<void> CheckGLError(String function) {
    ErrorCodeOr<void> err {};
    for (auto const _ : Range(20)) {
        auto const gl_err = glGetError();
        if (gl_err == GL_NO_ERROR) break;
        err = ErrorCode(k_gl_error_category, gl_err);
        LogDebug(ModuleName::Gui, "GL Error: {}: {}", function, err.Error());
    }
    if (err.HasError()) return err;
    return k_success;
}

struct OpenGLDrawContext : public DrawContext {
    ErrorCodeOr<void> CreateDeviceObjects(void* _window) override {
        Trace(ModuleName::Gui);
        ASSERT(_window != nullptr);

        {
            dyn::Clear(graphics_device_info);
            if (auto const vendor = (char const*)glGetString(GL_VENDOR); vendor)
                fmt::Append(graphics_device_info, "Vendor:   {}\n", FromNullTerminated(vendor));
            if (auto const renderer = (char const*)glGetString(GL_RENDERER); renderer)
                fmt::Append(graphics_device_info, "Renderer: {}\n", FromNullTerminated(renderer));
            if (auto const version = (char const*)glGetString(GL_VERSION); version)
                fmt::Append(graphics_device_info, "Version:  {}\n", FromNullTerminated(version));
            if (graphics_device_info.size) dyn::Pop(graphics_device_info); // remove last newline
        }

        return k_success;
    }

    void DestroyDeviceObjects() override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        DestroyAllTextures();
        DestroyFontTexture();
    }

    ErrorCodeOr<void> CreateFontTexture() override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        unsigned char* pixels;
        int width;
        int height;
        fonts.GetTexDataAsRGBA32(&pixels, &width, &height);

        // Upload texture to graphics system
        GLint last_texture;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
        glGenTextures(1, &font_texture);
        glBindTexture(GL_TEXTURE_2D, font_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

        // Store our identifier
        fonts.tex_id = (void*)(intptr_t)font_texture;

        // Restore state
        glBindTexture(GL_TEXTURE_2D, (GLuint)last_texture);
        fonts.ClearTexData();

        if (auto gl_err = glGetError(); gl_err != GL_NO_ERROR) return ErrorCode(k_gl_error_category, gl_err);

        return k_success;
    }

    void DestroyFontTexture() override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        if (font_texture) {
            glDeleteTextures(1, &font_texture);
            fonts.tex_id = nullptr;
            font_texture = 0;
        }
    }

    void Resize(UiSize) override { DestroyDeviceObjects(); }

    ErrorCodeOr<void> Render(DrawData draw_data, UiSize window_size) override {
        ZoneScoped;
        if (draw_data.draw_lists.size == 0) return k_success;

        GLint last_texture;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
        GLint last_viewport[4];
        glGetIntegerv(GL_VIEWPORT, last_viewport);
        GLint last_scissor_box[4];
        glGetIntegerv(GL_SCISSOR_BOX, last_scissor_box);
        glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_TRANSFORM_BIT);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_SCISSOR_TEST);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnable(GL_TEXTURE_2D);
        // glHint( GL_POLYGON_SMOOTH_HINT, GL_NICEST );

        // Setup viewport, orthographic projection matrix
        glViewport(0, 0, (GLsizei)(window_size.width), (GLsizei)(window_size.height));
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, window_size.width, window_size.height, 0.0, -1.0, +1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        for (auto const& draw_list : draw_data.draw_lists) {
            if (draw_list->idx_buffer.size == 0 || draw_list->idx_buffer.size == 0) continue;
            DrawVert const* vtx_buffer = draw_list->vtx_buffer.data;
            DrawIdx const* idx_buffer = draw_list->idx_buffer.data;
            glVertexPointer(2,
                            GL_FLOAT,
                            sizeof(DrawVert),
                            (void*)((char*)vtx_buffer + offsetof(DrawVert, pos)));
            glTexCoordPointer(2,
                              GL_FLOAT,
                              sizeof(DrawVert),
                              (void*)((char*)vtx_buffer + offsetof(DrawVert, uv)));
            glColorPointer(4,
                           GL_UNSIGNED_BYTE,
                           sizeof(DrawVert),
                           (void*)((char*)vtx_buffer + offsetof(DrawVert, col)));

            for (int cmd_i = 0; cmd_i < draw_list->cmd_buffer.size; cmd_i++) {
                DrawCmd const* pcmd = &draw_list->cmd_buffer[cmd_i];
                if (pcmd->user_callback) {
                    pcmd->user_callback(draw_list, pcmd);
                } else {
                    glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->texture_id);
                    glScissor((int)pcmd->clip_rect.x,
                              (int)((window_size.height) - pcmd->clip_rect.w),
                              (int)(pcmd->clip_rect.z - pcmd->clip_rect.x),
                              (int)(pcmd->clip_rect.w - pcmd->clip_rect.y));
                    glDrawElements(GL_TRIANGLES,
                                   (GLsizei)pcmd->elem_count,
                                   sizeof(DrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                                   idx_buffer);
                }
                idx_buffer += pcmd->elem_count;
            }
        }

        // Restore modified state
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glBindTexture(GL_TEXTURE_2D, (GLuint)last_texture);
        glMatrixMode(GL_MODELVIEW);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glPopAttrib();
        glViewport(last_viewport[0], last_viewport[1], (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
        glScissor(last_scissor_box[0],
                  last_scissor_box[1],
                  (GLsizei)last_scissor_box[2],
                  (GLsizei)last_scissor_box[3]);

        glFlush();

        TRY(CheckGLError("Render"));

        return k_success;
    }

    ErrorCodeOr<TextureHandle> CreateTexture(u8 const* data, UiSize size, u16 bytes_per_pixel) override {
        ZoneScoped;
        Trace(ModuleName::Gui);
        // Upload texture to graphics system
        GLint last_texture;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);

        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        GLenum format = (bytes_per_pixel == 3) ? GL_RGB : GL_RGBA;

        // unsigned char* data_result = data;
        //
        // // I have no idea why I have to do this... without doing this it seems to work 90% of the time but
        // // then some images that have 3 channels do not show correctly...
        // if (bytes_per_pixel == 3) {
        //     data_result = new unsigned char[size.width * size.height * 4];
        //     int in_i = 0;
        //     for (int i = 0; i < size.width * size.height * 4; i += 4) {
        //         data_result[i + 0] = data[in_i + 0];
        //         data_result[i + 1] = data[in_i + 1];
        //         data_result[i + 2] = data[in_i + 2];
        //         data_result[i + 3] = 255;
        //         in_i += 3;
        //     }
        //     format = GL_RGBA;
        // }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.width, size.height, 0, format, GL_UNSIGNED_BYTE, data);

        // if (bytes_per_pixel == 3) delete[] data_result;

        if (auto error = glGetError(); error != GL_NO_ERROR)
            return ErrorCode(k_gl_error_category, (s64)error);

        // Restore state
        glBindTexture(GL_TEXTURE_2D, (GLuint)last_texture);

        if (auto gl_err = glGetError(); gl_err != GL_NO_ERROR) return ErrorCode(k_gl_error_category, gl_err);

        return (void*)(intptr_t)texture;
    }

    void DestroyTexture(TextureHandle& texture) override {
        ZoneScoped;
        if (texture) {
            auto gluint_tex = (GLuint)(uintptr_t)texture;
            glDeleteTextures(1, &gluint_tex);
            texture = nullptr;
        }
    }

    GLuint font_texture = 0;
};

DrawContext* CreateNewDrawContext() { return new OpenGLDrawContext(); }

} // namespace graphics

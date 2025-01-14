// SPDX-FileCopyrightText: 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");

const c = @cImport({
    @cInclude("embedded_files.h");
});

fn embeddedFile(comptime filename: []const u8) c.BinaryData {
    const data = @embedFile(filename);
    const name = std.fs.path.stem(filename);
    return .{
        .data = data,
        .size = data.len,
        .name = .{ .data = name.ptr, .size = name.len },
        .filename = .{ .data = filename.ptr, .size = filename.len },
    };
}

export fn EmbeddedFontAwesome() c.BinaryData {
    return embeddedFile("fonts/fa-solid-900.ttf");
}
export fn EmbeddedFiraSans() c.BinaryData {
    return embeddedFile("fonts/FiraSans-Regular.ttf");
}
export fn EmbeddedRoboto() c.BinaryData {
    return embeddedFile("fonts/Roboto-Regular.ttf");
}
export fn EmbeddedMada() c.BinaryData {
    return embeddedFile("fonts/Mada-SemiBold.ttf");
}

export fn EmbeddedDefaultBackground() c.BinaryData {
    return embeddedFile("images/default-background.jpg");
}

export fn EmbeddedAboutLibraryTemplateRtf() c.BinaryData {
    return embeddedFile("about_library_template.rtf");
}

export fn EmbeddedPackageInstallationRtf() c.BinaryData {
    return embeddedFile("package_installation.rtf");
}

export fn EmbeddedIrs() c.EmbeddedIrData {
    var result: c.EmbeddedIrData = undefined;
    result.irs[c.EmbeddedIr_Cold] = embeddedFile(
        "reverb_irs/Cold.flac",
    );
    result.irs[c.EmbeddedIr_Smooth] = embeddedFile(
        "reverb_irs/Smooth.flac",
    );
    result.irs[c.EmbeddedIr_Cathedral] = embeddedFile(
        "reverb_irs/Cathedral.flac",
    );
    result.irs[c.EmbeddedIr_Subtle] = embeddedFile(
        "reverb_irs/Subtle.flac",
    );
    return result;
}

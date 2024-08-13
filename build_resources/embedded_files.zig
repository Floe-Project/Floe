// SPDX-FileCopyrightText: 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

const std = @import("std");

const c = @cImport({
    @cInclude("embedded_files.h");
});

fn embeddedFile(comptime filename: []const u8, comptime legacy_name: []const u8) c.BinaryData {
    const data = @embedFile(filename);
    const name = std.fs.path.stem(filename);
    return .{
        .data = data,
        .size = data.len,
        .name = .{ .data = name.ptr, .size = name.len },
        .legacy_name = .{ .data = legacy_name.ptr, .size = legacy_name.len },
        .filename = .{ .data = filename.ptr, .size = filename.len },
    };
}

export fn EmbeddedFontAwesome() c.BinaryData {
    return embeddedFile("fonts/fa-solid-900.ttf", "");
}
export fn EmbeddedFiraSans() c.BinaryData {
    return embeddedFile("fonts/FiraSans-Regular.ttf", "");
}
export fn EmbeddedRoboto() c.BinaryData {
    return embeddedFile("fonts/Roboto-Regular.ttf", "");
}
export fn EmbeddedMada() c.BinaryData {
    return embeddedFile("fonts/Mada-SemiBold.ttf", "");
}

export fn EmbeddedDefaultBackground() c.BinaryData {
    return embeddedFile("images/default-background.jpg", "");
}

export fn EmbeddedIrs() c.EmbeddedIrData {
    var result: c.EmbeddedIrData = undefined;
    result.irs[c.EmbeddedIr_Cold] = embeddedFile(
        "reverb_irs/Cold.flac",
        "3s Shivering Cold.flac",
    );
    result.irs[c.EmbeddedIr_Smooth] = embeddedFile(
        "reverb_irs/Smooth.flac",
        "3s Smooooth.flac",
    );
    result.irs[c.EmbeddedIr_Cathedral] = embeddedFile(
        "reverb_irs/Cathedral.flac",
        "Realistic Cathedral B.flac",
    );
    result.irs[c.EmbeddedIr_Subtle] = embeddedFile(
        "reverb_irs/Subtle.flac",
        "Realistic Subtle.flac",
    );
    return result;
}

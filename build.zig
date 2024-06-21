// Copyright 2018-2024 Sam Windell
// SPDX-License-Identifier: GPL-3.0-or-later

// Notes on compiling/cross-compiling:
// Windows:
// - Zig has MinGW built-in. So Windows.h and pretty much all win32 API is available by default.
//   Cross-compiling is therefore easy.
// macOS:
// - The macOS SDKs are not included in Zig, we have to ensure they are available ourselves, and tell
//   zig where they are. We use Nix for this. See flake.nix. After that, cross-compiling works
//   well. And as a bonus, we don't have to install Xcode if we're on a mac.
// Linux:
// - We don't support cross-compiling for Linux at the moment.
// - For native compiling, we rely on nix and pkg-config. Use 'pkg-config --list-all | fzf' to find
//   the names of libraries to use when doing linkSystemLibrary2().

const std = @import("std");
const builtin = @import("builtin");

const floe_description = "Sample-based synth plugin";
const floe_copyright = "Sam Windell";
const floe_vendor = "Floe";
const floe_url = "https://github.com/Floe-Synth/Floe";
const floe_au_factory_function = "FloeFactoryFunction";
const floe_plugin_gui = true;

const rootdir = struct {
    fn getSrcDir() []const u8 {
        return std.fs.path.dirname(@src().file).?;
    }
}.getSrcDir();

const build_gen_relative = "build_gen";
const build_gen_abs = rootdir ++ "/" ++ build_gen_relative;

const ConcatCompileCommandsStep = struct {
    step: std.Build.Step,
    target: std.Build.ResolvedTarget,
    use_as_default: bool,
};

fn archAndOsPair(target: std.Target) std.BoundedArray(u8, 32) {
    var result = std.BoundedArray(u8, 32).init(0) catch @panic("OOM");
    std.fmt.format(result.writer(), "{s}-{s}", .{ @tagName(target.cpu.arch), @tagName(target.os.tag) }) catch @panic("OOM");
    return result;
}

fn compileCommandsDirForTarget(alloc: std.mem.Allocator, target: std.Target) ![]u8 {
    return std.fmt.allocPrint(alloc, "{s}/compile_commands_{s}", .{ build_gen_abs, archAndOsPair(target).slice() });
}

fn compileCommandsFileForTarget(alloc: std.mem.Allocator, target: std.Target) ![]u8 {
    return std.fmt.allocPrint(alloc, "{s}.json", .{compileCommandsDirForTarget(alloc, target) catch @panic("OOM")});
}

fn tryCopyCompileCommandsForTargetFileToDefault(alloc: std.mem.Allocator, target: std.Target) void {
    const generic_out_path = std.fs.path.join(alloc, &.{ build_gen_abs, "compile_commands.json" }) catch @panic("OOM");
    const out_path = compileCommandsFileForTarget(alloc, target) catch @panic("OOM");
    std.fs.copyFileAbsolute(out_path, generic_out_path, .{ .override_mode = null }) catch {};
}

fn tryConcatCompileCommands(step: *std.Build.Step) !void {
    const self: *ConcatCompileCommandsStep = @fieldParentPtr("step", step);

    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();

    const CompileFragment = struct {
        directory: []u8,
        file: []u8,
        output: []u8,
        arguments: [][]u8,
    };

    var compile_commands = std.ArrayList(CompileFragment).init(arena.allocator());
    const compile_commands_dir = try compileCommandsDirForTarget(arena.allocator(), self.target.result);

    {
        const maybe_dir = std.fs.openDirAbsolute(compile_commands_dir, .{ .iterate = true });
        if (maybe_dir != std.fs.Dir.OpenError.FileNotFound) {
            var dir = try maybe_dir;
            defer dir.close();
            var dir_it = dir.iterate();
            while (try dir_it.next()) |entry| {
                const read_file = try dir.openFile(entry.name, .{ .mode = std.fs.File.OpenMode.read_only });
                defer read_file.close();

                const file_contents = try read_file.readToEndAlloc(arena.allocator(), 1024 * 1024 * 1024);
                defer arena.allocator().free(file_contents);

                var trimmed_json = std.mem.trimRight(u8, file_contents, "\n\r \t");
                if (std.mem.endsWith(u8, trimmed_json, ",")) {
                    trimmed_json = trimmed_json[0 .. trimmed_json.len - 1];
                }

                var parsed_data = try std.json.parseFromSlice(CompileFragment, arena.allocator(), trimmed_json, .{});

                var already_present = false;
                for (compile_commands.items) |command| {
                    if (std.mem.eql(u8, command.file, parsed_data.value.file)) {
                        already_present = true;
                        break;
                    }
                }
                if (!already_present) {
                    var args = std.ArrayList([]u8).fromOwnedSlice(arena.allocator(), parsed_data.value.arguments);

                    var to_remove = std.ArrayList(u32).init(arena.allocator());
                    var index: u32 = 0;
                    for (args.items) |arg| {
                        // clangd crashes when using c++2b on mac (May 2023)
                        // if (std.mem.eql(u8, arg.*, "-std=c++2b"))
                        //     arg.* = try arena.allocator().dupe(u8, "-std=c++20");

                        // clangd doesn't like this flag
                        if (std.mem.eql(u8, arg, "--no-default-config"))
                            try to_remove.append(index);

                        // clang-tidy doesn't like this flag being there
                        if (std.mem.eql(u8, arg, "-ftime-trace"))
                            try to_remove.append(index);

                        // clang-tidy doesn't like this
                        if (std.mem.eql(u8, arg, "-ObjC++"))
                            try to_remove.append(index);

                        index = index + 1;
                    }

                    // clang-tidy doesn't like this when cross-compiling macos, it's a sequence we need to look for and remove, it's no good just removing the '+pan' by itself
                    index = 0;
                    for (args.items) |arg| {
                        if (std.mem.eql(u8, arg, "-Xclang")) {
                            if (index + 3 < args.items.len) {
                                if (std.mem.eql(u8, args.items[index + 1], "-target-feature") and std.mem.eql(u8, args.items[index + 2], "-Xclang") and std.mem.eql(u8, args.items[index + 3], "+pan")) {
                                    try to_remove.append(index);
                                    try to_remove.append(index + 1);
                                    try to_remove.append(index + 2);
                                    try to_remove.append(index + 3);
                                }
                            }
                        }
                        index = index + 1;
                    }

                    var num_removed: u32 = 0;
                    for (to_remove.items) |i| {
                        _ = args.orderedRemove(i - num_removed);
                        num_removed = num_removed + 1;
                    }

                    parsed_data.value.arguments = try args.toOwnedSlice();

                    try compile_commands.append(parsed_data.value);
                }
            }
        }
    }

    if (compile_commands.items.len != 0) {
        const out_path = compileCommandsFileForTarget(arena.allocator(), self.target.result) catch @panic("OOM");

        const maybe_file = std.fs.openFileAbsolute(out_path, .{});
        if (maybe_file != std.fs.File.OpenError.FileNotFound) {
            const f = try maybe_file;
            defer f.close();

            const file_contents = try f.readToEndAlloc(arena.allocator(), 1024 * 1024 * 1024);
            defer arena.allocator().free(file_contents);

            const existing_compile_commands = try std.json.parseFromSlice([]CompileFragment, arena.allocator(), file_contents, .{});

            for (existing_compile_commands.value) |existing_c| {
                var is_replaced_by_newer = false;
                for (compile_commands.items) |new_c| {
                    if (std.mem.eql(u8, new_c.file, existing_c.file)) {
                        is_replaced_by_newer = true;
                        break;
                    }
                }

                if (!is_replaced_by_newer) {
                    try compile_commands.append(existing_c);
                }
            }
        }

        var out_f = try std.fs.createFileAbsolute(out_path, .{});
        defer out_f.close();
        var buffered_writer: std.io.BufferedWriter(20 * 1024, @TypeOf(out_f.writer())) = .{ .unbuffered_writer = out_f.writer() };

        try std.json.stringify(compile_commands.items, .{}, buffered_writer.writer());
        try buffered_writer.flush();

        try std.fs.deleteTreeAbsolute(compile_commands_dir);

        if (self.use_as_default) {
            tryCopyCompileCommandsForTargetFileToDefault(arena.allocator(), self.target.result);
        }
    }
}

fn concatCompileCommands(step: *std.Build.Step, prog_node: std.Progress.Node) !void {
    _ = prog_node;

    tryConcatCompileCommands(step) catch |err| {
        std.debug.print("failed to concatenate compile commands: {any}\n", .{err});
    };
}

const PostInstallStep = struct {
    step: std.Build.Step,
    make_macos_bundle: bool,
    compile_step: *std.Build.Step.Compile,
    context: *BuildContext,
};

fn postInstallMacosBinary(context: *BuildContext, step: *std.Build.Step, make_macos_bundle: bool, path: []const u8, bundle_name: []const u8, version: ?std.SemanticVersion) !void {
    var b = step.owner;

    var final_binary_path: ?[]const u8 = null;
    if (make_macos_bundle) {
        const working_dir = std.fs.path.dirname(path).?;
        var dir = try std.fs.openDirAbsolute(working_dir, .{});
        defer dir.close();

        try dir.deleteTree(bundle_name);
        try dir.makePath(b.pathJoin(&.{ bundle_name, "Contents", "MacOS" }));

        const exe_name = std.fs.path.stem(bundle_name);
        final_binary_path = b.pathJoin(&.{ working_dir, bundle_name, "Contents", "MacOS", exe_name });
        {
            var dest_dir = try dir.openDir(b.pathJoin(&.{ bundle_name, "Contents", "MacOS" }), .{});
            defer dest_dir.close();
            try dir.copyFile(std.fs.path.basename(path), dest_dir, exe_name, .{});
        }

        {
            const pkg_info_file = try dir.createFile(b.pathJoin(&.{ bundle_name, "Contents", "PkgInfo" }), std.fs.File.CreateFlags{});
            defer pkg_info_file.close();
            try pkg_info_file.writeAll("BNDL????");
        }

        {
            const pkg_info_file = try dir.createFile(b.pathJoin(&.{ bundle_name, "Contents", "Info.plist" }), std.fs.File.CreateFlags{});
            defer pkg_info_file.close();

            // TODO: include AU info
            // \\ <?xml version="1.0" encoding="UTF-8"?>
            // \\ <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
            // \\ <plist version="1.0">
            // \\ </plist>

            try std.fmt.format(pkg_info_file.writer(),
                \\<?xml version="1.0" encoding="UTF-8"?>
                \\<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
                \\<plist version="1.0">
                \\    <dict>
                \\        <key>CFBundleDevelopmentRegion</key>
                \\        <string>English</string>
                \\        <key>CFBundleDisplayName</key>
                \\        <string>{[display_name]s}</string>
                \\        <key>CFBundleExecutable</key>
                \\        <string>{[executable_name]s}</string>
                \\        <key>CFBundleIdentifier</key>
                \\        <string>{[bundle_identifier]s}</string>
                \\        <key>CFBundleName</key>
                \\        <string>{[bundle_name]s}</string>
                \\        <key>CFBundlePackageType</key>
                \\        <string>BNDL</string>
                \\        <key>CFBundleShortVersionString</key>
                \\        <string>{[major]d}.{[minor]d}.{[patch]d}</string>
                \\        <key>CFBundleVersion</key>
                \\        <string>{[major]d}.{[minor]d}.{[patch]d}</string>
                \\        <key>CFBundleSupportedPlatforms</key>
                \\        <array>
                \\            <string>MacOSX</string>
                \\        </array>
                \\        <key>NSHighResolutionCapable</key>
                \\        <true />
                \\        <key>NSHumanReadableCopyright</key>
                \\        <string>Copyright {[copyright]s}</string>
                \\{[audio_unit_dict]s}
                \\    </dict>
                \\</plist>
            , .{
                .display_name = bundle_name,
                .executable_name = exe_name,
                .bundle_identifier = try std.fmt.allocPrint(b.allocator, "com.{s}.{s}", .{ floe_vendor, bundle_name }),
                .bundle_name = bundle_name,
                .major = if (version != null) version.?.major else 1,
                .minor = if (version != null) version.?.minor else 0,
                .patch = if (version != null) version.?.patch else 0,
                .copyright = floe_copyright,
                .audio_unit_dict = if (std.mem.count(u8, bundle_name, ".component") == 1)
                    b.fmt(
                        \\        <key>AudioComponents</key>
                        \\        <array>
                        \\            <dict>
                        \\                <key>name</key>
                        \\                <string>Floe</string>
                        \\                <key>description</key>
                        \\                <string>{[description]s}</string>
                        \\                <key>factoryFunction</key>
                        \\                <string>{[factory_function]s}</string>
                        \\                <key>manufacturer</key>
                        \\                <string>{[vendor]s}</string>
                        \\                <key>subtype</key>
                        \\                <string>smpl</string>
                        \\                <key>type</key>
                        \\                <string>aumu</string>
                        \\                <key>version</key>
                        \\                <integer>{[version_packed]d}</integer>
                        \\                <key>resourceUsage</key>
                        \\                <dict>
                        \\                    <key>network.client</key>
                        \\                    <true/>
                        \\                    <key>temporary-exception.files.all.read-write</key>
                        \\                    <true/>
                        \\                </dict>
                        \\            </dict>
                        \\        </array>
                    , .{
                        .description = floe_description,
                        .factory_function = floe_au_factory_function,
                        .vendor = floe_vendor,
                        .version_packed = if (version != null) (version.?.major << 16) | (version.?.minor << 8) | version.?.patch else 0,
                    })
                else
                    "",
            });
        }
    } else {
        final_binary_path = path;
    }

    if (context.build_mode != .production)
        try step.evalChildProcess(&.{ "dsymutil", final_binary_path.? });
}

const LipoStep = struct {
    step: std.Build.Step,
    make_macos_bundle: bool,
    step_x86: ?*std.Build.Step.Compile,
    step_arm: ?*std.Build.Step.Compile,
    context: *BuildContext,
};

fn addToLipoSteps(context: *BuildContext, step: *std.Build.Step.Compile, make_macos_bundle: bool) !void {
    if (step.rootModuleTarget().os.tag != .macos) return;

    const name = step.name;
    if (!context.lipo_steps.contains(name)) {
        var lipo_step = context.b.allocator.create(LipoStep) catch @panic("OOM");
        lipo_step.* = LipoStep{
            .step = std.Build.Step.init(.{
                .id = std.Build.Step.Id.custom,
                .name = "Lipo step",
                .owner = context.b,
                .makeFn = doLipoStep,
            }),
            .make_macos_bundle = make_macos_bundle,
            .step_x86 = null,
            .step_arm = null,
            .context = context,
        };
        context.master_step.dependOn(&lipo_step.step);
        try context.lipo_steps.put(name, lipo_step);
    }

    const lipo_step = context.lipo_steps.get(name);

    var add_step: ?*?*std.Build.Step.Compile = null;
    if (step.rootModuleTarget().cpu.arch == .x86_64) {
        add_step = &lipo_step.?.step_x86;
    } else if (step.rootModuleTarget().cpu.arch == .aarch64) {
        add_step = &lipo_step.?.step_arm;
    }

    if (add_step) |s| {
        s.* = step;
        lipo_step.?.step.dependOn(&step.step);
        lipo_step.?.step.dependOn(context.b.getInstallStep());
    }
}

fn doLipoStep(step: *std.Build.Step, prog_node: std.Progress.Node) !void {
    const self: *LipoStep = @fieldParentPtr("step", step);
    _ = prog_node;

    if (self.step_x86 == null) return;
    if (self.step_arm == null) return;
    const x86 = self.step_x86.?;
    const arm = self.step_arm.?;

    const path1 = x86.installed_path.?;
    const path2 = arm.installed_path.?;

    const working_dir = std.fs.path.dirname(std.fs.path.dirname(path1).?).?;
    var dir = try std.fs.openDirAbsolute(working_dir, .{});
    defer dir.close();

    const universal_dir = "universal-macos";
    try dir.makePath(universal_dir);
    const out_path = step.owner.pathJoin(&.{ working_dir, universal_dir, std.fs.path.basename(path1) });

    try step.evalChildProcess(&.{ "llvm-lipo", "-create", "-output", out_path, path1, path2 });

    try postInstallMacosBinary(self.context, step, self.make_macos_bundle, out_path, x86.name, x86.version);
}

const Win32EmbedInfo = struct {
    name: []const u8,
    description: []const u8,
    icon_path: ?[]const u8,
};

fn addWin32EmbedInfo(step: *std.Build.Step.Compile, info: Win32EmbedInfo) !void {
    if (step.rootModuleTarget().os.tag != .windows) return;

    const b = step.step.owner;
    const arena = b.allocator;

    const path = try std.fmt.allocPrint(arena, "{s}/{s}.rc", .{ build_gen_relative, step.name });
    const file = try std.fs.createFileAbsolute(b.pathJoin(&.{ rootdir, path }), .{});
    defer file.close();

    const this_year = 1970 + @divTrunc(std.time.timestamp(), 60 * 60 * 24 * 365);

    try std.fmt.format(file.writer(),
        \\ #include <windows.h>
        \\ 
        \\ VS_VERSION_INFO VERSIONINFO
        \\  FILEVERSION {[major]d},{[minor]d},{[patch]d},0
        \\  PRODUCTVERSION {[major]d},{[minor]d},{[patch]d},0
        \\ BEGIN
        \\     BLOCK "StringFileInfo"
        \\     BEGIN
        \\         BLOCK "040904b0"
        \\         BEGIN
        \\             VALUE "CompanyName", "{[vendor]s}\0"
        \\             VALUE "FileDescription", "{[description]s}\0"
        \\             VALUE "FileVersion", "{[major]d}.{[minor]d}.{[patch]d}\0"
        \\             VALUE "LegalCopyright", "{[copyright]s} © {[this_year]d}\0"
        \\             VALUE "ProductName", "{[name]s}\0"
        \\             VALUE "ProductVersion", "{[major]d}.{[minor]d}.{[patch]d}\0"
        \\         END
        \\     END
        \\     BLOCK "VarFileInfo"
        \\     BEGIN
        \\         VALUE "Translation", 0x409, 1200
        \\     END
        \\ END
    , .{
        .major = if (step.version != null) step.version.?.major else 0,
        .minor = if (step.version != null) step.version.?.minor else 0,
        .patch = if (step.version != null) step.version.?.patch else 0,
        .description = info.description,
        .name = info.name,
        .this_year = this_year,
        .copyright = floe_copyright,
        .vendor = floe_vendor,
    });

    if (info.icon_path) |p|
        try std.fmt.format(file.writer(), "icon_id ICON \"{s}\"\n", .{p});

    step.addWin32ResourceFile(.{ .file = b.path(path) });
}

fn performPostInstallConfig(step: *std.Build.Step, prog_node: std.Progress.Node) !void {
    var arena = std.heap.ArenaAllocator.init(std.heap.page_allocator);
    defer arena.deinit();

    const self: *PostInstallStep = @fieldParentPtr("step", step);
    _ = prog_node;
    var path = self.compile_step.installed_path.?;

    switch (self.compile_step.rootModuleTarget().os.tag) {
        .windows => {
            var out_filename = self.compile_step.out_filename;

            const dll_types_to_remove = [_][]const u8{ "clap", "vst3" };
            for (dll_types_to_remove) |t| {
                const suffix = try std.fmt.allocPrint(arena.allocator(), ".{s}.dll", .{t});
                if (std.mem.endsWith(u8, path, suffix)) {
                    const new_path = try std.fmt.allocPrint(arena.allocator(), "{s}.{s}", .{ path[0 .. path.len - suffix.len], t });
                    try std.fs.renameAbsolute(path, new_path);
                    path = new_path;

                    std.debug.assert(std.mem.endsWith(u8, out_filename, suffix));
                    out_filename = try std.fmt.allocPrint(arena.allocator(), "{s}.{s}", .{ out_filename[0 .. out_filename.len - suffix.len], t });
                }
            }
        },
        .macos => {
            try postInstallMacosBinary(self.context, step, self.make_macos_bundle, path, self.compile_step.name, self.compile_step.version);
        },
        .linux => {
            const working_dir = std.fs.path.dirname(path).?;
            var dir = try std.fs.openDirAbsolute(working_dir, .{});
            defer dir.close();
            const filename = std.fs.path.basename(path);

            if (std.mem.indexOf(u8, filename, "Floe.clap.so") != null) {
                try dir.rename(filename, "Floe.clap");
            } else if (std.mem.indexOf(u8, filename, "Floe.vst3.so") != null) {
                const subdir = "Floe.vst3/Contents/x86_64-linux";
                try dir.makePath(subdir);
                try dir.rename(filename, subdir ++ "/Floe.so");
            }
        },
        else => {
            unreachable;
        },
    }
}

const BuildMode = enum {
    development,
    performance_profiling,
    production, // a.k.a.: release, end-user, for-distribution
};

const BuildContext = struct {
    b: *std.Build,
    enable_tracy: bool,
    build_mode: BuildMode,
    lipo_steps: std.StringHashMap(*LipoStep),
    master_step: *std.Build.Step,
    test_step: *std.Build.Step,
    optimise: std.builtin.OptimizeMode,
};

fn genericFlags(context: *BuildContext, target: std.Build.ResolvedTarget, extra_flags: []const []const u8) ![][]const u8 {
    var flags = std.ArrayList([]const u8).init(context.b.allocator);
    try flags.appendSlice(extra_flags);
    try flags.append("-fchar8_t");
    try flags.append("-D_USE_MATH_DEFINES");
    try flags.append("-D__USE_FILE_OFFSET64");
    try flags.append("-D_FILE_OFFSET_BITS=64");
    try flags.append("-ftime-trace");

    if (target.result.os.tag == .linux) {
        // NOTE(Sam, June 2024): workaround for a bug in Zig (most likely) where our shared library always causes a crash after dlclose(), as described here: https://github.com/ziglang/zig/issues/17908
        // The workaround involves adding this flag and also adding a custom bit of code using __attribute__((destructor)) to manually call __cxa_finalize(): https://stackoverflow.com/questions/34308720/where-is-dso-handle-defined/48256026#48256026
        try flags.append("-fno-use-cxa-atexit");
    }

    if (context.build_mode == .production) {
        try flags.append("-fmacro-prefix-map=" ++ "=/"); // make the __FILE__ macro non-absolute
        try flags.append("-fvisibility=hidden");
    } else if (target.query.isNative() and context.enable_tracy) {
        try flags.append("-DTRACY_ENABLE");
        try flags.append("-DTRACY_MANUAL_LIFETIME");
        try flags.append("-DTRACY_DELAYED_INIT");
        try flags.append("-DTRACY_ONLY_LOCALHOST");
        if (target.result.os.tag == .linux) {
            // Couldn't get these working well so just disabling them
            try flags.append("-DTRACY_NO_CALLSTACK");
            try flags.append("-DTRACY_NO_SYSTEM_TRACING");
        }
    }

    // A bit of information about debug symbols:
    // DWARF is a debugging information format. It is used widely, particularly on Linux and macOS. libbacktrace,
    // which we use for getting nice stack traces can read DWARF information from the executable on any OS. All
    // we need to do is make sure that the DWARF info is available for libbacktrace to read.
    //
    // On Windows, there is the PDB format, this is a separate file that contains the debug information. Zig
    // generates this too, but we can tell it to also embed DWARF debug info into the executable, that's what the
    // -gdwarf flag does.
    //
    // On Linux, it's easy, just use the same flag.
    //
    // On macOS, there is a slightly different approach. DWARF info is embedded in the compiled .o flags. But it's
    // not aggregated into the final executable. Instead, the final executable contains a 'debug map' which points
    // to all of the object files and shows where the DWARF info is. You can see this map by running
    // 'dsymutil --dump-debug-map my-exe'.
    //
    // In order to aggregate the DWARF info into the final executable, we need to run 'dsymutil my-exe'. This then
    // outputs a .dSYM folder which contains the aggregated DWARF info. libbacktrace looks for this dSYM folder
    // adjacent to the executable.

    // Include dwark debug info, even on windows. This means we can use the libbacktrace library everywhere to get really
    // good stack traces.
    try flags.append("-gdwarf");

    if (context.optimise != .ReleaseFast) {
        if (target.result.os.tag != .windows) {
            // By default, zig enables UBSan (unless ReleaseFast mode) in trap mode. Meaning it will catch undefined
            // behaviour and trigger a trap which can be caught by signal handlers. UBSan also has a mode where undefined
            // behaviour will instead call various functions. This is called the UBSan runtime. It's really easy to implement
            // the 'minimal' version of this runtime: we just have to declare a nuch of functions like __ubsan_handle_x. So
            // that's what we do rather than trying to link with the system's version.
            // https://github.com/ziglang/zig/issues/5163#issuecomment-811606110
            try flags.append("-fno-sanitize-trap=undefined"); // undo zig's default behaviour (trap mode)
            const minimal_runtime_mode = false; // I think it's better performance. Certainly less information.
            if (minimal_runtime_mode) {
                try flags.append("-fsanitize-runtime"); // set it to 'minimal' mode
            }
        } else {
            // For some reason the same method of creating our own UBSan runtime doesn't work on windows. These are the link
            // errors that we get:
            // error: lld-link: could not open 'liblibclang_rt.ubsan_standalone-x86_64.a': No such file or directory
            // error: lld-link: could not open 'liblibclang_rt.ubsan_standalone_cxx-x86_64.a': No such file or directory
        }
    }

    if (target.result.os.tag == .windows) {
        // On Windows, fix compile errors related to deprecated usage of string in mingw
        try flags.append("-DSTRSAFE_NO_DEPRECATE");
        try flags.append("-DUNICODE");
        try flags.append("-D_UNICODE");
    } else if (target.result.os.tag == .macos) {
        try flags.append("-DGL_SILENCE_DEPRECATION"); // disable opengl warnings on macos

        // don't fail when compiling macOS obj-c SDK headers
        try flags.appendSlice(&.{
            "-Wno-elaborated-enum-base",
            "-Wno-missing-method-return-type",
            "-Wno-deprecated-declarations",
            "-Wno-deprecated-anon-enum-enum-conversion",
        });
    }

    return try flags.toOwnedSlice();
}

fn cppFlags(b: *std.Build, generic_flags: [][]const u8, extra_flags: []const []const u8) ![][]const u8 {
    var flags = std.ArrayList([]const u8).init(b.allocator);
    try flags.appendSlice(generic_flags);
    try flags.appendSlice(extra_flags);
    try flags.append("-std=c++2b");
    return try flags.toOwnedSlice();
}

fn objcppFlags(b: *std.Build, cpp_flags: [][]const u8, extra_flags: []const []const u8) ![][]const u8 {
    var flags = std.ArrayList([]const u8).init(b.allocator);
    try flags.appendSlice(cpp_flags);
    try flags.appendSlice(extra_flags);
    try flags.append("-ObjC++");
    try flags.append("-fobjc-arc");
    return try flags.toOwnedSlice();
}

fn applyUniversalSettings(context: *BuildContext, step: *std.Build.Step.Compile) void {
    var b = context.b;
    if (!step.rootModuleTarget().isDarwin()) {
        // TODO: try LTO on mac again
        // LTO doesn't seem like it's supported on mac
        step.want_lto = context.build_mode == .production;
    }
    step.rdynamic = context.build_mode != .production;
    step.linkLibC();

    step.addIncludePath(b.path("."));
    step.addIncludePath(b.path("src"));
    step.addIncludePath(b.path("third_party_libs"));
    step.addIncludePath(b.path("third_party_libs/clap/include"));
    step.addIncludePath(b.path("third_party_libs/tracy/public"));
    step.addIncludePath(b.path("third_party_libs/pugl/include"));
    step.addIncludePath(b.path("third_party_libs/flac/include"));
    step.addIncludePath(b.path("third_party_libs/portmidi/pm_common"));

    if (step.rootModuleTarget().isDarwin()) {
        const sdk_root = b.graph.env_map.get("MACOSX_SDK_SYSROOT");
        if (sdk_root == null) {
            // This environment variable should be set and should be a path containing the macOS SDKS.
            // Nix is a great way to provide this. See flake.nix.
            //
            // An alternative option would be to download the macOS SDKs manually. For example: https://github.com/joseluisq/macosx-sdks. And then set the env-var to that.
            @panic("env var $MACOSX_SDK_SYSROOT must be set");
        }
        b.sysroot = sdk_root;

        step.addSystemIncludePath(.{ .cwd_relative = b.pathJoin(&.{ sdk_root.?, "/usr/include" }) });
        step.addLibraryPath(.{ .cwd_relative = b.pathJoin(&.{ sdk_root.?, "/usr/lib" }) });
        step.addFrameworkPath(.{ .cwd_relative = b.pathJoin(&.{ sdk_root.?, "/System/Library/Frameworks" }) });
    }
}

fn buildLua(b: *std.Build, target: std.Build.ResolvedTarget, optimize: std.builtin.OptimizeMode) *std.Build.Step.Compile {
    const lib_opts = .{
        .name = "lua",
        .target = target,
        .optimize = optimize,
        .version = std.SemanticVersion{ .major = 5, .minor = 4, .patch = 6 },
    };
    const lib = b.addStaticLibrary(lib_opts);

    const lua_dir = "third_party_libs/lua";

    lib.addIncludePath(b.path(lua_dir));

    const flags = [_][]const u8{
        switch (target.result.os.tag) {
            .linux => "-DLUA_USE_LINUX",
            .macos => "-DLUA_USE_MACOSX",
            .windows => "-DLUA_USE_WINDOWS",
            else => "-DLUA_USE_POSIX",
        },
        if (optimize == .Debug) "-DLUA_USE_APICHECK" else "",
    };

    // compile as C++ so as to use exceptions
    lib.addCSourceFile(.{ .file = b.path(b.pathJoin(&.{ lua_dir, "onelua.cpp" })), .flags = &flags });
    lib.linkLibC();

    return lib;
}

fn getTargets(b: *std.Build, user_given_target_presets: ?[]const u8) !std.ArrayList(std.Build.ResolvedTarget) {
    var preset_strings: []const u8 = "native";
    if (user_given_target_presets != null) {
        preset_strings = user_given_target_presets.?;
    }

    const SupportedTargetId = enum {
        native,
        x86_64_windows,
        x86_64_linux,
        x86_64_macos,
        aarch64_macos,
    };

    // declare a hash map of target presets to SupportedTargetId
    var target_map = std.StringHashMap([]const SupportedTargetId).init(b.allocator);

    // the actual targets
    try target_map.put("native", &.{.native});
    try target_map.put("x86_64-windows", &.{.x86_64_windows});
    try target_map.put("x86_64-linux", &.{.x86_64_linux});
    try target_map.put("x86_64-macos", &.{.x86_64_macos});
    try target_map.put("aarch64-macos", &.{.aarch64_macos});

    // aliases/shortcuts
    try target_map.put("windows", &.{.x86_64_windows});
    try target_map.put("linux", &.{.x86_64_linux});
    try target_map.put("mac_x86", &.{.x86_64_macos});
    try target_map.put("mac_arm", &.{.aarch64_macos});
    try target_map.put("mac_ub", &.{ .x86_64_macos, .aarch64_macos });
    if (builtin.os.tag == .linux) {
        try target_map.put("dev", &.{ .native, .x86_64_windows, .aarch64_macos });
    } else if (builtin.os.tag == .macos) {
        try target_map.put("dev", &.{ .native, .x86_64_windows });
    }

    var targets = std.ArrayList(std.Build.ResolvedTarget).init(b.allocator);

    // I think Win10+ and macOS 11+ would allow us to target x86_64_v2 (which includes SSE3 and SSE4), but I can't
    // find definitive information on this. It's not a big deal for now; the baseline x86_64 target includes SSE2
    // which is the important feature for our performance-critical code.
    const x86_cpu = "x86_64";
    const apple_arm_cpu = "apple_m1";
    const min_windows_version = "win10";
    const min_macos_version = "11.0";

    var it = std.mem.splitSequence(u8, preset_strings, ",");
    while (it.next()) |preset_string| {
        const target_ids = target_map.get(preset_string) orelse {
            std.debug.print("unknown target preset: {s}\n", .{preset_string});
            @panic("unknown target preset");
        };

        for (target_ids) |t| {
            var arch_os_abi: []const u8 = undefined;
            var cpu_features: []const u8 = undefined;
            switch (t) {
                .native => {
                    arch_os_abi = "native";
                    // valgrind doesn't like some AVX instructions so we'll target the baseline x86_64 for now
                    cpu_features = if (builtin.cpu.arch == .x86_64) x86_cpu else "native";
                },
                .x86_64_windows => {
                    arch_os_abi = "x86_64-windows." ++ min_windows_version;
                    cpu_features = x86_cpu;
                },
                .x86_64_linux => {
                    arch_os_abi = "x86_64-linux-gnu.2.29";
                    cpu_features = x86_cpu;
                },
                .x86_64_macos => {
                    arch_os_abi = "x86_64-macos." ++ min_macos_version;
                    cpu_features = x86_cpu;
                },
                .aarch64_macos => {
                    arch_os_abi = "aarch64-macos." ++ min_macos_version;
                    cpu_features = apple_arm_cpu;
                },
            }

            try targets.append(b.resolveTargetQuery(try std.Target.Query.parse(.{
                .arch_os_abi = arch_os_abi,
                .cpu_features = cpu_features,
            })));
        }
    }

    return targets;
}

fn getLicenceText(b: *std.Build, filename: []const u8) ![]const u8 {
    const file = try std.fs.openFileAbsolute(b.pathJoin(&.{ rootdir, "LICENSES", filename }), .{ .mode = std.fs.File.OpenMode.read_only });
    defer file.close();

    return try file.readToEndAlloc(b.allocator, 1024 * 1024 * 1024);
}

pub fn build(b: *std.Build) void {
    const gen_docs_step = b.step("gen-docs", "Generate HTML documentation snippets");

    const build_mode = b.option(
        BuildMode,
        "build-mode",
        "The preset for building the project, affects optimisation, debug settings, etc.",
    ) orelse .development;

    // Installing plugins to global plugin folders requires admin rights but it's often easier to debug
    // things without requiring admin. For production builds it's always enabled.
    const windows_installer_require_admin = b.option(
        bool,
        "win-installer-elevated",
        "Whether the installer should be set to administrator-required mode",
    ) orelse (build_mode == .production);

    const core_library_path = b.option(
        []const u8,
        "core-lib-path",
        "Full path to the Core library, used for embedding in the installer",
    ) orelse (b.pathJoin(&.{ rootdir, "build_resources", "Core.library" }));

    var enable_tracy = b.option(bool, "tracy", "Enable Tracy profiler") orelse false;
    if (build_mode == .performance_profiling) enable_tracy = true;

    var build_context: BuildContext = .{
        .b = b,
        .enable_tracy = enable_tracy,
        .build_mode = build_mode,
        .lipo_steps = std.StringHashMap(*LipoStep).init(b.allocator),
        .master_step = b.step("compile", "Compile all"),
        .test_step = b.step("test", "Run tests"),
        .optimise = switch (build_mode) {
            .development => std.builtin.OptimizeMode.Debug,
            .performance_profiling, .production => std.builtin.OptimizeMode.ReleaseSafe,
        },
    };

    const user_given_target_presets = b.option([]const u8, "targets", "Target operating system");

    // ignore any error
    std.fs.makeDirAbsolute(build_gen_abs) catch {};

    // const install_dir = b.install_path; // zig-out

    const targets = getTargets(b, user_given_target_presets) catch @panic("OOM");

    // If we're building for multiple targets at the same time, we need to choose one that gets to be the final compile_commands.json.
    const target_for_compile_commands = targets.items[0];
    // We'll try installing the desired compile_commands.json version here in case any previous build already created it.
    tryCopyCompileCommandsForTargetFileToDefault(b.allocator, target_for_compile_commands.result);

    for (targets.items) |target| {
        var join_compile_commands = b.allocator.create(ConcatCompileCommandsStep) catch @panic("OOM");
        join_compile_commands.* = ConcatCompileCommandsStep{
            .step = std.Build.Step.init(.{
                .id = std.Build.Step.Id.custom,
                .name = "Concatenate compile_commands JSON",
                .owner = b,
                .makeFn = concatCompileCommands,
            }),
            .target = target,
            .use_as_default = target.query.eql(target_for_compile_commands.query),
        };

        // NOTE (Sam, 27th June 2023): we can set the field override_dest_dir to this value, but it does not effect the PDB location on Windows. PDB's will end up in different folders. I'm not sure if this is a bug or not.
        const install_subfolder_string = b.dupe(archAndOsPair(target.result).slice());
        const install_subfolder = std.Build.Step.InstallArtifact.Options.Dir{
            .override = std.Build.InstallDir{ .custom = install_subfolder_string },
        };

        var floe_version_string: ?[]const u8 = null;
        {
            var file = std.fs.openFileAbsolute(rootdir ++ "/version.txt", .{ .mode = .read_only }) catch @panic("version.txt not found");
            defer file.close();
            floe_version_string = file.readToEndAlloc(b.allocator, 256) catch @panic("version.txt error");
            floe_version_string = std.mem.trim(u8, floe_version_string.?, " \r\n\t");
        }

        const floe_version = std.SemanticVersion.parse(floe_version_string.?) catch @panic("invalid version");

        const generic_flags = genericFlags(&build_context, target, &.{}) catch unreachable;
        const generic_fp_flags = genericFlags(&build_context, target, &.{
            "-gen-cdb-fragment-path",
            compileCommandsDirForTarget(b.allocator, target.result) catch unreachable, // IMPROVE: will this error if the path contains a space?
            "-Werror",
            "-Wconversion",
            "-Wexit-time-destructors",
            "-Wglobal-constructors",
            "-Wall",
            "-Wextra",
            "-Wextra-semi",
            "-Wshadow",
            "-Wimplicit-fallthrough",
            "-Wunused-member-function",
            "-Wunused-template",
            "-Wcast-align",
            "-Wdouble-promotion",
            "-Woverloaded-virtual",
            "-Wno-missing-field-initializers",
            b.fmt("-DOBJC_NAME_PREFIX=Floe{d}{d}{d}", .{ floe_version.major, floe_version.minor, floe_version.patch }),
            "-DFLAC__NO_DLL",
            "-DPUGL_DISABLE_DEPRECATED",
            "-DPUGL_STATIC",

            // Minimise windows.h size for faster compile times:
            // "Define one or more of the NOapi symbols to exclude the API. For example, NOCOMM excludes the serial communication API. For a list of support NOapi symbols, see Windows.h."
            "-DWIN32_LEAN_AND_MEAN",
            "-DNOKANJI",
            "-DNOHELP",
            "-DNOMCX",
            "-DNOCLIPBOARD",
            "-DNONLS",
            "-DNOMEMMGR",
            "-DNOMETAFILE",
            "-DNOMINMAX",
            "-DNOOPENFILE",
            "-DNOSERVICE",
            "-DNOSOUND",
            "-DNOTEXTMETRIC",
            "-DSTRICT",
            "-DNOMINMAX",
        }) catch unreachable;
        const cpp_flags = cppFlags(b, generic_flags, &.{}) catch unreachable;
        const cpp_fp_flags = cppFlags(b, generic_fp_flags, &.{}) catch unreachable;
        const objcpp_flags = objcppFlags(b, cpp_flags, &.{}) catch unreachable;
        const objcpp_fp_flags = objcppFlags(b, cpp_fp_flags, &.{}) catch unreachable;

        const floe_version_major: i64 = @intCast(floe_version.major);
        const floe_version_minor: i64 = @intCast(floe_version.minor);
        const floe_version_patch: i64 = @intCast(floe_version.patch);

        const build_config_step = b.addConfigHeader(.{
            .style = .blank,
        }, .{
            .PRODUCTION_BUILD = build_context.build_mode == .production,
            .RUNTIME_SAFETY_CHECKS_ON = build_context.optimise == .Debug or build_context.optimise == .ReleaseSafe,
            .FLOE_MAJOR_VERSION = floe_version_major,
            .FLOE_MINOR_VERSION = floe_version_minor,
            .FLOE_PATCH_VERSION = floe_version_patch,
            .FLOE_VERSION_STRING = floe_version_string,
            .FLOE_DESCRIPTION = floe_description,
            .FLOE_URL = floe_url,
            .FLOE_VENDOR = floe_vendor,
            .IS_WINDOWS = target.result.os.tag == .windows,
            .IS_MACOS = target.result.os.tag == .macos,
            .IS_LINUX = target.result.os.tag == .linux,
            .FLOE_GUI = floe_plugin_gui,
        });

        var stb_sprintf = b.addObject(.{
            .name = "stb_sprintf",
            .target = target,
            .optimize = build_context.optimise,
        });
        stb_sprintf.addCSourceFile(.{ .file = b.path("third_party_libs/stb/stb_sprintf.c") });
        stb_sprintf.linkLibC();

        var xxhash = b.addObject(.{
            .name = "xxhash",
            .target = target,
            .optimize = build_context.optimise,
        });
        xxhash.addCSourceFile(.{ .file = b.path("third_party_libs/xxhash/xxhash.c") });
        xxhash.linkLibC();

        const tracy = b.addStaticLibrary(.{
            .name = "tracy",
            .target = target,
            .optimize = build_context.optimise,
        });
        {
            tracy.addCSourceFiles(.{
                .files = &.{"third_party_libs/tracy/public/TracyClient.cpp"},
                .flags = cpp_flags,
            });

            switch (target.result.os.tag) {
                .windows => {
                    tracy.linkSystemLibrary("ws2_32");
                },
                .macos => {},
                .linux => {},
                else => {
                    unreachable;
                },
            }
            tracy.linkLibCpp();
            applyUniversalSettings(&build_context, tracy);
        }

        const vitfx = b.addStaticLibrary(.{
            .name = "vitfx",
            .target = target,
            .optimize = build_context.optimise,
        });
        {
            const vitfx_path = "third_party_libs/vitfx";
            vitfx.addCSourceFiles(.{
                .files = &.{
                    vitfx_path ++ "/src/synthesis/effects/reverb.cpp",
                    vitfx_path ++ "/src/synthesis/effects/phaser.cpp",
                    vitfx_path ++ "/src/synthesis/effects/delay.cpp",
                    vitfx_path ++ "/src/synthesis/framework/processor.cpp",
                    vitfx_path ++ "/src/synthesis/framework/processor_router.cpp",
                    vitfx_path ++ "/src/synthesis/framework/value.cpp",
                    vitfx_path ++ "/src/synthesis/framework/feedback.cpp",
                    vitfx_path ++ "/src/synthesis/framework/operators.cpp",
                    vitfx_path ++ "/src/synthesis/filters/phaser_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/synth_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/sallen_key_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/comb_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/digital_svf.cpp",
                    vitfx_path ++ "/src/synthesis/filters/dirty_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/ladder_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/diode_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/formant_filter.cpp",
                    vitfx_path ++ "/src/synthesis/filters/formant_manager.cpp",
                    vitfx_path ++ "/wrapper.cpp",
                },
                .flags = cpp_flags,
            });
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/synthesis"));
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/synthesis/framework"));
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/synthesis/filters"));
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/synthesis/lookups"));
            vitfx.addIncludePath(b.path(vitfx_path ++ "/src/common"));
            vitfx.linkLibCpp();

            b.getInstallStep().dependOn(&b.addInstallArtifact(vitfx, .{ .dest_dir = install_subfolder }).step);
        }

        const libbacktrace = b.addStaticLibrary(.{
            .name = "backtrace",
            .target = target,
            .optimize = build_context.optimise,
            .strip = false,
        });
        if (true) {
            const posix_sources = .{
                "third_party_libs/libbacktrace/mmap.c",
                "third_party_libs/libbacktrace/mmapio.c",
            };

            libbacktrace.addCSourceFiles(.{
                .files = &.{
                    "third_party_libs/libbacktrace/backtrace.c",
                    "third_party_libs/libbacktrace/dwarf.c",
                    "third_party_libs/libbacktrace/fileline.c",
                    "third_party_libs/libbacktrace/print.c",
                    "third_party_libs/libbacktrace/read.c",
                    "third_party_libs/libbacktrace/simple.c",
                    "third_party_libs/libbacktrace/sort.c",
                    "third_party_libs/libbacktrace/state.c",
                    "third_party_libs/libbacktrace/posix.c",
                },
            });

            const backtrace_supported_header = b.addConfigHeader(
                .{
                    .include_path = "backtrace-supported.h",
                },
                .{
                    .BACKTRACE_SUPPORTED = 1,
                    .BACKTRACE_USES_MALLOC = @intFromBool(target.result.os.tag == .windows),
                    .BACKTRACE_SUPPORTS_THREADS = 1,
                    .BACKTRACE_SUPPORTS_DATA = 1,
                },
            );

            const config_header = b.addConfigHeader(
                .{
                    .include_path = "config.h",
                },
                .{
                    .BACKTRACE_ELF_SIZE = target.result.ptrBitWidth(),
                    .BACKTRACE_XCOFF_SIZE = target.result.ptrBitWidth(),
                    .HAVE_ATOMIC_FUNCTIONS = 1,
                    .HAVE_SYNC_FUNCTIONS = 1,
                    .HAVE_DECL_STRNLEN = 1,
                    .HAVE_DLFCN_H = 1,
                    .HAVE_DL_ITERATE_PHDR = 1,
                    .HAVE_INTTYPES_H = 1,
                    .HAVE_LINK_H = 1,
                    .HAVE_LSTAT = 1,
                    .HAVE_MEMORY_H = 1,
                    .HAVE_READLINK = 1,
                    .HAVE_STDINT_H = 1,
                    .HAVE_STDLIB_H = 1,
                    .HAVE_STRINGS_H = 1,
                    .HAVE_STRING_H = 1,
                    .HAVE_SYS_LDR_H = 1,
                    .HAVE_SYS_MMAN_H = 1,
                    .HAVE_SYS_STAT_H = 1,
                    .HAVE_SYS_TYPES_H = 1,
                    .HAVE_UNISTD_H = 1,
                    .STDC_HEADERS = 1,
                    ._FILE_OFFSET_BITS = 64,
                    ._LARGE_FILES = 1,
                },
            );

            switch (target.result.os.tag) {
                .windows => {
                    config_header.addValues(.{
                        .HAVE_DECL__PGMPTR = target.result.os.tag == .windows,
                        .HAVE_WINDOWS_H = 1,
                    });

                    libbacktrace.addCSourceFiles(.{ .files = &.{
                        "third_party_libs/libbacktrace/pecoff.c",
                        "third_party_libs/libbacktrace/alloc.c",
                    } });
                },
                .macos => {
                    config_header.addValues(.{
                        .HAVE_MACH_O_DYLD_H = 1,
                        .HAVE_FCNTL = 1,
                    });

                    libbacktrace.addCSourceFiles(.{ .files = &posix_sources });
                    libbacktrace.addCSourceFiles(.{ .files = &.{"third_party_libs/libbacktrace/macho.c"} });
                },
                .linux => {
                    config_header.addValues(.{
                        ._POSIX_SOURCE = 1,
                        ._GNU_SOURCE = 1,
                        .HAVE_CLOCK_GETTIME = target.result.os.tag == .linux,
                        .HAVE_DECL_GETPAGESIZE = target.result.os.tag == .linux,
                        .HAVE_FCNTL = 1,
                    });

                    libbacktrace.addCSourceFiles(.{ .files = &.{"third_party_libs/libbacktrace/elf.c"} });
                    libbacktrace.addCSourceFiles(.{ .files = &posix_sources });
                },
                else => {
                    unreachable;
                },
            }

            libbacktrace.linkLibC();
            libbacktrace.addConfigHeader(config_header);
            libbacktrace.addConfigHeader(backtrace_supported_header);
            applyUniversalSettings(&build_context, libbacktrace);
        }

        const pugl = b.addStaticLibrary(.{
            .name = "pugl",
            .target = target,
            .optimize = build_context.optimise,
        });
        {
            const pugl_path = "third_party_libs/pugl";

            pugl.addCSourceFiles(.{ .files = &.{
                pugl_path ++ "/src/common.c",
                pugl_path ++ "/src/internal.c",
                pugl_path ++ "/src/internal.c",
            } });

            switch (target.result.os.tag) {
                .windows => {
                    pugl.addCSourceFiles(.{ .files = &.{
                        pugl_path ++ "/src/win.c",
                        pugl_path ++ "/src/win_gl.c",
                        pugl_path ++ "/src/win_stub.c",
                    } });
                    pugl.linkSystemLibrary("opengl32");
                    pugl.linkSystemLibrary("gdi32");
                    pugl.linkSystemLibrary("dwmapi");
                },
                .macos => {
                    pugl.addCSourceFiles(.{ .files = &.{
                        pugl_path ++ "/src/mac.m",
                        pugl_path ++ "/src/mac_gl.m",
                        pugl_path ++ "/src/mac_stub.m",
                    }, .flags = &.{
                        b.fmt("-DPuglWindow=PuglWindowFPFloe{}{}{}", .{
                            floe_version.major,
                            floe_version.minor,
                            floe_version.patch,
                        }),
                        b.fmt("-DPuglWrapperView=PuglWrapperViewFPFloe{}{}{}", .{
                            floe_version.major,
                            floe_version.minor,
                            floe_version.patch,
                        }),
                        b.fmt("-DPuglOpenGLView=PuglOpenGLViewFPFloe{}{}{}", .{
                            floe_version.major,
                            floe_version.minor,
                            floe_version.patch,
                        }),
                    } });
                    pugl.linkFramework("OpenGL");
                    pugl.linkFramework("CoreVideo");
                },
                else => {
                    pugl.addCSourceFiles(.{ .files = &.{
                        pugl_path ++ "/src/x11.c",
                        pugl_path ++ "/src/x11_gl.c",
                        pugl_path ++ "/src/x11_stub.c",
                    } });
                    pugl.root_module.addCMacro("USE_XRANDR", "0");
                    pugl.root_module.addCMacro("USE_XSYNC", "1");
                    pugl.root_module.addCMacro("USE_XCURSOR", "1");
                    pugl.linkSystemLibrary2("gl", .{ .use_pkg_config = .force });
                    pugl.linkSystemLibrary2("glx", .{ .use_pkg_config = .force });
                    pugl.linkSystemLibrary2("x11", .{ .use_pkg_config = .force });
                    pugl.linkSystemLibrary2("xcursor", .{ .use_pkg_config = .force });
                    pugl.linkSystemLibrary2("xext", .{ .use_pkg_config = .force });
                },
            }

            pugl.root_module.addCMacro("PUGL_DISABLE_DEPRECATED", "1");
            pugl.root_module.addCMacro("PUGL_STATIC", "1");

            applyUniversalSettings(&build_context, pugl);
        }

        // TODO: does this need to be a library? is foundation/os/plugin all linked together?
        const library = b.addStaticLibrary(.{
            .name = "library",
            .target = target,
            .optimize = build_context.optimise,
        });
        {
            const library_path = "src";

            const generic_source_files = .{
                library_path ++ "/utils/debug/debug.cpp",
                library_path ++ "/utils/directory_listing/directory_listing.cpp",
                library_path ++ "/utils/leak_detecting_allocator.cpp",
                library_path ++ "/tests/framework.cpp",
                library_path ++ "/utils/logger/logger.cpp",
                library_path ++ "/foundation/utils/string.cpp",
                library_path ++ "/os/filesystem.cpp",
                library_path ++ "/os/misc.cpp",
                library_path ++ "/os/threading.cpp",
            };

            const unix_source_files = .{
                library_path ++ "/os/filesystem_unix.cpp",
                library_path ++ "/os/misc_unix.cpp",
                library_path ++ "/os/threading_pthread.cpp",
            };

            const windows_source_files = .{
                library_path ++ "/os/filesystem_windows.cpp",
                library_path ++ "/os/misc_windows.cpp",
                library_path ++ "/os/threading_windows.cpp",
            };

            const macos_source_files = .{
                library_path ++ "/os/filesystem_mac.mm",
                library_path ++ "/os/misc_mac.mm",
                library_path ++ "/os/threading_mac.cpp",
            };

            const linux_source_files = .{
                library_path ++ "/os/filesystem_linux.cpp",
                library_path ++ "/os/misc_linux.cpp",
                library_path ++ "/os/threading_linux.cpp",
            };

            switch (target.result.os.tag) {
                .windows => {
                    library.addCSourceFiles(.{ .files = &windows_source_files, .flags = cpp_fp_flags });
                    library.linkSystemLibrary("dbghelp");
                    library.linkSystemLibrary("shlwapi");
                    library.linkSystemLibrary("ole32");
                    library.linkSystemLibrary("crypt32");
                    library.linkSystemLibrary("uuid");

                    // synchronization.lib (https://github.com/ziglang/zig/issues/14919)
                    library.linkSystemLibrary("api-ms-win-core-synch-l1-2-0");
                },
                .macos => {
                    library.addCSourceFiles(.{ .files = &unix_source_files, .flags = cpp_fp_flags });
                    library.addCSourceFiles(.{ .files = &macos_source_files, .flags = objcpp_fp_flags });
                    library.linkFramework("Cocoa");
                    library.linkFramework("CoreFoundation");
                    library.linkFramework("AppKit");
                },
                .linux => {
                    library.addCSourceFiles(.{ .files = &unix_source_files, .flags = cpp_fp_flags });
                    library.addCSourceFiles(.{ .files = &linux_source_files, .flags = cpp_fp_flags });
                },
                else => {
                    unreachable;
                },
            }

            library.addCSourceFiles(.{ .files = &generic_source_files, .flags = cpp_fp_flags });
            library.addConfigHeader(build_config_step);
            library.linkLibC();
            library.linkLibrary(tracy);
            library.addObject(stb_sprintf);
            // library.linkLibCpp(); // needed for __cxa_demangle
            library.linkLibrary(libbacktrace);
            join_compile_commands.step.dependOn(&library.step);
            applyUniversalSettings(&build_context, library);
        }

        var stb_image = b.addObject(.{
            .name = "stb_image",
            .target = target,
            .optimize = build_context.optimise,
        });
        stb_image.addCSourceFile(.{ .file = b.path("third_party_libs/stb/stb_image_impls.c") });
        stb_image.linkLibC();

        var miniz = b.addObject(.{
            .name = "miniz",
            .target = target,
            .optimize = build_context.optimise,
        });
        miniz.addCSourceFile(.{ .file = b.path("third_party_libs/miniz/miniz.c") });
        miniz.linkLibC();

        var dr_wav = b.addObject(.{
            .name = "dr_wav",
            .target = target,
            .optimize = build_context.optimise,
        });
        dr_wav.addCSourceFile(.{ .file = b.path("third_party_libs/dr_wav/dr_wav_implementation.c") });
        dr_wav.linkLibC();

        const flac = b.addStaticLibrary(.{ .name = "flac", .target = target, .optimize = build_context.optimise });
        {
            const flac_path = "third_party_libs/flac/";
            const sources = &[_][]const u8{
                flac_path ++ "src/libFLAC/bitmath.c",
                flac_path ++ "src/libFLAC/bitreader.c",
                flac_path ++ "src/libFLAC/bitwriter.c",
                flac_path ++ "src/libFLAC/cpu.c",
                flac_path ++ "src/libFLAC/crc.c",
                flac_path ++ "src/libFLAC/fixed.c",
                flac_path ++ "src/libFLAC/fixed_intrin_sse2.c",
                flac_path ++ "src/libFLAC/fixed_intrin_ssse3.c",
                flac_path ++ "src/libFLAC/fixed_intrin_sse42.c",
                flac_path ++ "src/libFLAC/fixed_intrin_avx2.c",
                flac_path ++ "src/libFLAC/float.c",
                flac_path ++ "src/libFLAC/format.c",
                flac_path ++ "src/libFLAC/lpc.c",
                flac_path ++ "src/libFLAC/lpc_intrin_neon.c",
                flac_path ++ "src/libFLAC/lpc_intrin_sse2.c",
                flac_path ++ "src/libFLAC/lpc_intrin_sse41.c",
                flac_path ++ "src/libFLAC/lpc_intrin_avx2.c",
                flac_path ++ "src/libFLAC/lpc_intrin_fma.c",
                flac_path ++ "src/libFLAC/md5.c",
                flac_path ++ "src/libFLAC/memory.c",
                flac_path ++ "src/libFLAC/metadata_iterators.c",
                flac_path ++ "src/libFLAC/metadata_object.c",
                flac_path ++ "src/libFLAC/stream_decoder.c",
                flac_path ++ "src/libFLAC/stream_encoder.c",
                flac_path ++ "src/libFLAC/stream_encoder_intrin_sse2.c",
                flac_path ++ "src/libFLAC/stream_encoder_intrin_ssse3.c",
                flac_path ++ "src/libFLAC/stream_encoder_intrin_avx2.c",
                flac_path ++ "src/libFLAC/stream_encoder_framing.c",
                flac_path ++ "src/libFLAC/window.c",
            };

            const sources_windows = &[_][]const u8{
                flac_path ++ "src/share/win_utf8_io/win_utf8_io.c",
            };

            const config_header = b.addConfigHeader(
                .{
                    .style = .{ .cmake = b.path(flac_path ++ "config.cmake.h.in") },
                    .include_path = "config.h",
                },
                .{
                    .CPU_IS_BIG_ENDIAN = target.result.cpu.arch.endian() == .big,
                    .ENABLE_64_BIT_WORDS = target.result.ptrBitWidth() == 64,
                    .FLAC__ALIGN_MALLOC_DATA = target.result.cpu.arch.isX86(),
                    .FLAC__CPU_ARM64 = target.result.cpu.arch.isAARCH64(),
                    .FLAC__SYS_DARWIN = target.result.isDarwin(),
                    .FLAC__SYS_LINUX = target.result.os.tag == .linux,
                    .HAVE_BYTESWAP_H = target.result.os.tag == .linux,
                    .HAVE_CPUID_H = target.result.cpu.arch.isX86(),
                    .HAVE_FSEEKO = true,
                    .HAVE_ICONV = target.result.os.tag != .windows,
                    .HAVE_INTTYPES_H = true,
                    .HAVE_MEMORY_H = true,
                    .HAVE_STDINT_H = true,
                    .HAVE_STRING_H = true,
                    .HAVE_STDLIB_H = true,
                    .HAVE_TYPEOF = true,
                    .HAVE_UNISTD_H = true,
                },
            );

            flac.linkLibC();
            flac.defineCMacro("HAVE_CONFIG_H", null);
            flac.addConfigHeader(config_header);
            flac.addIncludePath(b.path(flac_path ++ "include"));
            flac.addIncludePath(b.path(flac_path ++ "src/libFLAC/include"));
            flac.addCSourceFiles(.{ .files = sources, .flags = &.{} });
            if (target.result.os.tag == .windows) {
                flac.defineCMacro("FLAC__NO_DLL", null);
                flac.addCSourceFiles(.{ .files = sources_windows, .flags = &.{} });
            }
        }

        const fft_convolver = b.addStaticLibrary(.{
            .name = "fftconvolver",
            .target = target,
            .optimize = build_context.optimise,
        });
        {
            var fft_flags: []const []const u8 = &.{};
            if (target.result.os.tag == .macos) {
                fft_convolver.linkFramework("Accelerate");
                fft_flags = &.{ "-DAUDIOFFT_APPLE_ACCELERATE", "-ObjC++" };
            } else {
                fft_convolver.addCSourceFiles(.{ .files = &.{"third_party_libs/pffft/pffft.c"}, .flags = &.{} });
                fft_flags = &.{"-DAUDIOFFT_PFFFT"};
            }

            fft_convolver.addCSourceFiles(.{ .files = &.{
                "third_party_libs/FFTConvolver/AudioFFT.cpp",
                "third_party_libs/FFTConvolver/FFTConvolver.cpp",
                "third_party_libs/FFTConvolver/TwoStageFFTConvolver.cpp",
                "third_party_libs/FFTConvolver/Utilities.cpp",
                "third_party_libs/FFTConvolver/wrapper.cpp",
            }, .flags = fft_flags });
            fft_convolver.linkLibCpp();
            applyUniversalSettings(&build_context, fft_convolver);
        }

        const plugin = b.addStaticLibrary(.{
            .name = "plugin",
            .target = target,
            .optimize = build_context.optimise,
        });
        {
            const plugin_path = "src/plugin";

            plugin.addCSourceFiles(.{
                .files = &(.{
                    plugin_path ++ "/common/common_errors.cpp",
                    plugin_path ++ "/cross_instance_systems.cpp",
                    plugin_path ++ "/layer_processor.cpp",
                    plugin_path ++ "/param_info.cpp",
                    plugin_path ++ "/plugin_clap.cpp",
                    plugin_path ++ "/plugin_instance.cpp",
                    plugin_path ++ "/presets_folder.cpp",
                    plugin_path ++ "/processing/audio_utils.cpp",
                    plugin_path ++ "/processing/midi.cpp",
                    plugin_path ++ "/processing/volume_fade.cpp",
                    plugin_path ++ "/processor.cpp",
                    plugin_path ++ "/sample_library_loader.cpp",
                    plugin_path ++ "/scanned_folder.cpp",
                    plugin_path ++ "/voices.cpp",
                    plugin_path ++ "/settings/settings_file.cpp",

                    plugin_path ++ "/sample_library/audio_file.cpp",
                    plugin_path ++ "/sample_library/sample_library_lua.cpp",
                    plugin_path ++ "/sample_library/sample_library_mdata.cpp",
                    plugin_path ++ "/state/state_coding.cpp",
                } ++ if (floe_plugin_gui) .{
                    plugin_path ++ "/gui/framework/draw_list.cpp",
                    plugin_path ++ "/gui/framework/gui_imgui.cpp",
                    plugin_path ++ "/gui/framework/gui_platform.cpp",
                    plugin_path ++ "/gui/gui.cpp",
                    plugin_path ++ "/gui/gui_bot_panel.cpp",
                    plugin_path ++ "/gui/gui_button_widgets.cpp",
                    plugin_path ++ "/gui/gui_dragger_widgets.cpp",
                    plugin_path ++ "/gui/gui_drawing_helpers.cpp",
                    plugin_path ++ "/gui/gui_editor_widgets.cpp",
                    plugin_path ++ "/gui/gui_effects.cpp",
                    plugin_path ++ "/gui/gui_envelope.cpp",
                    plugin_path ++ "/gui/gui_keyboard.cpp",
                    plugin_path ++ "/gui/gui_knob_widgets.cpp",
                    plugin_path ++ "/gui/gui_label_widgets.cpp",
                    plugin_path ++ "/gui/gui_layer.cpp",
                    plugin_path ++ "/gui/gui_mid_panel.cpp",
                    plugin_path ++ "/gui/gui_peak_meter_widget.cpp",
                    plugin_path ++ "/gui/gui_preset_browser.cpp",
                    plugin_path ++ "/gui/gui_standalone_popups.cpp",
                    plugin_path ++ "/gui/gui_top_panel.cpp",
                    plugin_path ++ "/gui/gui_velocity_buttons.cpp",
                    plugin_path ++ "/gui/gui_waveform.cpp",
                    plugin_path ++ "/gui/gui_widget_compounds.cpp",
                    plugin_path ++ "/gui/gui_widget_helpers.cpp",
                    plugin_path ++ "/gui/gui_window.cpp",
                    plugin_path ++ "/gui/framework/draw_list_opengl.cpp",
                    plugin_path ++ "/gui/framework/gui_platform_pugl.cpp",
                } else .{}),
                .flags = cpp_fp_flags,
            });

            const licences_header = b.addConfigHeader(.{
                .include_path = "licence_texts.h",
                .style = .blank,
            }, .{
                .GPL_3_LICENSE = getLicenceText(b, "GPL-3.0-or-later.txt") catch @panic("missing license text"),
                .APACHE_2_0_LICENSE = getLicenceText(b, "Apache-2.0.txt") catch @panic("missing license text"),
                .FFTPACK_LICENSE = getLicenceText(b, "LicenseRef-FFTPACK.txt") catch @panic("missing license text"),
                .OFL_1_1_LICENSE = getLicenceText(b, "OFL-1.1.txt") catch @panic("missing license text"),
                .BSD_3_CLAUSE_LICENSE = getLicenceText(b, "BSD-3-Clause.txt") catch @panic("missing license text"),
                .BSD_2_CLAUSE_LICENSE = getLicenceText(b, "BSD-2-Clause.txt") catch @panic("missing license text"),
                .ISC_LICENSE = getLicenceText(b, "ISC.txt") catch @panic("missing license text"),
                .MIT_LICENSE = getLicenceText(b, "MIT.txt") catch @panic("missing license text"),
            });
            plugin.addConfigHeader(licences_header);

            plugin.addIncludePath(b.path("src/plugin"));
            plugin.addIncludePath(b.path("src"));
            plugin.addConfigHeader(build_config_step);
            plugin.linkLibrary(library);
            plugin.linkLibrary(fft_convolver);
            const embedded_files = b.addObject(.{
                .name = "embedded_files",
                .root_source_file = b.path("build_resources/embedded_files.zig"),
                .target = target,
                .optimize = build_context.optimise,
                .pic = true,
            });
            embedded_files.linkLibC();
            embedded_files.addIncludePath(b.path("build_resources"));
            plugin.addObject(embedded_files);
            plugin.linkLibrary(tracy);
            if (floe_plugin_gui) {
                plugin.linkLibrary(pugl);
                plugin.addObject(stb_image);
                plugin.addIncludePath(b.path("src/plugin/gui/live_edit_defs"));
            }
            plugin.linkLibrary(flac);
            plugin.addObject(dr_wav);
            plugin.addObject(xxhash);
            plugin.linkLibrary(buildLua(b, target, build_context.optimise));
            plugin.linkLibrary(vitfx);
            applyUniversalSettings(&build_context, plugin);
            join_compile_commands.step.dependOn(&plugin.step);
        }

        if (build_context.build_mode != .production) {
            var gen_docs = b.addExecutable(.{ .name = "gen_docs", .target = target, .optimize = build_context.optimise });
            const gen_docs_path = "src/gen_param_docs_html";
            gen_docs.addCSourceFiles(.{ .files = &.{
                gen_docs_path ++ "/gen_param_docs_html_main.cpp",
            }, .flags = cpp_fp_flags });
            gen_docs.linkLibrary(plugin);
            gen_docs.addIncludePath(b.path("src"));
            gen_docs.addIncludePath(b.path("src/plugin"));
            gen_docs.addConfigHeader(build_config_step);
            join_compile_commands.step.dependOn(&gen_docs.step);
            applyUniversalSettings(&build_context, gen_docs);
            b.getInstallStep().dependOn(&b.addInstallArtifact(gen_docs, .{ .dest_dir = install_subfolder }).step);

            var run_gen_docs = b.addRunArtifact(gen_docs);
            run_gen_docs.addArgs(&.{"zig-out/parameters.html"});
            gen_docs_step.dependOn(&run_gen_docs.step);
        }

        var clap_post_install_step = b.allocator.create(PostInstallStep) catch @panic("OOM");
        {
            const clap = b.addSharedLibrary(.{
                .name = "Floe.clap",
                .target = target,
                .optimize = build_context.optimise,
                .version = floe_version,
            });
            clap.addCSourceFiles(.{ .files = &.{"src/plugin/plugin_clap_entry.cpp"}, .flags = cpp_fp_flags });
            clap.addConfigHeader(build_config_step);
            clap.addIncludePath(b.path("src"));
            clap.linkLibrary(plugin);
            const clap_install_artifact_step = b.addInstallArtifact(clap, .{ .dest_dir = install_subfolder });
            b.getInstallStep().dependOn(&clap_install_artifact_step.step);
            applyUniversalSettings(&build_context, clap);
            addWin32EmbedInfo(clap, .{
                .name = "Floe CLAP",
                .description = floe_description,
                .icon_path = null,
            }) catch @panic("OOM");
            join_compile_commands.step.dependOn(&clap.step);
            addToLipoSteps(&build_context, clap, true) catch @panic("OOM");

            clap_post_install_step.* = PostInstallStep{
                .step = std.Build.Step.init(.{
                    .id = std.Build.Step.Id.custom,
                    .name = "Post install config",
                    .owner = b,
                    .makeFn = performPostInstallConfig,
                }),
                .make_macos_bundle = true,
                .context = &build_context,
                .compile_step = clap,
            };
            clap_post_install_step.step.dependOn(&clap.step);
            clap_post_install_step.step.dependOn(b.getInstallStep());

            build_context.master_step.dependOn(&clap_post_install_step.step);
        }

        const miniaudio = b.addStaticLibrary(.{ .name = "miniaudio", .target = target, .optimize = build_context.optimise });
        {
            // disabling pulse audio because it was causing lots of stutters on my machine
            miniaudio.addCSourceFiles(.{
                .files = &.{"third_party_libs/miniaudio/miniaudio.c"},
                .flags = genericFlags(&build_context, target, &.{"-DMA_NO_PULSEAUDIO"}) catch @panic("OOM"),
            });
            miniaudio.linkLibC();
            switch (target.result.os.tag) {
                .macos => {
                    applyUniversalSettings(&build_context, miniaudio);
                    miniaudio.linkFramework("CoreAudio");
                },
                .windows => {
                    miniaudio.linkSystemLibrary("dsound");
                },
                .linux => {
                    miniaudio.linkSystemLibrary2("alsa", .{ .use_pkg_config = .force });
                },
                else => {
                    unreachable;
                },
            }
        }

        // standalone is for development-only at the moment
        if (build_context.build_mode != .production and floe_plugin_gui) {
            const portmidi = b.addStaticLibrary(.{
                .name = "portmidi",
                .target = target,
                .optimize = build_context.optimise,
                .version = floe_version,
            });
            {
                portmidi.addCSourceFiles(.{ .files = &.{
                    "third_party_libs/portmidi/pm_common/portmidi.c",
                    "third_party_libs/portmidi/pm_common/pmutil.c",
                    "third_party_libs/portmidi/porttime/porttime.c",
                }, .flags = generic_flags });
                switch (target.result.os.tag) {
                    .macos => {
                        portmidi.addCSourceFiles(.{ .files = &.{
                            "third_party_libs/portmidi/pm_mac/pmmacosxcm.c",
                            "third_party_libs/portmidi/pm_mac/pmmac.c",
                            "third_party_libs/portmidi/porttime/ptmacosx_cf.c",
                            "third_party_libs/portmidi/porttime/ptmacosx_mach.c",
                        }, .flags = generic_flags });
                        portmidi.linkFramework("CoreAudio");
                        portmidi.linkFramework("CoreMIDI");
                        applyUniversalSettings(&build_context, portmidi);
                    },
                    .windows => {
                        portmidi.addCSourceFiles(.{ .files = &.{
                            "third_party_libs/portmidi/pm_win/pmwin.c",
                            "third_party_libs/portmidi/pm_win/pmwinmm.c",
                            "third_party_libs/portmidi/porttime/ptwinmm.c",
                        }, .flags = generic_flags });
                        portmidi.linkSystemLibrary("winmm");
                    },
                    .linux => {
                        portmidi.addCSourceFiles(.{ .files = &.{
                            "third_party_libs/portmidi/pm_linux/pmlinux.c",
                            "third_party_libs/portmidi/pm_linux/pmlinuxalsa.c",
                            "third_party_libs/portmidi/porttime/ptlinux.c",
                        }, .flags = genericFlags(&build_context, target, &.{"-DPMALSA"}) catch @panic("OOM") });
                        portmidi.linkSystemLibrary2("alsa", .{ .use_pkg_config = .force });
                    },
                    else => {
                        unreachable;
                    },
                }

                portmidi.linkLibC();
                portmidi.addIncludePath(b.path("third_party_libs/portmidi/porttime"));
                portmidi.addIncludePath(b.path("third_party_libs/portmidi/pm_common"));
            }

            const floe_standalone = b.addExecutable(.{
                .name = "floe_standalone",
                .target = target,
                .optimize = build_context.optimise,
                .version = floe_version,
            });

            const standalone_path = "src/standalone_wrapper";
            floe_standalone.addCSourceFiles(.{
                .files = &.{
                    standalone_path ++ "/standalone_wrapper.cpp",
                },
                .flags = cpp_fp_flags,
            });

            floe_standalone.addConfigHeader(build_config_step);
            floe_standalone.addIncludePath(b.path("src"));
            floe_standalone.linkLibrary(portmidi);
            floe_standalone.linkLibrary(miniaudio);
            floe_standalone.linkLibrary(plugin);
            b.getInstallStep().dependOn(&b.addInstallArtifact(floe_standalone, .{ .dest_dir = install_subfolder }).step);
            join_compile_commands.step.dependOn(&floe_standalone.step);
            applyUniversalSettings(&build_context, floe_standalone);
        }

        const vst3_sdk = b.addStaticLibrary(.{
            .name = "VST3",
            .target = target,
            .optimize = build_context.optimise,
        });
        const vst3_validator = b.addExecutable(.{
            .name = "VST3-Validator",
            .target = target,
            .optimize = build_context.optimise,
        });
        const vst3_path = "third_party_libs/vst3";
        {
            var extra_flags = std.ArrayList([]const u8).init(b.allocator);
            defer extra_flags.deinit();
            if (build_context.optimise == .Debug) {
                extra_flags.append("-DDEVELOPMENT=1") catch unreachable;
            } else {
                extra_flags.append("-DRELEASE=1") catch unreachable;
            }
            const flags = cppFlags(b, generic_flags, extra_flags.items) catch unreachable;

            {
                vst3_sdk.addCSourceFiles(.{ .files = &.{
                    vst3_path ++ "/base/source/baseiids.cpp",
                    vst3_path ++ "/base/source/fbuffer.cpp",
                    vst3_path ++ "/base/source/fdebug.cpp",
                    vst3_path ++ "/base/source/fdynlib.cpp",
                    vst3_path ++ "/base/source/fobject.cpp",
                    vst3_path ++ "/base/source/fstreamer.cpp",
                    vst3_path ++ "/base/source/fstring.cpp",
                    vst3_path ++ "/base/source/timer.cpp",
                    vst3_path ++ "/base/source/updatehandler.cpp",

                    vst3_path ++ "/base/thread/source/fcondition.cpp",
                    vst3_path ++ "/base/thread/source/flock.cpp",

                    vst3_path ++ "/public.sdk/source/common/commoniids.cpp",
                    vst3_path ++ "/public.sdk/source/common/memorystream.cpp",
                    vst3_path ++ "/public.sdk/source/common/openurl.cpp",
                    vst3_path ++ "/public.sdk/source/common/pluginview.cpp",
                    vst3_path ++ "/public.sdk/source/common/readfile.cpp",
                    vst3_path ++ "/public.sdk/source/common/systemclipboard_linux.cpp",
                    vst3_path ++ "/public.sdk/source/common/systemclipboard_mac.mm",
                    vst3_path ++ "/public.sdk/source/common/systemclipboard_win32.cpp",
                    vst3_path ++ "/public.sdk/source/common/threadchecker_linux.cpp",
                    vst3_path ++ "/public.sdk/source/common/threadchecker_mac.mm",
                    vst3_path ++ "/public.sdk/source/common/threadchecker_win32.cpp",

                    vst3_path ++ "/pluginterfaces/base/conststringtable.cpp",
                    vst3_path ++ "/pluginterfaces/base/coreiids.cpp",
                    vst3_path ++ "/pluginterfaces/base/funknown.cpp",
                    vst3_path ++ "/pluginterfaces/base/ustring.cpp",

                    vst3_path ++ "/public.sdk/source/main/pluginfactory.cpp",
                    vst3_path ++ "/public.sdk/source/main/moduleinit.cpp",
                    vst3_path ++ "/public.sdk/source/vst/vstinitiids.cpp",
                    vst3_path ++ "/public.sdk/source/vst/vstnoteexpressiontypes.cpp",
                    vst3_path ++ "/public.sdk/source/vst/vstsinglecomponenteffect.cpp",
                    vst3_path ++ "/public.sdk/source/vst/vstaudioeffect.cpp",
                    vst3_path ++ "/public.sdk/source/vst/vstcomponent.cpp",
                    vst3_path ++ "/public.sdk/source/vst/vstsinglecomponenteffect.cpp",
                    vst3_path ++ "/public.sdk/source/vst/vstcomponentbase.cpp",
                    vst3_path ++ "/public.sdk/source/vst/vstbus.cpp",
                    vst3_path ++ "/public.sdk/source/vst/vstparameters.cpp",
                    vst3_path ++ "/public.sdk/source/vst/utility/stringconvert.cpp",
                }, .flags = flags });

                switch (target.result.os.tag) {
                    .windows => {},
                    .linux => {},
                    .macos => {
                        vst3_sdk.linkFramework("CoreFoundation");
                        vst3_sdk.linkFramework("Foundation");
                    },
                    else => {},
                }

                vst3_sdk.addIncludePath(b.path(vst3_path));
                vst3_sdk.linkLibCpp();
                applyUniversalSettings(&build_context, vst3_sdk);
            }

            {
                vst3_validator.addCSourceFiles(.{ .files = &.{
                    vst3_path ++ "/public.sdk/source/common/memorystream.cpp",
                    vst3_path ++ "/public.sdk/source/main/moduleinit.cpp",
                    vst3_path ++ "/public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/test/connectionproxytest.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/test/eventlisttest.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/test/hostclassestest.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/test/parameterchangestest.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/test/pluginterfacesupporttest.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/test/processdatatest.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/plugprovider.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/bus/busactivation.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/bus/busconsistency.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/bus/businvalidindex.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/bus/checkaudiobusarrangement.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/bus/scanbusses.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/bus/sidechainarrangement.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/general/editorclasses.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/general/midilearn.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/general/midimapping.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/general/plugcompat.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/general/scanparameters.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/general/suspendresume.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/general/terminit.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/noteexpression/keyswitch.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/noteexpression/noteexpression.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/processing/automation.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/processing/process.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/processing/processcontextrequirements.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/processing/processformat.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/processing/processinputoverwriting.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/processing/processtail.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/processing/processthreaded.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/processing/silenceflags.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/processing/silenceprocessing.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/processing/speakerarrangement.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/processing/variableblocksize.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/state/bypasspersistence.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/state/invalidstatetransition.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/state/repeatidenticalstatetransition.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/state/validstatetransition.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/testbase.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/unit/checkunitstructure.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/unit/scanprograms.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/unit/scanunits.cpp",
                    vst3_path ++ "/public.sdk/source/vst/testsuite/vsttestsuite.cpp",
                    vst3_path ++ "/public.sdk/source/vst/utility/testing.cpp",
                    vst3_path ++ "/public.sdk/samples/vst-hosting/validator/source/main.cpp",
                    vst3_path ++ "/public.sdk/samples/vst-hosting/validator/source/usediids.cpp",
                    vst3_path ++ "/public.sdk/samples/vst-hosting/validator/source/validator.cpp",

                    vst3_path ++ "/public.sdk/source/vst/hosting/connectionproxy.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/eventlist.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/hostclasses.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/module.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/parameterchanges.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/pluginterfacesupport.cpp",
                    vst3_path ++ "/public.sdk/source/vst/hosting/processdata.cpp",
                    vst3_path ++ "/public.sdk/source/vst/vstpresetfile.cpp",
                }, .flags = flags });

                switch (target.result.os.tag) {
                    .windows => {
                        vst3_validator.addCSourceFiles(.{ .files = &.{
                            vst3_path ++ "/public.sdk/source/vst/hosting/module_win32.cpp",
                        }, .flags = flags });
                        vst3_validator.linkSystemLibrary("ole32");
                    },
                    .linux => {
                        vst3_validator.addCSourceFiles(.{ .files = &.{
                            vst3_path ++ "/public.sdk/source/vst/hosting/module_linux.cpp",
                        }, .flags = flags });
                    },
                    .macos => {
                        vst3_validator.addCSourceFiles(.{ .files = &.{
                            vst3_path ++ "/public.sdk/source/vst/hosting/module_mac.mm",
                        }, .flags = objcpp_flags });
                    },
                    else => {},
                }

                vst3_validator.addIncludePath(b.path(vst3_path));
                vst3_validator.linkLibCpp();
                vst3_validator.linkLibrary(vst3_sdk);
                vst3_validator.linkLibrary(library); // for ubsan runtime
                applyUniversalSettings(&build_context, vst3_validator);
                b.getInstallStep().dependOn(&b.addInstallArtifact(vst3_validator, .{ .dest_dir = install_subfolder }).step);
                addToLipoSteps(&build_context, vst3_validator, false) catch @panic("OOM");

                // const run_tests = b.addRunArtifact(vst3_validator);
                // run_tests.addArg(b.pathJoin(&.{ install_dir, install_subfolder_string, "Floe.vst3" }));
                // build_context.test_step.dependOn(&run_tests.step);
            }
        }

        var vst3_post_install_step = b.allocator.create(PostInstallStep) catch @panic("OOM");
        {
            const vst3 = b.addSharedLibrary(.{
                .name = "Floe.vst3",
                .target = target,
                .optimize = build_context.optimise,
                .version = floe_version,
                .pic = true,
            });
            var extra_flags = std.ArrayList([]const u8).init(b.allocator);
            defer extra_flags.deinit();
            switch (target.result.os.tag) {
                .windows => {
                    extra_flags.append("-DWIN=1") catch unreachable;
                },
                .linux => {
                    extra_flags.append("-DLIN=1") catch unreachable;
                },
                .macos => {
                    extra_flags.append("-DMAC=1") catch unreachable;
                },
                else => {},
            }
            if (build_context.optimise == .Debug) {
                extra_flags.append("-DDEVELOPMENT=1") catch unreachable;
            } else {
                extra_flags.append("-DRELEASE=1") catch unreachable;
            }
            extra_flags.append("-fno-char8_t") catch unreachable;
            extra_flags.append("-DMACOS_USE_STD_FILESYSTEM=1") catch unreachable;
            extra_flags.append("-DCLAP_WRAPPER_VERSION=\"0.9.1\"") catch unreachable;
            const flags = cppFlags(b, generic_flags, extra_flags.items) catch unreachable;

            vst3.addCSourceFiles(.{ .files = &.{
                "src/plugin/plugin_clap_entry.cpp",
            }, .flags = cpp_fp_flags });

            const wrapper_path = "third_party_libs/clap-wrapper";
            vst3.addCSourceFiles(.{ .files = &.{
                wrapper_path ++ "/src/wrapasvst3.cpp",
                wrapper_path ++ "/src/wrapasvst3_entry.cpp",
                wrapper_path ++ "/src/wrapasvst3_export_entry.cpp",
                wrapper_path ++ "/src/detail/vst3/parameter.cpp",
                wrapper_path ++ "/src/detail/vst3/plugview.cpp",
                wrapper_path ++ "/src/detail/vst3/process.cpp",
                wrapper_path ++ "/src/detail/vst3/categories.cpp",
                wrapper_path ++ "/src/clap_proxy.cpp",
                wrapper_path ++ "/src/detail/shared/sha1.cpp",
                wrapper_path ++ "/src/detail/clap/fsutil.cpp",
            }, .flags = flags });

            switch (target.result.os.tag) {
                .windows => {
                    vst3.addCSourceFiles(.{ .files = &.{
                        wrapper_path ++ "/src/detail/os/windows.cpp",
                        vst3_path ++ "/public.sdk/source/main/dllmain.cpp",
                    }, .flags = flags });
                },
                .linux => {
                    vst3.addCSourceFiles(.{ .files = &.{
                        wrapper_path ++ "/src/detail/os/linux.cpp",
                        vst3_path ++ "/public.sdk/source/main/linuxmain.cpp",
                    }, .flags = flags });
                },
                .macos => {
                    vst3.addCSourceFiles(.{ .files = &.{
                        wrapper_path ++ "/src/detail/os/macos.mm",
                        wrapper_path ++ "/src/detail/clap/mac_helpers.mm",
                        vst3_path ++ "/public.sdk/source/main/macmain.cpp",
                    }, .flags = flags });
                },
                else => {},
            }

            vst3.addIncludePath(b.path("third_party_libs/clap/include"));
            vst3.addIncludePath(b.path("third_party_libs/clap-wrapper/include"));
            vst3.addIncludePath(b.path("third_party_libs/vst3"));
            vst3.addIncludePath(b.path("third_party_libs/clap-wrapper/libs/fmt"));
            vst3.linkLibCpp();

            vst3.linkLibrary(plugin);
            vst3.linkLibrary(vst3_sdk);

            vst3.addConfigHeader(build_config_step);
            vst3.addIncludePath(b.path("src"));

            const vst3_install_artifact_step = b.addInstallArtifact(vst3, .{ .dest_dir = install_subfolder });
            b.getInstallStep().dependOn(&vst3_install_artifact_step.step);
            applyUniversalSettings(&build_context, vst3);
            addWin32EmbedInfo(vst3, .{
                .name = "Floe VST3",
                .description = floe_description,
                .icon_path = null,
            }) catch @panic("OOM");
            addToLipoSteps(&build_context, vst3, true) catch @panic("OOM");

            vst3_post_install_step.* = PostInstallStep{
                .step = std.Build.Step.init(.{
                    .id = std.Build.Step.Id.custom,
                    .name = "Post install config",
                    .owner = b,
                    .makeFn = performPostInstallConfig,
                }),
                .make_macos_bundle = true,
                .context = &build_context,
                .compile_step = vst3,
            };
            vst3_post_install_step.step.dependOn(b.getInstallStep());
            build_context.master_step.dependOn(&vst3_post_install_step.step);
        }

        if (target.result.os.tag == .macos) {
            const au_sdk = b.addStaticLibrary(.{
                .name = "AU",
                .target = target,
                .optimize = build_context.optimise,
            });
            {
                const au_path = "third_party_libs/AudioUnitSDK";
                au_sdk.addCSourceFiles(.{
                    .files = &.{
                        au_path ++ "/src/AudioUnitSDK/AUBuffer.cpp",
                        au_path ++ "/src/AudioUnitSDK/AUBufferAllocator.cpp",
                        au_path ++ "/src/AudioUnitSDK/AUEffectBase.cpp",
                        au_path ++ "/src/AudioUnitSDK/AUInputElement.cpp",
                        au_path ++ "/src/AudioUnitSDK/AUMIDIBase.cpp",
                        au_path ++ "/src/AudioUnitSDK/AUBase.cpp",
                        au_path ++ "/src/AudioUnitSDK/AUMIDIEffectBase.cpp",
                        au_path ++ "/src/AudioUnitSDK/AUOutputElement.cpp",
                        au_path ++ "/src/AudioUnitSDK/AUPlugInDispatch.cpp",
                        au_path ++ "/src/AudioUnitSDK/AUScopeElement.cpp",
                        au_path ++ "/src/AudioUnitSDK/ComponentBase.cpp",
                        au_path ++ "/src/AudioUnitSDK/MusicDeviceBase.cpp",
                    },
                    .flags = cpp_flags,
                });
                au_sdk.addIncludePath(b.path(au_path ++ "/include"));
                au_sdk.linkLibCpp();
                applyUniversalSettings(&build_context, au_sdk);
            }

            {
                const au = b.addSharedLibrary(.{
                    .name = "Floe.component",
                    .target = target,
                    .optimize = build_context.optimise,
                    .version = floe_version,
                    .pic = true,
                });
                var extra_flags = std.ArrayList([]const u8).init(b.allocator);
                switch (target.result.os.tag) {
                    .windows => {
                        extra_flags.append("-DWIN=1") catch unreachable;
                    },
                    .linux => {
                        extra_flags.append("-DLIN=1") catch unreachable;
                    },
                    .macos => {
                        extra_flags.append("-DMAC=1") catch unreachable;
                    },
                    else => {},
                }
                if (build_context.optimise == .Debug) {
                    extra_flags.append("-DDEVELOPMENT=1") catch unreachable;
                } else {
                    extra_flags.append("-DRELEASE=1") catch unreachable;
                }
                extra_flags.append("-fno-char8_t") catch unreachable;
                extra_flags.append("-DMACOS_USE_STD_FILESYSTEM=1") catch unreachable;
                extra_flags.append("-DCLAP_WRAPPER_VERSION=\"0.9.1\"") catch unreachable;
                extra_flags.append("-DSTATICALLY_LINK_INTERNAL_CLAP_ENTRY=1") catch unreachable;
                const flags = cppFlags(b, generic_flags, extra_flags.items) catch unreachable;

                au.addCSourceFiles(.{ .files = &.{
                    "src/plugin/plugin_clap_entry.cpp",
                }, .flags = cpp_fp_flags });

                const wrapper_path = "third_party_libs/clap-wrapper";
                au.addCSourceFiles(.{ .files = &.{
                    wrapper_path ++ "/src/clap_proxy.cpp",
                    wrapper_path ++ "/src/detail/shared/sha1.cpp",
                    wrapper_path ++ "/src/detail/clap/fsutil.cpp",
                    wrapper_path ++ "/src/detail/os/macos.mm",
                    wrapper_path ++ "/src/detail/clap/mac_helpers.mm",
                    wrapper_path ++ "/src/wrapasauv2.cpp",
                    wrapper_path ++ "/src/detail/auv2/process.cpp",
                    wrapper_path ++ "/src/detail/auv2/wrappedview.mm",
                    wrapper_path ++ "/src/detail/auv2/parameter.cpp",
                }, .flags = flags });

                {
                    const file = std.fs.createFileAbsolute(b.pathJoin(&.{ rootdir, build_gen_relative, "generated_entrypoints.hxx" }), .{ .truncate = true }) catch @panic("could not create file");
                    defer file.close();
                    file.writeAll(b.fmt(
                        \\ #pragma once
                        \\ #include "detail/auv2/auv2_base_classes.h"
                        \\
                        \\ struct {[factory_function]s} : free_audio::auv2_wrapper::WrapAsAUV2 {{
                        \\     {[factory_function]s}(AudioComponentInstance ci) : 
                        \\         free_audio::auv2_wrapper::WrapAsAUV2(AUV2_Type::aumu_musicdevice, "Floe", "", 0, ci) {{}}
                        \\ }};
                        \\ AUSDK_COMPONENT_ENTRY(ausdk::AUMusicDeviceFactory, {[factory_function]s});
                    , .{
                        .factory_function = floe_au_factory_function,
                    })) catch @panic("could not write to file");
                }

                {
                    const file = std.fs.createFileAbsolute(b.pathJoin(&.{ rootdir, build_gen_relative, "generated_cocoaclasses.hxx" }), .{ .truncate = true }) catch @panic("could not create file");
                    defer file.close();
                    // TODO: add version to "floeinst" string for better objc name collision prevention
                    file.writeAll(b.fmt(
                        \\ #pragma once
                        \\
                        \\ #define CLAP_WRAPPER_COCOA_CLASS_NSVIEW {[name]s}_nsview
                        \\ #define CLAP_WRAPPER_COCOA_CLASS {[name]s}
                        \\ #define CLAP_WRAPPER_TIMER_CALLBACK timerCallback_{[name]s}
                        \\ #define CLAP_WRAPPER_FILL_AUCV fillAUCV_{[name]s}
                        \\ #include "detail/auv2/wrappedview.asinclude.mm"
                        \\ #undef CLAP_WRAPPER_COCOA_CLASS_NSVIEW
                        \\ #undef CLAP_WRAPPER_COCOA_CLASS
                        \\ #undef CLAP_WRAPPER_TIMER_CALLBACK
                        \\ #undef CLAP_WRAPPER_FILL_AUCV
                        \\
                        \\ bool fillAudioUnitCocoaView(AudioUnitCocoaViewInfo* viewInfo, std::shared_ptr<Clap::Plugin> _plugin) {{
                        \\     return fillAUCV_{[name]s}(viewInfo);
                        \\ }}
                    , .{
                        .name = b.fmt("Floe{d}{d}{d}", .{ floe_version.major, floe_version.minor, floe_version.patch }),
                    })) catch @panic("could not write to file");
                }

                au.addIncludePath(b.path("third_party_libs/clap/include"));
                au.addIncludePath(b.path("third_party_libs/clap-wrapper/include"));
                au.addIncludePath(b.path("third_party_libs/AudioUnitSDK/include"));
                au.addIncludePath(b.path("third_party_libs/clap-wrapper/libs/fmt"));
                au.addIncludePath(b.path(build_gen_relative));
                au.addIncludePath(b.path(wrapper_path ++ "/src"));
                au.linkLibCpp();

                au.linkLibrary(plugin);
                au.linkLibrary(au_sdk);
                au.linkFramework("AudioToolbox");
                au.linkFramework("CoreMIDI");

                au.addConfigHeader(build_config_step);
                au.addIncludePath(b.path("src"));

                const au_install_artifact_step = b.addInstallArtifact(au, .{ .dest_dir = install_subfolder });
                b.getInstallStep().dependOn(&au_install_artifact_step.step);
                applyUniversalSettings(&build_context, au);
                addToLipoSteps(&build_context, au, true) catch @panic("OOM");

                var au_post_install_step = b.allocator.create(PostInstallStep) catch @panic("OOM");
                au_post_install_step.* = PostInstallStep{
                    .step = std.Build.Step.init(.{
                        .id = std.Build.Step.Id.custom,
                        .name = "Post install config",
                        .owner = b,
                        .makeFn = performPostInstallConfig,
                    }),
                    .make_macos_bundle = true,
                    .context = &build_context,
                    .compile_step = au,
                };
                au_post_install_step.step.dependOn(b.getInstallStep());
                build_context.master_step.dependOn(&au_post_install_step.step);
            }
        }

        if (target.result.os.tag == .windows) {
            const installer_path = "src/windows_installer";

            // Images for the installer are optional and separate to this repo. If they are wanted, they should be placed in the build_resources/logos folder. See below for the expected filenames within that folder.
            var logo_path_relative: ?[]const u8 = null;
            var sidebar_image_path_relative: ?[]const u8 = null;
            {
                const subdir = b.pathJoin(&.{ "build_resources", "logos" });
                var found = true;
                std.fs.accessAbsolute(b.pathJoin(&.{ rootdir, subdir }), .{}) catch {
                    found = false;
                };
                if (found) {
                    logo_path_relative = b.pathJoin(&.{ subdir, "logo.ico" });
                    sidebar_image_path_relative = b.pathJoin(&.{ subdir, "small_square_logo.png" });
                } else {
                    std.debug.print("WARNING: missing logos, some aspects of the final build might be missing\n", .{});
                }
            }

            {
                const win_installer_description = "Installer for Floe plugins";
                const manifest_path = std.fs.path.join(b.allocator, &.{ build_gen_relative, "installer.manifest" }) catch @panic("OOM");
                {
                    const file = std.fs.createFileAbsolute(b.pathJoin(&.{ rootdir, manifest_path }), .{ .truncate = true }) catch @panic("could not create file");
                    defer file.close();
                    file.writeAll(b.fmt(
                        \\ <?xml version="1.0" encoding="UTF-8" standalone="yes"?>
                        \\ <assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0">
                        \\ <assemblyIdentity
                        \\     version="1.0.0.0"
                        \\     processorArchitecture="amd64"
                        \\     name="{[vendor]s}.Floe.Installer"
                        \\     type="win32"
                        \\ />
                        \\ <description>{[description]s}</description>
                        \\ <dependency>
                        \\     <dependentAssembly>
                        \\         <assemblyIdentity
                        \\             type="win32"
                        \\             name="Microsoft.Windows.Common-Controls"
                        \\             version="6.0.0.0"
                        \\             processorArchitecture="amd64"
                        \\             publicKeyToken="6595b64144ccf1df"
                        \\             language="*"
                        \\         />
                        \\     </dependentAssembly>
                        \\ </dependency>
                        \\ <compatibility xmlns="urn:schemas-microsoft-com:compatibility.v1">
                        \\     <application>
                        \\         <!-- Windows 10, 11 -->
                        \\         <supportedOS Id="{{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}}"/>
                        \\         <!-- Windows 8.1 -->
                        \\         <supportedOS Id="{{1f676c76-80e1-4239-95bb-83d0f6d0da78}}"/>
                        \\         <!-- Windows 8 -->
                        \\         <supportedOS Id="{{4a2f28e3-53b9-4441-ba9c-d69d4a4a6e38}}"/>
                        \\     </application>
                        \\ </compatibility>
                        \\ <trustInfo xmlns="urn:schemas-microsoft-com:asm.v3">
                        \\     <security>
                        \\         <requestedPrivileges>
                        \\         <requestedExecutionLevel level="{[execution_level]s}" uiAccess="false" />
                        \\         </requestedPrivileges>
                        \\     </security>
                        \\ </trustInfo>
                        \\ <asmv3:application>
                        \\     <asmv3:windowsSettings xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">
                        \\         <dpiAwareness>PerMonitorV2</dpiAwareness>
                        \\         <longPathAware>true</longPathAware>
                        \\     </asmv3:windowsSettings>
                        \\     <asmv3:windowsSettings xmlns="http://schemas.microsoft.com/SMI/2019/WindowsSettings">
                        \\         <activeCodePage>UTF-8</activeCodePage>
                        \\     </asmv3:windowsSettings>
                        \\ </asmv3:application>
                        \\ </assembly>
                    , .{
                        .description = win_installer_description,
                        .execution_level = if (windows_installer_require_admin) "requireAdministrator" else "asInvoker",
                        .vendor = floe_vendor,
                    })) catch @panic("could not write to file");
                }

                const win_installer = b.addExecutable(.{
                    .name = "Floe Installer",
                    .target = target,
                    .optimize = build_context.optimise,
                    .version = floe_version,
                    .win32_manifest = b.path(manifest_path),
                });
                var flags = std.ArrayList([]const u8).init(b.allocator);

                var found = true;
                std.fs.accessAbsolute(core_library_path, .{}) catch {
                    found = false;
                };
                if (found) {
                    const core_library_zip_path_relative = b.pathJoin(&.{ "zig-out", "floe-core-library.zip" });
                    // IMPROVE: it's slow to zip this every time
                    // NOTE: we enter the library folder and build the zip from there. This way, the zip contains only the contents of the Core Library folder, not the folder itself. Additionally, we exclude the .git folder.
                    const zip_core = b.addSystemCommand(&.{ "zip", "-x", ".git/*", "-r", b.pathJoin(&.{ rootdir, core_library_zip_path_relative }), "." });
                    zip_core.setCwd(b.path(core_library_path));
                    win_installer.step.dependOn(&zip_core.step);
                    flags.append(b.fmt("-DCORE_LIBRARY_ZIP_PATH=\"{s}\"", .{core_library_zip_path_relative})) catch unreachable;
                } else {
                    std.debug.print("WARNING: missing core library, some aspects of the final build might be missing\n", .{});
                }

                if (sidebar_image_path_relative != null) {
                    flags.append(b.fmt("-DSIDEBAR_IMAGE_PATH=\"{s}\"", .{sidebar_image_path_relative.?})) catch unreachable;
                }
                flags.append("-DCLAP_PLUGIN_PATH=\"zig-out/x86_64-windows/Floe.clap\"") catch unreachable;
                flags.append("-DVST3_PLUGIN_PATH=\"zig-out/x86_64-windows/Floe.vst3\"") catch unreachable;
                win_installer.addWin32ResourceFile(.{
                    .file = b.path(installer_path ++ "/resources.rc"),
                    .flags = flags.items,
                });
                flags.appendSlice(cpp_fp_flags) catch unreachable;

                win_installer.addCSourceFiles(.{
                    .files = &.{
                        installer_path ++ "/installer.cpp",
                        installer_path ++ "/gui.cpp",
                    },
                    .flags = flags.items,
                });

                win_installer.linkSystemLibrary("gdi32");
                win_installer.linkSystemLibrary("version");
                win_installer.linkSystemLibrary("comctl32");

                addWin32EmbedInfo(win_installer, .{
                    .name = "Floe Installer",
                    .description = win_installer_description,
                    .icon_path = logo_path_relative,
                }) catch @panic("OOM");
                win_installer.addConfigHeader(build_config_step);
                win_installer.addIncludePath(b.path("src"));
                win_installer.addObject(stb_image);
                win_installer.linkLibrary(library);
                win_installer.addObject(miniz);
                applyUniversalSettings(&build_context, win_installer);

                // everything needs to be installed before we compile the installer because it needs to embed the plugins
                win_installer.step.dependOn(&vst3_post_install_step.step);
                win_installer.step.dependOn(&clap_post_install_step.step);

                const artifact_step = b.addInstallArtifact(win_installer, .{ .dest_dir = install_subfolder });
                build_context.master_step.dependOn(&artifact_step.step);
                join_compile_commands.step.dependOn(&win_installer.step);
            }
        }

        if (build_context.build_mode != .production) {
            const tests = b.addExecutable(.{
                .name = "tests",
                .target = target,
                .optimize = build_context.optimise,
            });
            tests.addCSourceFiles(.{ .files = &.{
                "src/tests/tests_main.cpp",
            }, .flags = cpp_fp_flags });
            tests.addConfigHeader(build_config_step);
            tests.linkLibrary(plugin);
            b.getInstallStep().dependOn(&b.addInstallArtifact(tests, .{ .dest_dir = install_subfolder }).step);
            applyUniversalSettings(&build_context, tests);
            join_compile_commands.step.dependOn(&tests.step);
            addToLipoSteps(&build_context, tests, false) catch @panic("OOM");

            var post_install_step = b.allocator.create(PostInstallStep) catch @panic("OOM");
            post_install_step.* = PostInstallStep{
                .step = std.Build.Step.init(.{
                    .id = std.Build.Step.Id.custom,
                    .name = "Post install config",
                    .owner = b,
                    .makeFn = performPostInstallConfig,
                }),
                .make_macos_bundle = false,
                .context = &build_context,
                .compile_step = tests,
            };
            post_install_step.step.dependOn(&tests.step);
            post_install_step.step.dependOn(b.getInstallStep());
            build_context.master_step.dependOn(&post_install_step.step);

            const run_tests = b.addRunArtifact(tests);
            build_context.test_step.dependOn(&run_tests.step);
        }

        build_context.master_step.dependOn(&join_compile_commands.step);
    }

    build_context.master_step.dependOn(b.getInstallStep());
    b.default_step = build_context.master_step;
}

const std = @import("std");

pub fn build(b: *std.Build) void {
    const idf_export = b.option([]const u8, "idf_export", "Path to ESP-IDF export.sh") orelse "/opt/esp-idf/export.sh";
    const project_dir = b.option([]const u8, "idf_project_dir", "ESP-IDF project directory") orelse ".";
    const port = b.option([]const u8, "port", "Serial port (e.g. /dev/ttyACM0)") orelse "/dev/ttyACM0";
    const baud = b.option([]const u8, "baud", "Monitor baud rate") orelse "115200";
    const target = b.option([]const u8, "target", "ESP-IDF chip target (e.g. esp32s3)") orelse "esp32s3";

    const idf_env = computeEspIdfEnv(b, idf_export) catch |err| {
        std.debug.panic(
            "Failed to load ESP-IDF environment ({s}). Pass -Didf_export=/path/to/export.sh",
            .{@errorName(err)},
        );
    };

    b.default_step.dependOn(&idf(b, idf_env, project_dir, &.{"build"}).step);

    b.step("set-target", "Run `idf.py set-target ...` once per project")
        .dependOn(&idf(b, idf_env, project_dir, &.{ "set-target", target }).step);

    b.step("build", "Build firmware (idf.py build)")
        .dependOn(&idf(b, idf_env, project_dir, &.{"build"}).step);

    b.step("idf-version", "Print ESP-IDF version (idf.py --version)")
        .dependOn(&idf(b, idf_env, project_dir, &.{"--version"}).step);

    b.step("menuconfig", "Configure project (idf.py menuconfig)")
        .dependOn(&idf(b, idf_env, project_dir, &.{"menuconfig"}).step);

    b.step("reconfigure", "Re-run CMake/Kconfig configuration (idf.py reconfigure)")
        .dependOn(&idf(b, idf_env, project_dir, &.{"reconfigure"}).step);

    b.step("add-depencency", "Add a dependency")
        .dependOn(&idf(b, idf_env, project_dir, &.{"add-dependency"}).step);

    b.step("clean", "Full clean (idf.py fullclean)")
        .dependOn(&idf(b, idf_env, project_dir, &.{"fullclean"}).step);

    b.step("flash", "Flash firmware")
        .dependOn(&idfPort(b, idf_env, project_dir, port, &.{"flash"}).step);

    b.step("monitor", "Serial monitor")
        .dependOn(&idfPort(b, idf_env, project_dir, port, &.{ "monitor", "-b", baud }).step);

    b.step("flash-monitor", "Flash then monitor")
        .dependOn(&idfPort(b, idf_env, project_dir, port, &.{ "flash", "monitor", "-b", baud }).step);

    const formatc = b.step("formatc", "Run uncrustify on C source");
    setupUncrustify(b, formatc, project_dir) catch {};
}

fn resolveIdfPy(b: *std.Build, idf_env: *const std.process.Environ.Map) []const u8 {
    if (idf_env.get("IDF_PATH")) |idf_path| {
        return std.fs.path.join(b.allocator, &.{ idf_path, "tools", "idf.py" }) catch "idf.py";
    }
    return "idf.py";
}

fn idf(
    b: *std.Build,
    idf_env: *const std.process.Environ.Map,
    project_dir: []const u8,
    args: []const []const u8,
) *std.Build.Step.Run {
    const idf_py = resolveIdfPy(b, idf_env);
    const run = b.addSystemCommand(&.{idf_py});
    run.addArgs(args);
    applyEnv(run, idf_env);
    run.setCwd(b.path(project_dir));
    return run;
}

fn idfPort(
    b: *std.Build,
    idf_env: *const std.process.Environ.Map,
    project_dir: []const u8,
    port: []const u8,
    args: []const []const u8,
) *std.Build.Step.Run {
    const idf_py = resolveIdfPy(b, idf_env);
    const run = b.addSystemCommand(&.{ idf_py, "-p", port });
    run.addArgs(args);
    applyEnv(run, idf_env);
    run.setCwd(b.path(project_dir));
    return run;
}

fn applyEnv(run: *std.Build.Step.Run, env: *const std.process.Environ.Map) void {
    const keys = env.keys();
    const values = env.values();
    for (keys, values) |k, v| run.setEnvironmentVariable(k, v);
}

fn computeEspIdfEnv(b: *std.Build, idf_export: []const u8) !*std.process.Environ.Map {
    const env_cache_path = try espIdfEnvCachePath(b, idf_export);
    defer b.allocator.free(env_cache_path);
    if (tryLoadEspIdfEnvFromCache(b, idf_export, env_cache_path)) |cached| return cached;

    const cmd = try std.fmt.allocPrint(b.allocator, ". {s} >/dev/null 2>&1; env -0", .{idf_export});
    defer b.allocator.free(cmd);

    const result = try std.process.run(b.allocator, b.graph.io, .{
        .argv = &.{ "bash", "-lc", cmd },
        .environ_map = &b.graph.environ_map,
    });
    defer {
        b.allocator.free(result.stderr);
        b.allocator.free(result.stdout);
    }

    switch (result.term) {
        .exited => |code| if (code != 0) return error.EspIdfExportFailed,
        else => return error.EspIdfExportFailed,
    }

    std.Io.Dir.writeFile(.cwd(), b.graph.io, .{
        .sub_path = env_cache_path,
        .data = result.stdout,
        .flags = .{ .truncate = true },
    }) catch {};

    return try parseEnvToMap(b, result.stdout);
}

fn espIdfEnvCachePath(b: *std.Build, idf_export: []const u8) ![]const u8 {
    var hh: std.Build.Cache.HashHelper = .{};
    hh.addBytes(idf_export);
    const digest = hh.peek();

    const file_name = try std.fmt.allocPrint(b.allocator, "esp-idf-env-{s}.env", .{digest});
    defer b.allocator.free(file_name);
    return try b.cache_root.join(b.allocator, &.{file_name});
}

fn tryLoadEspIdfEnvFromCache(
    b: *std.Build,
    idf_export: []const u8,
    env_cache_path: []const u8,
) ?*std.process.Environ.Map {
    const io = b.graph.io;
    const cwd = std.Io.Dir.cwd();

    const cache_stat = cwd.statFile(io, env_cache_path, .{}) catch return null;
    const export_stat = cwd.statFile(io, idf_export, .{}) catch null;

    if (export_stat) |st| {
        if (cache_stat.mtime.nanoseconds < st.mtime.nanoseconds) return null;
    }

    const env0 = cwd.readFileAlloc(io, env_cache_path, b.allocator, .unlimited) catch return null;
    defer b.allocator.free(env0);
    return parseEnvToMap(b, env0) catch return null;
}

fn parseEnvToMap(b: *std.Build, env0: []const u8) !*std.process.Environ.Map {
    const env_ptr = try b.allocator.create(std.process.Environ.Map);
    env_ptr.* = std.process.Environ.Map.init(b.allocator);
    errdefer {
        env_ptr.deinit();
        b.allocator.destroy(env_ptr);
    }

    var it = std.mem.splitScalar(u8, env0, 0);
    while (it.next()) |entry| {
        if (entry.len == 0) continue;
        const eq = std.mem.indexOfScalar(u8, entry, '=') orelse continue;
        const k = entry[0..eq];
        const v = entry[eq + 1 ..];
        try env_ptr.put(k, v);
    }

    return env_ptr;
}

/// Setup the uncrustify step to format C files.
fn setupUncrustify(b: *std.Build, step: *std.Build.Step, project_dir: []const u8) !void {
    const sources = try collectUncrustifySources(b, project_dir);
    if (sources.len == 0) return;

    const uncrustify = b.dependency("uncrustify", .{
        .optimize = .ReleaseFast,
    });
    const uncrustify_exe = uncrustify.artifact("uncrustify");
    const update = b.addUpdateSourceFiles();
    for (sources) |file| {
        const run = b.addRunArtifact(uncrustify_exe);
        run.addArgs(&.{ "-c", "uncrustify.cfg", "-f" });
        run.addFileArg(b.path(file));
        run.addArg("-o");
        const output_name = try std.mem.replaceOwned(u8, b.allocator, file, "/", "-");
        const output = run.addOutputFileArg(output_name);
        update.addCopyFileToSource(output, file);
    }
    step.dependOn(&update.step);
}

/// Collect all the files to format.
fn collectUncrustifySources(b: *std.Build, project_dir: []const u8) ![]const []const u8 {
    const roots = &[_][]const u8{"main"};
    const io = b.graph.io;
    const cwd = std.Io.Dir.cwd();
    const gpa = b.allocator;

    var files: std.ArrayList([]const u8) = .empty;

    for (roots) |root| {
        const root_path = try std.Io.Dir.path.join(gpa, &.{ project_dir, root });
        const dir = cwd.openDir(io, root_path, .{ .iterate = true }) catch continue;
        defer dir.close(io);

        var walker = try dir.walk(gpa);
        defer walker.deinit();

        while (try walker.next(io)) |entry| {
            if (entry.kind != .file) continue;
            if (!std.mem.endsWith(u8, entry.path, ".c") and
                !std.mem.endsWith(u8, entry.path, ".h"))
                continue;

            try files.append(gpa, try std.Io.Dir.path.join(gpa, &.{
                root_path,
                entry.path,
            }));
        }
    }

    return files.items;
}

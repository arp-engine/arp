#pragma once

#include "core/io/file_io.h"
#include "core/system/process_runner.h"
#include "scriptvm/plugin/compiler_toolchain_layout.h"
#include "scriptvm/plugin/immutable_build_input_snapshot.h"

#include <cstdlib>
#include <initializer_list>
#include <unistd.h>

namespace arp::core {

// Private AppleClangSnapshot/1 policy. The public compile/snapshot entry points
// remain CompilerToolchainLayout; no alternate importer or compiler is selected.
class AppleClangToolchain final {
    class Queries final {
    public:
        Queries()
        {
            const auto templatePath = std::filesystem::temp_directory_path() / "arp-clang-XXXXXX";
            const auto& pattern = templatePath.native();
            pmr::String writable(pattern.c_str(), &defaultCpuMemoryResource());
            if (const auto* path = ::mkdtemp(writable.data()))
                root_ = path;
        }

        ~Queries()
        {
            if (!root_.empty()) {
                std::error_code error;
                std::filesystem::remove_all(root_, error);
            }
        }

        bool run(const std::filesystem::path& tool,
            std::initializer_list<std::string_view> args,
            pmr::String& output)
        {
            if (root_.empty())
                return false;
            ProcessRunRequest request;
            request.executable = tool;
            request.outputFile = root_ / "output";
            for (const auto arg : args)
                request.arguments.emplace_back(arg.data(), arg.size());
            const auto process = ProcessRunner().run(request);
            const auto read = readTextFile(request.outputFile);
            if (!process.ok() || !read.success || read.text.empty())
                return false;
            output = read.text;
            return true;
        }

    private:
        std::filesystem::path root_;
    };

    static bool within(const std::filesystem::path& path, const std::filesystem::path& root)
    {
        const auto relative = path.lexically_relative(root);
        return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
    }

    static pmr::Vector<std::filesystem::path> options(std::string_view text, std::string_view key)
    {
        pmr::String marker("\"", &defaultCpuMemoryResource());
        marker += key;
        marker += "\" \"";
        pmr::Vector<std::filesystem::path> values{&defaultCpuMemoryResource()};
        std::size_t offset = 0;
        while ((offset = text.find(marker, offset)) != std::string_view::npos) {
            offset += marker.size();
            const auto end = text.find('"', offset);
            if (end == std::string_view::npos)
                return {};
            const auto value = text.substr(offset, end - offset);
            if (value.empty() || value.find_first_of("\\\r\n") != std::string_view::npos)
                return {};
            values.emplace_back(value);
            offset = end + 1;
        }
        return values;
    }

    static bool inspectImpl(const std::filesystem::path& compiler,
        Queries& query,
        CompilerToolchainLayoutResult& result,
        std::filesystem::path& lto)
    {
        pmr::String version{&defaultCpuMemoryResource()}, trace{&defaultCpuMemoryResource()};
        if (!query.run(compiler, {"--version"}, version)
            || version.find("Apple clang version ") != 0u
            || !query.run(compiler, {"-###", "-shared", "-x", "c++", "/dev/null"}, trace)
            || trace.find("\"-platform_version\" \"macos\"") == pmr::String::npos) {
            result.diagnostics =
                "AppleClangSnapshot/1 requires inspected Apple Clang macOS metadata";
            return false;
        }
        const auto sdk = options(trace, "-isysroot");
        const auto linkSdk = options(trace, "-syslibroot");
        const auto resource = options(trace, "-resource-dir");
        const auto ltoPaths = options(trace, "-lto_library");
        if (sdk.size() != 1u || linkSdk != sdk || resource.size() != 1u || ltoPaths.size() != 1u) {
            result.diagnostics = "AppleClangSnapshot/1 ambiguous SDK/resource/linker metadata";
            return false;
        }
        std::error_code error;
        result.systemRoot = std::filesystem::canonical(sdk.front(), error);
        if (!error)
            result.resourceDirectory = std::filesystem::canonical(resource.front(), error);
        if (!error)
            lto = std::filesystem::canonical(ltoPaths.front(), error);
        pmr::String linker{&defaultCpuMemoryResource()};
        if (error || !query.run(compiler, {"-print-prog-name=ld"}, linker)) {
            result.diagnostics = "AppleClangSnapshot/1 unavailable SDK/resource/linker";
            return false;
        }
        while (!linker.empty() && (linker.back() == '\n' || linker.back() == '\r'))
            linker.pop_back();
        result.linkerExecutable = std::filesystem::canonical(std::filesystem::path(linker), error);
        if (error || !result.linkerExecutable.is_absolute()
            || !std::filesystem::is_regular_file(result.systemRoot / "SDKSettings.json", error)
            || error) {
            result.diagnostics = "AppleClangSnapshot/1 invalid linker or SDK metadata";
            return false;
        }
        result.programSearchDirectory = result.linkerExecutable.parent_path();
        // Preserve exactly the system include order reported by the driver.
        const std::string_view source(trace.data(), trace.size());
        std::size_t lineOffset = 0;
        while ((lineOffset = source.find("\"-internal-", lineOffset)) != std::string_view::npos) {
            const auto end = source.find('"', lineOffset + 1);
            if (end == std::string_view::npos)
                break;
            const auto key = source.substr(lineOffset + 1, end - lineOffset - 1);
            // Parse one argument, not subsequent occurrences of the same key.
            const auto valueStart = end + 3;
            const auto valueEnd = source.find('"', valueStart);
            if (valueEnd == std::string_view::npos)
                break;
            if (key == "-internal-isystem" || key == "-internal-externc-isystem") {
                const std::filesystem::path path(source.substr(valueStart, valueEnd - valueStart));
                error.clear();
                const auto status = std::filesystem::status(path, error);
                if (status.type() != std::filesystem::file_type::not_found) {
                    if (error || !std::filesystem::is_directory(status)) {
                        result.diagnostics = "AppleClangSnapshot/1 invalid system include input";
                        return false;
                    }
                    const auto canonicalInclude = std::filesystem::canonical(path, error);
                    if (error) {
                        result.diagnostics =
                            "AppleClangSnapshot/1 system include is not canonicalizable";
                        return false;
                    }
                    result.systemIncludeDirectories.push_back(canonicalInclude);
                }
            }
            lineOffset = valueEnd + 1;
        }
        result.libraryDirectories.push_back(result.systemRoot / "usr/lib");
        result.success = !result.systemIncludeDirectories.empty();
        if (!result.success)
            result.diagnostics = "AppleClangSnapshot/1 has no declared system includes";
        return result.success;
    }

public:
    static CompilerToolchainLayoutResult inspect(const std::filesystem::path& compiler)
    {
        CompilerToolchainLayoutResult result;
        Queries query;
        std::filesystem::path lto;
        inspectImpl(compiler, query, result, lto);
        return result;
    }

    static CompilerToolchainSnapshotResult capture(ImmutableBuildInputSnapshot& snapshot,
        std::string_view root,
        const std::filesystem::path& compiler)
    {
        CompilerToolchainSnapshotResult result;
        if (root.empty()) {
            result.diagnostics = "AppleClangSnapshot/1 empty logical root";
            return result;
        }
        Queries query;
        CompilerToolchainLayoutResult inspected;
        std::filesystem::path lto;
        if (!inspectImpl(compiler, query, inspected, lto)) {
            result.diagnostics = inspected.diagnostics;
            return result;
        }
        const auto logical = [&](std::string_view suffix) {
            pmr::String value(root.data(), root.size(), &defaultCpuMemoryResource());
            value += suffix;
            return value;
        };
        std::error_code error;
        const auto canonical = std::filesystem::canonical(compiler, error);
        const auto install = canonical.parent_path().parent_path();
        auto driver = logical("/bin/");
        driver += compiler.filename().generic_string();
        if (error || !snapshot.captureExecutable(driver, canonical, result.diagnostics)
            || !snapshot.captureResolvedDirectory(
                logical("/sdk"), inspected.systemRoot, result.diagnostics)
            || !snapshot.captureResolvedDirectory(
                logical("/resource"), inspected.resourceDirectory, result.diagnostics))
            return result;
        pmr::Vector<std::filesystem::path> pending{&defaultCpuMemoryResource()};
        pmr::Set<std::filesystem::path> seen{&defaultCpuMemoryResource()};
        pending.push_back(inspected.linkerExecutable);
        pending.push_back(lto);
        while (!pending.empty()) {
            const auto tool = pending.back();
            pending.pop_back();
            if (!seen.insert(tool).second)
                continue;
            if (!within(tool, install)) {
                result.diagnostics = "AppleClangSnapshot/1 native tool escapes installation";
                return result;
            }
            auto target = logical("/");
            target += tool.lexically_relative(install).generic_string();
            if (!snapshot.captureResolvedFile(target, tool, install, result.diagnostics))
                return result;
            pmr::String dependencies{&defaultCpuMemoryResource()};
            const auto& text = tool.native();
            if (!query.run("/usr/bin/otool", {"-L", text}, dependencies)) {
                result.diagnostics = "AppleClangSnapshot/1 native dependency inspection failed";
                return result;
            }
            std::size_t offset = dependencies.find('\n');
            while (offset != pmr::String::npos) {
                const auto start = dependencies.find_first_not_of("\r\n\t ", offset);
                if (start == pmr::String::npos)
                    break;
                const auto end = dependencies.find(" (compatibility version", start);
                if (end == pmr::String::npos) {
                    result.diagnostics = "AppleClangSnapshot/1 invalid native dependency metadata";
                    return result;
                }
                const auto name = std::string_view(dependencies).substr(start, end - start);
                if (name.find("@rpath/") == 0u) {
                    const std::filesystem::path relative(name.substr(7));
                    const auto dependency =
                        std::filesystem::canonical(install / "lib" / relative, error);
                    if (error || relative.is_absolute() || !within(dependency, install / "lib")) {
                        result.diagnostics = "AppleClangSnapshot/1 invalid rpath dependency";
                        return result;
                    }
                    pending.push_back(dependency);
                } else if (name.find("/usr/lib/") != 0u && name.find("/System/Library/") != 0u) {
                    result.diagnostics = "AppleClangSnapshot/1 unsupported host library dependency";
                    return result;
                }
                offset = dependencies.find('\n', end);
            }
        }
        pmr::String rpaths{&defaultCpuMemoryResource()};
        const auto& linker = inspected.linkerExecutable.native();
        if (!query.run("/usr/bin/otool", {"-l", linker}, rpaths)
            || rpaths.find("path @executable_path/../lib/") == pmr::String::npos) {
            result.diagnostics = "AppleClangSnapshot/1 unsupported native linker rpath layout";
            return result;
        }
        result.compilerExecutable = snapshot.path(driver);
        result.layout = inspected;
        result.layout.programSearchDirectory = snapshot.path(logical("/bin"));
        result.layout.systemRoot = snapshot.path(logical("/sdk"));
        result.layout.resourceDirectory = snapshot.path(logical("/resource"));
        result.layout.linkerExecutable = snapshot.path(logical("/bin/ld"));
        result.layout.systemIncludeDirectories.clear();
        for (const auto& include : inspected.systemIncludeDirectories) {
            if (within(include, inspected.systemRoot))
                result.layout.systemIncludeDirectories.push_back(
                    result.layout.systemRoot / include.lexically_relative(inspected.systemRoot));
            else if (within(include, inspected.resourceDirectory))
                result.layout.systemIncludeDirectories.push_back(result.layout.resourceDirectory
                    / include.lexically_relative(inspected.resourceDirectory));
            else if (within(include, install / "include")) {
                if (!snapshot.captureResolvedDirectory(
                        logical("/include"), install / "include", result.diagnostics))
                    return result;
                result.layout.systemIncludeDirectories.push_back(snapshot.path(logical("/include"))
                    / include.lexically_relative(install / "include"));
            } else {
                result.diagnostics = "AppleClangSnapshot/1 system include escapes captured roots";
                return result;
            }
        }
        result.layout.libraryDirectories = {result.layout.systemRoot / "usr/lib"};
        result.success = true;
        return result;
    }
};
} // namespace arp::core

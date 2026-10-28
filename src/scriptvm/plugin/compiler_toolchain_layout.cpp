#include "scriptvm/plugin/compiler_toolchain_layout.h"

#include "core/system/process_runner.h"
#include "core/text/number_format.h"
#include "scriptvm/plugin/immutable_build_input_snapshot.h"

#if defined(__APPLE__)
#include "scriptvm/plugin/detail/apple_clang_toolchain.h"
#endif

#include <algorithm>
#include <array>
#include <cctype>

namespace arp::core {

CompilerToolchainLayoutResult CompilerToolchainLayout::inspect(
    const std::filesystem::path& compilerExecutable) const
{
#if defined(__APPLE__)
    return AppleClangToolchain::inspect(compilerExecutable);
#endif
    CompilerToolchainLayoutResult result;
    std::error_code error;
    if (!std::filesystem::is_regular_file(compilerExecutable, error) || error) {
        result.diagnostics = "compiler toolchain layout requires a regular executable";
        return result;
    }
    const auto canonicalCompiler = std::filesystem::weakly_canonical(compilerExecutable, error);
    if (error || canonicalCompiler.empty()) {
        result.diagnostics = "compiler toolchain executable path is not canonicalizable";
        return result;
    }
    const auto installationRoot = canonicalCompiler.parent_path().parent_path();
    const auto gccProgramRoot = installationRoot / "libexec" / "gcc";
    error.clear();
    if (!std::filesystem::is_directory(gccProgramRoot, error)) {
        if (error) {
            result.diagnostics = "compiler toolchain program directory is not inspectable";
            return result;
        }
        result.success = true;
        return result;
    }

#if defined(_WIN32) || defined(__CYGWIN__)
    constexpr const char* programName = "cc1plus.exe";
#else
    constexpr const char* programName = "cc1plus";
#endif
    pmr::Vector<std::filesystem::path> candidates{&defaultCpuMemoryResource()};
    for (std::filesystem::recursive_directory_iterator iterator(gccProgramRoot, error), end;
        !error && iterator != end;
        iterator.increment(error)) {
        std::error_code entryError;
        if (iterator->is_regular_file(entryError) && iterator->path().filename() == programName) {
            candidates.push_back(iterator->path().parent_path());
        }
        if (entryError) {
            error = entryError;
            break;
        }
    }
    if (error || candidates.size() != 1u) {
        result.diagnostics = error
            ? "compiler toolchain program directory enumeration failed"
            : "compiler toolchain must contain exactly one cc1plus program directory";
        return result;
    }
    result.programSearchDirectory = std::move(candidates.front());
    const auto version = result.programSearchDirectory.filename();
    const auto target = result.programSearchDirectory.parent_path().filename();
    const auto appendDirectory = [](auto& output, const std::filesystem::path& path) {
        std::error_code pathError;
        if (std::filesystem::is_directory(path, pathError) && !pathError)
            output.push_back(path);
    };
    const auto standardCpp = installationRoot / "include" / "c++" / version;
    appendDirectory(result.systemIncludeDirectories, standardCpp);
    appendDirectory(result.systemIncludeDirectories, standardCpp / target);
    appendDirectory(result.systemIncludeDirectories, standardCpp / "backward");
    const auto gccLibrary = installationRoot / "lib" / "gcc" / target / version;
    appendDirectory(result.systemIncludeDirectories, gccLibrary / "include");
    appendDirectory(result.systemIncludeDirectories, gccLibrary / "include-fixed");
    appendDirectory(result.systemIncludeDirectories, installationRoot / target / "include");
    appendDirectory(result.systemIncludeDirectories, installationRoot / "include");
    appendDirectory(result.libraryDirectories, gccLibrary);
    appendDirectory(result.libraryDirectories, installationRoot / target / "lib");
    appendDirectory(result.libraryDirectories, installationRoot / "lib");
    appendDirectory(result.auxiliaryProgramSearchDirectories, installationRoot / target / "bin");
    result.success = true;
    return result;
}

void CompilerToolchainLayout::appendCompilerArguments(
    const CompilerToolchainLayoutResult& layout, ProcessRunRequest& request)
{
    const auto add = [&](std::string_view text) {
        request.arguments.emplace_back(text.data(), text.size());
    };
    const auto pathArgument =
        [&](std::string_view prefix, const std::filesystem::path& path, bool directory) {
            pmr::String argument(prefix.data(), prefix.size(), &defaultCpuMemoryResource());
            argument += path.generic_string();
            if (directory)
                argument += "/";
            request.arguments.push_back(std::move(argument));
        };
    if (!layout.programSearchDirectory.empty())
        pathArgument("-B", layout.programSearchDirectory, true);
    for (const auto& path : layout.auxiliaryProgramSearchDirectories)
        pathArgument("-B", path, true);
    add("-nostdinc");
    add("-nostdinc++");
    for (const auto& path : layout.systemIncludeDirectories) {
        add("-isystem");
        pathArgument("", path, false);
    }
    for (const auto& path : layout.libraryDirectories) {
        pathArgument("-B", path, true);
        pathArgument("-L", path, false);
    }
    if (!layout.systemRoot.empty()) {
        add("-isysroot");
        pathArgument("", layout.systemRoot, false);
    }
    if (!layout.resourceDirectory.empty()) {
        add("-resource-dir");
        pathArgument("", layout.resourceDirectory, false);
    }
    if (!layout.linkerExecutable.empty())
        pathArgument("-fuse-ld=", layout.linkerExecutable, false);
}

CompilerToolchainSnapshotResult CompilerToolchainLayout::captureSnapshot(
    ImmutableBuildInputSnapshot& snapshot,
    std::string_view logicalRoot,
    const std::filesystem::path& compilerExecutable) const
{
#if defined(__APPLE__)
    return AppleClangToolchain::capture(snapshot, logicalRoot, compilerExecutable);
#endif
    CompilerToolchainSnapshotResult result;
    if (logicalRoot.empty()) {
        result.diagnostics = "compiler toolchain snapshot logical root is empty";
        return result;
    }
    const auto sourceLayout = inspect(compilerExecutable);
    if (!sourceLayout.success) {
        result.diagnostics = sourceLayout.diagnostics;
        return result;
    }

    std::error_code error;
    const auto canonicalCompiler = std::filesystem::weakly_canonical(compilerExecutable, error);
    if (error || canonicalCompiler.empty()) {
        result.diagnostics = "compiler toolchain snapshot executable is not canonicalizable";
        return result;
    }

    const auto rootText =
        pmr::String(logicalRoot.data(), logicalRoot.size(), &defaultCpuMemoryResource());
    pmr::String driverLogical(rootText, &defaultCpuMemoryResource());
    driverLogical += "/d/";
    driverLogical += canonicalCompiler.filename().generic_string();
    if (!snapshot.captureExecutable(driverLogical, canonicalCompiler, result.diagnostics)) {
        return result;
    }
    result.compilerExecutable = snapshot.path(driverLogical);

    // A copied Windows compiler driver must resolve its runtime DLLs beside
    // the copied executable instead of falling back to the mutable PATH.
    const auto driverDirectory = canonicalCompiler.parent_path();
    for (std::filesystem::directory_iterator iterator(driverDirectory, error), end;
        !error && iterator != end;
        iterator.increment(error)) {
        std::error_code entryError;
        if (!iterator->is_regular_file(entryError)) {
            if (entryError)
                error = entryError;
            continue;
        }
        auto extension = iterator->path().extension().generic_string();
        std::transform(
            extension.begin(), extension.end(), extension.begin(), [](const unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
        if (extension != ".dll")
            continue;
        pmr::String logical(rootText, &defaultCpuMemoryResource());
        logical += "/d/";
        logical += iterator->path().filename().generic_string();
        if (!snapshot.captureFile(logical, iterator->path(), result.diagnostics)) {
            return result;
        }
    }
    if (error) {
        result.diagnostics = "compiler toolchain snapshot could not enumerate driver dependencies";
        return result;
    }

    struct RootMapping final {
        std::filesystem::path source;
        std::filesystem::path target;
    };

    pmr::Vector<RootMapping> mappings{&defaultCpuMemoryResource()};
    const auto mappedPath = [&](const std::filesystem::path& source) {
        for (const auto& mapping : mappings) {
            const auto relative = source.lexically_relative(mapping.source);
            if (!relative.empty() && *relative.begin() != "..")
                return mapping.target / relative;
            if (source == mapping.source)
                return mapping.target;
        }
        return std::filesystem::path{};
    };

    const auto captureSelectedDirectory =
        [&](const std::filesystem::path& source, const pmr::String& logical, const auto& selected) {
            pmr::Vector<std::filesystem::path> selectedFiles{&defaultCpuMemoryResource()};
            std::error_code iteratorError;
            for (std::filesystem::recursive_directory_iterator iterator(source, iteratorError), end;
                !iteratorError && iterator != end;
                iterator.increment(iteratorError)) {
                std::error_code entryError;
                if (!iterator->is_regular_file(entryError)) {
                    if (entryError)
                        iteratorError = entryError;
                    continue;
                }
                if (!selected(iterator->path()))
                    continue;
                selectedFiles.push_back(iterator->path());
            }
            if (iteratorError || selectedFiles.empty()) {
                result.diagnostics = iteratorError
                    ? "compiler toolchain selected directory enumeration failed"
                    : "compiler toolchain selected directory is empty";
                return false;
            }
            std::sort(selectedFiles.begin(), selectedFiles.end());
            for (const auto& selectedPath : selectedFiles) {
                const auto relative = selectedPath.lexically_relative(source);
                if (relative.empty() || *relative.begin() == "..") {
                    result.diagnostics = "compiler toolchain snapshot entry is not relative";
                    return false;
                }
                pmr::String fileLogical(logical, &defaultCpuMemoryResource());
                fileLogical.push_back('/');
                fileLogical += relative.generic_string();
                if (!snapshot.captureFile(fileLogical, selectedPath, result.diagnostics)) {
                    return false;
                }
            }
            mappings.push_back(RootMapping{source, snapshot.path(logical)});
            return true;
        };

    if (!sourceLayout.programSearchDirectory.empty()) {
        pmr::String logical(rootText, &defaultCpuMemoryResource());
        logical += "/p";
#if defined(_WIN32) || defined(__CYGWIN__)
        const auto selected = [](const std::filesystem::path& path) {
            auto name = path.filename().generic_string();
            std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
            return name == "cc1plus.exe" || name == "collect2.exe" || name == "lto-wrapper.exe"
                || name == "liblto_plugin.dll" || path.extension() == ".dll";
        };
#else
        const auto selected = [](const std::filesystem::path&) {
            return true;
        };
#endif
        if (!captureSelectedDirectory(sourceLayout.programSearchDirectory, logical, selected)) {
            return result;
        }
    }
    for (std::size_t index = 0u; index < sourceLayout.auxiliaryProgramSearchDirectories.size();
        ++index) {
        pmr::String logical(rootText, &defaultCpuMemoryResource());
        logical += "/a";
        appendUnsigned(logical, index);
#if defined(_WIN32) || defined(__CYGWIN__)
        const auto selected = [](const std::filesystem::path& path) {
            auto name = path.filename().generic_string();
            std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
            return name == "as.exe" || name == "ld.exe" || name == "ld.bfd.exe"
                || path.extension() == ".dll";
        };
#else
        const auto selected = [](const std::filesystem::path&) {
            return true;
        };
#endif
        if (!captureSelectedDirectory(
                sourceLayout.auxiliaryProgramSearchDirectories[index], logical, selected)) {
            return result;
        }
    }

    pmr::Vector<std::filesystem::path> roots{&defaultCpuMemoryResource()};
    roots.insert(roots.end(),
        sourceLayout.systemIncludeDirectories.begin(),
        sourceLayout.systemIncludeDirectories.end());
    std::sort(roots.begin(), roots.end(), [](const auto& lhs, const auto& rhs) {
        const auto lhsCount = static_cast<std::size_t>(std::distance(lhs.begin(), lhs.end()));
        const auto rhsCount = static_cast<std::size_t>(std::distance(rhs.begin(), rhs.end()));
        return lhsCount != rhsCount ? lhsCount < rhsCount : lhs < rhs;
    });
    for (const auto& rawRoot : roots) {
        const auto canonicalRoot = std::filesystem::weakly_canonical(rawRoot, error);
        if (error || canonicalRoot.empty()) {
            result.diagnostics =
                "compiler toolchain snapshot input directory is not canonicalizable";
            return result;
        }
        if (!mappedPath(canonicalRoot).empty())
            continue;
        pmr::String logical(rootText, &defaultCpuMemoryResource());
        logical += "/r";
        appendUnsigned(logical, mappings.size());
        const auto captureAll = [](const std::filesystem::path&) {
            return true;
        };
        if (!captureSelectedDirectory(canonicalRoot, logical, captureAll)) {
            return result;
        }
    }

    for (std::size_t index = 0u; index < sourceLayout.libraryDirectories.size(); ++index) {
        const auto& source = sourceLayout.libraryDirectories[index];
        pmr::String logical(rootText, &defaultCpuMemoryResource());
        logical += "/l";
        appendUnsigned(logical, index);
#if defined(_WIN32) || defined(__CYGWIN__)
        const auto selected = [](const std::filesystem::path& path) {
            static constexpr std::array<std::string_view, 20u> names{
                "crtbegin.o",
                "crtend.o",
                "dllcrt2.o",
                "libstdc++.a",
                "libstdc++.dll.a",
                "libgcc.a",
                "libgcc_eh.a",
                "libgcc_s.a",
                "libmingw32.a",
                "libmingwex.a",
                "libmsvcrt.a",
                "libkernel32.a",
                "libpthread.a",
                "libpthread.dll.a",
                "libwinpthread.a",
                "libwinpthread.dll.a",
                "libadvapi32.a",
                "libshell32.a",
                "libuser32.a",
                "liboldnames.a",
            };
            auto name = path.filename().generic_string();
            std::transform(name.begin(), name.end(), name.begin(), [](const unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
            return std::find(names.begin(), names.end(), name) != names.end();
        };
#else
        const auto selected = [](const std::filesystem::path&) {
            return true;
        };
#endif
        if (!captureSelectedDirectory(source, logical, selected))
            return result;
    }

    result.layout.success = true;
    if (!sourceLayout.programSearchDirectory.empty()) {
        result.layout.programSearchDirectory = mappedPath(
            std::filesystem::weakly_canonical(sourceLayout.programSearchDirectory, error));
        if (error || result.layout.programSearchDirectory.empty()) {
            result.diagnostics = "compiler toolchain program directory was not captured";
            return result;
        }
    }
    for (const auto& program : sourceLayout.auxiliaryProgramSearchDirectories) {
        const auto mapped = mappedPath(std::filesystem::weakly_canonical(program, error));
        if (error || mapped.empty()) {
            result.diagnostics = "compiler toolchain auxiliary program directory was not captured";
            return result;
        }
        result.layout.auxiliaryProgramSearchDirectories.push_back(mapped);
    }
    for (const auto& include : sourceLayout.systemIncludeDirectories) {
        const auto mapped = mappedPath(std::filesystem::weakly_canonical(include, error));
        if (error || mapped.empty()) {
            result.diagnostics = "compiler toolchain system include directory was not captured";
            return result;
        }
        result.layout.systemIncludeDirectories.push_back(mapped);
    }
    for (const auto& library : sourceLayout.libraryDirectories) {
        const auto mapped = mappedPath(std::filesystem::weakly_canonical(library, error));
        if (error || mapped.empty()) {
            result.diagnostics = "compiler toolchain library directory was not captured";
            return result;
        }
        result.layout.libraryDirectories.push_back(mapped);
    }
    result.success = true;
    return result;
}

} // namespace arp::core

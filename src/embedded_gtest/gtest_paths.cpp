#include "gtest_paths.hpp"

#include <unistd.h>

#include <atomic>
#include <fstream>
#include <stdexcept>
#include <string>

#include "embedded_gtest_data.hpp"

namespace cxxprobe::embedded_gtest {

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& path, const EmbeddedFile& file) {
    fs::create_directories(path.parent_path());
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
        throw std::runtime_error("embedded_gtest: failed to write " + path.string());
    }
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    ofs.write(reinterpret_cast<const char*>(file.data.data()),
              static_cast<std::streamsize>(file.data.size()));
}

fs::path unique_staging_dir(const fs::path& final_dir) {
    static std::atomic<int> counter{0};
    return final_dir.string() + ".tmp-" + std::to_string(::getpid()) + "-" +
           std::to_string(counter.fetch_add(1));
}

}  // namespace

ResolvedPaths resolve() {
    fs::path final_dir =
        fs::temp_directory_path() / ("cxxprobe-gtest-" + std::string(bundle_hash()));
    ResolvedPaths result{.include_dir = final_dir / "include", .lib_dir = final_dir / "lib"};

    if (fs::exists(final_dir)) {
        return result;
    }

    fs::path staging = unique_staging_dir(final_dir);
    fs::remove_all(staging);
    for (const auto& file : include_files()) {
        write_file(staging / "include" / std::string(file.relative_path), file);
    }
    for (const auto& file : lib_files()) {
        write_file(staging / "lib" / std::string(file.relative_path), file);
    }

    std::error_code ec;
    fs::rename(staging, final_dir, ec);
    if (ec) {
        // Lost the race to another thread/process that published first —
        // our staging copy is redundant, not needed.
        fs::remove_all(staging);
        if (!fs::exists(final_dir)) {
            throw std::runtime_error("embedded_gtest: failed to publish extraction to " +
                                     final_dir.string() + ": " + ec.message());
        }
    }

    return result;
}

}  // namespace cxxprobe::embedded_gtest

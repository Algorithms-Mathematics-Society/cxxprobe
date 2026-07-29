#pragma once

#include <deque>
#include <filesystem>
#include <string>
#include <vector>

struct zip;
using zip_t = struct zip;

namespace cxxprobe::pack::detail {

// Thin RAII wrapper around libzip's zip_t*, used to write a pack archive.
// Not copyable (owns a raw zip_t*). Buffers handed to add_data() are kept
// alive internally until close(), since libzip's zip_source_buffer()
// references the memory rather than copying it.
class ZipWriter {
public:
    explicit ZipWriter(const std::filesystem::path& out_path);
    ~ZipWriter();
    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;
    ZipWriter(ZipWriter&&) = delete;
    ZipWriter& operator=(ZipWriter&&) = delete;

    // Adds a file from disk at archive-relative entry_name (always '/'
    // separators, regardless of host), preserving source_path's Unix
    // permission bits (including the executable bit) as the zip entry's
    // external attributes. Throws std::runtime_error on failure.
    void add_file(const std::string& entry_name, const std::filesystem::path& source_path);

    // Adds an in-memory buffer as a regular file entry (0644). Throws on
    // failure.
    void add_data(const std::string& entry_name, std::string data);

    // Finalizes the archive. Throws on failure. Safe to call at most once;
    // the destructor discards (not closes) if close() was never called
    // explicitly, so a partially-built archive on an exception path never
    // gets left as a valid-looking zip file.
    void close();

private:
    zip_t* handle_{nullptr};
    bool closed_{false};
    std::deque<std::string> owned_buffers_;
};

struct ZipEntry {
    std::string name;
    bool is_directory{false};
    unsigned mode{0};
};

// Thin RAII wrapper around libzip's zip_t*, used to read a pack archive.
class ZipReader {
public:
    explicit ZipReader(const std::filesystem::path& zip_path);
    ~ZipReader();
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
    ZipReader(ZipReader&&) = delete;
    ZipReader& operator=(ZipReader&&) = delete;

    [[nodiscard]] std::vector<ZipEntry> list_entries() const;

    // Reads one entry's full contents into memory. Throws on failure.
    [[nodiscard]] std::string read_data(const ZipEntry& entry) const;

    // Extracts one entry to dest_path, creating parent directories as
    // needed, and chmod's it to the entry's stored mode bits (restoring a
    // checker binary's +x bit). Throws on failure. dest_path is not
    // validated against zip-slip here — callers must normalize/validate
    // entry.name before deriving dest_path (see unpack_contest).
    void extract_to(const ZipEntry& entry, const std::filesystem::path& dest_path) const;

private:
    zip_t* handle_{nullptr};
};

}  // namespace cxxprobe::pack::detail

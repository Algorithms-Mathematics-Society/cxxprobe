#include "zip_archive.hpp"

#include <sys/stat.h>
#include <zip.h>

#include <fstream>
#include <stdexcept>
#include <utility>

namespace cxxprobe::pack::detail {

namespace {

unsigned unix_mode_of(const std::filesystem::path& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) {
        throw std::runtime_error("zip: stat failed for " + path.string());
    }
    return st.st_mode;
}

[[noreturn]] void throw_zip_error(zip_t* handle, const std::string& context) {
    throw std::runtime_error("zip: " + context + ": " + zip_strerror(handle));
}

}  // namespace

ZipWriter::ZipWriter(const std::filesystem::path& out_path) {
    int err = 0;
    handle_ = zip_open(out_path.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (handle_ == nullptr) {
        zip_error_t zerr;
        zip_error_init_with_code(&zerr, err);
        std::string message = zip_error_strerror(&zerr);
        zip_error_fini(&zerr);
        throw std::runtime_error("zip: failed to create " + out_path.string() + ": " + message);
    }
}

ZipWriter::~ZipWriter() {
    if (handle_ != nullptr && !closed_) {
        // An unclosed writer means either an exception unwound past us or
        // the caller simply never called close() — either way, discard
        // rather than commit a possibly-incomplete archive.
        zip_discard(handle_);
    }
}

void ZipWriter::add_file(const std::string& entry_name, const std::filesystem::path& source_path) {
    zip_source_t* source = zip_source_file(handle_, source_path.c_str(), 0, -1);
    if (source == nullptr) {
        throw_zip_error(handle_, "zip_source_file for " + source_path.string());
    }
    zip_int64_t index = zip_file_add(handle_, entry_name.c_str(), source, ZIP_FL_OVERWRITE);
    if (index < 0) {
        zip_source_free(source);
        throw_zip_error(handle_, "zip_file_add for " + entry_name);
    }
    unsigned mode = unix_mode_of(source_path);
    // External attributes are (unix_mode << 16) | (dos_attrs); OPSYS_UNIX
    // tells readers (including libzip itself) to interpret them that way.
    if (zip_file_set_external_attributes(handle_, static_cast<zip_uint64_t>(index),
                                         ZIP_FL_UNCHANGED, ZIP_OPSYS_UNIX,
                                         static_cast<zip_uint32_t>(mode) << 16) != 0) {
        throw_zip_error(handle_, "zip_file_set_external_attributes for " + entry_name);
    }
}

void ZipWriter::add_data(const std::string& entry_name, std::string data) {
    // zip_source_buffer references this memory rather than copying it, and
    // only reads it back at close() time — keep it alive that long.
    owned_buffers_.push_back(std::move(data));
    const std::string& stored = owned_buffers_.back();
    zip_source_t* source = zip_source_buffer(handle_, stored.data(), stored.size(), 0);
    if (source == nullptr) {
        throw_zip_error(handle_, "zip_source_buffer for " + entry_name);
    }
    zip_int64_t index = zip_file_add(handle_, entry_name.c_str(), source, ZIP_FL_OVERWRITE);
    if (index < 0) {
        zip_source_free(source);
        throw_zip_error(handle_, "zip_file_add for " + entry_name);
    }
    constexpr zip_uint32_t kRegularFileMode = 0100644U << 16;
    if (zip_file_set_external_attributes(handle_, static_cast<zip_uint64_t>(index),
                                         ZIP_FL_UNCHANGED, ZIP_OPSYS_UNIX, kRegularFileMode) != 0) {
        throw_zip_error(handle_, "zip_file_set_external_attributes for " + entry_name);
    }
}

void ZipWriter::close() {
    if (zip_close(handle_) != 0) {
        throw_zip_error(handle_, "zip_close");
    }
    closed_ = true;
}

ZipReader::ZipReader(const std::filesystem::path& zip_path) {
    int err = 0;
    handle_ = zip_open(zip_path.c_str(), ZIP_RDONLY, &err);
    if (handle_ == nullptr) {
        zip_error_t zerr;
        zip_error_init_with_code(&zerr, err);
        std::string message = zip_error_strerror(&zerr);
        zip_error_fini(&zerr);
        throw std::runtime_error("zip: failed to open " + zip_path.string() + ": " + message);
    }
}

ZipReader::~ZipReader() {
    if (handle_ != nullptr) {
        zip_close(handle_);
    }
}

std::vector<ZipEntry> ZipReader::list_entries() const {
    std::vector<ZipEntry> entries;
    zip_int64_t count = zip_get_num_entries(handle_, 0);
    for (zip_int64_t i = 0; i < count; ++i) {
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(handle_, static_cast<zip_uint64_t>(i), 0, &st) != 0) {
            throw_zip_error(handle_, "zip_stat_index");
        }
        ZipEntry entry;
        entry.name = st.name != nullptr ? st.name : "";
        entry.is_directory = !entry.name.empty() && entry.name.back() == '/';

        zip_uint8_t opsys = 0;
        zip_uint32_t attrs = 0;
        if (zip_file_get_external_attributes(handle_, static_cast<zip_uint64_t>(i), 0, &opsys,
                                             &attrs) == 0 &&
            opsys == ZIP_OPSYS_UNIX && attrs != 0) {
            entry.mode = attrs >> 16;
        } else {
            entry.mode = entry.is_directory ? 0755 : 0644;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::string ZipReader::read_data(const ZipEntry& entry) const {
    zip_int64_t index = zip_name_locate(handle_, entry.name.c_str(), 0);
    if (index < 0) {
        throw_zip_error(handle_, "zip_name_locate for " + entry.name);
    }
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat_index(handle_, static_cast<zip_uint64_t>(index), 0, &st) != 0) {
        throw_zip_error(handle_, "zip_stat_index for " + entry.name);
    }

    zip_file_t* file = zip_fopen_index(handle_, static_cast<zip_uint64_t>(index), 0);
    if (file == nullptr) {
        throw_zip_error(handle_, "zip_fopen_index for " + entry.name);
    }
    std::string data(st.size, '\0');
    zip_int64_t read = zip_fread(file, data.data(), data.size());
    zip_fclose(file);
    if (read < 0 || std::cmp_not_equal(read, st.size)) {
        throw std::runtime_error("zip: short read for " + entry.name);
    }
    return data;
}

void ZipReader::extract_to(const ZipEntry& entry, const std::filesystem::path& dest_path) const {
    if (entry.is_directory) {
        std::filesystem::create_directories(dest_path);
        ::chmod(dest_path.c_str(), entry.mode);
        return;
    }
    std::filesystem::create_directories(dest_path.parent_path());
    std::string data = read_data(entry);
    {
        std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("zip: failed to open " + dest_path.string() + " for writing");
        }
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out) {
            throw std::runtime_error("zip: short write for " + dest_path.string());
        }
    }
    ::chmod(dest_path.c_str(), entry.mode);
}

}  // namespace cxxprobe::pack::detail

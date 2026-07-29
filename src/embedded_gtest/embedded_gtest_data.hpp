#pragma once

#include <span>
#include <string_view>

namespace cxxprobe::embedded_gtest {

struct EmbeddedFile {
    std::string_view relative_path;
    std::span<const unsigned char> data;
};

// Generated at build time (see generate_embedded_gtest.py) from the GTest
// Conan package's include/ and lib/ directories used to link
// cxxprobe-cli itself. bundle_hash() identifies this exact set of files —
// used to name the on-disk extraction cache so a rebuild against a
// different GTest version never reuses a stale extraction.
std::string_view bundle_hash();
std::span<const EmbeddedFile> include_files();
std::span<const EmbeddedFile> lib_files();

}  // namespace cxxprobe::embedded_gtest

#include "cxxprobe/bundle.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "cxxprobe/problem.hpp"

namespace cxxprobe::bundle {
namespace {

namespace fs = std::filesystem;

class Sha256 {
public:
    void update(std::span<const std::byte> input) {
        total_bytes_ += input.size();
        for (const std::byte value : input) {
            block_[block_size_++] = std::to_integer<std::uint8_t>(value);
            if (block_size_ == block_.size()) {
                transform();
                block_size_ = 0;
            }
        }
    }

    void update(std::string_view input) {
        update(std::as_bytes(std::span{input.data(), input.size()}));
    }

    std::array<std::uint8_t, 32> finish() {
        const std::uint64_t bit_length = total_bytes_ * 8;
        block_[block_size_++] = 0x80;
        if (block_size_ > 56) {
            std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.end(), 0);
            transform();
            block_size_ = 0;
        }
        std::fill(block_.begin() + static_cast<std::ptrdiff_t>(block_size_), block_.begin() + 56,
                  0);
        for (std::size_t i = 0; i < 8; ++i) {
            block_[63 - i] = static_cast<std::uint8_t>(bit_length >> (i * 8));
        }
        transform();

        std::array<std::uint8_t, 32> digest{};
        for (std::size_t i = 0; i < state_.size(); ++i) {
            digest[i * 4] = static_cast<std::uint8_t>(state_[i] >> 24);
            digest[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
            digest[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
            digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
        }
        return digest;
    }

private:
    static constexpr std::array<std::uint32_t, 64> kRoundConstants{
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};

    static std::uint32_t choose(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        return (x & y) ^ (~x & z);
    }
    static std::uint32_t majority(std::uint32_t x, std::uint32_t y, std::uint32_t z) {
        return (x & y) ^ (x & z) ^ (y & z);
    }
    static std::uint32_t big_sigma0(std::uint32_t x) {
        return std::rotr(x, 2) ^ std::rotr(x, 13) ^ std::rotr(x, 22);
    }
    static std::uint32_t big_sigma1(std::uint32_t x) {
        return std::rotr(x, 6) ^ std::rotr(x, 11) ^ std::rotr(x, 25);
    }
    static std::uint32_t small_sigma0(std::uint32_t x) {
        return std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3);
    }
    static std::uint32_t small_sigma1(std::uint32_t x) {
        return std::rotr(x, 17) ^ std::rotr(x, 19) ^ (x >> 10);
    }

    void transform() {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t pos = i * 4;
            words[i] = (static_cast<std::uint32_t>(block_[pos]) << 24) |
                       (static_cast<std::uint32_t>(block_[pos + 1]) << 16) |
                       (static_cast<std::uint32_t>(block_[pos + 2]) << 8) |
                       static_cast<std::uint32_t>(block_[pos + 3]);
        }
        for (std::size_t i = 16; i < words.size(); ++i) {
            words[i] = small_sigma1(words[i - 2]) + words[i - 7] + small_sigma0(words[i - 15]) +
                       words[i - 16];
        }

        auto [a, b, c, d, e, f, g, h] = state_;
        for (std::size_t i = 0; i < words.size(); ++i) {
            const std::uint32_t temp1 =
                h + big_sigma1(e) + choose(e, f, g) + kRoundConstants[i] + words[i];
            const std::uint32_t temp2 = big_sigma0(a) + majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_{};
    std::uint64_t total_bytes_{};
};

std::string hex_digest(const std::array<std::uint8_t, 32>& digest) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result;
    result.reserve(64);
    for (const std::uint8_t value : digest) {
        result.push_back(digits[value >> 4]);
        result.push_back(digits[value & 0x0f]);
    }
    return result;
}

std::array<std::uint8_t, 32> sha256(std::string_view content) {
    Sha256 hasher;
    hasher.update(content);
    return hasher.finish();
}

bool is_valid_utf8(std::string_view text) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto first = static_cast<unsigned char>(text[pos]);
        std::size_t count = 0;
        std::uint32_t codepoint = 0;
        if (first <= 0x7f) {
            count = 1;
            codepoint = first;
        } else if ((first & 0xe0) == 0xc0) {
            count = 2;
            codepoint = first & 0x1f;
        } else if ((first & 0xf0) == 0xe0) {
            count = 3;
            codepoint = first & 0x0f;
        } else if ((first & 0xf8) == 0xf0) {
            count = 4;
            codepoint = first & 0x07;
        } else {
            return false;
        }
        if (pos + count > text.size()) {
            return false;
        }
        for (std::size_t i = 1; i < count; ++i) {
            const auto byte = static_cast<unsigned char>(text[pos + i]);
            if ((byte & 0xc0) != 0x80) {
                return false;
            }
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        const bool overlong = (count == 2 && codepoint < 0x80) ||
                              (count == 3 && codepoint < 0x800) ||
                              (count == 4 && codepoint < 0x10000);
        if (overlong || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
            codepoint < 0x20 || codepoint == 0x7f) {
            return false;
        }
        pos += count;
    }
    return true;
}

std::string ascii_lower(std::string_view value) {
    std::string lowered{value};
    std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) {
        if (ch >= 'A' && ch <= 'Z') {
            return static_cast<char>(ch + ('a' - 'A'));
        }
        return static_cast<char>(ch);
    });
    return lowered;
}

bool is_windows_reserved(std::string_view component) {
    std::string base = ascii_lower(component.substr(0, component.find('.')));
    static const std::set<std::string> names{
        "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5", "com6", "com7",
        "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    return names.contains(base);
}

std::string validate_path(std::string path, const ValidationLimits& limits) {
    if (path.empty() || path.front() == '/' || path.size() > limits.max_path_bytes ||
        !is_valid_utf8(path)) {
        throw std::runtime_error{std::format("unsafe bundle path '{}'", path)};
    }
    std::size_t depth = 0;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string_view component = std::string_view{path}.substr(
            start, end == std::string::npos ? path.size() - start : end - start);
        ++depth;
        if (component.empty() || component == "." || component == ".." ||
            component.size() > limits.max_component_bytes || component.back() == '.' ||
            component.back() == ' ' || is_windows_reserved(component) ||
            component.find_first_of("\\<>:\"|?*") != std::string_view::npos) {
            throw std::runtime_error{std::format("unsafe bundle path '{}'", path)};
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    if (depth > limits.max_depth) {
        throw std::runtime_error{std::format("bundle path exceeds maximum depth: '{}'", path)};
    }
    return path;
}

void validate_reference(std::string_view reference, const ValidationLimits& limits,
                        std::string_view field) {
    try {
        validate_path(std::string{reference}, limits);
    } catch (const std::exception&) {
        throw std::runtime_error{
            std::format("problem.yaml {} contains unsafe path '{}'", field, reference)};
    }
}

struct HashedFile {
    std::uint64_t size_bytes{};
    std::array<std::uint8_t, 32> digest{};
};

HashedFile hash_file(const fs::path& path, std::uint64_t max_bytes) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        throw std::runtime_error{
            std::format("cannot open '{}': {}", path.string(), std::strerror(errno))};
    }
    struct FdGuard {
        int fd;
        ~FdGuard() { ::close(fd); }
    } guard{fd};

    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0) {
        throw std::runtime_error{
            std::format("bundle entry is not a regular file: '{}'", path.string())};
    }
    if (info.st_nlink != 1) {
        throw std::runtime_error{std::format(
            "multiply-linked regular files are not allowed in bundles: '{}'", path.string())};
    }
    const auto size = static_cast<std::uint64_t>(info.st_size);
    if (size > max_bytes) {
        throw std::runtime_error{
            std::format("bundle file exceeds {} bytes: '{}'", max_bytes, path.string())};
    }

    Sha256 hasher;
    std::array<std::byte, 64 * 1024> buffer{};
    std::uint64_t consumed = 0;
    while (true) {
        const ssize_t count = ::read(fd, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            throw std::runtime_error{
                std::format("cannot read '{}': {}", path.string(), std::strerror(errno))};
        }
        if (count == 0) {
            break;
        }
        consumed += static_cast<std::uint64_t>(count);
        if (consumed > max_bytes) {
            throw std::runtime_error{
                std::format("bundle file exceeds {} bytes: '{}'", max_bytes, path.string())};
        }
        hasher.update(std::span{buffer}.first(static_cast<std::size_t>(count)));
    }
    if (consumed != size) {
        throw std::runtime_error{
            std::format("bundle file changed while hashing: '{}'", path.string())};
    }
    struct stat final_info {};
    if (::fstat(fd, &final_info) != 0 || final_info.st_dev != info.st_dev ||
        final_info.st_ino != info.st_ino || final_info.st_size != info.st_size ||
        final_info.st_nlink != 1) {
        throw std::runtime_error{
            std::format("bundle file changed while hashing: '{}'", path.string())};
    }
    return {.size_bytes = size, .digest = hasher.finish()};
}

void validate_contest_yaml(const fs::path& path) {
    YAML::Node doc = YAML::LoadFile(path.string());
    if (!doc.IsMap()) {
        throw std::runtime_error{"contest.yaml must be a mapping"};
    }
    const int version = doc["version"] ? doc["version"].as<int>() : 0;
    if (version != 1) {
        throw std::runtime_error{
            std::format("unsupported contest.yaml version {} (expected 1)", version)};
    }
    if (!doc["name"] || !doc["name"].IsScalar() || doc["name"].as<std::string>().empty()) {
        throw std::runtime_error{"contest.yaml missing required non-empty 'name'"};
    }
}

void require_regular_file(const fs::path& problem_dir, std::string_view reference,
                          std::string_view field) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(problem_dir / reference, error);
    if (error || !fs::is_regular_file(status)) {
        throw std::runtime_error{
            std::format("problem.yaml {} file is missing or not regular: '{}'", field, reference)};
    }
}

void validate_problem_references(const problem::ProblemConfig& config,
                                 const ValidationLimits& limits) {
    validate_reference(config.statement, limits, "statement");
    require_regular_file(config.problem_dir, config.statement, "statement");
    validate_reference(config.solution_file, limits, "solution.file");
    require_regular_file(config.problem_dir, config.solution_file, "solution.file");
    for (const auto& source : config.compiler.extra_sources) {
        validate_reference(source, limits, "compiler.extra_sources");
        require_regular_file(config.problem_dir, source, "compiler.extra_sources");
    }
    if (config.tests.manifest) {
        validate_reference(*config.tests.manifest, limits, "tests.manifest");
        if (config.tests.enabled) {
            require_regular_file(config.problem_dir, *config.tests.manifest, "tests.manifest");
        }
    } else {
        validate_reference(config.tests.dir, limits, "tests.dir");
        if (config.tests.enabled && !fs::is_directory(config.problem_dir / config.tests.dir)) {
            throw std::runtime_error{
                std::format("problem.yaml tests.dir is missing or not a "
                            "directory: '{}'",
                            config.tests.dir)};
        }
    }
    if (config.tests.checker) {
        validate_reference(*config.tests.checker, limits, "tests.checker");
        require_regular_file(config.problem_dir, *config.tests.checker, "tests.checker");
    }
    validate_reference(config.behavior.checker_file, limits, "behavior.checker_file");
    if (config.behavior.enabled) {
        require_regular_file(config.problem_dir, config.behavior.checker_file,
                             "behavior.checker_file");
    }
}

bool is_valid_public_text(std::string_view text) {
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto first = static_cast<unsigned char>(text[pos]);
        std::size_t count = 0;
        std::uint32_t codepoint = 0;
        if (first <= 0x7f) {
            count = 1;
            codepoint = first;
        } else if ((first & 0xe0) == 0xc0) {
            count = 2;
            codepoint = first & 0x1f;
        } else if ((first & 0xf0) == 0xe0) {
            count = 3;
            codepoint = first & 0x0f;
        } else if ((first & 0xf8) == 0xf0) {
            count = 4;
            codepoint = first & 0x07;
        } else {
            return false;
        }
        if (pos + count > text.size()) {
            return false;
        }
        for (std::size_t i = 1; i < count; ++i) {
            const auto byte = static_cast<unsigned char>(text[pos + i]);
            if ((byte & 0xc0) != 0x80) {
                return false;
            }
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        const bool overlong = (count == 2 && codepoint < 0x80) ||
                              (count == 3 && codepoint < 0x800) ||
                              (count == 4 && codepoint < 0x10000);
        const bool disallowed_control =
            (codepoint < 0x20 && codepoint != '\t' && codepoint != '\n' && codepoint != '\r') ||
            (codepoint >= 0x80 && codepoint <= 0x9f);
        if (overlong || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
            disallowed_control || codepoint == 0x7f) {
            return false;
        }
        pos += count;
    }
    return true;
}

std::string read_public_file(const fs::path& path, std::uint64_t max_bytes,
                             std::string_view field) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        throw std::runtime_error{
            std::format("cannot open public {} '{}': {}", field, path.string(),
                        std::strerror(errno))};
    }
    struct FdGuard {
        int fd;
        ~FdGuard() { ::close(fd); }
    } guard{fd};

    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size < 0 ||
        info.st_nlink != 1) {
        throw std::runtime_error{
            std::format("public {} is not a single-linked regular file: '{}'", field,
                        path.string())};
    }
    const auto size = static_cast<std::uint64_t>(info.st_size);
    if (size == 0 || size > max_bytes) {
        throw std::runtime_error{std::format("public {} must contain 1..{} bytes: '{}'", field,
                                             max_bytes, path.string())};
    }
    std::string content(static_cast<std::size_t>(size), '\0');
    std::size_t consumed = 0;
    while (consumed < content.size()) {
        const ssize_t count = ::read(fd, content.data() + consumed, content.size() - consumed);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            throw std::runtime_error{
                std::format("cannot read complete public {} '{}'", field, path.string())};
        }
        consumed += static_cast<std::size_t>(count);
    }
    struct stat final_info {};
    if (::fstat(fd, &final_info) != 0 || final_info.st_dev != info.st_dev ||
        final_info.st_ino != info.st_ino || final_info.st_size != info.st_size ||
        final_info.st_nlink != 1) {
        throw std::runtime_error{
            std::format("public {} changed while reading: '{}'", field, path.string())};
    }
    return content;
}

bool same_or_child_path(std::string_view path, std::string_view parent) {
    return path == parent ||
           (path.size() > parent.size() && path.starts_with(parent) &&
            path[parent.size()] == '/');
}

void reject_private_overlap(const problem::ProblemConfig& config, std::string_view path,
                            std::string_view role) {
    auto reject_exact = [&](std::string_view secret, std::string_view label) {
        if (!secret.empty() && path == secret) {
            throw std::runtime_error{std::format("public {} '{}' overlaps private {}", role, path,
                                                 label)};
        }
    };
    reject_exact("problem.yaml", "problem configuration");
    reject_exact(config.solution_file, "solution.file");
    for (const auto& source : config.compiler.extra_sources) {
        reject_exact(source, "compiler.extra_sources");
    }
    if (config.tests.manifest) {
        reject_exact(*config.tests.manifest, "tests.manifest");
    }
    if (same_or_child_path(path, config.tests.dir)) {
        throw std::runtime_error{
            std::format("public {} '{}' overlaps private tests.dir", role, path)};
    }
    if (config.tests.checker) {
        reject_exact(*config.tests.checker, "tests.checker");
    }
    reject_exact(config.behavior.checker_file, "behavior.checker_file");
}

const FileRecord& require_manifest_file(const Manifest& manifest, std::string_view path,
                                        std::string_view role) {
    const auto found = std::ranges::find(manifest.files, path, &FileRecord::path);
    if (found == manifest.files.end()) {
        throw std::runtime_error{
            std::format("public {} is not an included bundle file: '{}'", role, path)};
    }
    return *found;
}

std::string bundle_path(const problem::ProblemConfig& config, std::string_view relative) {
    return config.slug + "/" + std::string{relative};
}

void require_public_subpath(std::string_view path, std::string_view role) {
    if (!path.starts_with("public/") || path.size() == std::string_view{"public/"}.size()) {
        throw std::runtime_error{
            std::format("public {} must be stored under the public/ directory: '{}'", role, path)};
    }
}

void validate_raster_asset(std::string_view path, std::string_view media_type,
                           std::string_view content) {
    const std::string extension = fs::path{path}.extension().string();
    bool matches = false;
    if (media_type == "image/png" && extension == ".png") {
        constexpr std::array<unsigned char, 8> signature{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a,
                                                         0x0a};
        matches = content.size() >= signature.size() &&
                  std::equal(signature.begin(), signature.end(), content.begin(),
                             [](unsigned char expected, char actual) {
                                 return expected == static_cast<unsigned char>(actual);
                             });
    } else if (media_type == "image/jpeg" &&
               (extension == ".jpg" || extension == ".jpeg")) {
        matches = content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xff &&
                  static_cast<unsigned char>(content[1]) == 0xd8 &&
                  static_cast<unsigned char>(content[2]) == 0xff;
    } else if (media_type == "image/webp" && extension == ".webp") {
        matches = content.size() >= 12 && content.substr(0, 4) == "RIFF" &&
                  content.substr(8, 4) == "WEBP";
    }
    if (!matches) {
        throw std::runtime_error{std::format(
            "public asset '{}' has unsupported or mismatched media type '{}'", path, media_type)};
    }
}

PublicRecord validate_public_files(const problem::ProblemConfig& config, const Manifest& manifest,
                                   const ValidationLimits& limits) {
    PublicRecord result;
    if (config.version == 1) {
        return result;
    }

    if (config.public_files.assets.size() > limits.max_public_assets) {
        throw std::runtime_error{std::format("public assets exceed maximum count {}",
                                             limits.max_public_assets)};
    }
    std::set<std::string> roles;
    std::uint64_t total = 0;
    auto account = [&](const FileRecord& file, std::string_view role) {
        if (file.size_bytes > limits.max_public_total_bytes - total) {
            throw std::runtime_error{std::format("public files exceed maximum total size {} bytes",
                                                 limits.max_public_total_bytes)};
        }
        total += file.size_bytes;
        if (!roles.insert(ascii_lower(file.path)).second) {
            throw std::runtime_error{
                std::format("bundle file has multiple public roles: '{}'", file.path)};
        }
        (void)role;
    };

    if (config.public_files.statement) {
        reject_private_overlap(config, config.statement, "statement");
        const std::string path = bundle_path(config, config.statement);
        const FileRecord& file = require_manifest_file(manifest, path, "statement");
        const std::string content = read_public_file(config.problem_dir / config.statement,
                                                     limits.max_public_statement_bytes,
                                                     "statement");
        if (!is_valid_public_text(content)) {
            throw std::runtime_error{"public statement must be valid UTF-8 text"};
        }
        account(file, "statement");
        result.statement = path;
    }

    for (const auto& asset : config.public_files.assets) {
        validate_reference(asset.path, limits, "public.assets.path");
        require_public_subpath(asset.path, "asset");
        reject_private_overlap(config, asset.path, "asset");
        require_regular_file(config.problem_dir, asset.path, "public.assets.path");
        const std::string path = bundle_path(config, asset.path);
        const FileRecord& file = require_manifest_file(manifest, path, "asset");
        const std::string content = read_public_file(config.problem_dir / asset.path,
                                                     limits.max_public_asset_bytes, "asset");
        validate_raster_asset(asset.path, asset.media_type, content);
        account(file, "asset");
        result.assets.push_back({.path = path, .media_type = asset.media_type});
    }
    std::ranges::sort(result.assets, {}, &PublicAssetRecord::path);

    if (config.public_files.starter) {
        const auto& starter = *config.public_files.starter;
        validate_reference(starter.path, limits, "public.starter.path");
        require_public_subpath(starter.path, "starter");
        reject_private_overlap(config, starter.path, "starter");
        if (starter.language != "cpp" || fs::path{starter.path}.extension() != ".cpp") {
            throw std::runtime_error{
                "public starter must use language 'cpp' and a lowercase .cpp path"};
        }
        require_regular_file(config.problem_dir, starter.path, "public.starter.path");
        const std::string path = bundle_path(config, starter.path);
        const FileRecord& file = require_manifest_file(manifest, path, "starter");
        const std::string content = read_public_file(config.problem_dir / starter.path,
                                                     limits.max_public_starter_bytes, "starter");
        if (!is_valid_public_text(content)) {
            throw std::runtime_error{"public starter must be valid UTF-8 text"};
        }
        account(file, "starter");
        result.starter = PublicStarterRecord{.path = path, .language = starter.language};
    }
    return result;
}

nlohmann::ordered_json manifest_json(const Manifest& manifest, bool include_digest) {
    if (manifest.schema_version != kSchemaVersionV1 &&
        manifest.schema_version != kSchemaVersionV2) {
        throw std::runtime_error{
            std::format("unsupported bundle manifest schema {}", manifest.schema_version)};
    }
    nlohmann::ordered_json problems = nlohmann::ordered_json::array();
    for (const auto& problem : manifest.problems) {
        nlohmann::ordered_json compiler{
            {"cxx", problem.execution.compiler.cxx},
            {"std_flag", problem.execution.compiler.std_flag},
            {"flags", problem.execution.compiler.flags},
            {"extra_sources", problem.execution.compiler.extra_sources}};
        nlohmann::ordered_json limits{{"memory_bytes", problem.execution.limits.memory_bytes},
                                      {"cpu_time_ms", problem.execution.limits.cpu_time_ms},
                                      {"wall_time_ms", problem.execution.limits.wall_time_ms},
                                      {"max_pids", problem.execution.limits.max_pids}};
        nlohmann::ordered_json problem_json{{"slug", problem.slug}, {"name", problem.name}};
        if (manifest.schema_version == kSchemaVersionV2) {
            nlohmann::ordered_json assets = nlohmann::ordered_json::array();
            for (const auto& asset : problem.public_files.assets) {
                assets.push_back({{"path", asset.path}, {"media_type", asset.media_type}});
            }
            nlohmann::ordered_json public_json;
            public_json["statement"] = problem.public_files.statement
                                           ? nlohmann::ordered_json(*problem.public_files.statement)
                                           : nlohmann::ordered_json(nullptr);
            public_json["assets"] = std::move(assets);
            if (problem.public_files.starter) {
                public_json["starter"] =
                    {{"path", problem.public_files.starter->path},
                     {"language", problem.public_files.starter->language}};
            } else {
                public_json["starter"] = nullptr;
            }
            problem_json["public"] = std::move(public_json);
        }
        problem_json["execution"] =
            {{"compiler", std::move(compiler)}, {"limits", std::move(limits)}};
        problems.push_back(std::move(problem_json));
    }
    nlohmann::ordered_json files = nlohmann::ordered_json::array();
    for (const auto& file : manifest.files) {
        files.push_back(
            {{"path", file.path}, {"size_bytes", file.size_bytes}, {"sha256", file.sha256}});
    }
    nlohmann::ordered_json result{{"contract", kContract},
                                  {"schema_version", manifest.schema_version},
                                  {"valid", true},
                                  {"hash_algorithm", "sha256"}};
    if (include_digest) {
        result["bundle_sha256"] = manifest.bundle_sha256;
    }
    result["file_count"] = manifest.files.size();
    result["total_bytes"] = manifest.total_bytes;
    result["problems"] = std::move(problems);
    result["files"] = std::move(files);
    return result;
}

}  // namespace

Manifest validate(const fs::path& contest_dir, const ValidationLimits& limits) {
    if (limits.max_files == 0 || limits.max_file_bytes == 0 || limits.max_total_bytes == 0 ||
        limits.max_path_bytes == 0 || limits.max_component_bytes == 0 || limits.max_depth == 0 ||
        limits.max_public_assets == 0 || limits.max_public_statement_bytes == 0 ||
        limits.max_public_starter_bytes == 0 || limits.max_public_asset_bytes == 0 ||
        limits.max_public_total_bytes == 0) {
        throw std::runtime_error{"bundle validation limits must be non-zero"};
    }

    const fs::path root = fs::absolute(contest_dir).lexically_normal();
    const fs::file_status root_status = fs::symlink_status(root);
    if (fs::is_symlink(root_status) || !fs::is_directory(root_status)) {
        throw std::runtime_error{
            std::format("bundle root is not a real directory: '{}'", root.string())};
    }

    std::vector<std::pair<std::string, fs::path>> regular_files;
    std::set<std::string> portable_paths;
    std::error_code iterator_error;
    fs::recursive_directory_iterator iterator{root, fs::directory_options::none, iterator_error};
    const fs::recursive_directory_iterator end;
    if (iterator_error) {
        throw std::runtime_error{std::format("cannot enumerate bundle '{}': {}", root.string(),
                                             iterator_error.message())};
    }
    while (iterator != end) {
        const fs::directory_entry entry = *iterator;
        const fs::file_status status = entry.symlink_status(iterator_error);
        if (iterator_error) {
            throw std::runtime_error{std::format("cannot inspect bundle entry '{}': {}",
                                                 entry.path().string(), iterator_error.message())};
        }
        const fs::path relative = entry.path().lexically_relative(root);
        std::string path = validate_path(relative.generic_string(), limits);
        if (!portable_paths.insert(ascii_lower(path)).second) {
            throw std::runtime_error{std::format("case-colliding bundle path: '{}'", path)};
        }
        if (fs::is_symlink(status)) {
            iterator.disable_recursion_pending();
            throw std::runtime_error{
                std::format("symlinks are not allowed in bundles: '{}'", path)};
        }
        if (fs::is_regular_file(status)) {
            regular_files.emplace_back(std::move(path), entry.path());
            if (regular_files.size() > limits.max_files) {
                throw std::runtime_error{
                    std::format("bundle exceeds maximum file count {}", limits.max_files)};
            }
        } else if (!fs::is_directory(status)) {
            iterator.disable_recursion_pending();
            throw std::runtime_error{
                std::format("special files are not allowed in bundles: '{}'", path)};
        }
        iterator.increment(iterator_error);
        if (iterator_error) {
            throw std::runtime_error{std::format("cannot enumerate bundle '{}': {}", root.string(),
                                                 iterator_error.message())};
        }
    }

    std::ranges::sort(regular_files, {}, &std::pair<std::string, fs::path>::first);
    if (!std::ranges::any_of(regular_files,
                             [](const auto& file) { return file.first == "contest.yaml"; })) {
        throw std::runtime_error{"bundle is missing contest.yaml"};
    }
    validate_contest_yaml(root / "contest.yaml");

    Manifest manifest;
    for (const auto& [path, absolute_path] : regular_files) {
        HashedFile hashed = hash_file(absolute_path, limits.max_file_bytes);
        if (hashed.size_bytes > limits.max_total_bytes - manifest.total_bytes) {
            throw std::runtime_error{
                std::format("bundle exceeds maximum total size {} bytes", limits.max_total_bytes)};
        }
        manifest.total_bytes += hashed.size_bytes;
        manifest.files.push_back(
            {.path = path, .size_bytes = hashed.size_bytes, .sha256 = hex_digest(hashed.digest)});
    }

    for (const auto& file : manifest.files) {
        const fs::path relative{file.path};
        if (relative.filename() != "problem.yaml") {
            continue;
        }
        if (std::distance(relative.begin(), relative.end()) != 2) {
            throw std::runtime_error{std::format(
                "problem.yaml must belong to one direct-child problem directory: '{}'", file.path)};
        }
        problem::ProblemConfig config = problem::load_from_dir(root / relative.parent_path());
        validate_problem_references(config, limits);
        manifest.schema_version =
            config.version == 2 ? kSchemaVersionV2 : kSchemaVersionV1;
        PublicRecord public_files = validate_public_files(config, manifest, limits);
        if (!config.tests.enabled && !config.symbolic.enabled && !config.behavior.enabled) {
            throw std::runtime_error{
                "publishable question must enable at least one manual, symbolic, or behavior "
                "check"};
        }
        const problem::ProjectDefaults defaults;
        const problem::ResolvedCompiler compiler =
            problem::resolve_compiler(config.compiler, defaults);
        const cxxprobe::sandbox::Limits execution_limits =
            problem::resolve_limits(config.limits, defaults);
        manifest.problems.push_back(
            {.slug = config.slug,
             .name = config.name,
             .public_files = std::move(public_files),
             .execution = {.compiler = {.cxx = compiler.cxx,
                                        .std_flag = compiler.std_flag,
                                        .flags = compiler.flags,
                                        .extra_sources = compiler.extra_sources},
                           .limits = {.memory_bytes = execution_limits.memory_bytes,
                                      .cpu_time_ms = execution_limits.cpu.count(),
                                      .wall_time_ms = execution_limits.wall.count(),
                                      .max_pids = execution_limits.max_pids}}});
    }
    std::ranges::sort(manifest.problems, {}, &ProblemRecord::slug);
    if (manifest.problems.size() != 1) {
        throw std::runtime_error{
            std::format("question bundle must contain exactly one direct-child problem (found {})",
                        manifest.problems.size())};
    }
    const std::string canonical_content = manifest_json(manifest, false).dump();
    manifest.bundle_sha256 = hex_digest(sha256(canonical_content));
    return manifest;
}

nlohmann::ordered_json to_json(const Manifest& manifest) { return manifest_json(manifest, true); }

}  // namespace cxxprobe::bundle

#include "cxxprobe/cases.hpp"

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <format>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace cxxprobe::cases {

namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxOutputBytes = 4ULL * 1024ULL * 1024ULL;

// Numeric-aware sort: numeric runs compared by value, not lexicographically,
// so "10" sorts after "2".
bool natural_less(const std::string& a, const std::string& b) {
    std::size_t ia = 0;
    std::size_t ib = 0;
    while (ia < a.size() && ib < b.size()) {
        bool da = std::isdigit(static_cast<unsigned char>(a[ia])) != 0;
        bool db = std::isdigit(static_cast<unsigned char>(b[ib])) != 0;
        if (da && db) {
            std::size_t ea = ia;
            std::size_t eb = ib;
            while (ea < a.size() && std::isdigit(static_cast<unsigned char>(a[ea])) != 0) {
                ++ea;
            }
            while (eb < b.size() && std::isdigit(static_cast<unsigned char>(b[eb])) != 0) {
                ++eb;
            }
            std::string_view na = std::string_view{a}.substr(ia, ea - ia);
            std::string_view nb = std::string_view{b}.substr(ib, eb - ib);
            if (na.size() != nb.size()) {
                return na.size() < nb.size();
            }
            if (na != nb) {
                return na < nb;
            }
            ia = ea;
            ib = eb;
        } else {
            if (a[ia] != b[ib]) {
                return a[ia] < b[ib];
            }
            ++ia;
            ++ib;
        }
    }
    return a.size() < b.size();
}

std::string read_file(const fs::path& p) {
    std::ifstream ifs{p, std::ios::binary};
    if (!ifs) {
        throw std::runtime_error{std::format("cannot open: {}", p.string())};
    }
    return {std::istreambuf_iterator<char>{ifs}, {}};
}

struct TempFile {
    std::string path;

    explicit TempFile(std::string_view content = {}) {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        char tmpl[] = "/tmp/cxxprobe-XXXXXX";
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        int fd = ::mkstemp(tmpl);
        if (fd < 0) {
            throw std::runtime_error{std::format("mkstemp: {}", std::strerror(errno))};
        }
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
        path = tmpl;
        if (!content.empty()) {
            ssize_t written = ::write(fd, content.data(), content.size());
            if (written < 0 || static_cast<std::size_t>(written) != content.size()) {
                ::close(fd);
                ::unlink(path.c_str());
                throw std::runtime_error{"TempFile: write failed"};
            }
        }
        ::close(fd);
    }

    ~TempFile() {
        if (!path.empty()) {
            ::unlink(path.c_str());
        }
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&& other) noexcept : path{std::exchange(other.path, {})} {}
    TempFile& operator=(TempFile&&) = delete;
};

bool run_checker(const std::string& checker_bin, const std::string& input_path,
                 const std::string& output_path, const std::string& answer_path) {
    pid_t pid = ::fork();
    if (pid < 0) {
        throw std::runtime_error{std::format("fork: {}", std::strerror(errno))};
    }
    if (pid == 0) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::close(devnull);
        }
        std::array<const char*, 5> exec_argv{
            checker_bin.c_str(),
            input_path.c_str(),
            output_path.c_str(),
            answer_path.c_str(),
            nullptr,
        };
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        ::execv(checker_bin.c_str(), const_cast<char* const*>(exec_argv.data()));
        ::_exit(127);
    }
    int wstatus = 0;
    ::waitpid(pid, &wstatus, 0);
    return WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0;
}

}  // namespace

const char* verdict_str(Verdict v) {
    switch (v) {
        case Verdict::AC:
            return "AC";
        case Verdict::WA:
            return "WA";
        case Verdict::TLE:
            return "TLE";
        case Verdict::MLE:
            return "MLE";
        case Verdict::OLE:
            return "OLE";
        case Verdict::RE:
            return "RE";
    }
    return "?";
}

bool token_equal(std::string_view a, std::string_view b) {
    auto tokenize = [](std::string_view s) {
        std::vector<std::string_view> toks;
        std::size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
                ++i;
            }
            if (i >= s.size()) {
                break;
            }
            std::size_t j = i;
            while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) {
                ++j;
            }
            toks.push_back(s.substr(i, j - i));
            i = j;
        }
        return toks;
    };
    return tokenize(a) == tokenize(b);
}

std::vector<TestCase> load_cases_dir(const fs::path& dir) {
    if (!fs::is_directory(dir)) {
        throw std::runtime_error{std::format("not a directory: {}", dir.string())};
    }
    std::vector<std::pair<std::string, fs::path>> in_files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".in") {
            in_files.emplace_back(entry.path().stem().string(), entry.path());
        }
    }
    std::ranges::sort(in_files,
                      [](const auto& x, const auto& y) { return natural_less(x.first, y.first); });

    std::vector<TestCase> result;
    result.reserve(in_files.size());
    for (auto& [stem, in_path] : in_files) {
        TestCase tc;
        tc.label = stem;
        tc.input_data = read_file(in_path);
        for (const char* ext : {".ans", ".out"}) {
            fs::path ans_path = in_path;
            ans_path.replace_extension(ext);
            if (fs::exists(ans_path)) {
                tc.answer_data = read_file(ans_path);
                break;
            }
        }
        result.push_back(std::move(tc));
    }
    return result;
}

std::vector<TestCase> load_cases_manifest(const fs::path& manifest_path) {
    YAML::Node doc = YAML::LoadFile(manifest_path.string());
    if (!doc.IsSequence()) {
        throw std::runtime_error{"manifest must be a YAML/JSON array"};
    }
    const fs::path base = manifest_path.parent_path();
    std::vector<TestCase> result;
    int idx = 1;
    for (const auto& node : doc) {
        TestCase tc;
        tc.label = node["label"] ? node["label"].as<std::string>() : std::to_string(idx);
        ++idx;

        if (node["input"]) {
            fs::path p{node["input"].as<std::string>()};
            if (p.is_relative()) {
                p = base / p;
            }
            tc.input_data = read_file(p);
        } else if (node["input_data"]) {
            tc.input_data = node["input_data"].as<std::string>();
        }

        if (node["answer"]) {
            fs::path p{node["answer"].as<std::string>()};
            if (p.is_relative()) {
                p = base / p;
            }
            tc.answer_data = read_file(p);
        } else if (node["answer_data"]) {
            tc.answer_data = node["answer_data"].as<std::string>();
        }

        result.push_back(std::move(tc));
    }
    return result;
}

std::vector<TestCase> load_cases(const fs::path& path) {
    const std::string ext = path.extension().string();
    if (ext == ".json" || ext == ".yaml" || ext == ".yml") {
        return load_cases_manifest(path);
    }
    return load_cases_dir(path);
}

bool check_output(const std::string& checker_bin, const std::string& input_data,
                  const cxxprobe::sandbox::Result& result, const std::string& answer_data) {
    if (checker_bin.empty()) {
        return token_equal(result.stdout_data, answer_data);
    }
    TempFile input_tmp{input_data};
    TempFile output_tmp{result.stdout_data};
    TempFile answer_tmp{answer_data};
    return run_checker(checker_bin, input_tmp.path, output_tmp.path, answer_tmp.path);
}

Verdict compute_verdict(const cxxprobe::sandbox::Result& result, const cxxprobe::sandbox::Limits& limits,
                        bool checker_ac) {
    if (result.wall_timed_out || result.cpu_time >= limits.cpu) {
        return Verdict::TLE;
    }
    if (limits.memory_bytes > 0 && result.peak_memory_bytes >= limits.memory_bytes) {
        return Verdict::MLE;
    }
    if (result.stdout_data.size() >= kMaxOutputBytes) {
        return Verdict::OLE;
    }
    if (result.exit_code != 0) {
        return Verdict::RE;
    }
    return checker_ac ? Verdict::AC : Verdict::WA;
}

}  // namespace cxxprobe::cases

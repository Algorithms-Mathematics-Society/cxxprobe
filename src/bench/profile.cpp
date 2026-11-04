#include "detail/profile.hpp"

#include <fcntl.h>
#include <linux/perf_event.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/personality.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

// This file talks to the kernel directly: perf_event ring buffers (unions,
// raw pointers), fork/exec, and C varargs syscalls. The checks below are
// suppressed for that reason and only here.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-vararg,cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-type-const-cast)
namespace cxxprobe::bench::detail {

namespace fs = std::filesystem;

namespace {

constexpr std::uint64_t kSampleFrequency = 4999;  // Hz; odd, to avoid lockstep with timers
constexpr std::size_t kDataPages = 256;           // 1 MiB ring buffer
constexpr std::size_t kTopSymbols = 5;

// stdout of argv, run directly (no shell). Empty on failure.
std::string capture(const std::vector<std::string>& argv) {
    std::array<int, 2> fds{};
    if (::pipe2(fds.data(), O_CLOEXEC) != 0) {
        return {};
    }
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::dup2(fds[1], STDOUT_FILENO);
        const int devnull = ::open("/dev/null", O_WRONLY);
        ::dup2(devnull, STDERR_FILENO);
        std::vector<char*> raw;
        raw.reserve(argv.size() + 1);
        for (const auto& arg : argv) {
            raw.push_back(const_cast<char*>(arg.c_str()));
        }
        raw.push_back(nullptr);
        ::execvp(raw[0], raw.data());
        ::_exit(127);
    }
    ::close(fds[1]);
    std::string out;
    std::array<char, 4096> buf{};
    for (ssize_t got = 0; (got = ::read(fds[0], buf.data(), buf.size())) > 0;) {
        out.append(buf.data(), static_cast<std::size_t>(got));
    }
    ::close(fds[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);
    return out;
}

struct Symbol {
    std::uint64_t address{0};
    std::uint64_t size{0};
    std::string name;
};

// Function symbols of the linked binary, sorted by address.
std::vector<Symbol> binary_symbols(const fs::path& binary) {
    std::vector<Symbol> symbols;
    std::istringstream lines{capture({"nm", "-C", "--defined-only", "-S", "-n", binary.string()})};
    for (std::string line; std::getline(lines, line);) {
        std::istringstream fields{line};
        std::string address;
        std::string size;
        std::string type;
        if (!(fields >> address >> size >> type) || type.size() != 1 ||
            std::string_view{"TtWw"}.find(type[0]) == std::string_view::npos) {
            continue;
        }
        std::string name;
        std::getline(fields >> std::ws, name);
        symbols.push_back({.address = std::stoull(address, nullptr, 16),
                           .size = std::stoull(size, nullptr, 16),
                           .name = std::move(name)});
    }
    return symbols;
}

// Symbol name -> owner. A name several owners define (an inline function from
// a shared header, say) belongs to "shared": no one part wrote it.
std::unordered_map<std::string, std::string> owners_by_symbol(
    const std::vector<cxxprobe::judge::detail::OwnedObject>& objects) {
    std::unordered_map<std::string, std::string> owners;
    for (const auto& unit : objects) {
        std::istringstream lines{capture({"nm", "-C", "--defined-only", unit.object.string()})};
        for (std::string line; std::getline(lines, line);) {
            std::istringstream fields{line};
            std::string address;
            std::string type;
            if (!(fields >> address >> type) || type.size() != 1 ||
                std::string_view{"TtWw"}.find(type[0]) == std::string_view::npos) {
                continue;
            }
            std::string name;
            std::getline(fields >> std::ws, name);
            auto [it, inserted] = owners.emplace(name, unit.owner);
            if (!inserted && it->second != unit.owner) {
                it->second = "shared";
            }
        }
    }
    return owners;
}

const Symbol* lookup(const std::vector<Symbol>& symbols, std::uint64_t address) {
    auto it = std::ranges::upper_bound(symbols, address, {}, &Symbol::address);
    if (it == symbols.begin()) {
        return nullptr;
    }
    --it;
    if (it->size > 0 ? address < it->address + it->size : true) {
        return &*it;
    }
    return nullptr;
}

int open_sampler(pid_t pid) {
    perf_event_attr attr{};
    attr.size = sizeof(attr);
    attr.type = PERF_TYPE_SOFTWARE;
    attr.config = PERF_COUNT_SW_CPU_CLOCK;
    attr.freq = 1;
    attr.sample_freq = kSampleFrequency;
    attr.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_CALLCHAIN;
    attr.disabled = 1;
    attr.enable_on_exec = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.exclude_callchain_kernel = 1;
    attr.wakeup_events = 1;
    return static_cast<int>(::syscall(SYS_perf_event_open, &attr, pid, -1, -1, 0UL));
}

// Reads every complete record between tail and head, handling wraparound.
class RingReader {
public:
    RingReader(void* base, std::size_t page) : base_{static_cast<char*>(base)}, page_{page} {}

    template <typename OnSample>
    void drain(OnSample& on_sample) {
        auto* meta = reinterpret_cast<perf_event_mmap_page*>(base_);
        const std::uint64_t head = __atomic_load_n(&meta->data_head, __ATOMIC_ACQUIRE);
        std::uint64_t tail = meta->data_tail;
        const std::size_t size = kDataPages * page_;
        char* data = base_ + page_;
        std::vector<char> record;
        while (tail < head) {
            perf_event_header header{};
            copy(data, size, tail, &header, sizeof(header));
            if (header.size < sizeof(header)) {
                break;
            }
            record.resize(header.size);
            copy(data, size, tail, record.data(), header.size);
            if (header.type == PERF_RECORD_SAMPLE) {
                on_sample(record);
            }
            tail += header.size;
        }
        __atomic_store_n(&meta->data_tail, tail, __ATOMIC_RELEASE);
    }

private:
    static void copy(const char* data, std::size_t size, std::uint64_t at, void* out,
                     std::size_t length) {
        auto* dst = static_cast<char*>(out);
        for (std::size_t i = 0; i < length; ++i) {
            dst[i] = data[(at + i) % size];
        }
    }

    char* base_;
    std::size_t page_;
};

struct Sample {
    std::vector<std::uint64_t> frames;  // leaf first
};

// PERF_SAMPLE_IP | PERF_SAMPLE_CALLCHAIN: header, ip, nr, ips[nr].
std::optional<Sample> parse_sample(const std::vector<char>& record) {
    std::size_t at = sizeof(perf_event_header);
    auto next = [&](std::uint64_t& out) {
        if (at + sizeof(out) > record.size()) {
            return false;
        }
        std::memcpy(&out, record.data() + at, sizeof(out));
        at += sizeof(out);
        return true;
    };
    std::uint64_t ip = 0;
    std::uint64_t nr = 0;
    if (!next(ip) || !next(nr)) {
        return std::nullopt;
    }
    Sample sample;
    for (std::uint64_t i = 0; i < nr; ++i) {
        std::uint64_t frame = 0;
        if (!next(frame)) {
            break;
        }
        if (frame >= static_cast<std::uint64_t>(PERF_CONTEXT_MAX)) {
            continue;  // context markers, not addresses
        }
        sample.frames.push_back(frame);
    }
    if (sample.frames.empty()) {
        sample.frames.push_back(ip);
    }
    return sample;
}

struct Child {
    pid_t pid{-1};
    int go_write{-1};
};

// Forked and pinned, ASLR off so addresses match the -no-pie binary's own,
// waiting for one byte before it execs so the sampler is attached first.
Child spawn(const fs::path& binary, const fs::path& input, int cpu) {
    std::array<int, 2> go{};
    if (::pipe2(go.data(), O_CLOEXEC) != 0) {
        throw std::runtime_error(std::format("profile: pipe2: {}", std::strerror(errno)));
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        throw std::runtime_error(std::format("profile: fork: {}", std::strerror(errno)));
    }
    if (pid == 0) {
        ::personality(ADDR_NO_RANDOMIZE);
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        ::sched_setaffinity(0, sizeof(set), &set);
        const int in = ::open(input.c_str(), O_RDONLY);
        const int out = ::open("/dev/null", O_WRONLY);
        ::dup2(in, STDIN_FILENO);
        ::dup2(out, STDOUT_FILENO);
        ::dup2(out, STDERR_FILENO);
        char byte = 0;
        if (::read(go[0], &byte, 1) != 1) {
            ::_exit(126);
        }
        ::execl(binary.c_str(), binary.c_str(), nullptr);
        ::_exit(127);
    }
    ::close(go[0]);
    return {.pid = pid, .go_write = go[1]};
}

struct Collected {
    std::map<std::string, std::uint64_t> folded;
    std::map<std::string, std::map<std::string, std::uint64_t>> by_owner;  // owner -> symbol -> n
    std::uint64_t samples{0};
};

void attribute(const Sample& sample, const std::vector<Symbol>& symbols,
               const std::unordered_map<std::string, std::string>& owners, Collected& out) {
    std::vector<std::string> names;
    names.reserve(sample.frames.size());
    for (std::size_t i = 0; i < sample.frames.size(); ++i) {
        // A return address points past the call; step back into it.
        const auto address = i == 0 ? sample.frames[i] : sample.frames[i] - 1;
        const auto* symbol = lookup(symbols, address);
        names.push_back(symbol != nullptr ? symbol->name : "[external]");
    }
    std::string stack;
    for (const auto& name : std::views::reverse(names)) {
        stack += (stack.empty() ? "" : ";") + name;
    }
    ++out.folded[stack];
    const auto owner = owners.find(names.front());
    ++out.by_owner[owner != owners.end() ? owner->second : "other"][names.front()];
    ++out.samples;
}

Profile summarise(const Collected& collected) {
    Profile profile;
    profile.ran = true;
    profile.samples = collected.samples;
    for (const auto& [owner, symbols] : collected.by_owner) {
        PartProfile part{.owner = owner};
        std::vector<std::pair<std::string, std::uint64_t>> ranked(symbols.begin(), symbols.end());
        for (const auto& entry : ranked) {
            part.samples += entry.second;
        }
        std::ranges::sort(ranked, [](const auto& a, const auto& b) { return a.second > b.second; });
        ranked.resize(std::min(ranked.size(), kTopSymbols));
        part.top_symbols = std::move(ranked);
        part.share = collected.samples > 0 ? static_cast<double>(part.samples) /
                                                 static_cast<double>(collected.samples)
                                           : 0.0;
        profile.owners.push_back(std::move(part));
    }
    std::ranges::sort(profile.owners,
                      [](const auto& a, const auto& b) { return a.samples > b.samples; });
    return profile;
}

std::string escape_xml(std::string_view text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '&':
                out += "&amp;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out += c;
        }
    }
    return out;
}

struct Node {
    std::uint64_t total{0};
    std::map<std::string, Node> children;
};

void emit(const std::string& name, const Node& node, double x, int depth, double unit,
          int max_depth, std::string& svg) {
    constexpr int kRow = 16;
    const double width = static_cast<double>(node.total) * unit;
    if (width < 0.1) {
        return;
    }
    const int y = (max_depth - depth) * kRow;
    // Warm colours, varied by name so neighbours are distinguishable.
    const auto hash = std::hash<std::string>{}(name);
    const int red = 205 + static_cast<int>(hash % 50);
    const int green = 80 + static_cast<int>((hash >> 8) % 120);
    svg += std::format(
        "<g><title>{} ({} samples)</title><rect x=\"{:.2f}\" y=\"{}\" width=\"{:.2f}\" "
        "height=\"{}\" fill=\"rgb({},{},55)\" rx=\"2\"/>",
        escape_xml(name), node.total, x, y, width, kRow - 1, red, green);
    if (width > 40) {
        const auto chars = static_cast<std::size_t>(width / 7);
        auto label = name.size() > chars ? name.substr(0, chars > 2 ? chars - 2 : 0) + ".." : name;
        svg +=
            std::format(R"(<text x="{:.2f}" y="{}">{}</text>)", x + 3, y + 11, escape_xml(label));
    }
    svg += "</g>\n";
    double child_x = x;
    for (const auto& [child_name, child] : node.children) {
        emit(child_name, child, child_x, depth + 1, unit, max_depth, svg);
        child_x += static_cast<double>(child.total) * unit;
    }
}

}  // namespace

std::string flamegraph_svg(const std::vector<std::pair<std::string, std::uint64_t>>& folded,
                           const std::string& title) {
    constexpr double kWidth = 1200;
    Node root;
    int max_depth = 1;
    for (const auto& [stack, count] : folded) {
        root.total += count;
        Node* node = &root;
        int depth = 1;
        std::istringstream frames{stack};
        for (std::string frame; std::getline(frames, frame, ';');) {
            node = &node->children[frame];
            node->total += count;
            max_depth = std::max(max_depth, ++depth);
        }
    }
    const int height = (max_depth + 2) * 16;
    std::string svg = std::format(
        "<?xml version=\"1.0\" standalone=\"no\"?>\n"
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"{}\" height=\"{}\" "
        "viewBox=\"0 0 {} {}\" font-family=\"monospace\" font-size=\"11\">\n"
        "<text x=\"4\" y=\"{}\" font-size=\"13\">{}</text>\n",
        kWidth, height, kWidth, height, height - 4, escape_xml(title));
    if (root.total > 0) {
        emit("all", root, 0, 0, kWidth / static_cast<double>(root.total), max_depth, svg);
    }
    svg += "</svg>\n";
    return svg;
}

Profile record_profile(const fs::path& binary, const std::string& input, int cpu,
                       const std::vector<cxxprobe::judge::detail::OwnedObject>& objects,
                       const fs::path& out_dir) {
    Profile failed;
    const auto symbols = binary_symbols(binary);
    if (symbols.empty()) {
        failed.error = "no symbols could be read from the benchmark binary (is nm installed?)";
        return failed;
    }
    const auto owners = owners_by_symbol(objects);
    fs::create_directories(out_dir);
    const auto input_file = out_dir / ".profile-input";
    std::ofstream{input_file, std::ios::binary} << input;

    const auto child = spawn(binary, input_file, cpu);
    const int fd = open_sampler(child.pid);
    if (fd < 0) {
        failed.error = std::format("perf_event_open: {}", std::strerror(errno));
        ::kill(child.pid, SIGKILL);
        ::waitpid(child.pid, nullptr, 0);
        ::close(child.go_write);
        fs::remove(input_file);
        return failed;
    }
    const auto page = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    const std::size_t length = (kDataPages + 1) * page;
    void* ring = ::mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ring == MAP_FAILED) {
        failed.error = std::format("profile mmap: {}", std::strerror(errno));
        ::kill(child.pid, SIGKILL);
        ::waitpid(child.pid, nullptr, 0);
        ::close(child.go_write);
        ::close(fd);
        fs::remove(input_file);
        return failed;
    }

    char go = 1;
    (void)::write(child.go_write, &go, 1);
    ::close(child.go_write);

    Collected collected;
    RingReader reader{ring, page};
    auto on_sample = [&](const std::vector<char>& record) {
        if (auto sample = parse_sample(record)) {
            attribute(*sample, symbols, owners, collected);
        }
    };
    int status = 0;
    while (::waitpid(child.pid, &status, WNOHANG) == 0) {
        reader.drain(on_sample);
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    reader.drain(on_sample);
    ::munmap(ring, length);
    ::close(fd);
    fs::remove(input_file);

    auto profile = summarise(collected);
    std::vector<std::pair<std::string, std::uint64_t>> folded(collected.folded.begin(),
                                                              collected.folded.end());
    profile.folded = out_dir / "stacks.folded";
    profile.svg = out_dir / "flamegraph.svg";
    {
        std::ofstream out{profile.folded};
        for (const auto& [stack, count] : folded) {
            out << stack << ' ' << count << '\n';
        }
    }
    std::ofstream{profile.svg} << flamegraph_svg(
        folded, std::format("{} samples at {} Hz", collected.samples, kSampleFrequency));
    return profile;
}

}  // namespace cxxprobe::bench::detail
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-vararg,cppcoreguidelines-pro-type-union-access,cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-constant-array-index,cppcoreguidelines-pro-type-const-cast)

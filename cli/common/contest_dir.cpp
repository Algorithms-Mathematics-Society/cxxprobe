#include "contest_dir.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;

std::optional<fs::path> find_contest_dir(const fs::path& start) {
    fs::path cur = fs::absolute(start);
    while (true) {
        if (fs::exists(cur / "contest.yaml")) {
            return cur;
        }
        fs::path parent = cur.parent_path();
        if (parent == cur) {
            return std::nullopt;
        }
        cur = parent;
    }
}

}  // namespace cxxprobe::cli

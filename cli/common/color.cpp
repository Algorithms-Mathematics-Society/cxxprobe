#include "color.hpp"

namespace cxxprobe::cli {

Col make_col(bool enable) {
    if (!enable) {
        return {};
    }
    // NOLINTBEGIN(readability-magic-numbers)
    return {"\033[32m", "\033[31m", "\033[33m", "\033[36m", "\033[1m", "\033[0m"};
    // NOLINTEND(readability-magic-numbers)
}

}  // namespace cxxprobe::cli

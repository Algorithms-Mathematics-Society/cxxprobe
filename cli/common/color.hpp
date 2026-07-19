#pragma once

namespace cxxprobe::cli {

struct Col {
    const char* grn{""};
    const char* red{""};
    const char* yel{""};
    const char* cyn{""};
    const char* bold{""};
    const char* rst{""};
};

Col make_col(bool enable);

}  // namespace cxxprobe::cli

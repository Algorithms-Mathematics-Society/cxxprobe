#pragma once

#include <CLI/CLI.hpp>

#include <string>

namespace cxxprobe::cli {

class BundleCommand {
public:
    explicit BundleCommand(CLI::App& parent);
    int execute();

    [[nodiscard]] bool validate_invoked() const { return validate_app_->parsed(); }

private:
    CLI::App* bundle_app_{};
    CLI::App* validate_app_{};
    std::string contest_dir_;
    bool json_output_{false};
};

}  // namespace cxxprobe::cli

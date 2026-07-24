#pragma once

#include <CLI/CLI.hpp>

namespace cxxprobe::cli {

class IdentityCommand {
public:
    explicit IdentityCommand(CLI::App& parent);
    int execute() const;

    [[nodiscard]] bool invoked() const { return identity_app_->parsed(); }

private:
    CLI::App* identity_app_{};
    bool json_output_{false};
};

}  // namespace cxxprobe::cli

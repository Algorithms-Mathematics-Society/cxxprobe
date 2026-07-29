#pragma once

#include <CLI/CLI.hpp>
#include <string>

namespace cxxprobe::cli {

// `cxxprobe pack` / `cxxprobe unpack` — two independent top-level
// subcommands (not nested under a shared verb, unlike `new contest`/`new
// problem`) sharing one class for their closely-related implementation.
// `pack` bundles a contest directory (or a --problems subset of it) into a
// portable zip; `unpack` extracts one back into a directly-servable
// contest directory.
class PackCommand {
public:
    explicit PackCommand(CLI::App& parent);
    int execute();

    [[nodiscard]] bool pack_invoked() const { return pack_app_->parsed(); }
    [[nodiscard]] bool unpack_invoked() const { return unpack_app_->parsed(); }

private:
    CLI::App* pack_app_;
    CLI::App* unpack_app_;

    // pack
    std::string dir_override_;
    std::string problems_csv_;
    std::string out_override_;

    // unpack
    std::string zip_path_;
    std::string unpack_dir_override_;
    bool force_{false};

    int execute_pack();
    int execute_unpack();
};

}  // namespace cxxprobe::cli

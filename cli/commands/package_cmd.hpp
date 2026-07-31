#pragma once

#include <CLI/CLI.hpp>

namespace cxxprobe::cli {

// `cxxprobe package <init|validate|inspect|pack|unpack>` — the single verb
// group for everything that operates on a problem package as a package,
// rather than on what's inside it:
//
//   init     scaffold a fresh v2 package directory
//   validate lint the package's structure (distinct from `cxxprobe validate`,
//            which lints *test data* against the problem's constraints)
//   inspect  print the package's preview JSON
//   pack     bundle a contest's problems into a zip
//   unpack   extract such a zip back into a contest directory
class PackageCommand {
public:
    explicit PackageCommand(CLI::App& parent);
    int execute();

    [[nodiscard]] bool invoked() const { return package_app_->parsed(); }

private:
    CLI::App* package_app_;
    CLI::App* init_app_;
    CLI::App* validate_app_;
    CLI::App* inspect_app_;
    CLI::App* pack_app_;
    CLI::App* unpack_app_;

    // init
    std::string init_name_;
    std::string init_dir_override_;

    // validate / inspect
    std::string problem_name_;
    std::string dir_override_;
    bool json_output_{false};
    bool no_color_{false};

    // pack
    std::string pack_dir_override_;
    std::string problems_csv_;
    std::string out_override_;

    // unpack
    std::string zip_path_;
    std::string unpack_dir_override_;
    bool force_{false};

    int execute_init();
    int execute_validate();
    int execute_inspect();
    int execute_pack();
    int execute_unpack();
};

}  // namespace cxxprobe::cli

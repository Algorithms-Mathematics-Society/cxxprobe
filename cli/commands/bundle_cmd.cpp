#include "bundle_cmd.hpp"

#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>

#include "cxxprobe/bundle.hpp"

namespace cxxprobe::cli {

BundleCommand::BundleCommand(CLI::App& parent) {
    bundle_app_ = parent.add_subcommand("bundle", "Validate and identify problem bundles");
    bundle_app_->require_subcommand(1);
    validate_app_ =
        bundle_app_->add_subcommand("validate", "Validate a contest directory as a bundle");
    validate_app_->add_option("contest-dir", contest_dir_, "Contest directory")->required();
    validate_app_->add_flag("--json", json_output_, "Emit the canonical bundle manifest as JSON");
}

int BundleCommand::execute() {
    try {
        const auto manifest = cxxprobe::bundle::validate(std::filesystem::path{contest_dir_});
        if (json_output_) {
            std::cout << cxxprobe::bundle::to_json(manifest).dump(2) << '\n';
        } else {
            std::cout << "Valid bundle " << manifest.bundle_sha256 << " (" << manifest.files.size()
                      << " files, " << manifest.total_bytes << " bytes, "
                      << manifest.problems.size() << " problems)\n";
        }
        return 0;
    } catch (const std::exception& error) {
        if (json_output_) {
            nlohmann::ordered_json output{{"contract", cxxprobe::bundle::kContract},
                                          {"schema_version", cxxprobe::bundle::kSchemaVersion},
                                          {"valid", false},
                                          {"error", error.what()}};
            std::cout << output.dump(2) << '\n';
        } else {
            std::cerr << "cxxprobe: invalid bundle: " << error.what() << '\n';
        }
        return 2;
    }
}

}  // namespace cxxprobe::cli

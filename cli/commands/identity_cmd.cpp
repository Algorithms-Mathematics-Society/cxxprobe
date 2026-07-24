#include "identity_cmd.hpp"

#include <iostream>
#include <nlohmann/json.hpp>
#include <string_view>

#ifndef CXXPROBE_VERSION
#define CXXPROBE_VERSION "0.0.0-dev"
#endif

#ifndef CXXPROBE_GIT_COMMIT
#define CXXPROBE_GIT_COMMIT "unknown"
#endif

#ifndef CXXPROBE_GIT_DIRTY
#define CXXPROBE_GIT_DIRTY true
#endif

namespace cxxprobe::cli {
namespace {

constexpr std::string_view kIdentityContract = "cxxprobe.engine-identity";
constexpr int kIdentitySchemaVersion = 1;

}  // namespace

IdentityCommand::IdentityCommand(CLI::App& parent) {
    identity_app_ = parent.add_subcommand("identity", "Print the embedded engine build identity");
    identity_app_->add_flag("--json", json_output_, "Emit the versioned engine identity JSON");
}

int IdentityCommand::execute() const {
    if (json_output_) {
        nlohmann::ordered_json output{{"contract", kIdentityContract},
                                      {"schema_version", kIdentitySchemaVersion},
                                      {"name", "cxxprobe"},
                                      {"version", CXXPROBE_VERSION},
                                      {"commit", CXXPROBE_GIT_COMMIT},
                                      {"dirty", CXXPROBE_GIT_DIRTY}};
        std::cout << output.dump() << '\n';
    } else {
        std::cout << "cxxprobe " << CXXPROBE_VERSION << " (commit " << CXXPROBE_GIT_COMMIT
                  << ", dirty=" << (CXXPROBE_GIT_DIRTY ? "true" : "false") << ")\n";
    }
    return 0;
}

}  // namespace cxxprobe::cli

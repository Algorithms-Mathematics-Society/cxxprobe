#pragma once

// Building one binary out of a project's parts: shared by the integration
// check (entry is a GTest suite) and `cxxprobe bench` (entry is a driver with
// its own main). Internal to libcxxprobe.

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "cxxprobe/integration.hpp"
#include "cxxprobe/project.hpp"
#include "judge_detail.hpp"

namespace cxxprobe::judge::detail {

// RAII cleanup for every temporary a build produces.
class TempFiles {
public:
    TempFiles() = default;
    TempFiles(const TempFiles&) = delete;
    TempFiles& operator=(const TempFiles&) = delete;
    TempFiles(TempFiles&&) = delete;
    TempFiles& operator=(TempFiles&&) = delete;
    ~TempFiles();
    std::filesystem::path add(std::string_view prefix, std::string_view suffix = {});

private:
    std::vector<std::filesystem::path> paths_;
};

struct LinkOptions {
    bool gtest{false};                       // link gtest_main/gtest, add its include path
    std::vector<std::string> compile_flags;  // appended to every object compile
    std::vector<std::string> link_flags;     // appended to the final link
};

// Who an object file's symbols belong to: a part label, "shared" or "entry".
struct OwnedObject {
    std::string owner;
    std::filesystem::path object;
};

struct ProjectLink {
    std::vector<IntegrationPart> parts;  // one per declared part, in manifest order
    std::vector<OwnedObject> objects;
    std::vector<std::string> failed;  // labels whose code did not compile
    CompileStepReport link;           // shared sources, entry and the final link
    std::filesystem::path binary;     // set only when everything built and linked
};

// Each part's chosen source becomes its own object with main() renamed,
// built with that part's compiler settings, followed by its own extra
// sources. A part that does not compile is replaced from
// integration.fallback_dir/<label>.cpp when present, and the substitution is
// recorded on its IntegrationPart. Shared sources and the entry are compiled
// with the project defaults, then everything is linked. `sources` must name
// every declared label exactly once.
ProjectLink link_project(const cxxprobe::project::ProjectConfig& project,
                         const cxxprobe::problem::ProjectDefaults& defaults,
                         const std::vector<PartSource>& sources, const std::filesystem::path& entry,
                         const LinkOptions& options, TempFiles& temps);

// "part '1' (submission): /path" lines, for a link failure's diagnostics.
std::string linked_summary(const std::vector<IntegrationPart>& parts);

}  // namespace cxxprobe::judge::detail

#include "pack_cmd.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>

#include "../common/contest_dir.hpp"
#include "cxxprobe/pack.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;

namespace {

std::vector<std::string> split_csv(const std::string& csv) {
    std::vector<std::string> out;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

}  // namespace

PackCommand::PackCommand(CLI::App& parent) {
    pack_app_ = parent.add_subcommand("pack", "Bundle a contest's problems into a zip for import");
    pack_app_->add_option(
        "-C,--dir", dir_override_,
        "Contest directory (default: auto-detect via contest.yaml, walking up from cwd)");
    pack_app_->add_option("--problems", problems_csv_,
                          "Comma-separated problem slugs to include (default: all)");
    pack_app_->add_option("-o,--out", out_override_,
                          "Output zip path (default: <contest-dir-basename>.zip in cwd)");

    unpack_app_ =
        parent.add_subcommand("unpack", "Extract a cxxprobe pack into a contest directory");
    unpack_app_->add_option("zip", zip_path_, "Path to the pack zip")->required();
    unpack_app_->add_option("-C,--dir", unpack_dir_override_,
                            "Destination directory (default: current directory)");
    unpack_app_->add_flag("--force", force_,
                          "Overwrite even if the destination already has a contest.yaml");
}

int PackCommand::execute() {
    if (pack_app_->parsed()) {
        return execute_pack();
    }
    return execute_unpack();
}

int PackCommand::execute_pack() {
    fs::path contest_dir;
    if (!dir_override_.empty()) {
        contest_dir = fs::absolute(dir_override_);
    } else {
        auto found = find_contest_dir(fs::current_path());
        if (!found) {
            std::cerr
                << "cxxprobe: no contest.yaml found in the current directory or any ancestor — "
                   "pass --dir to specify one\n";
            return 2;
        }
        contest_dir = *found;
    }

    fs::path out_path = out_override_.empty()
                            ? fs::current_path() / (contest_dir.filename().string() + ".zip")
                            : fs::absolute(out_override_);

    cxxprobe::pack::PackOptions opts;
    if (!problems_csv_.empty()) {
        opts.problem_slugs = split_csv(problems_csv_);
    }

    cxxprobe::pack::PackResult result;
    try {
        result = cxxprobe::pack::pack_contest(contest_dir, out_path, opts);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    std::cout << "Packed " << result.problems.size() << " problem(s) into "
              << result.output_path.string() << "\n";
    for (const auto& p : result.problems) {
        std::cout << "  " << p.slug << "  (" << p.name << ")\n";
    }
    return 0;
}

int PackCommand::execute_unpack() {
    fs::path dest_dir =
        unpack_dir_override_.empty() ? fs::current_path() : fs::absolute(unpack_dir_override_);

    cxxprobe::pack::UnpackResult result;
    try {
        result = cxxprobe::pack::unpack_contest(fs::absolute(zip_path_), dest_dir, force_);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    std::cout << "Unpacked " << result.problem_slugs.size() << " problem(s) into "
              << result.dest_dir.string() << "\n";
    for (const auto& slug : result.problem_slugs) {
        std::cout << "  " << slug << "\n";
    }
    return 0;
}

}  // namespace cxxprobe::cli

#include "new_cmd.hpp"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>

#include "cxxprobe/problem.hpp"

namespace cxxprobe::cli {

namespace fs = std::filesystem;

namespace {

void write_file(const fs::path& path, std::string_view content) {
    std::ofstream ofs{path, std::ios::binary};
    ofs << content;
}

}  // namespace

NewCommand::NewCommand(CLI::App& parent) {
    new_app_ = parent.add_subcommand("new", "Scaffold a contest");
    new_app_->require_subcommand(1);

    contest_app_ = new_app_->add_subcommand("contest", "Create a new contest folder");
    contest_app_->add_option("name", contest_name_, "Contest name")->required();
}

int NewCommand::execute() { return execute_new_contest(); }

int NewCommand::execute_new_contest() {
    std::string slug = cxxprobe::problem::slugify(contest_name_);
    if (slug.empty()) {
        std::cerr << "cxxprobe: contest name must contain at least one alphanumeric character\n";
        return 2;
    }
    fs::path dir = fs::current_path() / slug;
    if (fs::exists(dir)) {
        std::cerr << "cxxprobe: '" << dir.string() << "' already exists\n";
        return 2;
    }

    fs::create_directories(dir);
    write_file(dir / "contest.yaml",
               std::format("version: 1\nname: \"{}\"\ndescription: \"\"\n", contest_name_));

    std::cout << "Created contest '" << contest_name_ << "' in " << dir.string() << "\n";
    std::cout << "Next: cxxprobe package init \"Your Problem Name\"\n";
    return 0;
}

}  // namespace cxxprobe::cli

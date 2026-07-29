#!/usr/bin/env python3
"""Embeds GTest's include tree and compiled static libraries into a C++
source as byte arrays + lookup tables, so the behavior checker (compiled by
a plain compiler invocation outside of CMake, at judge time) never depends
on the absolute Conan cache path of the machine that built cxxprobe-cli —
that path doesn't exist on any other machine, which is exactly why the
distributed release binary's Type-3 (behavior) judging was broken on every
machine except the one that built it.

Usage: generate_embedded_gtest.py <gtest_include_dir> <gtest_lib_dir> <output_cpp_path>
"""

import hashlib
import os
import sys


def walk_files(root):
    files = []
    for dirpath, _dirs, names in os.walk(root):
        for name in names:
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            files.append((rel, full))
    files.sort()
    return files


def walk_dirs(semicolon_separated_dirs):
    # CMake generator-expression properties (INTERFACE_INCLUDE_DIRECTORIES /
    # INTERFACE_LINK_DIRECTORIES) can in principle resolve to more than one
    # semicolon-separated directory, even though this GTest Conan package
    # only ever produces one of each — handle the general case defensively,
    # matching the tolerance the code being replaced here already had.
    seen = {}
    for root in semicolon_separated_dirs.split(";"):
        if not root:
            continue
        for rel, full in walk_files(root):
            seen.setdefault(rel, full)
    return sorted(seen.items())


def write_table(out, table_name, files, start_index):
    array_names = []
    for i, (_rel, full) in enumerate(files, start=start_index):
        with open(full, "rb") as f:
            data = f.read()
        out.write(f"constexpr unsigned char kData{i}[] = {{")
        out.write(",".join(str(b) for b in data) if data else "0")
        out.write("};\n")
        array_names.append(f"kData{i}")

    out.write(f"\nconstexpr EmbeddedFile {table_name}[] = {{\n")
    for (rel, full), array_name in zip(files, array_names):
        size_expr = f"sizeof({array_name})" if os.path.getsize(full) > 0 else "0"
        out.write(f'    {{"{rel}", {{{array_name}, {size_expr}}}}},\n')
    out.write("};\n\n")


def main():
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 1

    include_dirs, lib_dirs, output_path = sys.argv[1], sys.argv[2], sys.argv[3]

    include_files = walk_dirs(include_dirs)
    lib_files = [(rel, full) for rel, full in walk_dirs(lib_dirs) if rel.endswith(".a")]

    hasher = hashlib.sha256()
    for rel, full in include_files + lib_files:
        hasher.update(rel.encode("utf-8"))
        hasher.update(str(os.path.getsize(full)).encode("utf-8"))
    bundle_hash = hasher.hexdigest()[:16]

    with open(output_path, "w", encoding="utf-8") as out:
        out.write('#include "embedded_gtest/embedded_gtest_data.hpp"\n\n')
        out.write("namespace cxxprobe::embedded_gtest {\n\n")
        out.write("namespace {\n\n")

        write_table(out, "kIncludeFiles", include_files, 0)
        write_table(out, "kLibFiles", lib_files, len(include_files))

        out.write("}  // namespace\n\n")
        out.write(f'std::string_view bundle_hash() {{ return "{bundle_hash}"; }}\n\n')
        out.write(
            "std::span<const EmbeddedFile> include_files() { return kIncludeFiles; }\n\n"
        )
        out.write("std::span<const EmbeddedFile> lib_files() { return kLibFiles; }\n\n")
        out.write("}  // namespace cxxprobe::embedded_gtest\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())

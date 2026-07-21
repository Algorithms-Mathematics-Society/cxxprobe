#!/usr/bin/env python3
"""Embeds every file under an assets directory into a C++ source as a byte
array + {path, content_type, data} lookup table, so the developer UI ships
inside the cxxprobe-cli binary with no runtime filesystem dependency and no
Node/bundler involved at any point in the build.

Usage: generate_embedded_assets.py <assets_dir> <output_cpp_path>
"""

import os
import sys

CONTENT_TYPES = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json",
    ".svg": "image/svg+xml",
    ".ico": "image/x-icon",
    ".png": "image/png",
}


def content_type_for(rel_path):
    _, ext = os.path.splitext(rel_path)
    return CONTENT_TYPES.get(ext, "application/octet-stream")


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 1

    assets_dir, output_path = sys.argv[1], sys.argv[2]

    files = []
    for root, _dirs, names in os.walk(assets_dir):
        for name in names:
            full = os.path.join(root, name)
            rel = "/" + os.path.relpath(full, assets_dir).replace(os.sep, "/")
            files.append((rel, full))
    files.sort()

    with open(output_path, "w", encoding="utf-8") as out:
        out.write('#include "server/ui/embedded_assets.hpp"\n\n')
        out.write("namespace cxxprobe::server::ui {\n\n")
        out.write("namespace {\n\n")

        for i, (_rel, full) in enumerate(files):
            with open(full, "rb") as f:
                data = f.read()
            out.write(f"constexpr unsigned char kData{i}[] = {{")
            out.write(",".join(str(b) for b in data) if data else "0")
            out.write("};\n")

        out.write("\nconstexpr EmbeddedAsset kAssets[] = {\n")
        for i, (rel, _full) in enumerate(files):
            ct = content_type_for(rel)
            size_expr = f"sizeof(kData{i})" if os.path.getsize(_full) > 0 else "0"
            out.write(f'    {{"{rel}", "{ct}", {{kData{i}, {size_expr}}}}},\n')
        out.write("};\n\n")

        out.write("}  // namespace\n\n")
        out.write("std::span<const EmbeddedAsset> all_embedded_assets() { return kAssets; }\n\n")
        out.write("}  // namespace cxxprobe::server::ui\n")

    return 0


if __name__ == "__main__":
    sys.exit(main())

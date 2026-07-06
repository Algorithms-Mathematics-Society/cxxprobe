# Contributing to cxxprobe

## Commit Conventions

All commits must follow [Conventional Commits](https://www.conventionalcommits.org/).
Allowed types: `feat`, `fix`, `docs`, `style`, `refactor`, `test`, `chore`, `perf`, `ci`.
Format: `type(scope): description`
Example: `feat(sandbox): add cgroup v2 resource limits`

## Environment Setup

```bash
git clone https://github.com/your-org/cxxprobe.git
cd cxxprobe
pip install conan pre-commit cmake-format
pre-commit install --hook-type pre-commit --hook-type commit-msg
conan install . -s build_type=Debug --build=missing
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

## Code Style

- **C++**: Google style, 4-space indentation (`.clang-format` enforced by pre-commit)
- **CMake**: formatted with `cmake-format`
- **Commits**: Conventional Commits (enforced by pre-commit `commitizen` hook)

## Pull Request Process

1. Fork and create a feature branch from `main`.
2. Make changes and add tests. Ensure all CI checks pass locally.
3. Run `pre-commit run --all-files` before pushing.
4. Open a PR against `main`.
5. Obtain approval from at least one maintainer.
6. Squash-merge into `main` (release workflow handles versioning).

## Testing Guidelines

- Every new module requires unit tests.
- Integration tests for sandbox behavior must cover exact resource limits (time, memory, file descriptors).
- Use AddressSanitizer and ThreadSanitizer during development (`cmake --preset asan`).
- Code coverage target: ≥90% on `libcxxprobe`.

## CI Overview

| Trigger | Job |
|---------|-----|
| `push`, `pull_request` | Matrix build: GCC + Clang × Debug + Release |
| | Sanitizer build (Clang + ASan) |
| | `cppcheck` |
| | `clang-tidy` (Clang only) |
| | Pre-commit checks |
| | Unit + integration tests |

All status checks are required before merging to `main`.

### Release Workflow

Triggered on every push to `main`. Uses **semantic-release** to:

1. Analyze commit messages.
2. Bump version in `CMakeLists.txt` and `CHANGELOG.md`.
3. Create a Git tag and publish a GitHub Release.

Requires `GITHUB_TOKEN` with write permissions.

## [0.8.1](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.8.0...v0.8.1) (2026-07-22)


### Bug Fixes

* **server:** fix clang-tidy failure breaking CI and release asset builds ([128d4a5](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/128d4a5187a089e8f20c95941853e64de7c89ccf))

# [0.8.0](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.7.1...v0.8.0) (2026-07-21)


### Features

* **serve:** add cxxprobe serve — HTTP judging service with embedded dev UI ([6c24482](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/6c24482fb2a329fb9fb4c433286fb90405d2e061))

## [0.7.1](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.7.0...v0.7.1) (2026-07-21)


### Bug Fixes

* **cli:** stop clang-format from corrupting scaffold file templates ([9c28289](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/9c28289b1b7b5d4e27a1952eaffbd04251a920dc))

# [0.7.0](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.6.0...v0.7.0) (2026-07-21)


### Features

* **install:** automatically add install dir to PATH ([db55295](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/db55295a4f818f05259b33d73e2db82ce9835624))

# [0.6.0](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.5.0...v0.6.0) (2026-07-21)


### Features

* **install:** modern terminal UI + actionable cgroup setup hint ([88c320a](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/88c320ab45103ec1bd244a3f6af7048a1a0da2a3))

# [0.5.0](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.4.2...v0.5.0) (2026-07-21)


### Features

* **release:** publish a static release binary + curl|sh installer ([e2a2483](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/e2a24836b3f4431059834a54f32c20087f192c5b))

## [0.4.2](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.4.1...v0.4.2) (2026-07-19)


### Bug Fixes

* **ci:** fix remaining implicit-bool-conversion in cases.cpp token_equal ([f397b4b](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/f397b4bf62343fc9ccb4a3ec029bbe9f98cc0d7c))

## [0.4.1](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.4.0...v0.4.1) (2026-07-19)


### Bug Fixes

* **ci:** resolve clang-tidy failures in new contest/problem modules ([6d2be7a](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/6d2be7a39dd1bf3470403f98d5d412e6fb5defa1))

# [0.4.0](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.3.5...v0.4.0) (2026-07-19)


### Features

* **contest:** add contest/problem scaffolding, symbolic & behavior checks ([dfc5212](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/dfc52128f80f22d185b031e21ca3e55638c71c7a)), closes [#include](https://github.com/Algorithms-Mathematics-Society/cxxprobe/issues/include)

## [0.3.5](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.3.4...v0.3.5) (2026-07-12)


### Bug Fixes

* **sandbox:** RLIMIT_AS fallback when cgroup migration blocked by nsdelegate ([4d0253f](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/4d0253f489d18718b1853b15c740d8c17fd229db))

## [0.3.4](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.3.3...v0.3.4) (2026-07-12)


### Bug Fixes

* **sandbox:** close child-bound pipe ends in parent after clone ([c552f39](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/c552f39b574b8831a17e3d4f25eccfe583211154))

## [0.3.3](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.3.2...v0.3.3) (2026-07-12)


### Bug Fixes

* **sandbox:** satisfy Rule of Five on ChildGuard (clang-tidy) ([3fdc4b9](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/3fdc4b970626e74019ce62e6445dc2324c24a5ba))

## [0.3.2](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.3.1...v0.3.2) (2026-07-12)


### Bug Fixes

* **ci:** propagate sanitizers to executables; prevent orphaned child on cgroup failure ([09f3c06](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/09f3c06cdab35701eeee5af479b30feaa9663e98))

## [0.3.1](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.3.0...v0.3.1) (2026-07-12)


### Bug Fixes

* **ci:** add ctest timeout and silence nodiscard warnings in tests ([8728e0f](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/8728e0f0594293d27e75820b1e069ee58a8e2a40))

# [0.3.0](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.2.0...v0.3.0) (2026-07-12)


### Features

* **cli:** complete CLI with judging, batch mode, colors, and duration units ([9d8c3f4](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/9d8c3f498ca03122dbb442fe8042bad5f5722975))
* **sandbox:** expose wall_time and wall_timed_out in Result ([1998e5e](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/1998e5e2b10e8092ebf03276580911f4ce4acc16))

# [0.1.0](https://github.com/Algorithms-Mathematics-Society/cxxprobe/compare/v0.0.0...v0.1.0) (2026-07-06)


### Bug Fixes

* **build:** align ASAN option name and fix release workflow ([049c190](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/049c190eb7529e5c9453f1ab6ac850708a88fd56))
* **ci:** isolate Conan output to build/ and disable fail-fast ([06f878a](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/06f878aed342e5c0565c606193c6b4e5390447b8))
* **ci:** separate CC and CXX in build matrix ([2a9d927](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/2a9d927d63d6cc42fd0cc6fefb4c319c6e6824ae))
* **ci:** set compiler.cppstd=23 for Conan install ([795a2da](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/795a2da40bf46e7fe8b66bcde30d3d04e9b5b11c))
* **ci:** use ubuntu-24.04 for full C++23 support ([ef0f117](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/ef0f117c5b853368356d132d5735b634ff29f0c5))


### Features

* **cli:** add cxxprobe-cli skeleton ([57cbcc8](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/57cbcc8b206fae04a1486244c93d29ee1374a09b))
* **lib:** add libcxxprobe skeleton ([285754b](https://github.com/Algorithms-Mathematics-Society/cxxprobe/commit/285754ba75ba0a36babe8550c42013832849b862))

# Changelog

All notable changes to this project will be documented in this file.

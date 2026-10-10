# Contributing to xuan

Thanks for your interest in improving xuan! This guide explains how to set up a
development environment, build, test, and submit changes.

Please keep in mind the project priorities:

1. Node.js API compatibility
2. small binary size
3. minimal dependencies
4. suitability for embedded systems

Prefer simple, predictable implementations and avoid heavy dependencies or large
frameworks.

## Development setup

### Prerequisites

- **macOS**: Xcode or Command Line Tools
- **Linux**: clang-15 or newer, `cmake`, `make`, `git`
- **Node.js**: see `.nvmrc` (used for the JS build tooling)
- **MIPS cross-compilation** (optional): `zig`

### Clone and build

```bash
git clone https://github.com/topelinux/xuan.git
cd xuan

# Initialize submodules
git submodule update --init --recursive --force

# Linux only: use clang-15
export CC=clang-15
export CXX=clang++-15

# Install JS build dependencies (esbuild, polyfills, etc.)
npm install

# Build
make

# Run the test suite
make test
```

The binary is produced at `./build/xuan`.

> `make` invokes `npx esbuild` to generate bundles, so `npm install` must be run
> before the first build.

## Working with dependencies

`deps/` directories are git submodules. **Do not commit direct edits inside
submodules.** Store local dependency changes as patch files under `patches/`
and let the build apply them automatically.

To capture a change you made inside a submodule:

```bash
# From the repository root
git -C deps/<submodule> diff > patches/<submodule>.patch

# Reset the submodule checkout to its pinned commit
./scripts/reset-deps-checkout.sh <submodule>
```

The `Makefile` target `apply-deps-patches` applies every patch referenced by
`DEPS_PATCHES` idempotently before building.

## JavaScript sources and bundles

xuan compiles JavaScript modules into C files (`src/bundles/c/**/*.c`) that are
embedded into the binary. These C bundles are build artifacts and are **not**
committed.

After modifying JS sources under `src/js/`:

```bash
make js
make
```

## Coding style

- C sources follow the repository `.clang-format`:

  ```bash
  make format
  ```

- JavaScript sources are linted with ESLint:

  ```bash
  npm run lint
  ```

## Tests

- Run the full suite with `make test`.
- Add tests for new features and bug fixes whenever possible, and prefer tests
  that validate Node.js compatibility.
- Tests live under `tests/`; fixtures go in `tests/fixtures/`.

## Submitting changes

1. Fork the repository and create a feature branch:
   `git checkout -b feature/AmazingFeature`
2. Make your change, keeping it focused and consistent with the surrounding code.
3. Ensure it builds and the relevant tests pass.
4. Commit with a clear message:
   `git commit -m 'Add some AmazingFeature'`
5. Push the branch and open a Pull Request.

Please describe **what** changed and **why**, and note any differences from
Node.js behavior in the PR description.

## Reporting issues

- Use the GitHub issue templates for bug reports and feature requests.
- Include your platform, xuan version (`./build/xuan --version`), and a minimal
  reproduction when possible.

## License

By contributing, you agree that your contributions are licensed under the
[MIT License](LICENSE).

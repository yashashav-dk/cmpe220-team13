# Contributing

How to build and test on any machine, what CI checks, and how changes reach `main`.

## Build and test: one command everywhere

```sh
make test
```

It builds everything with `-std=c99 -Wall -Wextra -Wpedantic -Werror`, runs the unit tests and then the end-to-end golden tests (`tests/golden.sh`), and exits non-zero on the first failure. CI runs exactly this command, so if it passes on your machine it should pass in CI.

| Machine | Setup, once | Then |
|---|---|---|
| macOS | `xcode-select --install` (clang and make) | `make test` |
| Linux | a C compiler and make, e.g. `sudo apt install build-essential` | `make test` (add `CC=clang` to try clang) |
| Windows | [MSYS2](https://www.msys2.org/), then in the **UCRT64** shell: `pacman -S make diffutils mingw-w64-ucrt-x86_64-gcc` | `make test CC=gcc` |
| Windows with WSL | as Linux | `make test` |
| Anything with Docker | nothing else | see below |

With Docker, this reproduces the Alpine CI job (gcc, musl libc, busybox `sh`) without touching your checkout:

```sh
docker run --rm -v "$PWD":/src:ro alpine:3.20 sh -c \
  'apk add --no-cache build-base >/dev/null && cp -r /src /w && cd /w && make test CC=gcc'
```

`make clean` removes `build/`. The build directory can be moved with `make test BUILD=/tmp/t13`.

## Portability rules

The tools must build and behave the same on macOS, Linux and Windows:

- C99 only: no compiler extensions, no POSIX-only or Windows-only calls. Use `fopen`, `fgetc`, `snprintf` and friends.
- Open files in binary mode (`"rb"`, `"wb"`) so that Windows does not add or strip CR characters. The image and trace formats are LF text.
- Fixed-size arrays, no `malloc`: the machine has 1 KiB of memory, so the programs are tiny.
- Builds clean with `-Werror` on gcc and clang.
- LF line endings in every file; `.gitattributes` enforces this on checkout.

## What CI runs

| Workflow | Job | Blocks a merge? |
|---|---|---|
| CI (`ci.yml`) | Linux gcc, Linux clang | **yes**, once `ci.yml` is on `main` |
| CI (`ci.yml`) | macOS clang | no (macOS runners are sometimes unavailable; re-run the job if it is cancelled) |
| Extended checks (`extended.yml`) | clang AddressSanitizer + UndefinedBehaviorSanitizer | no |
| Extended checks (`extended.yml`) | Windows gcc (MSYS2 UCRT64) | no |
| Extended checks (`extended.yml`) | Alpine gcc (musl, busybox `sh`) | no |
| CodeQL (repository setting) | C/C++ code scanning; results under **Security → Code scanning** | no |

The jobs that do not block a merge are still worth reading: a red sanitizer or Windows job is a real bug. Dependabot opens a weekly PR when an action used by a workflow has a new version.

To reproduce the sanitizer job on Linux:

```sh
make test CC=clang CFLAGS="-std=c99 -Wall -Wextra -Wpedantic -Werror -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
```

On macOS, AddressSanitizer can hang; use `-fsanitize=undefined` alone there.

## How a change reaches `main`

1. Branch from `main`, one branch per change.
2. Run `make test`, push, and open a pull request. The template asks for the OS and compiler you tested with.
3. One teammate reviews and approves. Direct pushes to `main` are blocked.
4. Merge with **squash**, the only method allowed. The PR title and description become the commit message on `main`, so keep them accurate. The branch is deleted after the merge.

If a PR changes a shared format (the ISA, the image or the trace), update its document in `docs/`, the code on both sides and the golden files in `tests/golden/` in the same PR, and tell the team.

## Reporting a security problem

Use **Security → Report a vulnerability** on GitHub rather than a public issue.

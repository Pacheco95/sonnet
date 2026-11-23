# The image the Linux CI jobs run in (docs/build.md, "Linux packages"). Everything the jobs used to
# install with apt on every run is installed here once, so a job no longer fails when the kisak Mesa PPA or
# apt.llvm.org answers 503. The CI workflow builds and pushes it to GHCR, tagged with a hash of this folder:
# changing any file here is what publishes a new image, and re-running the "Linux image" job after a
# failed build is how to retry.
FROM ubuntu:24.04

LABEL org.opencontainers.image.source="https://github.com/Pacheco95/sonnet"
LABEL org.opencontainers.image.description="Toolchains, Mesa Lavapipe and system headers for Sonnet's Linux CI jobs"

ENV DEBIAN_FRONTEND=noninteractive

# Sources first, then one index update and one install, so apt resolves Mesa's LLVM 21 and apt.llvm.org's
# LLVM 20 together. Mesa comes from the kisak PPA, recent enough for Lavapipe to expose Vulkan 1.4; Clang 20
# from apt.llvm.org, because Ubuntu 24.04's Clang 18 with libstdc++ hides std::expected. GCC 14 is the GCC
# jobs' compiler and the libstdc++ Clang selects on its own. The Clang packages go in without recommends
# (llvm-20-dev and clang-tools-20, used by no job); llvm-20 holds llvm-ar-20, llvm-ranlib-20 and the
# llvm-symbolizer-20 the sanitizer runtimes run, libclang-rt-20-dev the runtimes themselves. The last line
# is what a GitHub-hosted runner has and the jobs' steps use: git, CMake, Python, jq, gh (cache cleanup),
# zstd (actions/cache) and what vcpkg runs to fetch and unpack sources.
RUN set -eux; \
    echo 'Acquire::Retries "5";' > /etc/apt/apt.conf.d/80-retries; \
    apt-get update; \
    apt-get install -y --no-install-recommends ca-certificates curl gnupg lsb-release software-properties-common; \
    add-apt-repository -y --no-update ppa:kisak/kisak-mesa; \
    install -d -m 0755 /etc/apt/keyrings; \
    curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key -o /etc/apt/keyrings/apt.llvm.org.asc; \
    echo "deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] https://apt.llvm.org/noble/ llvm-toolchain-noble-20 main" \
      > /etc/apt/sources.list.d/apt.llvm.org.list; \
    curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg -o /etc/apt/keyrings/githubcli.gpg; \
    echo "deb [signed-by=/etc/apt/keyrings/githubcli.gpg] https://cli.github.com/packages stable main" \
      > /etc/apt/sources.list.d/github-cli.list; \
    apt-get update; \
    apt-get install -y --no-install-recommends \
      ninja-build gcc-14 g++-14 clang-20 llvm-20 libclang-rt-20-dev clang-tidy-20 \
      libvulkan1 mesa-vulkan-drivers vulkan-tools \
      libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev libxss-dev libxkbcommon-dev \
      libwayland-dev libdecor-0-dev libegl1-mesa-dev libgl-dev libdrm-dev libgbm-dev \
      libasound2-dev libpulse-dev libudev-dev libdbus-1-dev libibus-1.0-dev \
      git cmake make python3 jq gh zstd zip unzip tar pkg-config; \
    rm -rf /var/lib/apt/lists/*

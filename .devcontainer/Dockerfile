# FujiNet firmware dev container.
#
# Wraps the published defoogi build image (https://github.com/FozzTexx/defoogi)
# so it can be used as a long-lived VS Code Dev Container / GitHub Codespace
# instead of the one-shot `defoogi <cmd>` invocation it was designed for.
FROM fozztexx/defoogi:latest

# defoogi's ENTRYPOINT (cntnr-init) is built for one-shot invocation: with no
# command it drops into `su wario` and exits, which would immediately stop a
# long-lived dev container. Clear it so the Dev Container / Codespaces runtime
# manages the container lifecycle and user/UID mapping itself.
ENTRYPOINT []
CMD ["sleep", "infinity"]

# Extra packages the base image doesn't carry:
#   - clang-format: required by the repo's coding-standard pre-commit hook
#   - libssl-dev / pkg-config: needed by the fujinet-PC CMake build (the
#     vendored libssh defaults to the OpenSSL crypto backend)
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        clang-format \
        libssl-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

# defoogi bundles the `abimap` tool. Its mere presence makes the vendored
# libssh's CMake (ABIMAP_FOUND=TRUE) try to read src/ABI/current from the
# *parent* fujinet project, a file that doesn't exist there -- which aborts
# the fujinet-PC CMake build. A normal dev box has no abimap and skips that
# path, so take it off PATH to match. (abimap is unused by firmware builds.)
RUN rm -f /usr/local/bin/abimap

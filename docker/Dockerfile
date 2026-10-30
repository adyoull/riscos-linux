# riscos-linux-testbed as a container image: the riscos-crossdev image it
# builds on, plus qemu-arm-riscos, the test bed's modules, the RISC OS for
# Linux ROM image, HardDisc4 (for the desktop mode), rol-kvm, BASICVFP
# (from RISC OS Open's Pi ROM) and UnixLib with the port's patch, with
# bin/rol-run on PATH.
#
# The patched UnixLib replaces the toolchain's libunixlib.a in the image
# (the original is kept as libunixlib.a.crossdev), so programs built in it
# can use threads on the port. It behaves as the original elsewhere.
#
#   docker build -f docker/Dockerfile -t riscos-linux-testbed .   (from the repo root)
#   docker run --rm -v "$PWD":/src riscos-linux-testbed rol-run out/myprog
#
# BASE is the riscos-crossdev image (the version in .riscos-crossdev).
ARG BASE=ghcr.io/adyoull/riscos-crossdev:1.8.1

# The build context, so that dl/ (local copies of downloads) can be used
# without being copied into the image.
FROM scratch AS src
COPY . /

FROM ${BASE}

ARG DEBIAN_FRONTEND=noninteractive
# For building QEMU (meson from pip: Ubuntu's QEMU source tarball has no
# Python wheels in it).
RUN apt-get update \
 && apt-get install -y --no-install-recommends ninja-build python3-venv libglib2.0-dev \
 && pip3 install --no-cache-dir --break-system-packages meson==1.2.3 \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/riscos-linux-testbed
RUN --mount=type=bind,from=src,target=/ctx \
    tar -C /ctx --exclude=./dl -cf - . | tar -xf -
# HardDisc4 and the Pi ROM (for BASICVFP) come from riscosopen.org and
# UnixLib's sources from GitHub; to build without those, put them in dl/:
# HardDisc4.5.30.util, BCM2835.5.30.zip, and
# gcc-10.2.0.tar.gz or the gcc-10.2.0 source (top-level files and config/
# are enough) with a riscos-gccsdk checkout (see unixlib/build.sh).
# dl/ is mounted from the build context for this step only (writable, but
# nothing written there is kept).
# The build secret unixlib_token, if given, is a token that can read
# riscos-unixlib (if it's private); it's not kept in the image.
RUN --mount=type=secret,id=unixlib_token,required=false \
    --mount=type=bind,from=src,source=dl,target=/opt/riscos-linux-testbed/dl,rw \
    if [ -s /run/secrets/unixlib_token ]; then \
      git config --global url."https://x-access-token:$(cat /run/secrets/unixlib_token)@github.com/adyoull/riscos-unixlib".insteadOf \
        https://github.com/adyoull/riscos-unixlib; fi \
 && if [ -f dl/HardDisc4.5.30.util ]; then export ROL_HD4_FILE=$PWD/dl/HardDisc4.5.30.util; fi \
 && if [ -f dl/BCM2835.5.30.zip ]; then export ROL_PIROM_FILE=$PWD/dl/BCM2835.5.30.zip; fi \
 && if [ -f dl/gcc-10.2.0.tar.gz ]; then export GCC_TARBALL=$PWD/dl/gcc-10.2.0.tar.gz; fi \
 && if [ -d dl/gcc-10.2.0 ] && [ -d dl/riscos-gccsdk ]; then \
      export GCC_SRC=$PWD/dl/gcc-10.2.0 GCCSDK_SRC=$PWD/dl/riscos-gccsdk; fi \
 && make qemu modules rom tools unixlib harddisc basicvfp \
 && lib=$RISCOS_TOOLCHAIN/arm-riscos-gnueabihf/lib \
 && mv "$lib/libunixlib.a" "$lib/libunixlib.a.crossdev" \
 && cp out/unixlib/libunixlib.a "$lib/libunixlib.a" \
 && rm -rf qemu/work out/unixlib/work out/unixlib/deps out/unixlib/riscos-unixlib \
      out/harddisc/HardDisc4.5.30.util out/pirom/BCM2835.5.30.zip \
 && chmod -R a+rX out \
 && rm -f /root/.gitconfig

ARG VERSION=dev
RUN echo "riscos-linux-testbed $VERSION" > /opt/riscos-linux-testbed/VERSION
ENV TB_DIR=/opt/riscos-linux-testbed \
    PATH=/opt/riscos-linux-testbed/bin:$PATH
LABEL org.opencontainers.image.source="https://github.com/adyoull/riscos-linux-testbed" \
      org.opencontainers.image.version="$VERSION" \
      org.opencontainers.image.description="riscos-crossdev plus RISC OS for Linux: run RISC OS programs headless in CI" \
      org.opencontainers.image.licenses="MIT AND Apache-2.0 AND GPL-2.0-or-later AND GPL-3.0-or-later"
WORKDIR /src

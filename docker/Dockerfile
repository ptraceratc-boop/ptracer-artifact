# Same distro + glibc as the machine the vendored kits and suite images were built on:
# Ubuntu 24.04 (glibc 2.39).  Do not change the tag: the shadow CPython, Memcached and the
# Pin/E9Patch kits are linked against this glibc.
FROM ubuntu:24.04@sha256:786a8b558f7be160c6c8c4a54f9a57274f3b4fb1491cf65146521ae77ff1dc54

ENV DEBIAN_FRONTEND=noninteractive
# Package versions are frozen at an Ubuntu archive snapshot: the shipped HiFi plans (data/plans) address
# instructions inside libc/libm/libevent/libgcc_s/libz, so a later security update would silently invalidate them.
# The snapshot is served over https only, so ca-certificates is bootstrapped from the live archive first; every
# package that bootstrap installs or upgrades (openssl, libssl3t64, ...) is then put back to its snapshot version
# (--allow-downgrades), so the result does not depend on the day of the build.
ARG APT_SNAPSHOT=20260923T160000Z
RUN dpkg-query -W -f='${Package} ${Version}\n' | sort > /tmp/dpkg.before \
    && apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    && dpkg-query -W -f='${Package} ${Version}\n' | sort > /tmp/dpkg.after \
    && apt-get update --snapshot $APT_SNAPSHOT \
    && BOOT=$(for p in $(comm -13 /tmp/dpkg.before /tmp/dpkg.after | cut -d' ' -f1); do \
                  echo "$p=$(apt-cache -o APT::Snapshot=$APT_SNAPSHOT madison $p | awk 'NR==1{print $3}')"; done) \
    && echo "bootstrap packages pinned to the snapshot:" $BOOT \
    && apt-get install -y --no-install-recommends --allow-downgrades --snapshot $APT_SNAPSHOT $BOOT \
        build-essential make cmake pkg-config git ca-certificates xxd \
        python3 python3-venv python3-pip \
        libipt-dev libipt2 libelf-dev zlib1g-dev libssl-dev libffi-dev libbz2-dev \
        libevent-2.1-7 libexpat1 libsqlite3-0 util-linux procps \
    && rm -rf /var/lib/apt/lists/* /tmp/dpkg.before /tmp/dpkg.after

# Python for the analyzer (angr/pyvex), the reconstructor's VEX library, and the figure generators.
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir \
        matplotlib==3.10.9 numpy==2.4.6 pypdf==6.13.0 \
        pyvex==9.2.221 angr==9.2.221
ENV PATH=/opt/venv/bin:$PATH \
    PYBIN=/opt/venv/bin/python \
    VIRTUAL_ENV=/opt/venv

# Traditional-tracer baselines (run/fig5_traditional.sh only): LLVM 18 for the Spindle-plus pass and its targets.
# Every installed package is held so that no system library the plans address changes; llvm-18-dev (headers,
# whose package would upgrade ncurses) is unpacked, not installed.
RUN apt-mark hold $(dpkg-query -W -f='${Package}\n') > /dev/null \
    && apt-get update --snapshot $APT_SNAPSHOT && apt-get install -y --no-install-recommends --snapshot $APT_SNAPSHOT \
        clang-18 llvm-18 \
    && cd /tmp && apt-get download --snapshot $APT_SNAPSHOT llvm-18-dev && dpkg -x llvm-18-dev_*.deb / && rm -f llvm-18-dev_*.deb \
    && apt-mark unhold $(dpkg-query -W -f='${Package}\n') > /dev/null \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /artifact
COPY . /artifact

# Files > 95 MB are shipped split; these scripts join them (no-op when absent).
RUN if [ -f third_party/reassemble.sh ]; then bash third_party/reassemble.sh; fi \
    && if [ -f suites/reassemble.sh ]; then bash suites/reassemble.sh; fi

# Builds E9Patch, the e9tool plugin + runtimes, the Pin tools (3.20 and 4.4), and ptrecon/pt_capture2.
RUN bash ptracer/build.sh

# Whole-process pyperformance: the benchmarks' pinned third-party packages, and a byte check of all 82 images
# a pyperformance process maps against the ones its Fast images and HiFi plan were built from.
RUN PYBIN=/opt/venv/bin/python bash suites/pyperformance/wp/setup_site.sh

# memtrace, libdft, Valgrind (+ lackey memtrace patch), Spindle-plus pass; a failed one is listed in
# third_party/BASELINES_ABSENT.txt and its Figure 5 cells become error rows (never fails the build).
RUN bash run/lib/build_baselines.sh

# The plans must match the system libraries they were computed on (fails loudly otherwise).
RUN cd / && md5sum -c /artifact/data/plans/SYSLIBS.md5

# docker/IMAGE_VERSION and the commit this image was built from (docker/build.sh); docker/run.sh checks the version.
ARG IMAGE_VERSION=0
ARG REVISION=unknown
LABEL org.opencontainers.image.version=$IMAGE_VERSION \
      org.opencontainers.image.revision=$REVISION \
      org.opencontainers.image.source=https://github.com/ptraceratc-boop/ptracer-artifact

CMD ["bash"]

# Same distro + glibc as the machine the vendored kits and suite images were built on:
# Ubuntu 24.04 (glibc 2.39).  Do not change the tag: the shadow CPython, Memcached and the
# Pin/E9Patch kits are linked against this glibc.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential make cmake pkg-config git ca-certificates \
        python3 python3-venv python3-pip \
        libipt-dev libipt2 libelf-dev zlib1g-dev libssl-dev libffi-dev libbz2-dev \
        libevent-2.1-7 libexpat1 util-linux procps \
    && rm -rf /var/lib/apt/lists/*

# Python for the analyzer (angr/pyvex), the reconstructor's VEX library, and the figure generators.
RUN python3 -m venv /opt/venv && /opt/venv/bin/pip install --no-cache-dir \
        matplotlib==3.10.9 numpy==2.4.6 pypdf==6.13.0 \
        pyvex==9.2.221 angr==9.2.221
ENV PATH=/opt/venv/bin:$PATH \
    PYBIN=/opt/venv/bin/python \
    VIRTUAL_ENV=/opt/venv

WORKDIR /artifact
COPY . /artifact

# Files > 95 MB are shipped split; these scripts join them (no-op when absent).
RUN if [ -f third_party/reassemble.sh ]; then bash third_party/reassemble.sh; fi \
    && if [ -f suites/reassemble.sh ]; then bash suites/reassemble.sh; fi

# Builds E9Patch, the e9tool plugin + runtimes, the Pin tools (3.20 and 4.4), and ptrecon/pt_capture2.
RUN bash ptracer/build.sh

CMD ["bash"]

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG DEV_USER=developer

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        bash \
        build-essential \
        ca-certificates \
        ccache \
        cmake \
        git \
        libffi-dev \
        libssl-dev \
        libsdl2-dev \
        locales \
        novnc \
        ninja-build \
        openssh-server \
        pkg-config \
        python3 \
        python3-pip \
        python3-venv \
        sudo \
        websockify \
        x11vnc \
        xvfb \
        xz-utils \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m venv /opt/platformio \
    && /opt/platformio/bin/pip install --no-cache-dir --upgrade pip \
    && /opt/platformio/bin/pip install --no-cache-dir platformio \
    && ln -s /opt/platformio/bin/pio /usr/local/bin/pio \
    && ln -s /opt/platformio/bin/platformio /usr/local/bin/platformio

RUN mkdir -p /usr/lib/ccache \
    && ln -sf /usr/bin/ccache /usr/lib/ccache/xtensa-esp32s3-elf-gcc \
    && ln -sf /usr/bin/ccache /usr/lib/ccache/xtensa-esp32s3-elf-g++ \
    && ln -sf /usr/bin/ccache /usr/lib/ccache/riscv32-esp-elf-gcc \
    && ln -sf /usr/bin/ccache /usr/lib/ccache/riscv32-esp-elf-g++

RUN locale-gen C.UTF-8 \
    && useradd -m -s /bin/bash "${DEV_USER}" \
    && usermod -aG sudo "${DEV_USER}" \
    && passwd -l "${DEV_USER}" \
    && printf '%s ALL=(ALL) NOPASSWD:ALL\n' "${DEV_USER}" >/etc/sudoers.d/dev-user \
    && chmod 0440 /etc/sudoers.d/dev-user \
    && mkdir -p /run/sshd /workspace \
    && sed -i 's/^#PasswordAuthentication .*/PasswordAuthentication no/' /etc/ssh/sshd_config \
    && sed -i 's/^#PermitRootLogin .*/PermitRootLogin no/' /etc/ssh/sshd_config \
    && printf '\nClientAliveInterval 60\nClientAliveCountMax 3\n' >>/etc/ssh/sshd_config

ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8
ENV DEV_USER=${DEV_USER}
ENV PLATFORMIO_CORE_DIR=/home/${DEV_USER}/.platformio
ENV CCACHE_DIR=/home/${DEV_USER}/.ccache
ENV PATH=/usr/lib/ccache:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

WORKDIR /workspace/tdeck-plus

EXPOSE 22 6080 8080 8081

CMD ["/usr/sbin/sshd", "-D", "-e"]

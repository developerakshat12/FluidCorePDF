FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ninja-build \
        git \
        python3 \
        pipx \
        gcc-12 \
        g++-12 \
        clang-16 \
        clang-format-17 \
        # Frontend deps for M1+ (GTK3 / Cairo / Poppler / SQLite)
        libgtk-3-dev \
        libcairo2-dev \
        libpoppler-glib-dev \
        libsqlite3-dev \
        libzip-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY .clang-format /workspace/.clang-format

CMD ["/bin/bash"]

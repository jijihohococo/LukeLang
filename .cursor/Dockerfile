FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
      ca-certificates \
      curl \
      g++ \
      gdb \
      git \
      libpq-dev \
      libsodium-dev \
      libsqlite3-dev \
      make \
      postgresql \
      postgresql-client \
      psmisc \
      python3 \
      python3-pip \
      sudo \
    && rm -rf /var/lib/apt/lists/* \
    && curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

# Cloud Agent runs as ubuntu; postgres + service control need sudo.
RUN useradd -m -s /bin/bash ubuntu \
    && echo 'ubuntu ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/ubuntu

USER ubuntu
WORKDIR /home/ubuntu

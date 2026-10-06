#!/usr/bin/env bash
set -euo pipefail

echo "[ECU V2] Stage A — base packages"

sudo apt update
sudo apt full-upgrade -y

sudo apt install -y \
  git \
  curl \
  ca-certificates \
  jq \
  zstd \
  rsync \
  usbutils \
  pciutils \
  ethtool \
  gpiod \
  i2c-tools \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  can-utils \
  network-manager \
  openssh-server

echo
echo "[ECU V2] Versions"
uname -a
git --version
cmake --version | head -1
ninja --version
g++ --version | head -1
ip -V
ssh -V 2>&1 | head -1 || true
candump --help 2>&1 | head -1 || true

echo
echo "[ECU V2] Services"
systemctl is-enabled ssh || true
systemctl is-active ssh || true
systemctl is-enabled NetworkManager || true
systemctl is-active NetworkManager || true

echo
echo "[ECU V2] Stage A complete"

# 品質の検査の道具（open-questions #78）。Mac（Docker Desktop）も CI も、このイメージの中で tools/quality.sh を動かす。
# ubuntu:24.04 の apt で GCC 13・clang-tidy 18・cppcheck 2.13・gcovr 7.0・Doxygen 1.9.8 を入れる。
# lizard は apt にないので pip で版を固定して入れる。
FROM ubuntu:24.04

ARG LIZARD_VERSION=1.17.31

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      ca-certificates git cmake ninja-build g++ gcc \
      clang-tidy cppcheck gcovr doxygen \
      python3 python3-pip \
 && pip3 install --no-cache-dir --break-system-packages "lizard==${LIZARD_VERSION}" \
 && rm -rf /var/lib/apt/lists/*

# ホストの利用者の uid で動かすので、HOME は書き込める場所にする
ENV HOME=/tmp
RUN git config --system --add safe.directory '*'

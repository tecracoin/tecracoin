## builder
FROM ubuntu:14.04
MAINTAINER Maciej Partyka <maciek@partyka.io>

RUN apt-get update && \
    apt-get install -y curl \
    g++-aarch64-linux-gnu \
    g++-4.8-aarch64-linux-gnu \
    gcc-4.8-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    g++-arm-linux-gnueabihf \
    g++-4.8-arm-linux-gnueabihf \
    gcc-4.8-arm-linux-gnueabihf \
    binutils-arm-linux-gnueabihf \
    g++-4.8-multilib \
    gcc-4.8-multilib \
    binutils-gold \
    git-core \
    pkg-config \
    autoconf \
    libtool \
    automake \
    faketime \
    bsdmainutils \
    ca-certificates \
    python \
    build-essential

ARG DEV_REPO
ARG DEV_REPO_CACHE_BUST
COPY ./id_rsa /root/.ssh/id_rsa
RUN cd /root && \
    ssh-keyscan -t rsa bitbucket.org > ~/.ssh/known_hosts && \
    git clone $DEV_REPO && \
    cd /root/tecracoin && \
    git checkout dev && \
    cd /root/tecracoin/depends && \
    make -j5 && \
    cd /root/tecracoin && \
    echo Building for system `sh ./depends/config.guess` && \
    ls `pwd`/depends/`sh ./depends/config.guess` && \
    ./autogen.sh && \
    ./configure \
    --without-gui \
    --prefix=`pwd`/depends/`sh ./depends/config.guess` \
    --enable-static && \
    make -j5 

WORKDIR /root/tecracoin

RUN git pull && git checkout dev && git pull && \
    make -j5 && \
    cd src \
    && strip tecracoind \
    && strip tecracoin-cli \
    && mv tecracoind /usr/local/bin \
    && mv tecracoin-cli /usr/local/bin

RUN git pull && git checkout feat/diff-func && git pull &&  \
    make -j5 && \
    cd src \
    && strip tecracoind \
    && strip tecracoin-cli \
    && mv tecracoind /usr/local/bin \
    && mv tecracoin-cli /usr/local/bin
RUN git pull && git checkout feat/diff-func && git pull && \
    make -j5 && \
    cd src \
    && strip tecracoind \
    && strip tecracoin-cli \
    && mv tecracoind /usr/local/bin \
    && mv tecracoin-cli /usr/local/bin

#ARG CACHEBUST
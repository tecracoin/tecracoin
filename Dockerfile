# This is a Dockerfile for tecracoind.
FROM debian:stretch

# Install required system packages
RUN apt-get update && apt-get install -y \
    automake \
    bsdmainutils \
    curl \
    g++ \
    libboost-all-dev \
    libevent-dev \
    libssl-dev \
    libtool \
    libzmq3-dev \
    make \
    openjdk-8-jdk \
    pkg-config \
    zlib1g-dev

# Install Berkeley DB 4.8
RUN curl -L http://download.oracle.com/berkeley-db/db-4.8.30.tar.gz | tar -xz -C /tmp && \
    cd /tmp/db-4.8.30/build_unix && \
    ../dist/configure --enable-cxx --includedir=/usr/include/bdb4.8 --libdir=/usr/lib && \
    make -j$(nproc) && make install && \
    cd / && rm -rf /tmp/db-4.8.30

# Create user to run daemon
RUN useradd -m -U tecracoind

# Build TecraCoin
COPY . /tmp/tecracoin/

RUN cd /tmp/tecracoin && \
    ./autogen.sh && \
    ./configure --without-gui --prefix=/usr && \
    make -j$(nproc) && \
    make check && \
    make install && \
    cd / && rm -rf /tmp/tecracoin

# Remove unused packages
RUN apt-get remove -y \
    automake \
    bsdmainutils \
    curl \
    g++ \
    libboost-all-dev \
    libevent-dev \
    libssl-dev \
    libtool \
    libzmq3-dev \
    make

# Start TecraCoin Daemon
USER tecracoind

RUN mkdir /home/tecracoind/.tecracoin
VOLUME [ "/home/tecracoind/.tecracoin" ]

EXPOSE 8168
EXPOSE 8888
EXPOSE 18444

ENTRYPOINT [ "/usr/bin/tecracoind" ]

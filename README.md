# What is TecraCoin

[TecraCoin](https://tecra.space) is open source cryptocurrency developed mainly by Tecra. It focuses on fast transactions and reliable network with low transaction fees. TecraCoin is circulating medium on [Tecra fundraising platform](https://tecra.space) which enables tokenization of high-tech projects.

Tokenization process is decentralised and is fully compatible with Omni Layer asset platform.

TecraCoin developed and utilizes [Merkle Tree Proofs (MTP)](https://arxiv.org/pdf/1606.03588.pdf) as its Proof-of-Work algorithm which aims to be memory-hard with fast verification and short proofs.

--------------

## Running with Docker

If you are already familiar with Docker, then running TecraCoin with Docker might be the easier method for you. To run TecraCoin using this method, first install [Docker](https://store.docker.com/search?type=edition&offering=community). After this, you may continue with the following instructions.

Please note that we currently don‘t support the GUI when running with Docker. Therefore, you can only use RPC (via HTTP or the `tecracoin-cli` utility) to interact with TecraCoin via this method.

1. Pull our latest official Docker image:

    ```sh
    docker pull tecracoin/tecranode-mainnet
    ```

2. Start TecraCoin daemon:

    ```sh
    docker run --detach --name tecracoind tecracoin/tecranode-mainnet
    ```

3. View current block count (this might take a while since the daemon needs to find other nodes and download blocks first):

    ```sh
    docker exec tecracoind tecracoin-cli getblockcount
    ```

4. View connected nodes:

    ```sh
    docker exec tecracoind tecracoin-cli getpeerinfo
    ```

5. Stop daemon:

    ```sh
    docker stop tecracoind
    ```

6. Backup wallet:

    ```sh
    docker cp tecracoind:/home/tecracoind/.tecracoin/wallet.dat .
    ```

7. Start daemon again:

    ```sh
    docker start tecracoind
    ```

--------------

## Linux Build Instructions and Notes

### Dependencies

1. Update packages

    ```sh
    sudo apt-get update
    ```

2. Install required packages

    ```sh
    sudo apt-get install build-essential libtool autotools-dev automake pkg-config libssl-dev libevent-dev bsdmainutils libboost-all-dev libgmp-dev cmake
    ```

3. Install Berkeley DB 4.8

    ```sh
    sudo apt-get install software-properties-common
    sudo add-apt-repository ppa:bitcoin/bitcoin
    sudo apt-get update
    sudo apt-get install libdb4.8-dev libdb4.8++-dev
    ```

4. Install QT 5

    ```sh
    sudo apt-get install libminiupnpc-dev libzmq3-dev
    sudo apt-get install libqt5gui5 libqt5core5a libqt5dbus5 qttools5-dev qttools5-dev-tools libprotobuf-dev protobuf-compiler libqrencode-dev
    ```

### Build

1. Clone the source:

    ```sh
    git clone https://github.com/tecracoin/tecracoin
    ```

2. Build TecraCoin-core:

    Configure and build the headless TecraCoin binaries as well as the GUI (if Qt is found).

    You can disable the GUI build by passing `--without-gui` to configure.

    ```sh
    ./autogen.sh
    ./configure
    make
    ```

3. It is recommended to build and run the unit tests:

    ```sh
    make check
    ```

--------------

## macOS Build Instructions and Notes

See (doc/build-macos.md) for instructions on building on macOS.

--------------

## Windows (64/32 bit) Build Instructions and Notes

See (doc/build-windows.md) for instructions on building on Windows 64/32 bit.

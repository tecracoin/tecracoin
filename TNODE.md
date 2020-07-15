Tnode Build Instructions and Notes **mainnet**
=============================
 - Version 0.1.6.1
 - Date: 14 December 2017
 - Updated: 07-02-2020
 - More detailed guide available here: https://tecracoin.io/tecracoin-tnode-setup-guide/

Prerequisites
-------------
 - Ubuntu 16.04+
 - Libraries to build from tecracoin source
 - Port **2718** is open

Step 1. Build
----------------------
**1.1.**  Check out from source:

    git clone https://github.com/tecracoin/tecracoin

**1.2.**  See [README.md](README.md) for instructions on building.

Step 2. (Optional - only if UFW firewall is running). Open port 2718
----------------------
**2.1.**  Run:

    sudo ufw allow 2718
    sudo ufw default allow outgoing
    
Step 3. First run on your Local Wallet
----------------------
**3.0.**  Go to the checked out folder

    cd tecracoin

**3.1.**  Start daemon in server mode:

    ./src/tecracoind -daemon -server

**3.2.**  Generate tnodeprivkey:

    ./src/tecracoin-cli tnode genkey

(Store this key)

**3.3.**  Get wallet address:

    ./src/tecracoin-cli getaccountaddress 0

**3.4.**  Send to received address **exactly 10000 TCR** in **1 transaction**. Wait for 15 confirmations.

**3.5.**  Stop daemon:

    ./src/tecracoin-cli stop

Step 4. In your VPS where you are hosting your Tnode. Update config files
----------------------
**4.1.**  Create file **tecracoin.conf** (in folder **~/.tecracoin**)

    txindex=1
    daemon=1
    server=1
    tnode=1
    tnodeprivkey=XXXXXXXXXXXXXXXXX  ## Replace with your tnode private key from **3.2**
    externalip=XXX.XXX.XXX.XXX:2718 ## Replace with your node external IP

**4.2.**  Create file **tnode.conf** (in folder **~/.tecracoin**) contains the following info:
 - LABEL: A one word name you make up to call your node (ex. TN1)
 - IP:PORT: Your tnode VPS's IP, and the port is always 2718.
 - TNODEPRIVKEY: This is the result of your "tnode genkey" from earlier.
 - TRANSACTION HASH: The collateral tx. hash from the 10000 TCR deposit.
 - INDEX: The Index is always 0 or 1.

To get TRANSACTION HASH, run:

    ./src/tecracoin-cli tnode outputs

The output will look like:

    { "d6fd38868bb8f9958e34d5155437d009b72dfd33fc28874c87fd42e51c0f74fdb" : "0", }

Sample of tnode.conf:

    TN1 51.52.53.54:2718 XrxSr3fXpX3dZcU7CoiFuFWqeHYw83r28btCFfIHqf6zkMp1PZ4 d6fd38868bb8f9958e34d5155437d009b72dfd33fc28874c87fd42e51c0f74fdb 0

Step 5. Run a tnode
----------------------
**5.1.**  Start tnode:

    ./src/tecracoin-cli tnode start-alias <LABEL>

For example:

    ./src/tecracoin-cli tnode start-alias TN1

**5.2.**  To check node status:

    ./src/tecracoin-cli tnode debug

If not successfully started, just repeat start command

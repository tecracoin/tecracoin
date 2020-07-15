## Solo mining

For founders reward to work properly, while mining TecraCoin sends block template to miner with coinbase transaction already built into it. Because of it, you must supply your miner address where reward for mining should go in following way:
```bash
$ ./tecracoind -mineraddr=XXX
```
#!/usr/bin/env python3
import time
from decimal import *

from test_framework.test_framework import TnodeTestFramework
from test_framework.util import *

class TnodeCheckStatus(TnodeTestFramework):
    def __init__(self):
        super().__init__()
        self.num_nodes = 4
        self.num_tnodes = 2
        self.setup_clean_chain = True

    def run_test(self):
        self.collateral = dict()
        for zn in range(self.num_tnodes):
            self.generate_tnode_collateral()
            self.collateral[zn] = self.send_mature_tnode_collateral(zn)
            self.generate_tnode_privkey(zn)
            self.write_master_tnode_conf(zn, self.collateral[zn])

        for zn in range(self.num_tnodes):
            self.restart_as_tnode(zn)
            self.tnode_start(zn)

        self.wait_tnode_enabled(self.num_tnodes)

        tnode_list = self.nodes[self.num_nodes - 1].tnodelist()
        for zno, status in tnode_list.items():
            if self.collateral[1].tx_id in zno:
                assert_equal(status, "ENABLED")


        generator_address = self.nodes[self.num_nodes - 1].getaccountaddress("")
        tnode_output = self.nodes[1].listlockunspent()
        self.nodes[1].lockunspent(True, tnode_output)
        self.nodes[1].sendtoaddress(generator_address, 10000, "", "", True)



        for i in range(50):
            self.nodes[3].generate(1)
            set_mocktime(get_mocktime() + 150)
            set_node_times(self.nodes, get_mocktime())
            self.sync_all()


        tnode_list = self.nodes[self.num_nodes - 1].tnodelist()
        for zno, status in tnode_list.items():
            self.log.info("1 full list status ".format(status))
            if self.collateral[1].tx_id in zno:
                self.log.info("1 status col1"+status)
                assert_equal(status, "OUTPOINT_SPENT")

        # need a restart of tnode 0
        self.nodes[0].stop()



        for bl in range(40):
            self.nodes[self.num_nodes - 1].generate(1)
            set_mocktime(get_mocktime() + 150)
            for k in range(self.num_nodes):
                if (k!=0):
                    self.nodes[k].setmocktime(get_mocktime())
            sync_blocks(self.nodes[1:])

        tnode_list = self.nodes[self.num_nodes - 1].tnodelist()
        self.log.info("2 tnode list {} ".format(tnode_list))

#        for zn in range(self.num_nodes):
#            self.log.info("full balance {}".format(get_full_balance(self.nodes[zn])))

        tnode_list = self.nodes[self.num_nodes - 1].tnodelist()
        self.log.info("tnode list {} ".format(tnode_list))
        for zno, status in tnode_list.items():
            if self.collateral[0].tx_id in zno:
                assert_equal(status, "NEW_START_REQUIRED")

        self.nodes[0] = start_node(0,self.options.tmpdir)

if __name__ == '__main__':
    TnodeCheckStatus().main()

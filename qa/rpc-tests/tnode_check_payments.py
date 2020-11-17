#!/usr/bin/env python3
import time
from decimal import *

from test_framework.test_framework import TnodeTestFramework
from test_framework.util import *

# Description: a very straightforward check of Tnode operability
# 1. Start several nodes
# 2. Mine blocks and check the reward comes to the proper nodes
# 3. Shut down a node and make sure no more reward
# 5+6n formula is explained at the bottom of the file

class TnodeCheckPayments(TnodeTestFramework):
    def __init__(self):
        super().__init__()
        self.num_nodes = 6
        self.num_tnodes = 4
        self.setup_clean_chain = False

    def run_test(self):

        for zn in range(self.num_tnodes):
            self.generate_tnode_collateral()
            collateral = self.send_mature_tnode_collateral(zn)
            self.generate_tnode_privkey(zn)
            self.write_master_tnode_conf(zn, collateral)


        self.generate_tnode_collateral()
        collateral3 = self.send_mature_tnode_collateral(4)
        
        for zn in range(self.num_tnodes):
            self.restart_as_tnode(zn)
            self.tnode_start(zn)


        self.generate(1)  
        self.sync_all()
        self.wait_tnode_enabled(self.num_tnodes)
        self.generate(4 + 3*6)

        for zn in range(self.num_tnodes):
            self.log.info("full balance {}".format(get_full_balance(self.nodes[zn])))
            assert_equal(10175.5, get_full_balance(self.nodes[zn]))

# New Tnode
        self.generate_tnode_privkey(3)
        self.write_master_tnode_conf(3, collateral3)

        self.restart_as_tnode(3)
        self.tnode_start(3)

        self.wait_tnode_enabled(4)

       
        self.generate(1) 
        self.generate(1) 
        self.generate(1) 
        self.generate(1) 
        self.generate(1)  
        for zn in range(self.num_tnodes):
            self.log.info("full balance {}".format(get_full_balance(self.nodes[zn])))
            assert_equal(10219.375, get_full_balance(self.nodes[zn]))
        self.log.info("full balance {}".format(get_full_balance(self.nodes[3])))
        assert_equal(10219.375, get_full_balance(self.nodes[3]))

# Spend Tnode output
        generator_address = self.nodes[self.num_nodes - 1].getaccountaddress("")
        tnode_output = self.nodes[3].listlockunspent()
        self.nodes[3].lockunspent(True, tnode_output)
        self.nodes[3].sendtoaddress(generator_address, 10000, "", "", True)

        self.generate(1) #The Tnode has been scheduled already, need one run for the schedule to get updated
        self.generate(1) 
        self.generate(1) 
        self.generate(1) 
        self.generate(1) 
        self.generate(1)        
        self.log.info("full balance {}".format(get_full_balance(self.nodes[3])))
        self.log.info("full balance node 0 {}".format(get_full_balance(self.nodes[0])))        
        assert_equal(263.25, get_full_balance(self.nodes[3]))

#        self.generate(2*6)
        self.generate(1) #The Tnode has been scheduled already, need one run for the schedule to get updated
        self.generate(1) 
        self.generate(1) 
        self.generate(1) 
        self.generate(1) 
        self.generate(1)        
        self.generate(1) #The Tnode has been scheduled already, need one run for the schedule to get updated
        self.generate(1) 
        self.generate(1) 
        self.generate(1) 
        self.generate(1) 
        self.generate(1)        
        self.sync_all()        
        self.log.info("full balance {}".format(get_full_balance(self.nodes[3])))
        self.log.info("full balance node 0 {}".format(get_full_balance(self.nodes[0])))           
        assert_equal(351, get_full_balance(self.nodes[3]))

        for zn in range(self.num_tnodes-1):
            self.log.info("full balance {}".format(get_full_balance(self.nodes[zn])))
            assert_equal(10351, get_full_balance(self.nodes[zn]))
if __name__ == '__main__':
    TnodeCheckPayments().main()

# 5+6n formula
# Let's say we are starting 3 Tnodes at once. The first full round when all Tnodes are payed finishes in 5 blocks. All
# the subsequent runs will take 6 blocks.

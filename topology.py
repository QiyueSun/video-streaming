#!/usr/bin/env python
"""

Measurement topology for EECS489, Fall 2020, Assignment 2
"""

from mininet.cli import CLI
from mininet.net import Mininet
from mininet.link import TCLink
from mininet.topo import Topo
from mininet.log import setLogLevel

class AssignmentNetworks(Topo):
    def __init__(self, **opts):
        Topo.__init__(self, **opts)
        h1 = self.addHost('h1')
        h2 = self.addHost('h2')
        h3 = self.addHost('h3')
        h4 = self.addHost('h4')
        h5 = self.addHost('h5')
        s0 = self.addSwitch('s0')
        self.addLink(h1, s0, bw=20, delay='40ms')
        self.addLink(h2, s0, bw=40, delay='10ms')
        self.addLink(h3, s0, bw=30, delay='30ms')
        self.addLink(h4, s0, bw=25, delay='5ms')
        self.addLink(h5, s0, bw=25, delay='5ms')        

        
if __name__ == '__main__':
    setLogLevel( 'info' )

    # Create data network
    topo = AssignmentNetworks()
    net = Mininet(topo=topo, link=TCLink, autoSetMacs=True,
           autoStaticArp=True)

    # Run network
    net.start()
    CLI( net )
    net.stop()


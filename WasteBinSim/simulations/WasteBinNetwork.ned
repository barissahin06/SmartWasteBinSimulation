package wastebinsim.simulations;

import inet.node.inet.Router;
import inet.node.internetcloud.InternetCloud;
import inet.node.inet.StandardHost;
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import inet.node.ethernet.Eth1G;

import flora.LoRaPhy.LoRaMedium;
import flora.LoraNode.LoRaNode;
import flora.LoraNode.LoRaGW;

network WasteBinNetwork
{
    parameters:
        int numberOfNodes = default(5);
        int numberOfGateways = default(1);
        int networkSizeX = default(500);
        int networkSizeY = default(500);
        @display("bgb=1100,450");

    submodules:
        loRaNodes[numberOfNodes]: LoRaNode {
            @display("p=400,300,row,90;i=block/bucket");
        }

        loRaGW[numberOfGateways]: LoRaGW {
            @display("p=180,240;is=s");
        }

        LoRaMedium: LoRaMedium {
            @display("p=350,100");
        }

        networkServer: StandardHost {
            parameters:
                @display("p=70,60");
        }

        configurator: Ipv4NetworkConfigurator {
            parameters:
                assignDisjunctSubnetAddresses = false;
                @display("p=360,35");
        }

        internetCloud: InternetCloud {
            @display("p=250,60");
        }

        gwRouter[numberOfGateways]: Router {
            @display("p=250,150");
        }

        nsRouter: Router {
            @display("p=150,60");
        }

    connections:
        networkServer.ethg++ <--> Eth1G <--> nsRouter.ethg++;
        nsRouter.pppg++ <--> Eth1G <--> internetCloud.pppg++;

        for i=0..numberOfGateways-1 {
            internetCloud.pppg++ <--> Eth1G <--> gwRouter[i].pppg++;
            gwRouter[i].ethg++ <--> Eth1G <--> loRaGW[i].ethg++;
        }
}

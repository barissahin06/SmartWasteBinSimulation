package wastebinsim;

import inet.applications.contract.IApp;

simple SmartBinLoRaApp like IApp
{
    parameters:
        @class(wastebinsim::SmartBinLoRaApp);

        int binId = default(0);
        string zoneType = default("residential");

        double initialFill = default(10);
        double maxCapacity = default(100);
        double warningThreshold = default(70);
        double criticalThreshold = default(90);
        double meanGrowth = default(3);

        double growthInterval @unit(s) = default(300s);
        double reportInterval @unit(s) = default(900s);
        double minSendGap @unit(s) = default(8s);
        
        int numberOfPacketsToSend = default(0);

        int expectedOverflowAlerts = default(5);
        double autoStopDelay @unit(s) = default(10s);

        double initialLoRaTP @unit(dBm) = default(14dBm);
        double initialLoRaCF @unit(Hz) = default(868MHz);
        int initialLoRaSF = default(12);
        double initialLoRaBW @unit(Hz) = default(125kHz);
        int initialLoRaCR = default(4);
        bool initialUseHeader = default(true);
        bool evaluateADRinNode = default(false);
        int dataSize @unit(B) = default(20B);

        @display("i=block/bucket");

    gates:
        input socketIn;
        output socketOut;
}
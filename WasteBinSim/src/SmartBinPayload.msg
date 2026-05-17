import LoRaApp.LoRaAppPacket;

namespace wastebinsim;

class SmartBinPayload extends flora::LoRaAppPacket
{
    int binId;
    string zoneType;
    int messageKind;
    double fillLevel;
    double maxCapacity;
    double warningThreshold;
    double criticalThreshold;
    simtime_t eventTime;
}

#include "SmartBinLoRaApp.h"

namespace wastebinsim {

Define_Module(SmartBinLoRaApp);

int SmartBinLoRaApp::globalOverflowAlerts = 0;
bool SmartBinLoRaApp::globalEndScheduled = false;

enum BinMessageKinds {
    STATUS_UPDATE = 1,
    THRESHOLD_ALERT = 2,
    OVERFLOW_ALERT = 3
};

void SmartBinLoRaApp::initialize(int stage)
{
    cSimpleModule::initialize(stage);

    if (stage == INITSTAGE_LOCAL) {
        binId = par("binId");
        zoneType = par("zoneType").stdstringValue();

        if (binId == 0) {
            globalOverflowAlerts = 0;
            globalEndScheduled = false;
        }

        fillLevel = par("initialFill").doubleValue();
        maxCapacity = par("maxCapacity").doubleValue();
        warningThreshold = par("warningThreshold").doubleValue();
        criticalThreshold = par("criticalThreshold").doubleValue();
        meanGrowth = par("meanGrowth").doubleValue();

        numberOfPacketsToSend = par("numberOfPacketsToSend");
        minSendGap = par("minSendGap").doubleValue();
        nextAllowedSendTime = 0;

        fillLevelSignal = registerSignal("fillLevel");
        appPacketSentSignal = registerSignal("SmartBinLoRaPacketSent");

        growthTimer = new cMessage("growthTimer");
        reportTimer = new cMessage("reportTimer");
        endTimer = new cMessage("endSimulationTimer");

        EV << "SmartBinLoRaApp initialized. binId=" << binId
           << " zone=" << zoneType
           << " initialFill=" << fillLevel << "\n";
    }
    else if (stage == INITSTAGE_APPLICATION_LAYER) {
        NodeStatus *nodeStatus = dynamic_cast<NodeStatus *>(findContainingNode(this)->getSubmodule("status"));
        bool isOperational = (!nodeStatus) || nodeStatus->getState() == NodeStatus::UP;

        if (!isOperational)
            throw cRuntimeError("SmartBinLoRaApp does not support starting in DOWN state");

        loRaRadio = check_and_cast<flora::LoRaRadio *>(
            getParentModule()->getSubmodule("LoRaNic")->getSubmodule("radio")
        );

        loRaRadio->loRaTP = par("initialLoRaTP").doubleValue();
        loRaRadio->loRaCF = units::values::Hz(par("initialLoRaCF").doubleValue());
        loRaRadio->loRaSF = par("initialLoRaSF");
        loRaRadio->loRaBW = units::values::Hz(par("initialLoRaBW").doubleValue());
        loRaRadio->loRaCR = par("initialLoRaCR");
        loRaRadio->loRaUseHeader = par("initialUseHeader");

        scheduleAt(simTime() + uniform(0, par("growthInterval").doubleValue()), growthTimer);
        scheduleAt(simTime() + uniform(0, par("reportInterval").doubleValue()), reportTimer);

        EV << "SmartBinLoRaApp application layer ready for bin " << binId << "\n";
    }
}

void SmartBinLoRaApp::handleMessage(cMessage *msg)
{
    if (msg->isSelfMessage()) {
        if (msg == growthTimer) {
            handleGrowthEvent();
        }
        else if (msg == reportTimer) {
            handleReportEvent();
        }
        else if (msg == endTimer) {
            EV << "Automatic stop timer expired. Ending simulation now.\n";
            endSimulation();
        }
    }
    else {
        receivedPackets++;

        EV << "SmartBinLoRaApp received downlink/control packet at bin "
           << binId << "\n";

        delete msg;
    }
}

void SmartBinLoRaApp::handleGrowthEvent()
{
    double increment = exponential(meanGrowth);
    fillLevel += increment;

    if (fillLevel > maxCapacity * 1.2)
        fillLevel = maxCapacity * 1.2;

    emit(fillLevelSignal, fillLevel);

    EV << "LoRa bin " << binId
       << " fill level increased to " << fillLevel
       << " zone=" << zoneType << "\n";

    /*
     * Important:
     * Only one LoRa packet is generated in one growth event.
     * Otherwise, warning + critical + overflow may be sent at the same simulation time,
     * and LoRaRadio may throw "already transmitting" error.
     */
    if (!overflowSent && fillLevel >= maxCapacity) {
        sendBinPacket("overflowAlert", OVERFLOW_ALERT);
        overflowSent = true;

        globalOverflowAlerts++;

        EV << "Overflow alert count: " << globalOverflowAlerts
           << " / " << par("expectedOverflowAlerts").intValue() << "\n";

        if (!globalEndScheduled &&
            globalOverflowAlerts >= par("expectedOverflowAlerts").intValue()) {

            globalEndScheduled = true;

            EV << "All expected overflow alerts were generated. "
               << "Simulation will stop after "
               << par("autoStopDelay").doubleValue()
               << " seconds to allow final LoRa forwarding.\n";

            scheduleAt(simTime() + par("autoStopDelay"), endTimer);
        }
    }
    else if (!criticalSent && fillLevel >= criticalThreshold) {
        sendBinPacket("thresholdCritical", THRESHOLD_ALERT);
        criticalSent = true;
    }
    else if (!warningSent && fillLevel >= warningThreshold) {
        sendBinPacket("thresholdWarning", THRESHOLD_ALERT);
        warningSent = true;
    }

    scheduleAt(simTime() + par("growthInterval"), growthTimer);
}

void SmartBinLoRaApp::handleReportEvent()
{
    sendBinPacket("statusUpdate", STATUS_UPDATE);
    scheduleAt(simTime() + par("reportInterval"), reportTimer);
}

void SmartBinLoRaApp::sendBinPacket(const char *name, int kind)
{
    if (simTime() < nextAllowedSendTime) {
        EV << "LoRa bin " << binId
           << " skipped " << name
           << " because radio is still in recent transmission period.\n";
        return;
    }

    nextAllowedSendTime = simTime() + minSendGap;
    auto pkt = new Packet(name);
    pkt->setKind(flora::DATA);

    auto payload = makeShared<flora::LoRaAppPacket>();
    payload->setChunkLength(B(par("dataSize").intValue()));

    /*
     * We encode waste-bin message type in sampleMeasurement:
     * 1 = status update
     * 2 = threshold alert
     * 3 = overflow alert
     */
    payload->setSampleMeasurement(kind);

    auto loraTag = pkt->addTagIfAbsent<flora::LoRaTag>();
    loraTag->setBandwidth(getBW());
    loraTag->setCenterFrequency(getCF());
    loraTag->setSpreadFactor(getSF());
    loraTag->setCodeRendundance(getCR());
    loraTag->setPower(mW(math::dBmW2mW(getTP())));

    pkt->insertAtBack(payload);

    send(pkt, "socketOut");

    sentPackets++;
    emit(appPacketSentSignal, kind);

    EV << "LoRa bin " << binId
       << " sent " << name
       << " kind=" << kind
       << " fillLevel=" << fillLevel
       << " zone=" << zoneType << "\n";
}

void SmartBinLoRaApp::finish()
{
    recordScalar("finalFillLevel", fillLevel);
    recordScalar("sentPackets", sentPackets);
    recordScalar("receivedPackets", receivedPackets);
    recordScalar("warningSent", warningSent ? 1 : 0);
    recordScalar("criticalSent", criticalSent ? 1 : 0);
    recordScalar("overflowSent", overflowSent ? 1 : 0);

    if (binId == 0) {
        recordScalar("globalOverflowAlerts", globalOverflowAlerts);
    }
}

bool SmartBinLoRaApp::handleOperationStage(LifecycleOperation *operation, IDoneCallback *doneCallback)
{
    Enter_Method_Silent();
    throw cRuntimeError("Unsupported lifecycle operation '%s'", operation->getClassName());
    return true;
}

SmartBinLoRaApp::~SmartBinLoRaApp()
{
    cancelAndDelete(growthTimer);
    cancelAndDelete(reportTimer);
    cancelAndDelete(endTimer);
}

void SmartBinLoRaApp::setSF(int sf)
{
    loRaRadio->loRaSF = sf;
}

int SmartBinLoRaApp::getSF()
{
    return loRaRadio->loRaSF;
}

void SmartBinLoRaApp::setTP(double tp)
{
    loRaRadio->loRaTP = tp;
}

double SmartBinLoRaApp::getTP()
{
    return loRaRadio->loRaTP;
}

void SmartBinLoRaApp::setCR(int cr)
{
    loRaRadio->loRaCR = cr;
}

int SmartBinLoRaApp::getCR()
{
    return loRaRadio->loRaCR;
}

void SmartBinLoRaApp::setCF(units::values::Hz cf)
{
    loRaRadio->loRaCF = cf;
}

units::values::Hz SmartBinLoRaApp::getCF()
{
    return loRaRadio->loRaCF;
}

void SmartBinLoRaApp::setBW(units::values::Hz bw)
{
    loRaRadio->loRaBW = bw;
}

units::values::Hz SmartBinLoRaApp::getBW()
{
    return loRaRadio->loRaBW;
}

}

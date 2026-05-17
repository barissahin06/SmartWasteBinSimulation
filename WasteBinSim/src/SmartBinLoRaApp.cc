#include "SmartBinLoRaApp.h"

namespace wastebinsim {

Define_Module(SmartBinLoRaApp);

int SmartBinLoRaApp::globalOverflowAlerts = 0;
bool SmartBinLoRaApp::globalEndScheduled = false;

enum BinMessageKinds {
    STATUS_UPDATE = 1,
    THRESHOLD_WARNING = 2,
    THRESHOLD_CRITICAL = 3,
    OVERFLOW_ALERT = 4
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

    bool warningReached = fillLevel >= warningThreshold;
    bool criticalReached = fillLevel >= criticalThreshold;
    bool overflowReached = fillLevel >= maxCapacity;

    if (warningReached && !warningDetected) {
        warningDetected = true;
        firstWarningTime = simTime();
        warningThresholdDetections++;
    }

    if (criticalReached && !criticalDetected) {
        criticalDetected = true;
        firstCriticalTime = simTime();
        criticalThresholdDetections++;
    }

    if (overflowReached && !overflowDetected) {
        overflowDetected = true;
        firstOverflowTime = simTime();
        overflowDetections++;
    }

    if (overflowReached && !overflowSent) {
        sendBinPacket("overflowAlert", OVERFLOW_ALERT);
        overflowSent = true;
        criticalSent = true;
        warningSent = true;

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
    else if (!overflowSent && criticalReached && !criticalSent) {
        sendBinPacket("thresholdCritical", THRESHOLD_CRITICAL);
        criticalSent = true;
        warningSent = true;
    }
    else if (!overflowSent && warningReached && !warningSent) {
        sendBinPacket("thresholdWarning", THRESHOLD_WARNING);
        warningSent = true;
    }

    scheduleAt(simTime() + par("growthInterval"), growthTimer);
}

void SmartBinLoRaApp::handleReportEvent()
{
    sendBinPacket("statusUpdate", STATUS_UPDATE);
    scheduleAt(simTime() + par("reportInterval"), reportTimer);
}

bool SmartBinLoRaApp::sendBinPacket(const char *name, int kind)
{
    if (simTime() < nextAllowedSendTime) {
        EV << "LoRa bin " << binId
           << " skipped " << name
           << " because radio is still in recent transmission period.\n";
        return false;
    }

    nextAllowedSendTime = simTime() + minSendGap;
    auto pkt = new Packet(name);
    pkt->setKind(flora::DATA);

    auto payload = makeShared<SmartBinPayload>();
    payload->setChunkLength(B(par("dataSize").intValue()));
    payload->setSampleMeasurement(getLegacySampleMeasurement(kind));
    payload->setBinId(binId);
    payload->setZoneType(zoneType.c_str());
    payload->setMessageKind(kind);
    payload->setFillLevel(fillLevel);
    payload->setMaxCapacity(maxCapacity);
    payload->setWarningThreshold(warningThreshold);
    payload->setCriticalThreshold(criticalThreshold);
    payload->setEventTime(simTime());

    auto loraTag = pkt->addTagIfAbsent<flora::LoRaTag>();
    loraTag->setBandwidth(getBW());
    loraTag->setCenterFrequency(getCF());
    loraTag->setSpreadFactor(getSF());
    loraTag->setCodeRendundance(getCR());
    loraTag->setPower(mW(math::dBmW2mW(getTP())));

    pkt->insertAtBack(payload);

    send(pkt, "socketOut");

    sentPackets++;
    countSentPacket(kind);
    emit(appPacketSentSignal, kind);

    EV << "LoRa bin " << binId
       << " sent " << name
       << " kind=" << kind
       << " binId=" << binId
       << " fillLevel=" << fillLevel
       << " zone=" << zoneType << "\n";

    return true;
}

int SmartBinLoRaApp::getLegacySampleMeasurement(int kind) const
{
    if (kind == STATUS_UPDATE)
        return 1;
    if (kind == OVERFLOW_ALERT)
        return 3;
    return 2;
}

void SmartBinLoRaApp::countSentPacket(int kind)
{
    if (kind == STATUS_UPDATE)
        statusUpdatePacketsSent++;
    else if (kind == THRESHOLD_WARNING)
        thresholdWarningPacketsSent++;
    else if (kind == THRESHOLD_CRITICAL)
        thresholdCriticalPacketsSent++;
    else if (kind == OVERFLOW_ALERT)
        overflowAlertPacketsSent++;
}

void SmartBinLoRaApp::finish()
{
    recordScalar("finalFillLevel", fillLevel);
    recordScalar("sentPackets", sentPackets);
    recordScalar("receivedPackets", receivedPackets);
    recordScalar("warningSent", warningSent ? 1 : 0);
    recordScalar("criticalSent", criticalSent ? 1 : 0);
    recordScalar("overflowSent", overflowSent ? 1 : 0);
    recordScalar("statusUpdatePacketsSent", statusUpdatePacketsSent);
    recordScalar("thresholdWarningPacketsSent", thresholdWarningPacketsSent);
    recordScalar("thresholdCriticalPacketsSent", thresholdCriticalPacketsSent);
    recordScalar("overflowAlertPacketsSent", overflowAlertPacketsSent);
    recordScalar("warningThresholdDetections", warningThresholdDetections);
    recordScalar("criticalThresholdDetections", criticalThresholdDetections);
    recordScalar("overflowDetections", overflowDetections);
    recordScalar("firstWarningTime", warningDetected ? firstWarningTime.dbl() : -1);
    recordScalar("firstCriticalTime", criticalDetected ? firstCriticalTime.dbl() : -1);
    recordScalar("firstOverflowTime", overflowDetected ? firstOverflowTime.dbl() : -1);

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

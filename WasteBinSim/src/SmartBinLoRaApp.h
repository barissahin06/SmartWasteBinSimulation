#ifndef __SMARTBINLORAAPP_H_
#define __SMARTBINLORAAPP_H_

#include <omnetpp.h>
#include <string>

#include "inet/common/lifecycle/ILifecycle.h"
#include "inet/common/lifecycle/NodeStatus.h"
#include "inet/common/ModuleAccess.h"
#include "inet/common/lifecycle/LifecycleOperation.h"
#include "inet/common/packet/Packet.h"
#include "inet/mobility/static/StationaryMobility.h"

#include "LoRaApp/LoRaAppPacket_m.h"
#include "LoRa/LoRaTagInfo_m.h"
#include "LoRa/LoRaRadio.h"

using namespace omnetpp;
using namespace inet;

namespace wastebinsim {

class SmartBinLoRaApp : public cSimpleModule, public ILifecycle
{
  private:
    cMessage *growthTimer = nullptr;
    cMessage *reportTimer = nullptr;
    cMessage *endTimer = nullptr;

    int binId = -1;
    std::string zoneType;

    double fillLevel = 0;
    double maxCapacity = 100;
    double warningThreshold = 70;
    double criticalThreshold = 90;
    double meanGrowth = 3;

    bool warningSent = false;
    bool criticalSent = false;
    bool overflowSent = false;

    int sentPackets = 0;
    int receivedPackets = 0;
    int numberOfPacketsToSend = 0;
    simtime_t nextAllowedSendTime = 0;
    double minSendGap = 8;

    static int globalOverflowAlerts;
    static bool globalEndScheduled;

    simsignal_t fillLevelSignal;
    simsignal_t appPacketSentSignal;

    flora::LoRaRadio *loRaRadio = nullptr;

  protected:
    virtual void initialize(int stage) override;
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;
    virtual bool handleOperationStage(LifecycleOperation *operation, IDoneCallback *doneCallback) override;
    virtual ~SmartBinLoRaApp() override;

    void handleGrowthEvent();
    void handleReportEvent();
    void sendBinPacket(const char *name, int kind);

    void setSF(int sf);
    int getSF();
    void setTP(double tp);
    double getTP();
    void setCR(int cr);
    int getCR();
    void setCF(units::values::Hz cf);
    units::values::Hz getCF();
    void setBW(units::values::Hz bw);
    units::values::Hz getBW();
};

}

#endif

# Smart Waste Bin Simulation

Interim project for CNG 476, focused on simulating a LoRa-based smart waste bin monitoring system in OMNeT++.

## Authors

- Barış Şahin
- Akın Özsaygın

## Project Context

This repository contains an OMNeT++ simulation project under `WasteBinSim/`. The simulation models smart waste bins that periodically report fill levels and send threshold or overflow alerts through a LoRa network.

The network includes:

- Smart waste bin LoRa nodes
- A LoRa gateway
- A network server
- IP routing components from INET
- LoRa/FLoRa simulation components

The custom application logic is implemented in `WasteBinSim/src/SmartBinLoRaApp.cc` and configured through `WasteBinSim/simulations/omnetpp.ini`.

## Project Structure

```text
WasteBinSim/
├── src/
│   ├── SmartBinLoRaApp.cc
│   ├── SmartBinLoRaApp.h
│   ├── SmartBinLoRaApp.ned
│   └── Makefile
└── simulations/
    ├── WasteBinNetwork.ned
    ├── omnetpp.ini
    └── run
```

## Requirements

- OMNeT++
- INET 4.4
- FLoRa

The project expects INET and FLoRa to be available to OMNeT++ as referenced projects or through the paths used by the Makefiles and simulation configuration.

## Build and Run

From the project directory:

```sh
cd WasteBinSim
make makefiles
make
cd simulations
./run
```

Simulation results are written under `WasteBinSim/simulations/results/`.

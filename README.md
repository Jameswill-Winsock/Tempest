# Tempest : Open Hardware EM Side-Channel Analysis Platform

## Important:
if you are here looking for review, firmware, gerber and pcb have been updated. There is no step models or 3d models for this project.
Firmware is a fork of the SIPEED RV Debugger Plus, ported from the BL507 to the RP2040.
All pcb files and gerbers have been uploaded for the carrier board for the primer. Rest of the scanning pcbs are in development. Gimme 2 weeks. (Yes I won't send it in for rereview until then)

latest readme for progress on the attack target available [here](rtl/shrike_accel_v1/readme.md).

# Status
Early build. Built for Stardance @ Hack Club.

# Current progress

## Done:
- Shrike-Lite ML-KEM accelerator is functionally complete (currently fighting Renesas place-and-route to get the final bitstream out).
- Finished designing the Tang Primer 20K carrier board.
<img width="1463" height="813" alt="image" src="https://github.com/user-attachments/assets/c52b16d7-a638-44e0-89c9-28c95248f62d" />
<img width="1327" height="789" alt="image" src="https://github.com/user-attachments/assets/aedf5bbf-7166-4cd2-9735-d3a87f8f4efd" />



## In progress:
- Designing the rest of the Tempest hardware stack (probe, analog front-end, calibration hardware, etc.)

If you're here for a demo video, give me a little time. Renesas Go Configure is... an experience, and the place-and-route tool keeps blowing up violently in my face.

# Description

Tempest is an open hardware electromagnetic side-channel analysis platform built around a collection of custom-designed PCBs, analog front-end hardware, FPGA development boards, and open-source software for evaluating cryptographic implementations.

Or, in simpler English:
Every chip leaks tiny electromagnetic signals while it's running. Those signals aren't supposed to contain useful information... but sometimes they do. Tempest tries to measure those leaks and figure out whether they reveal anything about the cryptographic algorithm running inside.
The project is built around Shrike-Lite, my ML-KEM (Kyber) hardware accelerator running on a hilariously tiny Renesas SLG47910 FPGA. Shrike answers one question: "Can modern post-quantum cryptography fit on ridiculously small hardware?"
Tempest answers the obvious follow-up: "Cool. *Now how badly does it leak?* <img width="32" height="32" alt="trollface" src="https://github.com/user-attachments/assets/9bc998d5-3169-4e2b-b7f6-e85d66522c68" />
". 
Rather than buying a commercial side-channel lab, the goal is to design as much of the measurement hardware as possible from scratch so anyone can build the same setup themselves.

## Why EM instead of power analysis?

Power analysis is the classic approach.
The problem is... it usually means taking a knife to your PCB. You know, cut traces, insert shunt resistors, resolder stuff. That's perfectly fine on a $2 dev board. I ain't sure as hell doing allat to a motherboard I actually want to keep using afterwards.
EM analysis measures the exact same switching currents, except instead of touching the power rail, you simply hold a small magnetic loop probe near the chip and measure the field it radiates. It's completely non-invasive, portable, and lets me move between different targets without modifying the hardware.

# Project Architecture

<img width="1529" height="654" alt="image" src="https://github.com/user-attachments/assets/4d93edb7-ad8e-4225-bf47-7403a4a83900" />

Tempest is really two projects that work together.
```
Shrike
(Resource-constrained ML-KEM accelerator)

            ↓

Produces real electromagnetic leakage

            ↓

Tempest
(Open hardware instrumentation platform)

            ↓

Measures leakage

            ↓

Correlation analysis

            ↓

Can we recover useful information?
```

**The long-term hardware stack looks like this:**
```
Target FPGA / TPM
        │
        ▼
Shielded EM Probe
        │
        ▼
Active Probe + LNA
        │
        ▼
Filter / Analog Front-End
        │
        ▼
Capture Hardware
        │
        ▼
Python Analysis Pipeline
```
# Hardware Designed For This Project

The goal is to design as much of the hardware myself as possible instead of buying commercial equipment. 

| Hardware | Status |
|---|---|
|Shrike-Lite ML-KEM Accelerator | Complete (bitstream generation pending) |
|Tang Primer 20K Carrier Board | Complete |
|Shielded PCB EM Probe | In design |
|Active Probe / LNA | In design |
|Analog Filter Board | Planned |
|Probe Calibration Board | Planned |
|FPGA Capture Hardware | Future revision |

# Roadmap
- Finish Shrike-Lite bring-up.
- Build and validate a passive EM probe.
- Design an active probe with integrated low-noise amplification.
- Build a calibration board to characterize probe performance.
- Validate the analog front-end on a deliberately leaky AES implementation before touching Kyber.
- Build the trigger/synchronization pipeline.
- Perform correlation EM analysis against Shrike-Lite.
- Scale to the Tang Primer 20K as a larger TPM stand-in.
- Eventually move to a real motherboard TPM once the entire workflow is validated.

# Original Hardware Contributions
Unlike the initial revision of this project, Tempest now focuses on designing the measurement hardware itself rather than simply integrating commercial equipment. Current hardware work includes:
- Tang Primer 20K carrier board
- Shielded PCB EM probe
- Active probe with integrated low-noise amplifier
- Probe calibration fixture
- Analog filter board
Commercial equipment (oscilloscope, SDR, etc.) is only used for validation and debugging during development.

# Bill of Materials

| Item | Est. Price (USD) | Source | Why |
|---|---|---|---|
|Tang Nano 20k | $35–40 | [REES52](https://rees52.com/products/sipeed-tang-nano-20k-fpga-board-high-performance-dev-board-for-robotics-electronics) | Main FPGA for trigger generation, timing/synchronization, counters, buffering/FIFOs, and control logic |
|Tang Primer 20K Carrier PCB | ~$20 | Self-designed | Carrier for the Primer module (I already own the core board) |
|Passive Probe PCB | ~$15 | Self-designed | Shielded PCB H-field probe|
|Active Probe PCB | ~$25 | Self-designed | Integrated low-noise amplifier |
|Probe Calibration PCB | ~$15 | Self-designed | Repeatable EM reference source |
|RTL-SDR | $30–40 | [RTL-SDR Aliexpress](https://www.aliexpress.com/item/1005005952566458.html) | Initial low-cost capture experiments |
|Components for Active Probe PCB | $20–40 | [Mini-Circuits](https://www.minicircuits.com/) | Prototype analog front-end |
|SMA Connectors, RG178, Adapters, Attenuators |	$30–60 | [Pasternack](https://www.pasternack.com/), [Amphenol](https://www.amphenolrf.com/en-us/), [Mini-Circuits](https://www.minicircuits.com/) | RF interconnects |
|Magnet Wire, Ferrites | $10–20 | [Remington Industries](https://www.remingtonindustries.com), [Fair-rite](https://fair-rite.com)  | Probe construction |
|Manual XY Positioning Stage | $30–40 | [Example at amazon](https://www.amazon.in/Mechanical-Moveable-Microscope-Caliper-Precision/dp/B07VPQT851) | Precise probe positioning |
|PCB fabrication & assembly | ~$50 | JLCPCB | Manufacturing the custom boards|
|Miscellaneous passives, shielding, perfboard | $20–40 | Digikey/Mouser/As per need | Prototyping (I am not building any PCB until I have a known good circuit) |
|FTDI Programmer or SiPEED programmer | ~25-50$ | [Mouser](https://www.mouser.in/en/ProductDetail/FTDI/FT2232H-MINI-MODULE?qs=pB3G9VbQXIf%252BpWyngo5ZjA%3D%3D&mgh=1) or a [SiPEED RV Debugger Plus](https://www.aliexpress.com/item/1005011815481146.html) | To program the FPGAs |

**Grand total**: anywhere from 325-450$ (can be rounded off to **$400 approx**)
The oscilloscope, precision positioning stage, and laboratory power supply have been removed from the grant request. The initial revision of Tempest is designed to be developed using low-cost hardware and custom-designed PCBs, with higher-end laboratory equipment treated as optional future upgrades rather than project requirements.
**BOM Revision 2**: The Tang Mega 138K, Thorlabs positioning equipment, and benchtop power supply have been removed due to cost. They have been replaced with lower-cost alternatives where necessary. A second, lower-cost FPGA is still required for <u>high-speed deterministic logic, trigger generation, timing/synchronization, buffering, and real-time control,</u> which cannot be reliably handled by the host computer or RTL-SDR alone.

# Repository Layout
- rtl: Shrike-Lite accelerator, Future hardware targets

- hardware: KiCad projects, Carrier boards, Probe boards, Analog front-end

- analysis: Capture software, CPA/CEMA pipeline

- docs: Research notes, Measurements, Design reviews


# Why this matters

Most public EM side-channel work either targets toy AES implementations or uses lab-grade near-field scanning rigs that cost tens of thousands of dollars. There's a real gap in documented, reproducible, low-cost EM side-channel analysis against post-quantum crypto specifically; ML-KEM side-channel resistance is an active research area (and a pretty hot one too - there's a hell lot of researchers taking a crack at it) and having a from-scratch hardware+software stack to poke at it is worth more to me than a paper result I can't independently reproduce.

If it works, you'll be able to:
- build the hardware,
- reproduce the measurements,
- attack real cryptographic implementations,
- and improve your own hardware designs by seeing exactly where they leak.

Besides...
The math behind modern cryptography is ridiculously strong. Turns out the easiest way to break it is often just listening to the chip while it does the math. It's cool, isn't it? The math can't be cracked, but we sure as hell can crack the data anyways because of bad engineering decisions. :)


# License
GNU GPLv3

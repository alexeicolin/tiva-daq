Synopsis
========

Data Acquisition (DAQ) device based on the ADC in TI Tiva C microcontroller.

This utility provides a means for configuring each of the (four) sequencers the
on the (two) ADCs in Tiva C (TM4C123) microcontroller in JSON format and
obtaining the data in a set of CSV files (one per sequencer) on the host.

The hardware is *not* abstracted, by design. There are *no* "virtual channels"
that are magically mapped to physical ADC hardware. Instead, the ADC hardware
is presented to the user as it is. In order to make sense of the configuration,
it is necessary to read the TM4C123 datasheet that describes the ADC sequencer
hardware architecture. However, the *means* of configuration are at a high
level (JSON) for convenience. The data delivery chain from the ADC, to packets
on UART, all the way to CSV files on the host is deliberately abstracted.

Example usage outline:

  1. Configure the hardware in `adc-config.json`, for example:

        {
            "adcs": {
                "0": {
                    "triggerTimer": { "idx": 1, "half": "A" },
                    "samplesPerSec": 10,
                    "hwAvgFactor": 16,
                    "seqs": {
                        "0": {
                            "name": "voltages",
                            "priority": 0,
                            "trigger": "timer",
                            "bufSize": 512,
                            "samples": [
                                { "Vin":  "A0" },
                                { "Vout": "A1" },
                            ]
                        },
                        "3": {
                            "name": "environment",
                            "priority": 1,
                            "trigger": "processor",
                            "bufSize": 64,
                            "samples": [
                                { "temp": "TS" }
                            ]
                        }
                    }
                }
            }
        }

    2. Build and flash the target application (see section below).

    3. Setup data recording on the host. The following assumes a UART->USB
       hooked up to UART *port 1*, since port 0 is used for console log output
       (configurable in `daq.cfg`):

        $ stty -F /dev/ttyUSB0 raw
        $ cat /dev/ttyUSB0 > example.dat

    4. Reset the board (by default, data collection starts on reset).

    5. Parse the data stream into CSV (third arg is the prefix for output files):

        $ host/daq-to-csv.py tivadaq/daq-config.json example.dat example

    6. Find your data in `<prefix>.<sequence_name>.csv`:

        $ head example.voltages.csv
        Vin,Vout
        3.299194,1.465503
        3.299194,1.474365

        $ head example.environment.csv
        temp
        15.592651
        15.713501

Dependencies
============

  * [XDCTools](http://downloads.ti.com/dsps/dsps_public_sw/sdo_sb/targetcontent/rtsc/)
    >= 3.30

  * [SYSBIOS](http://www.ti.com/tool/sysbios) (also part of
    [TI-RTOS](http://www.ti.com/tool/ti-rtos) v2)

  * [TivaWare](http://www.ti.com/tool/sw-tm4c) (also part of
    [TI-RTOS](http://www.ti.com/tool/ti-rtos) v2)

  * [rtsc-platforms](https://github.com/alexeicolin/rtsc-platforms): custom
    packages that build on top of lower-level libraries (TivaWare and parts of
    SYSBIOS) to allow convenient usage of the Tiva platform from
    [RTSC](http://rtsc.eclipse.org/docs-tip/Main_Page) components, by providing
    hardware attribute info, LED and UART Console IO.

  * [OpenOCD](http://openocd.sourceforge.net/): for flashing the application to
  the target device.

The `build-env.sh` script shows environment variables that need to be pointed
to the installation paths. The script can be modified and sourced:

    $ . build-env.sh

Build and Flash
===============

    $ xdc all tivadaq
    $ openocd -f openocd/ek-tm4c123gxl.cfg -c 'program tivadaq/daq.xm4fg 0x0'

TODO
====

Differential voltage channel type is not yet supported. It should be doable
without too much effort, but watch out for
[errata](http://www.ti.com/lit/pdf/spmz849) on the hardware functionality: the
data added to the FIFO comes from the channel that is listed one slot further
than expected.

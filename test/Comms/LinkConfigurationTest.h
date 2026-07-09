#pragma once

#include "UnitTest.h"

class LinkConfigurationTest : public UnitTest
{
    Q_OBJECT

private slots:
    // LinkConfiguration base (via TCPConfiguration)
    void _testBaseSetNameEmitsSignal();
    void _testBaseSetDynamicEmitsSignal();
    void _testBaseSetAutoConnectEmitsSignal();
    void _testBaseSetHighLatencyEmitsSignal();
    void _testBaseDefaults();
    void _testBaseSettingsRoot();

    // TCPConfiguration
    void _testTcpConstruction();
    void _testTcpSetHostEmitsSignal();
    void _testTcpSetPortEmitsSignal();
    void _testTcpCopyConstruction();
    void _testTcpCopyFrom();
    void _testTcpSettingsRoundtrip();

    // UDPConfiguration
    void _testUdpConstruction();
    void _testUdpAddRemoveHost();
    void _testUdpSetLocalPortEmitsSignal();
    void _testUdpCopyConstruction();
    void _testUdpCopyFrom();
    void _testUdpSettingsRoundtrip();

#ifndef QGC_NO_SERIAL_LINK
    // SerialConfiguration (Wave 16 / Q8e prep). SerialLink itself cannot be
    // round-tripped without hardware or a pty pair (no socat in the build/test
    // containers), so the QSerialPort-facing surface is pinned at the
    // configuration/API level: defaults, settings persistence, and copy
    // semantics that a QSerialPort replacement must keep intact.
    void _testSerialConstructionDefaults();
    void _testSerialSettingsRoundtrip();
    void _testSerialCopyConstruction();
    void _testSerialSupportedBaudRates();
#endif
};

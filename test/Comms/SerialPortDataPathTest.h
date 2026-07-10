#pragma once

#include "UnitTest.h"

/// Wave 17 (Q8e) serial data-path characterization test.
///
/// Exercises `QSerialPort` (from `<QtSerialPort/QSerialPort>`) directly, through a
/// self-created pseudo-terminal pair (`posix_openpt`/`grantpt`/`unlockpt`/`ptsname` -- no
/// external tooling such as `socat` is required, closing the gap the wave-16 baseline
/// flagged: the build image has no pty tooling installed).
///
/// This test source is IDENTICAL for both serial backends: `<QtSerialPort/QSerialPort>`
/// resolves to Qt6::SerialPort's real class when QGC_ENABLE_QT_SERIALPORT=ON (default), or
/// to src/Comms/portable/QGCSerialPortCompat.h's termios(2)+libudev reimplementation when
/// OFF (see src/Comms/CMakeLists.txt / cmake/CustomOptions.cmake) -- so a single green run
/// against the real Qt backend, followed by a second green run with
/// -DQGC_ENABLE_QT_SERIALPORT=OFF, is a genuine equivalence check between the two.
class SerialPortDataPathTest : public UnitTest
{
    Q_OBJECT

private slots:
    void init() override;
    void cleanup() override;

    /// Opening the slave end of a pty pair must succeed and report isOpen()==true.
    void _testOpenClose();

    /// Bytes written to the pty's master fd (simulating "the device sends data") must
    /// arrive byte-identical via QSerialPort::readyRead()/readAll().
    void _testDeviceToHostEcho();

    /// Bytes written via QSerialPort::write() (simulating "we send data to the device")
    /// must arrive byte-identical when read back from the pty's master fd.
    void _testHostToDeviceEcho();

    /// Several separate write() calls must arrive in order and undivided/uncorrupted --
    /// the realistic MAVLink usage pattern (many small writes over time), and the
    /// "partial writes" contract SerialWorker::writeData() (SerialLink.cc) itself already
    /// retries around at a higher level.
    void _testSequentialWritesPreserveOrder();

    /// A single write() far larger than the kernel pty buffer, with nothing draining the
    /// master side concurrently, is GUARANTEED to only partially complete within the
    /// backend's internal write timeout. Pins the partial-write contract explicitly: the
    /// returned byte count must exactly match what is actually readable on the other end
    /// (no corruption, no phantom bytes), whatever that count is.
    void _testLargeWritePartialCompletionIsConsistent();

    /// Baud rate / data bits / parity / stop bits / flow control setters must all report
    /// success on a pty even though a pty has no physical UART to actually rate-limit --
    /// this is a documented no-op (see QGCSerialPortCompat.h's design notes), not a bug,
    /// and must not be mistaken for one by a future maintainer.
    void _testConfigurationSettersNoOpOnPty();

private:
    int _masterFd = -1;
    QString _slavePath;
};

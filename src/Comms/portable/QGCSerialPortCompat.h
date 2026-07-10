#pragma once

// Portable termios(2)+libudev reimplementation of the narrow QSerialPort /
// QSerialPortInfo API surface this tree actually uses, selected when
// QGC_ENABLE_QT_SERIALPORT=OFF (see src/Comms/CMakeLists.txt and
// cmake/CustomOptions.cmake's QGC_ENABLE_QT_SERIALPORT comment for the full
// rationale/library-choice writeup). Reached via include-path shadowing of
// `<QtSerialPort/QSerialPort>` / `<QtSerialPort/QSerialPortInfo>`
// (src/Comms/portable/QtSerialPort/*) -- every real consumer (SerialLink,
// QGCSerialPortInfo, LinkManager's direct NMEA QSerialPort use,
// GPS/GPSProvider's blocking PX4-GPSDrivers callback bridge,
// Vehicle/VehicleSetup/Bootloader.cc's firmware upload protocol) includes
// one of those two headers unchanged -- zero consumer-side #ifdef, same
// pattern as src/Utilities/StateMachine/portable's QGC_ENABLE_QT_STATEMACHINE
// port.
//
// Design notes / documented simplifications vs real QSerialPort:
//  - writeData() blocks (poll(2)-based short retry loop, ~2s ceiling) until
//    every byte is handed to the kernel tty layer, so waitForBytesWritten()
//    always finds nothing pending and just returns true. Real QSerialPort
//    buffers internally and drains asynchronously via the event loop --
//    nothing in this tree depends on that distinction: SerialWorker already
//    loops on partial writes itself (SerialLink.cc's writeData()), and
//    Bootloader/GPSProvider only use waitForBytesWritten() to confirm
//    completion after a write() call.
//  - setBaudRate() applies to a real tty via a fixed table of standard
//    POSIX/glibc B-constants (every rate SerialConfiguration::
//    supportedBaudRates() ever offers has one on Linux, up to B921600 --
//    see the .cc). A rate outside that table fails gracefully (logs +
//    returns false) rather than fighting termios2/BOTHER-vs-glibc-termios.h
//    header conflicts for a case nothing in-tree ever hits. On a
//    pseudo-terminal (as used by the Q8e SerialLinkPtyTest data-path test)
//    the ioctl succeeds but has no throttling effect -- ptys are
//    rate-unlimited. Documented, not a bug.
//  - setDataTerminalReady() and other modem-control-line ioctls are
//    best-effort: return false without side effects if the underlying fd
//    doesn't support TIOCM* (e.g. a pty), rather than crashing.
//  - Port enumeration (QSerialPortInfo::availablePorts()) walks the "tty"
//    subsystem via libudev and pulls VID/PID/manufacturer/product/serial
//    off the nearest USB ancestor device, matching what Qt's own Linux
//    QSerialPortInfo backend does; ttys with no backing hardware (ptys,
//    virtual consoles) are skipped because they have no udev parent chain
//    to read USB descriptors from.

#include <QtCore/QIODevice>
#include <QtCore/QList>
#include <QtCore/QString>

class QSocketNotifier;
class QSerialPortInfo;

/*===========================================================================*/

class QSerialPort : public QIODevice
{
    Q_OBJECT

public:
    enum DataBits {
        Data5 = 5,
        Data6 = 6,
        Data7 = 7,
        Data8 = 8,
        UnknownDataBits = -1,
    };
    Q_ENUM(DataBits)

    enum Parity {
        NoParity = 0,
        EvenParity = 2,
        OddParity = 3,
        SpaceParity = 4,
        MarkParity = 5,
        UnknownParity = -1,
    };
    Q_ENUM(Parity)

    enum StopBits {
        OneStop = 1,
        OneAndHalfStop = 3,
        TwoStop = 2,
        UnknownStopBits = -1,
    };
    Q_ENUM(StopBits)

    enum FlowControl {
        NoFlowControl = 0,
        HardwareControl = 1,
        SoftwareControl = 2,
        UnknownFlowControl = -1,
    };
    Q_ENUM(FlowControl)

    enum SerialPortError {
        NoError = 0,
        DeviceNotFoundError,
        PermissionError,
        OpenError,
        ParityError,
        FramingError,
        BreakConditionError,
        WriteError,
        ReadError,
        ResourceError,
        UnsupportedOperationError,
        UnknownError,
        TimeoutError,
        NotOpenError,
    };
    Q_ENUM(SerialPortError)

    enum BaudRate {
        Baud1200 = 1200,
        Baud2400 = 2400,
        Baud4800 = 4800,
        Baud9600 = 9600,
        Baud19200 = 19200,
        Baud38400 = 38400,
        Baud57600 = 57600,
        Baud115200 = 115200,
    };
    Q_ENUM(BaudRate)

    enum Direction {
        Input = 1,
        Output = 2,
        AllDirections = Input | Output,
    };
    Q_ENUM(Direction)

    explicit QSerialPort(QObject *parent = nullptr);
    explicit QSerialPort(const QSerialPortInfo &info, QObject *parent = nullptr);
    ~QSerialPort() override;

    void setPortName(const QString &name);
    QString portName() const { return _portName; }

    bool open(QIODevice::OpenMode mode) override;
    void close() override;

    bool setBaudRate(qint32 baudRate, Direction directions = AllDirections);
    qint32 baudRate(Direction directions = AllDirections) const { Q_UNUSED(directions); return _baudRate; }
    bool setDataBits(DataBits dataBits);
    DataBits dataBits() const { return _dataBits; }
    bool setParity(Parity parity);
    Parity parity() const { return _parity; }
    bool setStopBits(StopBits stopBits);
    StopBits stopBits() const { return _stopBits; }
    bool setFlowControl(FlowControl flowControl);
    FlowControl flowControl() const { return _flowControl; }
    bool setDataTerminalReady(bool set);

    SerialPortError error() const { return _error; }
    QString errorString() const { return _errorString; }
    void clearError();

    bool flush();

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override;
    bool waitForReadyRead(int msecs = 30000) override;
    bool waitForBytesWritten(int msecs = 30000) override;

signals:
    void errorOccurred(QSerialPort::SerialPortError error);

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *data, qint64 maxSize) override;

private slots:
    void _handleReadyRead();

private:
    bool _applyTermios();
    void _setError(SerialPortError error, const QString &errorString);

    QString _portName;
    int _fd = -1;
    QSocketNotifier *_readNotifier = nullptr;

    qint32 _baudRate = Baud9600;
    DataBits _dataBits = Data8;
    Parity _parity = NoParity;
    StopBits _stopBits = OneStop;
    FlowControl _flowControl = NoFlowControl;

    SerialPortError _error = NoError;
    QString _errorString;
};

/*===========================================================================*/

class QSerialPortInfo
{
public:
    QSerialPortInfo();
    explicit QSerialPortInfo(const QSerialPort &port);
    explicit QSerialPortInfo(const QString &portName);
    ~QSerialPortInfo();

    QString portName() const { return _portName; }
    QString systemLocation() const { return _systemLocation; }
    QString description() const { return _description; }
    QString manufacturer() const { return _manufacturer; }
    QString serialNumber() const { return _serialNumber; }

    bool hasVendorIdentifier() const { return _hasVendorId; }
    quint16 vendorIdentifier() const { return _vendorId; }
    bool hasProductIdentifier() const { return _hasProductId; }
    quint16 productIdentifier() const { return _productId; }

    bool isNull() const { return _systemLocation.isEmpty(); }
    bool isBusy() const;

    static QList<QSerialPortInfo> availablePorts();
    static QList<qint32> standardBaudRates();

private:
    QString _portName;
    QString _systemLocation;
    QString _description;
    QString _manufacturer;
    QString _serialNumber;
    bool _hasVendorId = false;
    quint16 _vendorId = 0;
    bool _hasProductId = false;
    quint16 _productId = 0;
};

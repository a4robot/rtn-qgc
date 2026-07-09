#include "VehicleCameraControlTest.h"
#include <QtTest/QSignalSpy>

#include "AppSettings.h"
#include "Fact.h"
#include "FactMetaData.h"
#include "LinkManager.h"
#include "MAVLinkLib.h"
#include "MavlinkCameraControlInterface.h"
#include "MockConfiguration.h"
#include "MockLink.h"
#include "MultiVehicleManager.h"
#include "QGCCameraManager.h"
#include "SettingsManager.h"
#include "Vehicle.h"
#include "VehicleCameraControl.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QScopedPointer>

#include <cstring>

void VehicleCameraControlTest::initTestCase()
{
    UnitTest::initTestCase();
    MultiVehicleManager::instance()->init();
}

void VehicleCameraControlTest::init()
{
    UnitTest::init();
    _mockLink = nullptr;
    _vehicle = nullptr;
}

void VehicleCameraControlTest::cleanup()
{
    if (_mockLink) {
        QSignalSpy spyDisconnect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
        _mockLink->disconnect();
        _mockLink = nullptr;

        if (_vehicle) {
            UnitTest::waitForSignal(spyDisconnect, TestTimeout::longMs(), QStringLiteral("activeVehicleChanged"));
        }
        _vehicle = nullptr;

        UnitTest::settleEventLoopForCleanup();
    }

    dumpFailureContextIfTestFailed(QStringLiteral("cleanup"));
    UnitTest::cleanup();
}

void VehicleCameraControlTest::_testCameraCapFlags_data()
{
    // MockConfiguration camera flags
    QTest::addColumn<bool>("captureVideo");
    QTest::addColumn<bool>("captureImage");
    QTest::addColumn<bool>("hasModes");
    QTest::addColumn<bool>("hasVideoStream");
    QTest::addColumn<bool>("canCaptureImageInVideoMode");
    QTest::addColumn<bool>("canCaptureVideoInImageMode");
    QTest::addColumn<bool>("hasBasicZoom");
    QTest::addColumn<bool>("hasTrackingPoint");
    QTest::addColumn<bool>("hasTrackingRectangle");

    // Expected VehicleCameraControl properties
    QTest::addColumn<bool>("expectedCapturesVideo");
    QTest::addColumn<bool>("expectedCapturesPhotos");
    QTest::addColumn<bool>("expectedHasModes");
    QTest::addColumn<bool>("expectedHasZoom");
    QTest::addColumn<bool>("expectedHasVideoStream");
    QTest::addColumn<bool>("expectedPhotosInVideoMode");
    QTest::addColumn<bool>("expectedVideoInPhotoMode");
    QTest::addColumn<bool>("expectedHasTracking");

    // Note: xCapVid and xCapImg (capturesVideo/capturesPhotos) are "virtual" capabilities that account for
    // local gstreamer support. When a camera has a video stream (HAS_VIDEO_STREAM), QGC can locally record
    // video via gstreamer and screen-grab photos from the stream, even if the camera itself doesn't support
    // native video recording or photo capture. See "video-stream-only" and "stream-no-native-capture" rows.

    //                                       capVid   capImg   modes    stream   imgInVid vidInImg zoom     trkPt    trkRect   xCapVid  xCapImg  xModes   xZoom    xStream  xImgVid  xVidImg  xTrack
    QTest::newRow("all-caps")                << true  << true  << true  << true  << true  << true  << true  << true  << true   << true  << true  << true  << true  << true  << true  << true  << true;
    QTest::newRow("no-caps")                 << false << false << false << false << false << false << false << false << false  << false << false << false << false << false << false << false << false;
    QTest::newRow("photo-only")              << false << true  << false << false << false << false << false << false << false  << false << true  << false << false << false << false << false << false;
    QTest::newRow("video-only")              << true  << false << false << false << false << false << false << false << false  << true  << false << false << false << false << false << false << false;
    QTest::newRow("video-stream-only")       << false << false << false << true  << false << false << false << false << false  << true  << true  << false << false << true  << false << false << false;
    QTest::newRow("modes-zoom")              << false << true  << true  << false << false << false << true  << false << false  << false << true  << true  << true  << false << false << false << false;
    QTest::newRow("tracking-point")          << false << true  << false << false << false << false << false << true  << false  << false << true  << false << false << false << false << false << true;
    QTest::newRow("tracking-rect")           << false << true  << false << false << false << false << false << false << true   << false << true  << false << false << false << false << false << true;
    QTest::newRow("tracking-both")           << false << true  << false << false << false << false << false << true  << true   << false << true  << false << false << false << false << false << true;
    QTest::newRow("image-in-video")          << true  << true  << true  << false << true  << false << false << false << false  << true  << true  << true  << false << false << true  << false << false;
    QTest::newRow("video-in-image")          << true  << true  << true  << false << false << true  << false << false << false  << true  << true  << true  << false << false << false << true  << false;
    QTest::newRow("stream-no-native-capture")<< false << false << false << true  << false << false << false << false << false  << true  << true  << false << false << true  << false << false << false;
    QTest::newRow("stream-plus-photo")       << false << true  << false << true  << false << false << false << false << false  << true  << true  << false << false << true  << false << false << false;
}

void VehicleCameraControlTest::_testCameraCapFlags()
{
    // Fetch test data
    QFETCH(bool, captureVideo);
    QFETCH(bool, captureImage);
    QFETCH(bool, hasModes);
    QFETCH(bool, hasVideoStream);
    QFETCH(bool, canCaptureImageInVideoMode);
    QFETCH(bool, canCaptureVideoInImageMode);
    QFETCH(bool, hasBasicZoom);
    QFETCH(bool, hasTrackingPoint);
    QFETCH(bool, hasTrackingRectangle);

    QFETCH(bool, expectedCapturesVideo);
    QFETCH(bool, expectedCapturesPhotos);
    QFETCH(bool, expectedHasModes);
    QFETCH(bool, expectedHasZoom);
    QFETCH(bool, expectedHasVideoStream);
    QFETCH(bool, expectedPhotosInVideoMode);
    QFETCH(bool, expectedVideoInPhotoMode);
    QFETCH(bool, expectedHasTracking);

    // Create MockConfiguration with camera enabled and custom flags
    auto* mockConfig = new MockConfiguration(QStringLiteral("CameraCapFlagsTest"));
    mockConfig->setFirmwareType(MAV_AUTOPILOT_PX4);
    mockConfig->setVehicleType(MAV_TYPE_QUADROTOR);
    mockConfig->setDynamic(true);
    mockConfig->setEnableCamera(true);
    mockConfig->setCameraCaptureVideo(captureVideo);
    mockConfig->setCameraCaptureImage(captureImage);
    mockConfig->setCameraHasModes(hasModes);
    mockConfig->setCameraHasVideoStream(hasVideoStream);
    mockConfig->setCameraCanCaptureImageInVideoMode(canCaptureImageInVideoMode);
    mockConfig->setCameraCanCaptureVideoInImageMode(canCaptureVideoInImageMode);
    mockConfig->setCameraHasBasicZoom(hasBasicZoom);
    mockConfig->setCameraHasTrackingPoint(hasTrackingPoint);
    mockConfig->setCameraHasTrackingRectangle(hasTrackingRectangle);

    // Connect MockLink
    QSignalSpy spyVehicle(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
    QVERIFY(spyVehicle.isValid());

    SharedLinkConfigurationPtr linkConfig = LinkManager::instance()->addConfiguration(mockConfig);
    QVERIFY(LinkManager::instance()->createConnectedLink(linkConfig));

    QVERIFY2(UnitTest::waitForSignal(spyVehicle, TestTimeout::longMs(), QStringLiteral("activeVehicleChanged")),
             "Timeout waiting for vehicle connection");

    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(_vehicle);

    _mockLink = qobject_cast<MockLink*>(linkConfig->link());
    QVERIFY(_mockLink);

    // Wait for initial connect sequence to complete
    if (!_vehicle->isInitialConnectComplete()) {
        QSignalSpy spyConnect(_vehicle, &Vehicle::initialConnectComplete);
        QVERIFY(spyConnect.isValid());
        QVERIFY2(UnitTest::waitForSignal(spyConnect, TestTimeout::longMs(), QStringLiteral("initialConnectComplete")),
                 "Timeout waiting for initial connect");
    }

    // Wait for camera manager to discover cameras
    QGCCameraManager* cameraManager = _vehicle->cameraManager();
    QVERIFY(cameraManager);

    // MockLinkCamera creates two cameras; wait for both to be discovered
    QVERIFY_TRUE_WAIT(cameraManager->cameras()->count() >= 2, TestTimeout::longMs());

    // Find Camera 1 (MAV_COMP_ID_CAMERA) which has our configured flags.
    // Camera 2 (MAV_COMP_ID_CAMERA2) is always photo-only and not what we're testing.
    MavlinkCameraControlInterface* camera = nullptr;
    for (int i = 0; i < cameraManager->cameras()->count(); i++) {
        auto* cam = qobject_cast<MavlinkCameraControlInterface*>(cameraManager->cameras()->get(i));
        if (cam && cam->compID() == MAV_COMP_ID_CAMERA) {
            camera = cam;
            break;
        }
    }
    QVERIFY2(camera, "Camera 1 (MAV_COMP_ID_CAMERA) not found in camera list");

    // Verify capability properties match expected values
    QCOMPARE(camera->capturesVideo(),     expectedCapturesVideo);
    QCOMPARE(camera->capturesPhotos(),    expectedCapturesPhotos);
    QCOMPARE(camera->hasModes(),          expectedHasModes);
    QCOMPARE(camera->hasZoom(),           expectedHasZoom);
    QCOMPARE(camera->hasVideoStream(),    expectedHasVideoStream);
    QCOMPARE(camera->photosInVideoMode(), expectedPhotosInVideoMode);
    QCOMPARE(camera->videoInPhotoMode(),  expectedVideoInPhotoMode);
    QCOMPARE(camera->hasTracking(),       expectedHasTracking);

    // hasFocus is always false since MockLinkCamera doesn't support CAMERA_CAP_FLAGS_HAS_BASIC_FOCUS
    QCOMPARE(camera->hasFocus(), false);
}

void VehicleCameraControlTest::_testCameraDefinitionParsing()
{
    // MockLink's simulated cameras never populate cam_definition_uri (see
    // MockLinkCamera::_sendCameraInformation()), so they never exercise the
    // camera-definition XML parser. We only need a live Vehicle* here (for
    // FTPManager/SettingsManager access inside VehicleCameraControl) - the
    // mock camera capability flags configured on it are irrelevant.
    auto* mockConfig = new MockConfiguration(QStringLiteral("CameraDefinitionParsingTest"));
    mockConfig->setFirmwareType(MAV_AUTOPILOT_PX4);
    mockConfig->setVehicleType(MAV_TYPE_QUADROTOR);
    mockConfig->setDynamic(true);

    QSignalSpy spyVehicle(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged);
    QVERIFY(spyVehicle.isValid());

    SharedLinkConfigurationPtr linkConfig = LinkManager::instance()->addConfiguration(mockConfig);
    QVERIFY(LinkManager::instance()->createConnectedLink(linkConfig));
    QVERIFY2(UnitTest::waitForSignal(spyVehicle, TestTimeout::longMs(), QStringLiteral("activeVehicleChanged")),
             "Timeout waiting for vehicle connection");

    _vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(_vehicle);
    _mockLink = qobject_cast<MockLink*>(linkConfig->link());
    QVERIFY(_mockLink);

    if (!_vehicle->isInitialConnectComplete()) {
        QSignalSpy spyConnect(_vehicle, &Vehicle::initialConnectComplete);
        QVERIFY(spyConnect.isValid());
        QVERIFY2(UnitTest::waitForSignal(spyConnect, TestTimeout::longMs(), QStringLiteral("initialConnectComplete")),
                 "Timeout waiting for initial connect");
    }

    // Load the fixture that exercises: definition/vendor/model/version,
    // uint32/float/bool parameter types, defaults, enum options (including
    // negative and fractional values and an XML entity), exclusions,
    // parameterranges/roption/condition, and a localization block.
    QFile fixtureFile(QStringLiteral(CAMERA_DEFINITION_EXAMPLE_XML_PATH));
    QVERIFY2(fixtureFile.open(QIODevice::ReadOnly), qPrintable(fixtureFile.errorString()));
    const QByteArray fixtureBytes = fixtureFile.readAll();
    QVERIFY(!fixtureBytes.isEmpty());

    // Seed the on-disk definition cache with the fixture *before* constructing
    // VehicleCameraControl so the constructor's "already cached" boot path
    // (see VehicleCameraControl::_handleDefinitionFile()) fires synchronously:
    // this drives the exact same single dataReady() -> _loadCameraDefinitionFile()
    // sequence a real vehicle triggers after an HTTP/FTP download completes,
    // without needing a live download in the test.
    const QString vendor = QStringLiteral("Characterization Vendor");
    const QString model  = QStringLiteral("XmlParseFixture");
    const int version = 7;
    const QString parameterSavePath = SettingsManager::instance()->appSettings()->parameterSavePath();
    QVERIFY(!parameterSavePath.isEmpty());
    QVERIFY(QDir().mkpath(parameterSavePath));
    const QString cacheFile = QString::asprintf("%s/%s_%s_%03d.xml",
                                                  parameterSavePath.toStdString().c_str(),
                                                  vendor.toStdString().c_str(),
                                                  model.toStdString().c_str(),
                                                  version);
    QFile::remove(cacheFile);
    {
        QFile cache(cacheFile);
        QVERIFY2(cache.open(QIODevice::WriteOnly), qPrintable(cache.errorString()));
        QCOMPARE(cache.write(fixtureBytes), static_cast<qint64>(fixtureBytes.size()));
    }

    mavlink_camera_information_t camInfo{};
    const QByteArray vendorBa = vendor.toLocal8Bit();
    const QByteArray modelBa  = model.toLocal8Bit();
    memcpy(camInfo.vendor_name, vendorBa.constData(), std::min<size_t>(sizeof(camInfo.vendor_name), static_cast<size_t>(vendorBa.size())));
    memcpy(camInfo.model_name, modelBa.constData(), std::min<size_t>(sizeof(camInfo.model_name), static_cast<size_t>(modelBa.size())));
    camInfo.cam_definition_version = static_cast<uint16_t>(version);
    // Non-empty so the constructor takes the _handleDefinitionFile() branch,
    // but never actually fetched since the cache file above exists.
    const QByteArray uriBa = QByteArrayLiteral("http://192.0.2.1/unused.xml");
    memcpy(camInfo.cam_definition_uri, uriBa.constData(), std::min<size_t>(sizeof(camInfo.cam_definition_uri) - 1, static_cast<size_t>(uriBa.size())));

    // Owned locally (not left parented-and-forgotten on the test object): its
    // dtor stops all of VehicleCameraControl's internal QTimers, which
    // otherwise could fire after cleanup() tears down _vehicle/_mockLink.
    QScopedPointer<VehicleCameraControl> cameraGuard(new VehicleCameraControl(&camInfo, _vehicle, static_cast<int>(MAV_COMP_ID_CAMERA4)));
    VehicleCameraControl* camera = cameraGuard.data();
    QVERIFY(camera);
    QVERIFY(!camera->isBasic());

    // --- definition constants --------------------------------------------
    QCOMPARE(camera->modelName(), QStringLiteral("SD II"));
    QCOMPARE(camera->vendor(), QStringLiteral("Super Dupper Industries"));
    QCOMPARE(camera->version(), 1);

    // --- full parameter list, in document order ---------------------------
    QCOMPARE(camera->activeSettings(), QStringList({
        QStringLiteral("CAM_MODE"), QStringLiteral("CAM_WBMODE"), QStringLiteral("CAM_EXPMODE"),
        QStringLiteral("CAM_APERTURE"), QStringLiteral("CAM_SHUTTERSPD"), QStringLiteral("CAM_ISO"),
        QStringLiteral("CAM_EV"), QStringLiteral("CAM_VIDRES"), QStringLiteral("CAM_VIDFMT"),
        QStringLiteral("CAM_AUDIOREC"), QStringLiteral("CAM_METERING"), QStringLiteral("CAM_COLORMODE"),
        QStringLiteral("CAM_PHOTORES"), QStringLiteral("CAM_PHOTOFMT"), QStringLiteral("CAM_PHOTOQUAL")
    }));

    // --- plain enum parameter, default value ------------------------------
    Fact* camMode = camera->getFact(QStringLiteral("CAM_MODE"));
    QVERIFY(camMode);
    QCOMPARE(camMode->rawValue().toInt(), 1); // default="1"
    QCOMPARE(camMode->enumStrings(), QStringList({QStringLiteral("Photo"), QStringLiteral("Video")}));
    const QVariantList camModeValues = camMode->enumValues();
    QCOMPARE(camModeValues.size(), 2);
    QCOMPARE(camModeValues.at(0).toInt(), 0);
    QCOMPARE(camModeValues.at(1).toInt(), 1);

    // --- float type + fractional/negative option values -------------------
    Fact* shutterSpeed = camera->getFact(QStringLiteral("CAM_SHUTTERSPD"));
    QVERIFY(shutterSpeed);
    QCOMPARE(shutterSpeed->enumValues().size(), 13);
    QVERIFY(qAbs(shutterSpeed->enumValues().last().toDouble() - 0.001) < 0.0001);

    Fact* ev = camera->getFact(QStringLiteral("CAM_EV"));
    QVERIFY(ev);
    QVERIFY(qAbs(ev->enumValues().first().toDouble() - (-2.0)) < 0.0001);

    // --- bool type, no options/no default text ----------------------------
    Fact* audioRec = camera->getFact(QStringLiteral("CAM_AUDIOREC"));
    QVERIFY(audioRec);
    QCOMPARE(audioRec->rawValue().toBool(), false);
    QVERIFY(audioRec->enumStrings().isEmpty());

    // --- option text containing an XML entity ("&amp;") --------------------
    Fact* colorMode = camera->getFact(QStringLiteral("CAM_COLORMODE"));
    QVERIFY(colorMode);
    QVERIFY(colorMode->enumStrings().contains(QStringLiteral("Black & White")));

    // --- parameter absent from the definition ------------------------------
    QVERIFY(!camera->getFact(QStringLiteral("NOT_A_REAL_PARAMETER")));

    // exclusions (<exclusions>/<exclude>) and parameterranges
    // (<parameterranges>/<parameterrange>/<roption>) have no public getters on
    // VehicleCameraControl; a malformed block in either would make
    // _loadSettings() return false, which would leave activeSettings()/
    // getFact() empty above, so their parsing is exercised transitively by
    // the assertions above succeeding at all.
}

UT_REGISTER_TEST(VehicleCameraControlTest, TestLabel::Integration, TestLabel::Vehicle)

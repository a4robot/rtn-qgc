#include "GeoJsonHelper.h"
#include "JsonParsing.h"
#include "QGCGeoJsonImport.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonValue>
#include <QtCore/QCoreApplication>
#include <QtPositioning/QGeoCoordinate>
#include <QtPositioning/QGeoPath>
#include <QtPositioning/QGeoPolygon>

QGC_LOGGING_CATEGORY(GeoJsonHelperLog, "Utilities.GeoJsonHelper")

namespace GeoJsonHelper
{
    QJsonDocument _loadFile(const QString &filePath, QString &errorString);

    constexpr const char *_errorPrefix = QT_TRANSLATE_NOOP("GeoJsonHelper", "GeoJson file load failed. %1");
}

QJsonDocument GeoJsonHelper::_loadFile(const QString &filePath, QString &errorString)
{
    errorString.clear();

    QFile file(filePath);
    if (!file.exists()) {
        errorString = QCoreApplication::translate("GeoJsonHelper", _errorPrefix).arg(
            QCoreApplication::translate("GeoJson", "File not found: %1").arg(filePath));
        return QJsonDocument();
    }

    if (!file.open(QIODevice::ReadOnly)) {
        errorString = QCoreApplication::translate("GeoJsonHelper", _errorPrefix).arg(
            QCoreApplication::translate("GeoJson", "Unable to open file: %1 error: %2")
                .arg(filePath, file.errorString()));
        return QJsonDocument();
    }

    QJsonDocument jsonDoc;
    const QByteArray bytes = file.readAll();
    if (!JsonParsing::isJsonFile(bytes, jsonDoc, errorString)) {
        errorString = QCoreApplication::translate("GeoJsonHelper", _errorPrefix).arg(errorString);
    }

    return jsonDoc;
}

ShapeFileHelper::ShapeType GeoJsonHelper::determineShapeType(const QString &filePath, QString &errorString)
{
    using ShapeType = ShapeFileHelper::ShapeType;

    const QJsonDocument jsonDoc = GeoJsonHelper::_loadFile(filePath, errorString);
    if (!errorString.isEmpty()) {
        return ShapeType::Error;
    }

    const QList<QGCGeoJson::Shape> shapes = QGCGeoJson::importGeoJson(jsonDoc);
    if (shapes.isEmpty()) {
        errorString = QCoreApplication::translate("GeoJsonHelper", _errorPrefix).arg(
            QCoreApplication::translate("GeoJson", "No shapes found in GeoJson file."));
        return ShapeType::Error;
    }

    for (const QGCGeoJson::Shape &shape : shapes) {
        if (shape.kind == QGCGeoJson::ShapeKind::Polygon) {
            return ShapeType::Polygon;
        }
        if (shape.kind == QGCGeoJson::ShapeKind::Polyline) {
            return ShapeType::Polyline;
        }
    }

    errorString = QCoreApplication::translate("GeoJsonHelper", _errorPrefix).arg(
        QCoreApplication::translate("GeoJson", "No supported type found in GeoJson file."));
    return ShapeType::Error;
}

bool GeoJsonHelper::loadPolygonFromFile(const QString &filePath, QList<QGeoCoordinate> &vertices, QString &errorString)
{
    errorString.clear();
    vertices.clear();

    const QJsonDocument jsonDoc = GeoJsonHelper::_loadFile(filePath, errorString);
    if (!errorString.isEmpty()) {
        return false;
    }

    const QList<QGCGeoJson::Shape> shapes = QGCGeoJson::importGeoJson(jsonDoc);
    if (shapes.isEmpty()) {
        errorString = QCoreApplication::translate("GeoJsonHelper", _errorPrefix).arg(
            QCoreApplication::translate("GeoJson", "No polygon data found in GeoJson file."));
        return false;
    }

    for (const QGCGeoJson::Shape &shape : shapes) {
        if (shape.kind == QGCGeoJson::ShapeKind::Polygon) {
            vertices = shape.polygon.perimeter();
            if (!vertices.isEmpty()) {
                return true;
            }
        }
    }

    errorString = QCoreApplication::translate("GeoJsonHelper", _errorPrefix).arg(
        QCoreApplication::translate("GeoJson", "No polygon found in GeoJson file."));
    return false;
}

bool GeoJsonHelper::loadPolylineFromFile(const QString &filePath, QList<QGeoCoordinate> &coords, QString &errorString)
{
    errorString.clear();
    coords.clear();

    const QJsonDocument jsonDoc = GeoJsonHelper::_loadFile(filePath, errorString);
    if (!errorString.isEmpty()) {
        return false;
    }

    const QList<QGCGeoJson::Shape> shapes = QGCGeoJson::importGeoJson(jsonDoc);
    if (shapes.isEmpty()) {
        errorString = QCoreApplication::translate("GeoJsonHelper", _errorPrefix).arg(
            QCoreApplication::translate("GeoJson", "No polyline data found in GeoJson file."));
        return false;
    }

    for (const QGCGeoJson::Shape &shape : shapes) {
        if (shape.kind == QGCGeoJson::ShapeKind::Polyline) {
            coords = shape.path.path();
            if (!coords.isEmpty()) {
                return true;
            }
        }
    }

    errorString = QCoreApplication::translate("GeoJsonHelper", _errorPrefix).arg(
        QCoreApplication::translate("GeoJson", "No polyline found in GeoJson file."));
    return false;
}

bool GeoJsonHelper::loadGeoJsonCoordinate(const QJsonValue &jsonValue, bool altitudeRequired, QGeoCoordinate &coordinate, QString &errorString)
{
    if (!jsonValue.isArray()) {
        errorString = QCoreApplication::translate("GeoJsonHelper", "value for coordinate is not array");
        return false;
    }

    const QJsonArray coordinateArray = jsonValue.toArray();
    const int requiredCount = altitudeRequired ? 3 : 2;
    if (coordinateArray.count() != requiredCount) {
        errorString = QCoreApplication::translate("GeoJsonHelper", "Coordinate array must contain %1 values").arg(requiredCount);
        return false;
    }

    for (const QJsonValue &coordinateValue : coordinateArray) {
        if ((coordinateValue.type() != QJsonValue::Double) && (coordinateValue.type() != QJsonValue::Null)) {
            errorString =
                QCoreApplication::translate("GeoJsonHelper", "Coordinate array may only contain double values, found: %1").arg(coordinateValue.type());
            return false;
        }
    }

    // GeoJSON ordering is [lon, lat, alt] (RFC 7946).
    coordinate = QGeoCoordinate(coordinateArray[1].toDouble(), coordinateArray[0].toDouble());
    if (altitudeRequired) {
        coordinate.setAltitude(JsonParsing::possibleNaNJsonValue(coordinateArray[2]));
    }

    return true;
}

void GeoJsonHelper::saveGeoJsonCoordinate(const QGeoCoordinate &coordinate, bool writeAltitude, QJsonValue &jsonValue)
{
    QJsonArray coordinateArray;
    coordinateArray << coordinate.longitude() << coordinate.latitude();
    if (writeAltitude) {
        coordinateArray << coordinate.altitude();
    }
    jsonValue = QJsonValue(coordinateArray);
}

bool GeoJsonHelper::loadGeoCoordinate(const QJsonValue &jsonValue, bool altitudeRequired, QGeoCoordinate &coordinate,
                                      QString &errorString)
{
    if (!jsonValue.isArray()) {
        errorString = QCoreApplication::translate("GeoJsonHelper", "value for coordinate is not array");
        return false;
    }

    const QJsonArray coordinateArray = jsonValue.toArray();
    const int requiredCount = altitudeRequired ? 3 : 2;
    if (coordinateArray.count() != requiredCount) {
        errorString = QCoreApplication::translate("GeoJsonHelper", "Coordinate array must contain %1 values").arg(requiredCount);
        return false;
    }

    for (const QJsonValue &coordinateValue : coordinateArray) {
        if ((coordinateValue.type() != QJsonValue::Double) && (coordinateValue.type() != QJsonValue::Null)) {
            errorString =
                QCoreApplication::translate("GeoJsonHelper", "Coordinate array may only contain double values, found: %1").arg(coordinateValue.type());
            return false;
        }
    }

    coordinate = QGeoCoordinate(
        JsonParsing::possibleNaNJsonValue(coordinateArray[0]),
        JsonParsing::possibleNaNJsonValue(coordinateArray[1]));

    if (altitudeRequired) {
        coordinate.setAltitude(JsonParsing::possibleNaNJsonValue(coordinateArray[2]));
    }

    return true;
}

void GeoJsonHelper::saveGeoCoordinate(const QGeoCoordinate &coordinate, bool writeAltitude, QJsonValue &jsonValue)
{
    QJsonArray coordinateArray;
    coordinateArray << coordinate.latitude() << coordinate.longitude();

    if (writeAltitude) {
        coordinateArray << coordinate.altitude();
    }

    jsonValue = QJsonValue(coordinateArray);
}

bool GeoJsonHelper::loadGeoCoordinateArray(const QJsonValue &jsonValue, bool altitudeRequired,
                                           QVariantList &rgVarPoints, QString &errorString)
{
    if (!jsonValue.isArray()) {
        errorString = QCoreApplication::translate("GeoJsonHelper", "value for coordinate array is not array");
        return false;
    }

    const QJsonArray rgJsonPoints = jsonValue.toArray();

    rgVarPoints.clear();
    for (const QJsonValue &point : rgJsonPoints) {
        QGeoCoordinate coordinate;
        if (!loadGeoCoordinate(point, altitudeRequired, coordinate, errorString)) {
            return false;
        }
        rgVarPoints.append(QVariant::fromValue(coordinate));
    }

    return true;
}

bool GeoJsonHelper::loadGeoCoordinateArray(const QJsonValue &jsonValue, bool altitudeRequired,
                                           QList<QGeoCoordinate> &rgPoints, QString &errorString)
{
    QVariantList rgVarPoints;

    if (!loadGeoCoordinateArray(jsonValue, altitudeRequired, rgVarPoints, errorString)) {
        return false;
    }

    rgPoints.clear();
    for (const QVariant &point : rgVarPoints) {
        rgPoints.append(point.value<QGeoCoordinate>());
    }

    return true;
}

void GeoJsonHelper::saveGeoCoordinateArray(const QVariantList &rgVarPoints, bool writeAltitude, QJsonValue &jsonValue)
{
    QJsonArray rgJsonPoints;
    for (const QVariant &point : rgVarPoints) {
        QJsonValue jsonPoint;
        saveGeoCoordinate(point.value<QGeoCoordinate>(), writeAltitude, jsonPoint);
        rgJsonPoints.append(jsonPoint);
    }

    jsonValue = rgJsonPoints;
}

void GeoJsonHelper::saveGeoCoordinateArray(const QList<QGeoCoordinate> &rgPoints, bool writeAltitude,
                                           QJsonValue &jsonValue)
{
    QVariantList rgVarPoints;
    for (const QGeoCoordinate &coord : rgPoints) {
        rgVarPoints.append(QVariant::fromValue(coord));
    }

    saveGeoCoordinateArray(rgVarPoints, writeAltitude, jsonValue);
}

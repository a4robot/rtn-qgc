#include "QGCGeoJsonImport.h"
#include "JsonParsing.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonValue>
#include <QtPositioning/QGeoCoordinate>

namespace {

constexpr int kMaxRecursionDepth = 32;

/// Converts a single GeoJSON `position` ([lon, lat] or [lon, lat, alt]) into a QGeoCoordinate.
bool positionToCoordinate(const QJsonValue &positionValue, QGeoCoordinate &coordinate)
{
    if (!positionValue.isArray()) {
        return false;
    }

    const QJsonArray position = positionValue.toArray();
    if ((position.count() != 2) && (position.count() != 3)) {
        return false;
    }

    for (const QJsonValue &value : position) {
        if ((value.type() != QJsonValue::Double) && (value.type() != QJsonValue::Null)) {
            return false;
        }
    }

    // GeoJSON position ordering is [lon, lat, alt] (RFC 7946); altitude is optional.
    coordinate = QGeoCoordinate(JsonParsing::possibleNaNJsonValue(position[1]), JsonParsing::possibleNaNJsonValue(position[0]));
    if (position.count() == 3) {
        coordinate.setAltitude(JsonParsing::possibleNaNJsonValue(position[2]));
    }

    return true;
}

/// Converts a GeoJSON `position array` (a LineString's coordinates, or a single Polygon ring)
/// into a coordinate list.
bool positionArrayToCoordinates(const QJsonValue &positionArrayValue, QList<QGeoCoordinate> &coordinates)
{
    if (!positionArrayValue.isArray()) {
        return false;
    }

    coordinates.clear();
    const QJsonArray positionArray = positionArrayValue.toArray();
    for (const QJsonValue &positionValue : positionArray) {
        QGeoCoordinate coordinate;
        if (!positionToCoordinate(positionValue, coordinate)) {
            return false;
        }
        coordinates.append(coordinate);
    }

    return !coordinates.isEmpty();
}

void appendPolygon(const QJsonValue &coordinatesValue, QList<QGCGeoJson::Shape> &shapes)
{
    if (!coordinatesValue.isArray()) {
        return;
    }

    // The first ring is the outer boundary; any remaining rings are holes. GeoJsonHelper only
    // ever reads QGeoPolygon::perimeter(), so holes are dropped here -- the same outer-ring-only
    // convention SHPFileHelper already uses for shapefile polygons with holes.
    const QJsonArray rings = coordinatesValue.toArray();
    if (rings.isEmpty()) {
        return;
    }

    QList<QGeoCoordinate> outerRing;
    if (!positionArrayToCoordinates(rings.at(0), outerRing)) {
        return;
    }

    QGCGeoJson::Shape shape;
    shape.kind = QGCGeoJson::ShapeKind::Polygon;
    shape.polygon.setPerimeter(outerRing);
    shapes.append(shape);
}

void appendLineString(const QJsonValue &coordinatesValue, QList<QGCGeoJson::Shape> &shapes)
{
    QList<QGeoCoordinate> coordinates;
    if (!positionArrayToCoordinates(coordinatesValue, coordinates)) {
        return;
    }

    QGCGeoJson::Shape shape;
    shape.kind = QGCGeoJson::ShapeKind::Polyline;
    shape.path.setPath(coordinates);
    shapes.append(shape);
}

void importGeometry(const QJsonValue &geometryValue, QList<QGCGeoJson::Shape> &shapes, int depth);

void importGeometryCollection(const QJsonObject &geometry, QList<QGCGeoJson::Shape> &shapes, int depth)
{
    const QJsonArray geometries = geometry.value(QStringLiteral("geometries")).toArray();
    for (const QJsonValue &nestedGeometry : geometries) {
        importGeometry(nestedGeometry, shapes, depth + 1);
    }
}

void importGeometry(const QJsonValue &geometryValue, QList<QGCGeoJson::Shape> &shapes, int depth)
{
    if ((depth >= kMaxRecursionDepth) || !geometryValue.isObject()) {
        return;
    }

    const QJsonObject geometry = geometryValue.toObject();
    const QString type = geometry.value(QStringLiteral("type")).toString();
    const QJsonValue coordinates = geometry.value(QStringLiteral("coordinates"));

    if (type == QStringLiteral("Polygon")) {
        appendPolygon(coordinates, shapes);
    } else if (type == QStringLiteral("MultiPolygon")) {
        if (coordinates.isArray()) {
            const QJsonArray polygons = coordinates.toArray();
            for (const QJsonValue &polygonCoordinates : polygons) {
                appendPolygon(polygonCoordinates, shapes);
            }
        }
    } else if (type == QStringLiteral("LineString")) {
        appendLineString(coordinates, shapes);
    } else if (type == QStringLiteral("MultiLineString")) {
        if (coordinates.isArray()) {
            const QJsonArray lines = coordinates.toArray();
            for (const QJsonValue &lineCoordinates : lines) {
                appendLineString(lineCoordinates, shapes);
            }
        }
    } else if ((type == QStringLiteral("Point")) || (type == QStringLiteral("MultiPoint"))) {
        // Recognized GeoJSON geometry types QGC does not surface as a loadable shape. Recorded
        // as an Unsupported shape (rather than simply skipped) so callers can distinguish
        // "document has geometry, just nothing we support" from "document has no geometry at
        // all" -- matching GeoJsonHelper's historical error strings.
        shapes.append(QGCGeoJson::Shape());
    } else if (type == QStringLiteral("GeometryCollection")) {
        importGeometryCollection(geometry, shapes, depth);
    }
    // Any other/missing type is silently ignored.
}

void importFeature(const QJsonValue &featureValue, QList<QGCGeoJson::Shape> &shapes, int depth)
{
    if (!featureValue.isObject()) {
        return;
    }

    const QJsonObject feature = featureValue.toObject();
    importGeometry(feature.value(QStringLiteral("geometry")), shapes, depth + 1);
}

void importDocument(const QJsonValue &value, QList<QGCGeoJson::Shape> &shapes, int depth)
{
    if ((depth >= kMaxRecursionDepth) || !value.isObject()) {
        return;
    }

    const QJsonObject object = value.toObject();
    const QString type = object.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("FeatureCollection")) {
        const QJsonArray features = object.value(QStringLiteral("features")).toArray();
        for (const QJsonValue &feature : features) {
            importFeature(feature, shapes, depth + 1);
        }
    } else if (type == QStringLiteral("Feature")) {
        importFeature(value, shapes, depth);
    } else {
        // Bare geometry document (Polygon, LineString, Point, GeometryCollection, ...).
        importGeometry(value, shapes, depth);
    }
}

} // namespace

QList<QGCGeoJson::Shape> QGCGeoJson::importGeoJson(const QJsonDocument &jsonDoc)
{
    QList<Shape> shapes;

    if (jsonDoc.isObject()) {
        importDocument(QJsonValue(jsonDoc.object()), shapes, 0);
    }

    return shapes;
}

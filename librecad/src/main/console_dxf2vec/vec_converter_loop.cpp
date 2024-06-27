/******************************************************************************
**
** This file was created for the LibreCAD project, a 2D CAD program.
** File added by Kidev in Constructions-3D fork of the project
**
** Copyright (C) 2020 Nikita Letov <letovnn@gmail.com>
**
** This file may be distributed and/or modified under the terms of the
** GNU General Public License version 2 as published by the Free Software
** Foundation and appearing in the file gpl-2.0.txt included in the
** packaging of this file.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
**
** This copyright notice MUST APPEAR in all copies of the script!
**
******************************************************************************/

#include <QList>
#include <QVector3D>
#include <QtCore>

#include <iostream>

#include "rs.h"
#include "rs_block.h"
#include "rs_blocklist.h"
#include "rs_graphic.h"
#include "rs_insert.h"
#include "rs_polyline.h"
#include "rs_units.h"

#include "vec_converter_loop.h"

struct PolylineData
{
    bool visible;
    unsigned long int id;
    unsigned int count;
    unsigned int countDeep;
    bool closed;
};

static bool openDocAndSetGraphic(RS_Document **, RS_Graphic **, const QString &);
void serializePolylines(const QList<std::pair<PolylineData, QList<QVector3D>>> &, const QString &);
bool deserializePolylines(QList<std::pair<PolylineData, QList<QVector3D>>> &, const QString &);

void VecConverterLoop::run()
{
    if (params.outFile.isEmpty()) {
        for (auto &&f : params.dxfFiles) {
            convertOneDxfToOneVec(f);
        }
    } else {
        convertManyDxfToOneVec();
    }

    emit finished();
}

void VecConverterLoop::convertOneDxfToOneVec(const QString &dxfFile)
{
    QFileInfo dxfFileInfo(dxfFile);
    params.outFile = (params.outDir.isEmpty() ? dxfFileInfo.path() : params.outDir) + "/"
                     + dxfFileInfo.completeBaseName() + ".vec";

    RS_Document *doc;
    RS_Graphic *graphic;

    if (!openDocAndSetGraphic(&doc, &graphic, dxfFile))
        return;

    QList<std::pair<PolylineData, QList<QVector3D>>> allPolylinesPoints;

    auto processEntity = [&allPolylinesPoints, &graphic](RS_Entity *entity,
                                                         const RS_Vector& insertionPoint,
                                                         const RS_Vector& scaleFactorInput,
                                                         double rotation,
                                                         auto &&processEntityRef) -> void {
        RS_Vector scaleFactor = scaleFactorInput;
        if (scaleFactor.z == 0) {
            scaleFactor.z = 1;
        }

        if (entity->rtti() == RS2::EntityPolyline) {
            RS_Polyline *polyline = static_cast<RS_Polyline *>(entity);
            QList<QVector3D> polylinePoints;
            PolylineData polylineData;

            polylineData.visible = polyline->isVisible();
            polylineData.id = polyline->getId();
            polylineData.count = polyline->count();
            polylineData.countDeep = polyline->countDeep();
            polylineData.closed = polyline->isClosed();

            for (const RS_Entity *e : *polyline) {
                if (e->rtti() == RS2::EntityLine) {
                    RS_Vector startPoint = e->getStartpoint();
                    startPoint = startPoint * scaleFactor;
                    startPoint.rotate(rotation);
                    startPoint += insertionPoint;
                    polylinePoints.append(QVector3D(startPoint.x, startPoint.y, startPoint.z));
                } else {
                    qDebug() << "Ignore entity with rtti=" << e->rtti()
                             << "(not an EntityLine) in EntityPolyline";
                }
            }

            const RS_Entity *lastEntity = polyline->last();
            if (lastEntity && lastEntity->rtti() == RS2::EntityLine) {
                RS_Vector endPoint = lastEntity->getEndpoint();
                endPoint = endPoint * scaleFactor;
                endPoint.rotate(rotation);
                endPoint += insertionPoint;
                polylinePoints.append(QVector3D(endPoint.x, endPoint.y, endPoint.z));
            }

            allPolylinesPoints.append(std::make_pair(polylineData, polylinePoints));

        } else if (entity->rtti() == RS2::EntityInsert) {
            RS_Insert *insert = static_cast<RS_Insert *>(entity);
            RS_Block *block = graphic->getBlockList()->find(insert->getName());
            if (block) {
                RS_Vector newInsertionPoint = insert->getInsertionPoint();
                RS_Vector newScaleFactor = insert->getScale();
                double newRotation = insert->getAngle();

                if (newScaleFactor.z == 0) {
                    newScaleFactor.z = 1;
                }

                newInsertionPoint = newInsertionPoint * scaleFactor;
                newInsertionPoint.rotate(rotation);
                newInsertionPoint += insertionPoint;

                newScaleFactor.x *= scaleFactor.x;
                newScaleFactor.y *= scaleFactor.y;
                newScaleFactor.z *= scaleFactor.z;

                newRotation += rotation;

                for (RS_Entity *subEntity : *block) {
                    processEntityRef(subEntity, newInsertionPoint, newScaleFactor, newRotation, processEntityRef);
                }
            }
        } else if (entity->rtti() == RS2::EntityLine) {

            QList<QVector3D> linePoints;
            PolylineData lineData;

            lineData.visible = entity->isVisible();
            lineData.id = entity->getId();
            lineData.count = 2;
            lineData.countDeep = 2;
            lineData.closed = false;

            RS_Vector startPoint = entity->getStartpoint();
            RS_Vector endPoint = entity->getEndpoint();

            startPoint = startPoint * scaleFactor;
            startPoint.rotate(rotation);
            startPoint += insertionPoint;

            endPoint = endPoint * scaleFactor;
            endPoint.rotate(rotation);
            endPoint += insertionPoint;

            linePoints.append(QVector3D(startPoint.x, startPoint.y, startPoint.z));
            linePoints.append(QVector3D(endPoint.x, endPoint.y, endPoint.z));

            allPolylinesPoints.append(std::make_pair(lineData, linePoints));
        } else {
            qDebug() << "Ignore top level entity with rtti=" << entity->rtti()
                     << "(not an EntityPolyline, EntityInsert, or EntityLine)";
        }
    };

    for (RS_Entity *entity : *graphic) {
        processEntity(entity, RS_Vector(0, 0, 0), RS_Vector(1, 1, 1), 0, processEntity);
    }

    qDebug() << "Printing" << dxfFile << "to" << params.outFile << ">>>>";

    serializePolylines(allPolylinesPoints, params.outFile);

    qDebug() << "Printing" << dxfFile << "to" << params.outFile << "DONE";

    std::cout << "READING" << params.outFile.toStdString();

    QList<std::pair<PolylineData, QList<QVector3D>>> resultPolylinesPoints;
    deserializePolylines(resultPolylinesPoints, params.outFile);

    int polylineCount = 0;
    for (const auto &polylinePair : resultPolylinesPoints) {
        const PolylineData &polylineData = polylinePair.first;
        const QList<QVector3D> &polylinePoints = polylinePair.second;

        std::cout << "Polyline no=" << polylineCount++ << ":" << std::endl;
        std::cout << "Visible:" << (polylineData.visible ? "true" : "false") << std::endl;
        std::cout << "ID:" << polylineData.id << std::endl;
        std::cout << "Count:" << polylineData.count << std::endl;
        std::cout << "Count Deep:" << polylineData.countDeep << std::endl;
        std::cout << "Closed:" << (polylineData.closed ? "true" : "false") << std::endl;

        for (const auto &point : polylinePoints) {
            std::cout << point.x() << " " << point.y() << " " << point.z() << std::endl;
        }

        std::cout << std::endl;
    }

    std::cout << "READING DONE" << std::endl;

    delete doc;
}

void serializePolylines(const QList<std::pair<PolylineData, QList<QVector3D>>> &allPolylinesPoints,
                        const QString &filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug() << "Failed to open file for writing:" << filename;
        return;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_6_6);

    out << static_cast<quint32>(allPolylinesPoints.count());
    for (const auto &polylinePair : allPolylinesPoints) {
        const PolylineData &polylineData = polylinePair.first;
        const QList<QVector3D> &polylinePoints = polylinePair.second;

        out << polylineData.visible;
        out << static_cast<quint64>(polylineData.id);
        out << polylineData.count;
        out << polylineData.countDeep;
        out << polylineData.closed;

        out << static_cast<quint32>(polylinePoints.size());

        for (const auto &point : polylinePoints) {
            out << point;
        }
    }

    file.close();
}

bool deserializePolylines(QList<std::pair<PolylineData, QList<QVector3D>>> &outPolylinesPoints,
                          const QString &filename)
{
    QFile file(filename);

    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Failed to open file for reading:" << filename;
        return false;
    }

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_6_6);

    quint32 totalElements;
    in >> totalElements;

    outPolylinesPoints.reserve(totalElements);

    for (quint32 i = 0; i < totalElements; ++i) {
        PolylineData polylineData;
        quint64 id;
        in >> polylineData.visible;
        in >> id;
        polylineData.id = static_cast<unsigned long int>(id);
        in >> polylineData.count;
        in >> polylineData.countDeep;
        in >> polylineData.closed;

        quint32 numPoints;
        in >> numPoints;

        QList<QVector3D> polylinePoints;
        polylinePoints.resize(numPoints);
        for (quint32 j = 0; j < numPoints; ++j) {
            in >> polylinePoints[j];
        }

        outPolylinesPoints.append({polylineData, polylinePoints});
    }

    file.close();
    return true;
}

void VecConverterLoop::convertManyDxfToOneVec() {

    struct DxfPage {
        RS_Document* doc;
        RS_Graphic* graphic;
        QString dxfFile;
        QPageSize::PageSizeId paperSize;
    };

    if (!params.outDir.isEmpty()) {
        QFileInfo outFileInfo(params.outFile);
        params.outFile = params.outDir + "/" + outFileInfo.fileName();
    }

    QVector<DxfPage> pages;

    for (auto &dxfFile : params.dxfFiles) {
        DxfPage page;

        page.dxfFile = dxfFile;

        if (!openDocAndSetGraphic(&page.doc, &page.graphic, dxfFile))
            continue;

        qDebug() << "Opened" << dxfFile;

        //touchGraphic(page.graphic, params);

        pages.append(page);

        //nrPages++;
    }

    // TODO
}


static bool openDocAndSetGraphic(RS_Document** doc, RS_Graphic** graphic,
    const QString& dxfFile)
{
    *doc = new RS_Graphic();

    if (!(*doc)->open(dxfFile, RS2::FormatUnknown)) {
        qDebug() << "ERROR: Failed to open document" << dxfFile;
        delete *doc;
        return false;
    }

    *graphic = (*doc)->getGraphic();
    if (*graphic == nullptr) {
        qDebug() << "ERROR: No graphic in" << dxfFile;
        delete *doc;
        return false;
    }

    return true;
}

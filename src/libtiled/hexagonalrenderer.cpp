/*
 * hexagonalrenderer.cpp
 * Copyright 2011-2014, Thorbjørn Lindeijer <thorbjorn@lindeijer.nl>
 *
 * This file is part of libtiled.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *    1. Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "hexagonalrenderer.h"

#include "map.h"
#include "mapobject.h"
#include "tile.h"
#include "tilelayer.h"
#include "tileset.h"

#include <QVector2D>
#include <QtCore/qmath.h>

#include <limits>

using namespace Tiled;

HexagonalRenderer::RenderParams::RenderParams(const Map *map, const Workspace &workspace)
    : tileWidth(workspace.tileWidth() & ~1)
    , tileHeight(workspace.tileHeight() & ~1)
    , sideLengthX(0)
    , sideLengthY(0)
    , staggerX(map->staggerAxis() == Map::StaggerX)
    , staggerEven(map->staggerIndex() == Map::StaggerEven)
{
    if (map->orientation() == Map::Hexagonal) {
        if (staggerX)
            sideLengthX = map->hexSideLength();
        else
            sideLengthY = map->hexSideLength();
    }

    sideOffsetX = (tileWidth - sideLengthX) / 2;
    sideOffsetY = (tileHeight - sideLengthY) / 2;

    columnWidth = sideOffsetX + sideLengthX;
    rowHeight = sideOffsetY + sideLengthY;
}


QSize HexagonalRenderer::workSize(const Workspace &workspace) const
{
    const RenderParams p(map(), workspace);

    // The map size is the same regardless of which indexes are shifted.
    if (p.staggerX) {
        QSize size(workspace.width() * p.columnWidth + p.sideOffsetX,
                   workspace.height() * (p.tileHeight + p.sideLengthY));

        if (workspace.width() > 1)
            size.rheight() += p.rowHeight;

        return size;
    } else {
        QSize size(workspace.width() * (p.tileWidth + p.sideLengthX),
                   workspace.height() * p.rowHeight + p.sideOffsetY);

        if (workspace.height() > 1)
            size.rwidth() += p.columnWidth;

        return size;
    }
}

QRect HexagonalRenderer::boundingRect(const QRect &rect, const Workspace &workspace) const
{
    const RenderParams p(map(), workspace);

    QPoint topLeft = tileToScreenCoords(rect.topLeft(), workspace).toPoint();
    int width, height;

    if (p.staggerX) {
        width = rect.width() * p.columnWidth + p.sideOffsetX;
        height = rect.height() * (p.tileHeight + p.sideLengthY);

        if (rect.width() > 1) {
            height += p.rowHeight;
            if (p.doStaggerX(rect.x()))
                topLeft.ry() -= p.rowHeight;
        }
    } else {
        width = rect.width() * (p.tileWidth + p.sideLengthX);
        height = rect.height() * p.rowHeight + p.sideOffsetY;

        if (rect.height() > 1) {
            width += p.columnWidth;
            if (p.doStaggerY(rect.y()))
                topLeft.rx() -= p.columnWidth;
        }
    }

    return QRect(topLeft.x(), topLeft.y(), width, height);
}

void HexagonalRenderer::drawGrid(QPainter *painter, const QRectF &exposed, const Workspace &workspace,
                                 QColor gridColor) const
{
    QRect rect = exposed.toAlignedRect();
    if (rect.isNull())
        return;

    const RenderParams p(map(), workspace);

    // Determine the tile and pixel coordinates to start at
    QPoint startTile = screenToTileCoords(rect.topLeft(), workspace).toPoint();
    QPoint startPos = tileToScreenCoords(startTile, workspace).toPoint();

    /* Determine in which half of the tile the top-left corner of the area we
     * need to draw is. If we're in the upper half, we need to start one row
     * up due to those tiles being visible as well. How we go up one row
     * depends on whether we're in the left or right half of the tile.
     */
    const bool inUpperHalf = rect.y() - startPos.y() < p.sideOffsetY;
    const bool inLeftHalf = rect.x() - startPos.x() < p.sideOffsetX;

    if (inUpperHalf)
        startTile.ry()--;
    if (inLeftHalf)
        startTile.rx()--;

    startTile.setX(qMax(0, startTile.x()));
    startTile.setY(qMax(0, startTile.y()));

    startPos = tileToScreenCoords(startTile, workspace).toPoint();

    const QPoint oct[8] = {
        QPoint(0,                           p.tileHeight - p.sideOffsetY),
        QPoint(0,                           p.sideOffsetY),
        QPoint(p.sideOffsetX,               0),
        QPoint(p.tileWidth - p.sideOffsetX, 0),
        QPoint(p.tileWidth,                 p.sideOffsetY),
        QPoint(p.tileWidth,                 p.tileHeight - p.sideOffsetY),
        QPoint(p.tileWidth - p.sideOffsetX, p.tileHeight),
        QPoint(p.sideOffsetX,               p.tileHeight)
    };

    QVector<QLine> lines;
    lines.reserve(8);

    QPen gridPen = makeGridPen(painter->device(), gridColor);
    painter->setPen(gridPen);

    if (p.staggerX) {
        // Odd row shifting is applied in the rendering loop, so un-apply it here
        if (p.doStaggerX(startTile.x()))
            startPos.ry() -= p.rowHeight;

        for (; startPos.x() <= rect.right() && startTile.x() < workspace.width(); startTile.rx()++) {
            QPoint rowTile = startTile;
            QPoint rowPos = startPos;

            if (p.doStaggerX(startTile.x()))
                rowPos.ry() += p.rowHeight;

            for (; rowPos.y() <= rect.bottom() && rowTile.y() < workspace.height(); rowTile.ry()++) {
                lines.append(QLine(rowPos + oct[1], rowPos + oct[2]));
                lines.append(QLine(rowPos + oct[2], rowPos + oct[3]));
                lines.append(QLine(rowPos + oct[3], rowPos + oct[4]));

                const bool isStaggered = p.doStaggerX(startTile.x());
                const bool lastRow = rowTile.y() == workspace.height() - 1;
                const bool lastColumn = rowTile.x() == workspace.width() - 1;
                const bool bottomLeft = rowTile.x() == 0 || (lastRow && isStaggered);
                const bool bottomRight = lastColumn || (lastRow && isStaggered);

                if (bottomRight)
                    lines.append(QLine(rowPos + oct[5], rowPos + oct[6]));
                if (lastRow)
                    lines.append(QLine(rowPos + oct[6], rowPos + oct[7]));
                if (bottomLeft)
                    lines.append(QLine(rowPos + oct[7], rowPos + oct[0]));

                painter->drawLines(lines);
                lines.resize(0);

                rowPos.ry() += p.tileHeight + p.sideLengthY;
            }

            startPos.rx() += p.columnWidth;
        }
    } else {
        // Odd row shifting is applied in the rendering loop, so un-apply it here
        if (p.doStaggerY(startTile.y()))
            startPos.rx() -= p.columnWidth;

        for (; startPos.y() <= rect.bottom() && startTile.y() < workspace.height(); startTile.ry()++) {
            QPoint rowTile = startTile;
            QPoint rowPos = startPos;

            if (p.doStaggerY(startTile.y()))
                rowPos.rx() += p.columnWidth;

            for (; rowPos.x() <= rect.right() && rowTile.x() < workspace.width(); rowTile.rx()++) {
                lines.append(QLine(rowPos + oct[0], rowPos + oct[1]));
                lines.append(QLine(rowPos + oct[1], rowPos + oct[2]));
                lines.append(QLine(rowPos + oct[3], rowPos + oct[4]));

                const bool isStaggered = p.doStaggerY(startTile.y());
                const bool lastRow = rowTile.y() == workspace.height() - 1;
                const bool lastColumn = rowTile.x() == workspace.width() - 1;
                const bool bottomLeft = lastRow || (rowTile.x() == 0 && !isStaggered);
                const bool bottomRight = lastRow || (lastColumn && isStaggered);

                if (lastColumn)
                    lines.append(QLine(rowPos + oct[4], rowPos + oct[5]));
                if (bottomRight)
                    lines.append(QLine(rowPos + oct[5], rowPos + oct[6]));
                if (bottomLeft)
                    lines.append(QLine(rowPos + oct[7], rowPos + oct[0]));

                painter->drawLines(lines);
                lines.resize(0);

                rowPos.rx() += p.tileWidth + p.sideLengthX;
            }

            startPos.ry() += p.rowHeight;
        }
    }
}

void HexagonalRenderer::drawTileLayer(QPainter *painter,
                                      const TileLayer *layer,
									  const Workspace &workspace,
                                      const QRectF &exposed) const
{
	Workspace tileWorkspace(layer->width(), layer->height(), layer->tileWidth(), layer->tileHeight());
    const RenderParams p(map(), tileWorkspace);

    QRect rect = exposed.toAlignedRect();

    if (rect.isNull())
        rect = boundingRect(layer->bounds(), tileWorkspace);

    QMargins drawMargins = layer->drawMargins();
    drawMargins.setBottom(drawMargins.bottom() + p.tileHeight);
    drawMargins.setRight(drawMargins.right() - p.tileWidth);

    rect.adjust(-drawMargins.right(),
                -drawMargins.bottom(),
                drawMargins.left(),
                drawMargins.top());

    // Determine the tile and pixel coordinates to start at
    QPoint startTile = screenToTileCoords(rect.topLeft(), tileWorkspace).toPoint();

    // Compensate for the layer position
    startTile -= layer->position();

    QPoint startPos = tileToScreenCoords(startTile + layer->position(), tileWorkspace).toPoint();

    /* Determine in which half of the tile the top-left corner of the area we
     * need to draw is. If we're in the upper half, we need to start one row
     * up due to those tiles being visible as well. How we go up one row
     * depends on whether we're in the left or right half of the tile.
     */
    const bool inUpperHalf = rect.y() - startPos.y() < p.sideOffsetY;
    const bool inLeftHalf = rect.x() - startPos.x() < p.sideOffsetX;

    if (inUpperHalf)
        startTile.ry()--;
    if (inLeftHalf)
        startTile.rx()--;

    CellRenderer renderer(painter, CellRenderer::HexagonalCells);

    if (p.staggerX) {
        startTile.setX(qMax(-1, startTile.x()));
        startTile.setY(qMax(-1, startTile.y()));

        startPos = tileToScreenCoords(startTile + layer->position(), tileWorkspace).toPoint();
        startPos.ry() += p.tileHeight;

        bool staggeredRow = p.doStaggerX(startTile.x() + layer->x());

        for (; startPos.y() < rect.bottom() && startTile.y() < layer->height();) {
            QPoint rowTile = startTile;
            QPoint rowPos = startPos;

            for (; rowPos.x() < rect.right() && rowTile.x() < layer->width(); rowTile.rx() += 2) {
                if (layer->contains(rowTile)) {
                    const Cell &cell = layer->cellAt(rowTile);

                    if (!cell.isEmpty()) {
                        Tile *tile = cell.tile();
                        QSize size = tile ? tile->size() : tileWorkspace.tileSize();
                        renderer.render(cell, rowPos, size, CellRenderer::BottomLeft);
                    }
                }

                rowPos.rx() += p.tileWidth + p.sideLengthX;
            }

            if (staggeredRow) {
                startTile.rx() -= 1;
                startTile.ry() += 1;
                startPos.rx() -= p.columnWidth;
                staggeredRow = false;
            } else {
                startTile.rx() += 1;
                startPos.rx() += p.columnWidth;
                staggeredRow = true;
            }

            startPos.ry() += p.rowHeight;
        }
    } else {
        startTile.setX(qMax(0, startTile.x()));
        startTile.setY(qMax(0, startTile.y()));

        startPos = tileToScreenCoords(startTile + layer->position(), tileWorkspace).toPoint();
        startPos.ry() += p.tileHeight;

        // Odd row shifting is applied in the rendering loop, so un-apply it here
        if (p.doStaggerY(startTile.y() + layer->y()))
            startPos.rx() -= p.columnWidth;

        for (; startPos.y() < rect.bottom() && startTile.y() < layer->height(); startTile.ry()++) {
            QPoint rowTile = startTile;
            QPoint rowPos = startPos;

            if (p.doStaggerY(startTile.y() + layer->y()))
                rowPos.rx() += p.columnWidth;

            for (; rowPos.x() < rect.right() && rowTile.x() < layer->width(); rowTile.rx()++) {
                const Cell &cell = layer->cellAt(rowTile);

                if (!cell.isEmpty()) {
                    Tile *tile = cell.tile();
                    QSize size = tile ? tile->size() : tileWorkspace.tileSize();
                    renderer.render(cell, rowPos, size, CellRenderer::BottomLeft);
                }

                rowPos.rx() += p.tileWidth + p.sideLengthX;
            }

            startPos.ry() += p.rowHeight;
        }
    }
}

void HexagonalRenderer::drawTileSelection(QPainter *painter,
                                          const QRegion &region,
										  const Workspace &workspace,
                                          const QColor &color,
                                          const QRectF &exposed) const
{
    painter->setBrush(color);
    painter->setPen(Qt::NoPen);

    foreach (const QRect &r, region.rects()) {
        for (int y = r.top(); y <= r.bottom(); ++y) {
            for (int x = r.left(); x <= r.right(); ++x) {
                const QPolygonF polygon = tileToScreenPolygon(x, y);
                if (QRectF(polygon.boundingRect()).intersects(exposed))
                    painter->drawConvexPolygon(polygon);
            }
        }
    }
}

QPointF HexagonalRenderer::tileToPixelCoords(qreal x, qreal y, const Workspace &workspace) const
{
    return HexagonalRenderer::tileToScreenCoords(x, y, workspace);
}

QPointF HexagonalRenderer::pixelToTileCoords(qreal x, qreal y, const Workspace &workspace) const
{
    return HexagonalRenderer::screenToTileCoords(x, y, workspace);
}

/**
 * Converts screen to tile coordinates. Sub-tile return values are not
 * supported by this renderer.
 */
QPointF HexagonalRenderer::screenToTileCoords(qreal x, qreal y, const Workspace &workspace) const
{
    const RenderParams p(map(), workspace);

    if (p.staggerX)
        x -= p.staggerEven ? p.tileWidth : p.sideOffsetX;
    else
        y -= p.staggerEven ? p.tileHeight : p.sideOffsetY;

    // Start with the coordinates of a grid-aligned tile
    QPoint referencePoint = QPoint(qFloor(x / (p.columnWidth * 2)),
                                   qFloor(y / (p.rowHeight * 2)));

    // Relative x and y position on the base square of the grid-aligned tile
    const QVector2D rel(x - referencePoint.x() * (p.columnWidth * 2),
                        y - referencePoint.y() * (p.rowHeight * 2));

    // Adjust the reference point to the correct tile coordinates
    int &staggerAxisIndex = p.staggerX ? referencePoint.rx() : referencePoint.ry();
    staggerAxisIndex *= 2;
    if (p.staggerEven)
        ++staggerAxisIndex;

    // Determine the nearest hexagon tile by the distance to the center
    QVector2D centers[4];

    if (p.staggerX) {
        const int left = p.sideLengthX / 2;
        const int centerX = left + p.columnWidth;
        const int centerY = p.tileHeight / 2;

        centers[0] = QVector2D(left,                    centerY);
        centers[1] = QVector2D(centerX,                 centerY - p.rowHeight);
        centers[2] = QVector2D(centerX,                 centerY + p.rowHeight);
        centers[3] = QVector2D(centerX + p.columnWidth, centerY);
    } else {
        const int top = p.sideLengthY / 2;
        const int centerX = p.tileWidth / 2;
        const int centerY = top + p.rowHeight;

        centers[0] = QVector2D(centerX,                 top);
        centers[1] = QVector2D(centerX - p.columnWidth, centerY);
        centers[2] = QVector2D(centerX + p.columnWidth, centerY);
        centers[3] = QVector2D(centerX,                 centerY + p.rowHeight);
    }

    int nearest = 0;
    qreal minDist = std::numeric_limits<qreal>::max();

    for (int i = 0; i < 4; ++i) {
        const QVector2D &center = centers[i];
        const qreal dc = (center - rel).lengthSquared();
        if (dc < minDist) {
            minDist = dc;
            nearest = i;
        }
    }

    static const QPoint offsetsStaggerX[4] = {
        QPoint( 0,  0),
        QPoint(+1, -1),
        QPoint(+1,  0),
        QPoint(+2,  0),
    };
    static const QPoint offsetsStaggerY[4] = {
        QPoint( 0,  0),
        QPoint(-1, +1),
        QPoint( 0, +1),
        QPoint( 0, +2),
    };

    const QPoint *offsets = p.staggerX ? offsetsStaggerX : offsetsStaggerY;
    return referencePoint + offsets[nearest];
}

/**
 * Converts tile to screen coordinates. Sub-tile return values are not
 * supported by this renderer.
 */
QPointF HexagonalRenderer::tileToScreenCoords(qreal x, qreal y, const Workspace &workspace) const
{
    const RenderParams p(map(), workspace);
    const int tileX = qFloor(x);
    const int tileY = qFloor(y);
    int pixelX, pixelY;

    if (p.staggerX) {
        pixelY = tileY * (p.tileHeight + p.sideLengthY);
        if (p.doStaggerX(tileX))
            pixelY += p.rowHeight;

        pixelX = tileX * p.columnWidth;
    } else {
        pixelX = tileX * (p.tileWidth + p.sideLengthX);
        if (p.doStaggerY(tileY))
            pixelX += p.columnWidth;

        pixelY = tileY * p.rowHeight;
    }

    return QPointF(pixelX, pixelY);
}

QPoint HexagonalRenderer::topLeft(int x, int y) const
{
    if (map()->staggerAxis() == Map::StaggerY) {
        if ((y & 1) ^ map()->staggerIndex())
            return QPoint(x, y - 1);
        else
            return QPoint(x - 1, y - 1);
    } else {
        if ((x & 1) ^ map()->staggerIndex())
            return QPoint(x - 1, y);
        else
            return QPoint(x - 1, y - 1);
    }
}

QPoint HexagonalRenderer::topRight(int x, int y) const
{
    if (map()->staggerAxis() == Map::StaggerY) {
        if ((y & 1) ^ map()->staggerIndex())
            return QPoint(x + 1, y - 1);
        else
            return QPoint(x, y - 1);
    } else {
        if ((x & 1) ^ map()->staggerIndex())
            return QPoint(x + 1, y);
        else
            return QPoint(x + 1, y - 1);
    }
}

QPoint HexagonalRenderer::bottomLeft(int x, int y) const
{
    if (map()->staggerAxis() == Map::StaggerY) {
        if ((y & 1) ^ map()->staggerIndex())
            return QPoint(x, y + 1);
        else
            return QPoint(x - 1, y + 1);
    } else {
        if ((x & 1) ^ map()->staggerIndex())
            return QPoint(x - 1, y + 1);
        else
            return QPoint(x - 1, y);
    }
}

QPoint HexagonalRenderer::bottomRight(int x, int y) const
{
    if (map()->staggerAxis() == Map::StaggerY) {
        if ((y & 1) ^ map()->staggerIndex())
            return QPoint(x + 1, y + 1);
        else
            return QPoint(x, y + 1);
    } else {
        if ((x & 1) ^ map()->staggerIndex())
            return QPoint(x + 1, y + 1);
        else
            return QPoint(x + 1, y);
    }
}

QPolygonF HexagonalRenderer::tileToScreenPolygon(int x, int y) const
{
	// LUCA TODO: Do this properly, workspace is not set to anything
	const Workspace workspace;
    const RenderParams p(map(), workspace);
    const QPointF topRight = tileToScreenCoords(x, y, workspace);

    QPolygonF polygon(8);
    polygon[0] = topRight + QPoint(0,                           p.tileHeight - p.sideOffsetY);
    polygon[1] = topRight + QPoint(0,                           p.sideOffsetY);
    polygon[2] = topRight + QPoint(p.sideOffsetX,               0);
    polygon[3] = topRight + QPoint(p.tileWidth - p.sideOffsetX, 0);
    polygon[4] = topRight + QPoint(p.tileWidth,                 p.sideOffsetY);
    polygon[5] = topRight + QPoint(p.tileWidth,                 p.tileHeight - p.sideOffsetY);
    polygon[6] = topRight + QPoint(p.tileWidth - p.sideOffsetX, p.tileHeight);
    polygon[7] = topRight + QPoint(p.sideOffsetX,               p.tileHeight);
    return polygon;
}

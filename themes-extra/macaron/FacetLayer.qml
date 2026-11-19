// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// One layer of the "macaron triangle-grid background": opaque polygon patches, drawn at once on a Canvas (not hundreds of Items).
// The pattern and the parameters come from FACET in systems.js (tuned with facet_tool.html); offsetX/offsetY decide which part of the layer space is taken.
import QtQuick 2.15
import "systems.js" as Sys

Item {
    id: fl
    property var desc: null
    property real offsetX: 0
    property real offsetY: 0
    property real texScale: 2          // canvas resolution scale (still sharp on a 1080p screen)

    readonly property string baseHex: desc ? desc.base : "#EE8D87"
    readonly property string darkHex: desc ? desc.dark : "#D96A66"

    Canvas {
        id: cv
        anchors.fill: parent
        canvasSize: Qt.size(Math.max(1, Math.round(fl.width * fl.texScale)), Math.max(1, Math.round(fl.height * fl.texScale)))
        renderStrategy: Canvas.Threaded
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            ctx.scale(fl.texScale, fl.texScale);
            ctx.translate(-fl.offsetX, -fl.offsetY);
            ctx.fillStyle = fl.baseHex;
            ctx.fillRect(fl.offsetX - 2, fl.offsetY - 2, fl.width + 4, fl.height + 4);
            var polys = Sys.facetPolys(Sys.facetSeed(fl.desc));
            ctx.lineWidth = 0.7;
            ctx.lineJoin = "round";
            for (var i = 0; i < polys.length; i++) {
                var p = polys[i];
                var minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
                for (var q = 0; q < p.pts.length; q++) {
                    var x = p.pts[q][0], y = p.pts[q][1];
                    if (x < minx) minx = x;
                    if (x > maxx) maxx = x;
                    if (y < miny) miny = y;
                    if (y > maxy) maxy = y;
                }
                // skip polygons outside the visible area
                if (maxx < fl.offsetX - 4 || minx > fl.offsetX + fl.width + 4 || maxy < fl.offsetY - 4 || miny > fl.offsetY + fl.height + 4)
                    continue;
                var c = Sys.facetFill(p.tone, fl.baseHex, fl.darkHex);
                ctx.fillStyle = c;
                ctx.strokeStyle = c;
                ctx.beginPath();
                ctx.moveTo(p.pts[0][0], p.pts[0][1]);
                for (var k = 1; k < p.pts.length; k++)
                    ctx.lineTo(p.pts[k][0], p.pts[k][1]);
                ctx.closePath();
                ctx.fill();
                ctx.stroke();
            }
        }
    }
    onDescChanged: cv.requestPaint()
    onWidthChanged: cv.requestPaint()
    onHeightChanged: cv.requestPaint()
}

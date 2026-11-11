#!/usr/bin/env python3
"""Helper for the zone outlines: trace the buff-point areas of manual figure 5-23.

    pdfimages -png -f 103 -l 103 docs/rules/RMUC2027_rules_manual_CN_V1.0.0_20261015.pdf fig523
    python3 tools/field_extract/trace_zones.py fig523-002.png

Development-time tool (needs opencv-python-headless and numpy). The figure is a
1220 x 743 pixel top view; the battlefield occupies pixels x 52..1172 and
y 63..662, i.e. 25 mm per pixel. Areas drawn with green hatching and a yellow
outline are closed morphologically and traced; the printed polygons (field
millimetres, y up) were reviewed by hand, merged areas were split along their
yellow separators, and the red-side set was entered into build_field_json.py.
"""
import json
import sys

import cv2
import numpy as np

X0, X1, Y0, Y1 = 52, 1172, 63, 662


def main():
    image = cv2.imread(sys.argv[1])
    if image is None or image.shape[:2] != (743, 1220):
        raise SystemExit('expected the 1220 x 743 figure image extracted with pdfimages')
    b, g, r = (image[:, :, i].astype(int) for i in range(3))
    green = (g > 150) & (r < 120) & (b < 120)
    yellow = (r > 200) & (g > 200) & (b < 120)
    mask = (green | yellow).astype(np.uint8) * 255
    closed = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, cv2.getStructuringElement(cv2.MORPH_RECT, (5, 5)))
    sx, sy = 28000 / (X1 - X0 + 1), 15000 / (Y1 - Y0 + 1)
    contours, _ = cv2.findContours(closed, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    zones = []
    for contour in contours:
        area = cv2.contourArea(contour)
        if area < 300:
            continue
        poly = cv2.approxPolyDP(contour, 1.5, True)[:, 0, :]
        m = cv2.moments(contour)
        zones.append({'centre_mm': [round((m['m10'] / m['m00'] + 0.5 - X0) * sx), round((Y1 + 0.5 - m['m01'] / m['m00']) * sy)],
                      'area_m2': round(area * sx * sy / 1e6, 2),
                      'polygon_mm': [[round((x + 0.5 - X0) * sx), round((Y1 + 0.5 - y) * sy)] for x, y in poly]})
    zones.sort(key=lambda z: z['centre_mm'])
    print(json.dumps(zones, indent=1))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Step 1 of the field extraction: official STEP model -> triangle cache.

Development-time tool. It is NOT needed to build or run the simulator; the
extracted field data is checked in under field_sim/field/. Requirements:
Python 3.10+, numpy and the OpenCASCADE bindings (`pip install cadquery-ocp`).

    python3 tools/field_extract/extract_step.py RMUC2027_V1.0.0.stp cache_dir

The official model is the one published by the RoboMaster Organizing Committee
(see field_sim/field/rmuc2027_v1/provenance.json for URL, size and checksums).
Reading the 653 MB file takes about three minutes and 5 GB of memory.

Output: <cache_dir>/mesh.npz and <cache_dir>/mesh.json. Every leaf instance of
the assembly is tessellated (1 mm chord tolerance) in the CAD frame with its
assembly placement applied. Triangles keep their outward orientation and are
tagged with the solid or free shell they belong to, so that the next step can
tell closed volumes from open sheets.
"""
import hashlib
import json
import pathlib
import resource
import sys
import time

import numpy as np
from OCP.BRep import BRep_Tool
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.IFSelect import IFSelect_RetDone
from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDataStd import TDataStd_Name
from OCP.TDF import TDF_Label
from OCP.TDocStd import TDocStd_Document
from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED, TopAbs_SHELL, TopAbs_SOLID
from OCP.TopExp import TopExp_Explorer
from OCP.TopLoc import TopLoc_Location
from OCP.TopoDS import TopoDS
from OCP.XCAFDoc import XCAFDoc_DocumentTool
from OCP.collections import Sequence_TDF_Label

LINEAR_DEFLECTION_MM = 1.0
ANGULAR_DEFLECTION_RAD = 0.35


def file_digests(path):
    md5, sha = hashlib.md5(), hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 22), b''):
            md5.update(block)
            sha.update(block)
    return md5.hexdigest(), sha.hexdigest()


def main():
    src, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    start = time.time()

    def log(*a):
        print(f'[{time.time() - start:6.1f}s {resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / 1e6:5.2f}GB]', *a, flush=True)

    md5, sha = file_digests(src)
    log('source', src.name, src.stat().st_size, 'bytes md5', md5)
    doc = TDocStd_Document(TCollection_ExtendedString('doc'))
    reader = STEPCAFControl_Reader()
    reader.SetNameMode(True)
    reader.SetColorMode(False)
    reader.SetLayerMode(False)
    if reader.ReadFile(str(src)) != IFSelect_RetDone:
        raise SystemExit('STEP file could not be read')
    log('file read')
    if not reader.Transfer(doc):
        raise SystemExit('STEP transfer failed')
    log('transferred')
    tool = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())

    def name(label):
        attribute = TDataStd_Name()
        return attribute.Get().ToExtString() if label.FindAttribute(TDataStd_Name.GetID_s(), attribute) else ''

    leaves = []

    def walk(label, loc, path):
        if tool.IsReference_s(label):
            ref = TDF_Label()
            tool.GetReferredShape_s(label, ref)
            walk(ref, loc.Multiplied(tool.GetLocation_s(label)), path + [name(label) or name(ref)])
        elif tool.IsAssembly_s(label):
            parts = Sequence_TDF_Label()
            tool.GetComponents_s(label, parts, False)
            for i in range(1, parts.Length() + 1):
                walk(parts.Value(i), loc, path)
        else:
            leaves.append((label, loc, path))

    roots = Sequence_TDF_Label()
    tool.GetFreeShapes(roots)
    for i in range(1, roots.Length() + 1):
        walk(roots.Value(i), TopLoc_Location(), [name(roots.Value(i))])
    log(len(leaves), 'leaf instances')

    def face_triangles(face, placement):
        loc = TopLoc_Location()
        tri = BRep_Tool.Triangulation_s(face, loc)
        if tri is None:
            return None
        ft = loc.Transformation()
        pts = np.empty((tri.NbNodes(), 3))
        for i in range(1, tri.NbNodes() + 1):
            p = tri.Node(i).Transformed(ft).Transformed(placement)
            pts[i - 1] = (p.X(), p.Y(), p.Z())
        idx = np.empty((tri.NbTriangles(), 3), dtype=np.int64)
        flip = (face.Orientation() == TopAbs_REVERSED) != placement.IsNegative()
        for i in range(1, tri.NbTriangles() + 1):
            a, b, c = tri.Triangle(i).Get()
            idx[i - 1] = (a - 1, c - 1, b - 1) if flip else (a - 1, b - 1, c - 1)
        return pts[idx]

    def faces_of(shape, kind=None, avoid=None):
        e = TopExp_Explorer(shape, TopAbs_FACE) if kind is None else TopExp_Explorer(shape, TopAbs_FACE, avoid)
        while e.More():
            yield TopoDS.Face(e.Current())
            e.Next()

    def children(shape, kind, avoid=None):
        e = TopExp_Explorer(shape, kind) if avoid is None else TopExp_Explorer(shape, kind, avoid)
        while e.More():
            yield e.Current()
            e.Next()

    meshed = set()
    arrays, index = {}, []
    for k, (label, loc, path) in enumerate(leaves):
        shape = tool.GetShape_s(label)
        tag = tuple(label.Tag() for label in [label])
        if tag not in meshed:
            BRepMesh_IncrementalMesh(shape, LINEAR_DEFLECTION_MM, False, ANGULAR_DEFLECTION_RAD, True)
            meshed.add(tag)
        placement = loc.Transformation()
        tris, part_of, parts, missing = [], [], [], 0
        groups = [('solid', s) for s in children(shape, TopAbs_SOLID)]
        groups += [('shell', s) for s in children(shape, TopAbs_SHELL, TopAbs_SOLID)]
        groups += [('faces', None)]
        for kind, sub in groups:
            faces = faces_of(sub) if sub is not None else faces_of(shape, TopAbs_FACE, TopAbs_SHELL)
            count = 0
            for face in faces:
                t = face_triangles(face, placement)
                if t is None:
                    missing += 1
                    continue
                tris.append(t)
                part_of.append(np.full(len(t), len(parts), dtype=np.int32))
                count += len(t)
            if count:
                parts.append(kind)
        t = np.concatenate(tris) if tris else np.zeros((0, 3, 3))
        arrays[f't{k}'] = t.astype(np.float32)
        arrays[f'p{k}'] = np.concatenate(part_of) if part_of else np.zeros(0, dtype=np.int32)
        index.append({'leaf': k, 'path': path, 'parts': parts, 'triangles': int(len(t)), 'faces_without_mesh': missing})
        log(f'{k:3d}', '/'.join(path)[-60:], len(t), 'triangles', len(parts), 'parts')
    np.savez_compressed(out / 'mesh.npz', **arrays)
    meta = {'source': {'name': src.name, 'bytes': src.stat().st_size, 'md5': md5, 'sha256': sha},
            'linear_deflection_mm': LINEAR_DEFLECTION_MM, 'angular_deflection_rad': ANGULAR_DEFLECTION_RAD, 'leaves': index}
    (out / 'mesh.json').write_text(json.dumps(meta, ensure_ascii=False, indent=1), encoding='utf-8')
    log('saved', out)


if __name__ == '__main__':
    main()

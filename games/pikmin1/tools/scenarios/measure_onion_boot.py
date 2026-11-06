#!/usr/bin/env python3
"""Read only structural Onion boot metadata; never export animation curves.

Usage: python3 measure_onion_boot.py /path/to/install-holding-assets
Formats: AnimInfo::doread/AnimMgr::read (animMgr.cpp), bundle entry headers
(StdSystem::loadBundle), and the first two AnimDck payload words (shapeBase.cpp).
"""
import hashlib
import json
from pathlib import Path
import struct
import sys


class Reader:
    def __init__(self, data):
        self.data, self.at = data, 0

    def read(self, fmt):
        result = struct.unpack_from(fmt, self.data, self.at)
        self.at += struct.calcsize(fmt)
        return result

    def integer(self):
        return self.read(">I")[0]

    def string(self):
        return self.bytes(self.integer()).rstrip(b"\0").decode("ascii")

    def bytes(self, size):
        end = self.at + size
        if end > len(self.data):
            raise ValueError("truncated data")
        result = self.data[self.at:end]
        self.at = end
        return result

    def params(self):
        result = {}
        while (header := self.integer()) != 0xFFFFFFFF:
            name = (header >> 8).to_bytes(3, "big").decode("ascii")
            # The string parameter's header size describes its in-memory type,
            # not its variable-length serialized string (Parameters::read).
            if name == "a01":
                result[name] = self.string()
            elif name == "spd":
                result[name] = self.read(">f")[0]
            else:
                result[name] = int.from_bytes(self.bytes(header & 255), "big")
        return result


def measure(install):
    root = install / "assets/dataDir/objects/goal"
    table, bundle = (root / "anims.bin").read_bytes(), (root / "goal.anm").read_bytes()
    payloads = {}
    r = Reader(bundle)
    for _ in range(r.integer()):
        kind, size = r.read(">II")
        name = r.string()
        # Only the payload header is decoded; no curves or geometry are decoded.
        payload = r.bytes(size)
        payloads[name] = dict(zip(("joints", "frames"), struct.unpack_from(">ii", payload)))
        payloads[name]["bundle_type"] = kind
    if r.at != len(bundle):
        raise ValueError("unparsed bundle tail")
    r = Reader(table)
    version, count = r.read(">II")
    mgr = r.params()
    boots = []
    for index in range(count):
        name, params = r.string(), r.params()
        keys = [r.integer() for _ in range(r.integer())]
        info = [r.read(">iHBB") for _ in range(r.integer())]
        events = [r.read(">iHBB") for _ in range(r.integer())]
        if index in (3, 4, 5):
            payload = payloads[mgr["a01"] + "/" + name]
            boots.append(dict(info=index, colour={3: "blue", 4: "red", 5: "yellow"}[index],
                              params=params, payload=payload, raw_keys=keys,
                              loaded_keys=[min(k, payload["frames"] - 1) for k in keys],
                              info_keys=info, event_keys=events))
    if r.at != len(table):
        raise ValueError("unparsed table tail")
    return dict(table_sha256=hashlib.sha256(table).hexdigest(),
                bundle_sha256=hashlib.sha256(bundle).hexdigest(),
                version=version, info_count=count, manager=mgr, boots=boots)


if __name__ == "__main__":
    print(json.dumps(measure(Path(sys.argv[1])), indent=2))

#!/usr/bin/env python3
"""A torrents file as another engine would have written it (docs/57 §5, DECISIONS C40 X1, X4; #88).

    foreign-torrents-file.py IN.omaseed-torrents OUT.omaseed-torrents --engine transmission|deluge

Takes a qBittorrent's export and rewrites what names the engine: the manifest's
server.engine (its version under "version", "qbittorrent" and "webApi" left out) and each
record's grouping the way that engine's file holds it (src/core/TorrentsBackup.hpp):

  transmission  tags = the category, then the tags; category ""
  deluge        the category kept, tags dropped

and what neither engine has (automatic management, the manifest's categories and tags).
The .torrent files are copied as they are. For a look at the import sheet's crossing blocks
without that engine's exporter; the result is private (0600), as an export is.
"""

import argparse
import io
import json
import os
import sys
import tarfile

VERSIONS = {"transmission": "4.1.3", "deluge": "2.2.0"}


def manifest_for(manifest: dict, engine: str) -> dict:
    server = dict(manifest.get("server", {}))
    server["engine"] = engine
    server["version"] = VERSIONS[engine]
    server.pop("qbittorrent", None)
    server.pop("webApi", None)
    out = dict(manifest)
    out["server"] = server
    out["categories"] = {}
    out["tags"] = []
    return out


def record_for(record: dict, engine: str) -> dict:
    out = dict(record)
    category = out.get("category") or ""
    tags = list(out.get("tags") or [])
    if engine == "transmission":
        labels = [category] if category else []
        labels += [tag for tag in tags if tag not in labels]
        out["category"] = ""
        out["tags"] = labels
    else:
        out["tags"] = []
    if "autoTmm" in out:
        out["autoTmm"] = False
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("source")
    parser.add_argument("target")
    parser.add_argument("--engine", required=True, choices=sorted(VERSIONS))
    args = parser.parse_args()

    fd = os.open(args.target, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    os.fchmod(fd, 0o600)
    records = 0
    with tarfile.open(args.source, "r:") as source, os.fdopen(fd, "wb") as raw:
        with tarfile.open(fileobj=raw, mode="w", format=tarfile.USTAR_FORMAT) as target:
            for member in source:
                if not member.isfile():
                    continue
                data = source.extractfile(member).read()
                if member.name == "manifest.json":
                    data = json.dumps(manifest_for(json.loads(data), args.engine), indent=4).encode()
                elif member.name.startswith("torrents/") and member.name.endswith(".json"):
                    data = json.dumps(record_for(json.loads(data), args.engine), indent=4).encode()
                    records += 1
                member.size = len(data)
                target.addfile(member, io.BytesIO(data))
    print(f"{args.target}: {records} torrents as {args.engine}'s", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())

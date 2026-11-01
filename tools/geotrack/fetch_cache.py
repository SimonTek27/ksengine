#!/usr/bin/env python3
"""Cache locale per la pipeline geotrack (vettoriale + tile).

Il binario C++ non fa rete: questo script riempie una cartella che il
generatore legge. Solo standard library.

    python3 fetch_cache.py <outdir> --bounds S,W,N,E [--overpass]
                           [--tile-url TEMPLATE] [--zoom 18] [--max-tiles 256]

--overpass        scarica le way raceway/track e le scrive in vector.geojson
                  (LineString, coordinate [lon, lat], l'ordine che legge
                  ks::geo::readCenterline)
--tile-url        template con {z}/{x}/{y}, per esempio
                  "https://example.invalid/{z}/{x}/{y}.png". Il provider lo
                  sceglie l'utente: qui non ce n'e' nessuno cablato e nessuna
                  chiave viene mai scritta su disco.

Scrive:
    <outdir>/vector.geojson
    <outdir>/tiles/<z>_<x>_<y>.png
    <outdir>/cache.json      bounds, zoom, elenco tile, attribuzione
"""

import argparse
import json
import math
import pathlib
import sys
import urllib.error
import urllib.parse
import urllib.request

OVERPASS_URL = "https://overpass-api.de/api/interpreter"
USER_AGENT = "ksengine-geotrack/1.0 (+offline cache builder)"


def parse_bounds(text):
    parts = [p.strip() for p in text.split(",")]
    if len(parts) != 4:
        raise SystemExit("--bounds wants south,west,north,east")
    south, west, north, east = (float(p) for p in parts)
    if not (-90.0 <= south < north <= 90.0):
        raise SystemExit("--bounds: south/north out of order or range")
    if west == east:
        raise SystemExit("--bounds: west and east are the same")
    return south, west, north, east


def overpass_query(bounds):
    south, west, north, east = bounds
    bbox = f"{south},{west},{north},{east}"
    parts = "\n".join(
        f'  way["highway"="{tag}"]({bbox});' for tag in ("raceway", "track")
    )
    return f"[out:json][timeout:120];\n(\n{parts}\n);\nout geom;\n"


def http_post(url, body):
    data = urllib.parse.urlencode({"data": body}).encode("utf-8")
    request = urllib.request.Request(
        url, data=data, headers={"User-Agent": USER_AGENT}
    )
    with urllib.request.urlopen(request, timeout=120) as response:
        return response.read()


def http_get(url, binary=False):
    request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(request, timeout=60) as response:
        payload = response.read()
    return payload if binary else payload.decode("utf-8")


def overpass_to_geojson(payload):
    document = json.loads(payload)
    features = []
    for element in document.get("elements", []):
        geometry = element.get("geometry") or []
        coords = [
            [node["lon"], node["lat"]]
            for node in geometry
            if "lon" in node and "lat" in node
        ]
        if len(coords) < 2:
            continue
        features.append(
            {
                "type": "Feature",
                "properties": {
                    "id": element.get("id"),
                    "tags": element.get("tags", {}),
                },
                "geometry": {"type": "LineString", "coordinates": coords},
            }
        )
    return {"type": "FeatureCollection", "features": features}


def tile_x(lon, zoom):
    return int((lon + 180.0) / 360.0 * (1 << zoom))


def tile_y(lat, zoom):
    lat_rad = math.radians(max(-85.05112878, min(85.05112878, lat)))
    return int(
        (1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * (1 << zoom)
    )


def tile_range(bounds, zoom):
    south, west, north, east = bounds
    return (
        tile_x(west, zoom),
        tile_y(north, zoom),
        tile_x(east, zoom),
        tile_y(south, zoom),
    )


def provider_name(template):
    host = urllib.parse.urlsplit(template).hostname or ""
    return host or "custom"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("outdir", help="cache directory to create")
    parser.add_argument("--bounds", required=True, help="south,west,north,east")
    parser.add_argument("--overpass", action="store_true", help="fetch vector data")
    parser.add_argument("--overpass-url", default=OVERPASS_URL)
    parser.add_argument("--tile-url", help="template with {z}/{x}/{y}")
    parser.add_argument("--zoom", type=int, default=18)
    parser.add_argument("--max-tiles", type=int, default=256)
    args = parser.parse_args(argv)

    bounds = parse_bounds(args.bounds)
    outdir = pathlib.Path(args.outdir)
    (outdir / "tiles").mkdir(parents=True, exist_ok=True)

    manifest = {
        "bounds": {
            "south": bounds[0],
            "west": bounds[1],
            "north": bounds[2],
            "east": bounds[3],
        },
        "zoom": args.zoom,
        "tiles": [],
        "vector": None,
        "attribution": [],
    }

    if args.overpass:
        try:
            payload = http_post(args.overpass_url, overpass_query(bounds))
        except (urllib.error.URLError, OSError) as error:
            print(f"overpass: {error}", file=sys.stderr)
            return 1
        geojson = overpass_to_geojson(payload)
        vector = outdir / "vector.geojson"
        vector.write_text(json.dumps(geojson, indent=1), encoding="utf-8")
        manifest["vector"] = str(vector)
        manifest["attribution"].append("© OpenStreetMap contributors, ODbL")
        print(f"vector: {len(geojson['features'])} ways -> {vector}")

    if args.tile_url:
        x_min, y_min, x_max, y_max = tile_range(bounds, args.zoom)
        count = (x_max - x_min + 1) * (y_max - y_min + 1)
        if count > args.max_tiles:
            print(
                f"tiles: {count} requested, over --max-tiles {args.max_tiles}",
                file=sys.stderr,
            )
            return 1
        fetched = 0
        for x in range(x_min, x_max + 1):
            for y in range(y_min, y_max + 1):
                target = outdir / "tiles" / f"{args.zoom}_{x}_{y}.png"
                manifest["tiles"].append(target.name)
                if target.is_file():
                    fetched += 1
                    continue
                url = args.tile_url.format(z=args.zoom, x=x, y=y)
                try:
                    target.write_bytes(http_get(url, binary=True))
                    fetched += 1
                except (urllib.error.URLError, OSError) as error:
                    print(f"tile {x}/{y}: {error}", file=sys.stderr)
        manifest["attribution"].append(provider_name(args.tile_url))
        print(f"tiles: {fetched}/{count} in {outdir / 'tiles'}")

    (outdir / "cache.json").write_text(
        json.dumps(manifest, indent=1), encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())

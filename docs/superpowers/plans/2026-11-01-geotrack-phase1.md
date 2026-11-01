# Geotrack fase 1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** un tool Qt-free `ks_geotrack` che, da una centerline geografica, scrive una cartella pista caricabile dal simulatore: quadrato di terreno delle dimensioni del tracciato, strisce di asfalto/kerb/runoff/pit lane/griglia in `.nmsh`, `baked/manifest.txt`, `data/data.json`, `scene.json`.

**Architecture:** logica geometrica in header (`GeoProjection.h`, `Centerline.h`, `Strips.h`), orchestrazione I/O in `TrackSceneCore.cpp`, CLI in `main.cpp`. Le mesh passano da `ks::engine::terrain::writeTerrainNMSH`, il JSON da `ks::engine::FileFormat/Json.cpp` compilato nel tool (stesso schema di `ks_cartool`). Nessuna rete nel binario: la cache di imagery è uno script Python separato.

**Tech Stack:** C++17, `ksengine` (terrain NMSH, AI line, Json), harness `tests/ksengine/KsTest.h`, CMake + CTest, Python 3 stdlib per il fetch.

**Spec:** `docs/superpowers/specs/2026-11-01-geotrack-generation-design.md`

## Global Constraints

- C++17, nessuna dipendenza Qt nei target geotrack (né tool né test).
- Nessuna chiamata di rete nel binario C++; l'imagery arriva solo da file in cache.
- Formati immagine ammessi: PNG (il motore non ha decoder JPEG).
- Un layer = un `.nmsh`; il manifest è una riga per mesh, solo il nome senza estensione (`NativeRenderer::loadMeshesFromManifest` aggiunge `.nmsh`).
- I layer vanno in `<trackDir>/baked/` perché `ks::sim::findBakedManifestDir` lo cerca come primo candidato.
- Il manifest deve contenere solo le chiavi che i reader esistenti consumano: `name`, `length`, `width` (`TrackDataJsonLoader`), `marshal_posts` con `x`/`y`/`z` piatti e `mesh`, `safety_car`, `pit_pads`, `garages`.
- La pista generata non deve avere `.kn5`: `SimulationLoop::loadTrack` accetta una cartella manifest-only.
- Sidecar opzionale `baked/materials.txt`, una riga per mesh separate da TAB (`mesh \t albedo \t roughness \t metalness \t normal`), con le immagini risolte in `baked/textures/` (formato di `MaterialCache.h`).
- Altezze: terreno `y=0`, runoff `0.01`, asfalto e pit lane `0.02`, kerb `0.03`, cubetti griglia `0.06` (evitano lo z-fighting a 20 km² di terreno).
- Sistema: origine nel centroide, +X est, +Z nord, R = 6378137 m, `metresPerDegLat = R*π/180`.
- Coordinate `Enu{x,z}` e non `vec3`: la Y entra solo alla fine, come parametro `y` dei layer.
- I test usano `KS_CHECK` / `KS_CHECK_NEAR` da `KsTest.h` e tornano `KS_TEST_RESULT("nome")`.
- Non toccare i file con modifiche locali non committate (`TrackLoader.cpp`, `AiFileReader.*`, `tests/ksengine/ai_line_test.cpp`, `tests/ksengine/track_loader_test.cpp`, `tools/kn5baker/main.cpp`, `tools/trackmodules/*`, `TracksidePosts.h`, i `.bak`).

## Review Focus

1. **Centerline aperta (GPX di una recinzione, giro non chiuso):** con `--closed` la giuntura fra ultimo e primo punto è un salto di decine di metri, che produce una striscia a razzo attraverso il terreno. Atteso: la chiusura usa il salto reale e la lunghezza resta quella misurata, non quella gonfiata (test in Task 2).
2. **Tracciato che ripassa vicino a sé stesso (Nordschleife):** le strisce fuori da una curva possono sovrapporsi e i triangoli si capovolgono. Atteso: normali sempre +Y anche nei tratti con curvatura alta e offset laterale grande (test in Task 3).
3. **Centerline degenerata (2 punti, punti coincidenti, passo più lungo della pista):** `buildStations` non deve produrre stazioni zero, lunghezza zero o divisioni per zero; deve fallire con errore leggibile (test in Task 2).
4. **Longitudini che attraversano ±180° e latitudini alte:** la proiezione equirettangolare non deve produrre un quadrato di lato sbagliato né un salto di 360° in longitudine (test in Task 1).
5. **Nome pista con spazi, accenti o barre:** `data.json` deve restare JSON valido e `name` deve sopravvivere al parser naive del simulatore (test in Task 4).

---

### Task 1: Proiezione geografica e quadrato del terreno

**Files:**
- Create: `tools/geotrack/GeoProjection.h`
- Create: `tests/ksengine/geotrack_geo_test.cpp`
- Modify: `tests/ksengine/CMakeLists_p1.cmake` (aggiunge `geotrack_geo_test` alla lista `KS_QT_FREE_TESTS` e `include_directories("${CMAKE_CURRENT_SOURCE_DIR}/../../tools/geotrack")` accanto all'include di `src`)

**Interfaces:**
- Consumes: niente.
- Produces:
  ```cpp
  namespace ks::geo {
  struct LatLon { double lat = 0.0, lon = 0.0; };
  struct Enu { double x = 0.0, z = 0.0; };                 // metri, +X est, +Z nord
  class Projection {
  public:
      static Projection fromOrigin(LatLon origin);
      LatLon origin() const;
      Enu toEnu(LatLon p) const;
      LatLon toLatLon(Enu p) const;
  };
  struct Square { double minX = 0, maxX = 0, minZ = 0, maxZ = 0;
                  double side() const; double centreX() const; double centreZ() const; };
  Square squareAround(const std::vector<Enu>& pts, double padMeters);
  void uvOn(const Square& sq, double x, double z, float* u, float* v);
  constexpr double kEarthRadius = 6378137.0;
  }
  ```

- [ ] **Step 1: Write the failing test** — `tests/ksengine/geotrack_geo_test.cpp`

```cpp
#include "KsTest.h"
#include "geotrack/GeoProjection.h"
using namespace ks::geo;

int main() {
    const LatLon origin{50.3355, 6.9474};          // Nordschleife
    const Projection pr = Projection::fromOrigin(origin);

    // 0.001 deg a questa latitudine: 111.319 m in lat, 111.319*cos(lat) in lon.
    KS_CHECK_NEAR(pr.toEnu({origin.lat + 0.001, origin.lon}).z, 111.319, 0.1);
    KS_CHECK_NEAR(pr.toEnu({origin.lat, origin.lon + 0.001}).x, 71.05, 0.2);

    // Andata e ritorno coerenti entro il millimetro.
    const Enu back = pr.toEnu(pr.toLatLon({1234.5, -987.25}));
    KS_CHECK_NEAR(back.x, 1234.5, 1e-3);
    KS_CHECK_NEAR(back.z, -987.25, 1e-3);

    // Quadrato: lato = estensione maggiore + 2*pad, centrato sul bbox.
    const std::vector<Enu> pts{{0, 0}, {2000, 0}, {2000, 500}, {0, 500}};
    const Square sq = squareAround(pts, 250.0);
    KS_CHECK_NEAR(sq.side(), 2500.0, 1e-9);
    KS_CHECK_NEAR(sq.centreX(), 1000.0, 1e-9);
    KS_CHECK_NEAR(sq.centreZ(), 250.0, 1e-9);

    // UV: angolo NW = (0,1), SE = (1,0).
    float u = -1, v = -1;
    uvOn(sq, sq.minX, sq.maxZ, &u, &v);
    KS_CHECK_NEAR(u, 0.0f, 1e-6); KS_CHECK_NEAR(v, 1.0f, 1e-6);
    uvOn(sq, sq.maxX, sq.minZ, &u, &v);
    KS_CHECK_NEAR(u, 1.0f, 1e-6); KS_CHECK_NEAR(v, 0.0f, 1e-6);

    // Antimeridiano: due punti a lon 179.999 e -179.999 distano ~200 m, non 40000 km.
    const Projection dateline = Projection::fromOrigin({0.0, 180.0});
    const Enu a = dateline.toEnu({0.0, 179.999});
    const Enu b = dateline.toEnu({0.0, -179.999});
    KS_CHECK_NEAR(std::fabs(a.x - b.x), 222.6, 1.0);

    return KS_TEST_RESULT("geotrack_geo_test");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -S tests/ksengine -B build_ksengine_tests -DCMAKE_BUILD_TYPE=Release && cmake --build build_ksengine_tests --target geotrack_geo_test -j`
Expected: build error, `geotrack/GeoProjection.h: No such file or directory`.

- [ ] **Step 3: Implement `tools/geotrack/GeoProjection.h`** — equirettangolare: `toEnu` usa `x = wrapDegrees(lon - origin.lon) * metresPerDegLon(origin.lat)` con `wrapDegrees` che riporta in `[-180, 180]`, `z = (lat - origin.lat) * kMetresPerDegLat`. `toLatLon` è l'inversa, con `lon` normalizzata in `(-180, 180]`. `squareAround` prende il bbox, `side = max(extentX, extentZ) + 2*pad`, e centra il quadrato sul centro del bbox. `uvOn` come nel test.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build_ksengine_tests --target geotrack_geo_test -j && ctest --test-dir build_ksengine_tests -R geotrack_geo_test --output-on-failure`
Expected: `geotrack_geo_test: OK`, 1 test passed.

- [ ] **Step 5: Commit**

```bash
git add tools/geotrack/GeoProjection.h tests/ksengine/geotrack_geo_test.cpp tests/ksengine/CMakeLists_p1.cmake
git commit -m "feat(geotrack): local ENU projection and terrain square"
```

---

### Task 2: Lettura centerline, ricampionamento, stazioni

**Files:**
- Create: `tools/geotrack/Centerline.h`
- Create: `tests/ksengine/geotrack_centerline_test.cpp`
- Modify: `tests/ksengine/CMakeLists_p1.cmake` (aggiunge `geotrack_centerline_test`)

**Interfaces:**
- Consumes: `ks::geo::LatLon`, `Enu`, `Projection`.
- Produces:
  ```cpp
  namespace ks::geo {
  struct Station { Enu p; Enu tangent; Enu left; double s = 0.0; double curvature = 0.0; };
  std::vector<LatLon> readCenterline(const std::string& path, std::string* error);
  double polylineLength(const std::vector<Enu>& pts, bool closed);
  std::vector<Enu> resample(const std::vector<Enu>& pts, double stepMeters, bool closed);
  std::vector<Station> buildStations(const std::vector<Enu>& pts, double stepMeters, bool closed);
  int countCorners(const std::vector<Station>& st, double minRadiusMetres, double mergeMetres);
  }
  ```
  `left` è la normale sinistra del verso di marcia: per tangente `(tx,tz)` vale `(-tz, tx)` (verso nord `(0,1)` → sinistra `(-1,0)` = ovest).

- [ ] **Step 1: Write the failing test** — `tests/ksengine/geotrack_centerline_test.cpp`

```cpp
#include "KsTest.h"
#include "geotrack/Centerline.h"
#include "geotrack/GeoProjection.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
using namespace ks::geo;

static void writeFile(const std::string& p, const std::string& text) {
    std::ofstream(p) << text;
}

int main() {
    const std::string dir = "geotrack_tmp";
    std::filesystem::create_directories(dir);

    // Rettangolo 1000 x 500 m, chiuso: perimetro 3000 m.
    writeFile(dir + "/rect.csv", "# lat,lon\n50.3355,6.9474\n50.3355,6.96182\n50.3400,6.96182\n50.3400,6.9474\n");
    std::string err;
    const std::vector<LatLon> csv = readCenterline(dir + "/rect.csv", &err);
    KS_CHECK(err.empty());
    KS_CHECK(csv.size() == 4);

    const Projection pr = Projection::fromOrigin(csv.front());
    std::vector<Enu> pts;
    for (const LatLon& p : csv) pts.push_back(pr.toEnu(p));
    KS_CHECK_NEAR(polylineLength(pts, true), 3000.0, 15.0);

    const std::vector<Station> st = buildStations(pts, 5.0, true);
    KS_CHECK(st.size() > 550 && st.size() < 620);      // 3000/5 stazioni
    KS_CHECK_NEAR(st.back().s, 3000.0, 15.0);
    KS_CHECK_NEAR(st[0].left.x, 0.0, 1e-6);            // verso nord-est... vedi Assert sotto

    // Tangente e normale sono unitarie e perpendicolari.
    for (const Station& s : st) {
        KS_CHECK_NEAR(std::hypot(s.tangent.x, s.tangent.z), 1.0, 1e-9);
        KS_CHECK_NEAR(s.tangent.x * s.left.x + s.tangent.z * s.left.z, 0.0, 1e-9);
    }

    // GeoJSON e GPX producono la stessa centerline.
    writeFile(dir + "/rect.geojson",
              "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\",\"properties\":{},"
              "\"geometry\":{\"type\":\"LineString\",\"coordinates\":[[6.9474,50.3355],[6.96182,50.3355],"
              "[6.96182,50.3400],[6.9474,50.3400]]}}]}");
    const std::vector<LatLon> gj = readCenterline(dir + "/rect.geojson", &err);
    KS_CHECK(err.empty());
    KS_CHECK_NEAR(gj.size(), csv.size(), 0.0);
    KS_CHECK_NEAR(gj[1].lon, csv[1].lon, 1e-9);

    writeFile(dir + "/rect.gpx",
              "<?xml version=\"1.0\"?><gpx><trk><trkseg>"
              "<trkpt lat=\"50.3355\" lon=\"6.9474\"/><trkpt lat=\"50.3355\" lon=\"6.96182\"/>"
              "<trkpt lat=\"50.3400\" lon=\"6.96182\"/><trkpt lat=\"50.3400\" lon=\"6.9474\"/>"
              "</trkseg></trk></gpx>");
    KS_CHECK(readCenterline(dir + "/rect.gpx", &err).size() == 4);

    // Degeneri: 2 punti chiusi, punti coincidenti, file inesistente.
    KS_CHECK(buildStations({{0, 0}, {100, 0}}, 5.0, true).size() >= 2);
    KS_CHECK(buildStations({{0, 0}, {0, 0}, {0, 0}}, 5.0, false).empty());
    KS_CHECK(readCenterline(dir + "/missing.csv", &err).empty());
    KS_CHECK(!err.empty());

    std::filesystem::remove_all(dir);
    return KS_TEST_RESULT("geotrack_centerline_test");
}
```

Sostituire l'asserzione marcata "vedi Assert" con la normale attesa del primo segmento, che va da `(0,0)` verso est: tangente `(1,0)`, quindi `left = (0,1)` → `KS_CHECK_NEAR(st[0].left.z, 1.0, 1e-6)`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build_ksengine_tests --target geotrack_centerline_test -j`
Expected: errore di build, `geotrack/Centerline.h` assente.

- [ ] **Step 3: Implement `tools/geotrack/Centerline.h`** — `readCenterline` sceglie il parser dall'estensione: `.csv` righe `lat,lon` con `#` come commento (virgola o punto e virgola, `strtod` avanzando manualmente sul separatore), `.geojson`/`.json` il primo array `coordinates` con coppie `[lon,lat,...]`, `.gpx` ogni `<trkpt lat=".." lon="..">`. `error` va riempito e il vettore lasciato vuoto su qualunque fallimento. `resample` cammina la polilinea a passo costante (`closed` aggiunge il segmento di chiusura) e ripete l'ultimo punto se la pista non è multipla del passo. `buildStations` calcola tangenti centrate, `left`, `s` cumulato e `curvature` come 1/raggio da tre punti consecutivi (0 sui tratti rettilinei). `countCorners` marca i punti con `curvature > 1/minRadiusMetres` e fonde le sequenze separate da meno di `mergeMetres`.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build_ksengine_tests --target geotrack_centerline_test -j && ctest --test-dir build_ksengine_tests -R geotrack_centerline_test --output-on-failure`
Expected: `geotrack_centerline_test: OK`.

- [ ] **Step 5: Commit**

```bash
git add tools/geotrack/Centerline.h tests/ksengine/geotrack_centerline_test.cpp tests/ksengine/CMakeLists_p1.cmake
git commit -m "feat(geotrack): centerline readers and station build"
```

---

### Task 3: Generazione mesh — quadrato e strisce

**Files:**
- Create: `tools/geotrack/Strips.h`
- Create: `tests/ksengine/geotrack_strips_test.cpp`
- Modify: `tests/ksengine/CMakeLists_p1.cmake` (aggiunge `geotrack_strips_test`)

**Interfaces:**
- Consumes: `ks::geo::Station`, `Square`; `ks::engine::terrain::TerrainMeshData` / `TerrainVertex`.
- Produces:
  ```cpp
  namespace ks::geo {
  struct StripSpec { double offset = 0.0; double halfWidth = 6.0; double y = 0.0;
                     float uvRepeatMetres = 10.0f; };
  ks::engine::terrain::TerrainMeshData groundQuad(const Square& sq, double y, float uvRepeatMetres);
  ks::engine::terrain::TerrainMeshData ribbon(const std::vector<Station>& st, const StripSpec& spec);
  ks::engine::terrain::TerrainMeshData gridSlots(const std::vector<Station>& st, double sStart,
                                                 int count, double spacing, double lateral,
                                                 double boxWidth, double boxLength, double y);
  }
  ```

- [ ] **Step 1: Write the failing test** — `tests/ksengine/geotrack_strips_test.cpp`

```cpp
#include "KsTest.h"
#include "geotrack/Strips.h"
#include <cmath>
using namespace ks::geo;
using ks::engine::terrain::TerrainMeshData;

static void checkMesh(const TerrainMeshData& m, const char* what) {
    KS_CHECK(!m.vertices.empty());
    KS_CHECK(m.indices.size() % 3 == 0);
    for (std::uint32_t i : m.indices) KS_CHECK(i < m.vertices.size());
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const auto& a = m.vertices[m.indices[t]];
        const auto& b = m.vertices[m.indices[t + 1]];
        const auto& c = m.vertices[m.indices[t + 2]];
        const float ux = b.px - a.px, uy = b.py - a.py, uz = b.pz - a.pz;
        const float vx = c.px - a.px, vy = c.py - a.py, vz = c.pz - a.pz;
        const float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        KS_CHECK(nx * nx + ny * ny + nz * nz > 1e-12f);            // non degenere
        KS_CHECK(ny > 0.0f);                                      // normale verso l'alto
    }
    (void)what;
}

int main() {
    const Square sq{0, 2500, 0, 2500};
    const TerrainMeshData g = groundQuad(sq, 0.0, 500.0f);
    checkMesh(g, "ground");
    KS_CHECK(g.vertices.size() == 4);
    KS_CHECK_NEAR(g.vertices[0].py, 0.0f, 1e-6);

    // Rettilineo di 200 m verso nord, 10 stazioni ogni 5 m oltre la prima.
    std::vector<Station> st;
    for (int i = 0; i <= 40; ++i) {
        Station s;
        s.p = {0.0, i * 5.0};
        s.tangent = {0.0, 1.0};
        s.left = {-1.0, 0.0};
        s.s = i * 5.0;
        st.push_back(s);
    }
    const TerrainMeshData r = ribbon(st, {0.0, 6.0, 0.02, 10.0f});
    checkMesh(r, "track");
    KS_CHECK(r.vertices.size() == 2 * st.size());
    // Con offset 0 e mezza larghezza 6: 12 m fra i due bordi della prima coppia.
    KS_CHECK_NEAR(std::fabs(r.vertices[0].px - r.vertices[1].px), 12.0, 1e-4);
    KS_CHECK_NEAR(r.vertices[0].py, 0.02f, 1e-6);

    // Offset laterale: la striscia si sposta di `offset` sul lato sinistro.
    const TerrainMeshData off = ribbon(st, {8.0, 3.0, 0.02, 10.0f});
    KS_CHECK_NEAR(off.vertices[0].px, -11.0, 1e-4);   // 8 + 3 verso sinistra (-X)
    KS_CHECK_NEAR(off.vertices[1].px, -5.0, 1e-4);

    // Curvatura forte: normali ancora verso l'alto.
    std::vector<Station> loop;
    for (int i = 0; i < 72; ++i) {
        const double a = i * 5.0 / 60.0;
        Station s;
        s.p = {60.0 * std::cos(a), 60.0 * std::sin(a)};
        s.tangent = {-std::sin(a), std::cos(a)};
        s.left = {-s.tangent.z, s.tangent.x};
        s.s = i * 5.0;
        loop.push_back(s);
    }
    checkMesh(ribbon(loop, {0.0, 6.0, 0.02, 10.0f}), "loop");

    // Griglia: 30 slot sfalsati, mesh non vuota e sopra l'asfalto.
    const TerrainMeshData slots = gridSlots(st, 20.0, 30, 8.0, 0.0, 1.2, 4.0, 0.06);
    checkMesh(slots, "grid");
    KS_CHECK(slots.vertices.size() >= 4 * 30);

    return KS_TEST_RESULT("geotrack_strips_test");
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build_ksengine_tests --target geotrack_strips_test -j`
Expected: `geotrack/Strips.h` assente.

- [ ] **Step 3: Implement `tools/geotrack/Strips.h`** — `groundQuad` emette 4 vertici con UV da `uvOn`/`uvRepeatMetres`; `ribbon` emette per ogni stazione due vertici `p + left*(offset ± halfWidth)` e due triangoli per segmento con l'ordine **(L_i, L_{i+1}, R_i)** e **(L_{i+1}, R_{i+1}, R_i)**, che dà normale +Y con +X est / +Z nord; `v` di UV = `s / uvRepeatMetres`, `u` = 0 sul bordo sinistro e 1 sul destro. `gridSlots` posiziona `count` rettangoli ruotati secondo la tangente, con sfalsamento laterale alternato di ±0.6 m, partendo da `s = sStart` a passo `spacing`.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build_ksengine_tests --target geotrack_strips_test -j && ctest --test-dir build_ksengine_tests -R geotrack_strips_test --output-on-failure`
Expected: `geotrack_strips_test: OK`.

- [ ] **Step 5: Commit**

```bash
git add tools/geotrack/Strips.h tests/ksengine/geotrack_strips_test.cpp tests/ksengine/CMakeLists_p1.cmake
git commit -m "feat(geotrack): ground quad and ribbon meshes"
```

---

### Task 4: Scrittura della cartella pista (ground + tracciato + manifest + data.json)

**Files:**
- Create: `tools/geotrack/TrackSceneCore.h`, `tools/geotrack/TrackSceneCore.cpp`
- Create: `tests/ksengine/geotrack_generate_test.cpp`
- Modify: `tests/ksengine/CMakeLists_p3.cmake` (target con due sorgenti)

**Interfaces:**
- Consumes: tutto quanto sopra; `ks::engine::terrain::writeTerrainNMSH`; `ks::Json` (compilato da `src/engine/FileFormat/Json.cpp`).
- Produces:
  ```cpp
  namespace ks::geo {
  struct PitConfig { bool enabled = false; double fromS = 0, toS = 0, offset = 8.0, halfWidth = 5.0; };
  struct GeotrackConfig {
      std::string centerlinePath, outDir, trackName;
      double widthM = 12.0, padM = 250.0, stepM = 5.0;
      bool closed = true;
      PitConfig pit;
  };
  struct GeotrackResult { double lengthM = 0; double sideM = 0; int corners = 0;
                          int marshalPosts = 0; int lapPoints = 0; };
  bool generateTrack(const GeotrackConfig& cfg, GeotrackResult* out, std::string* error);
  }
  ```

- [ ] **Step 1: Write the failing test** — `tests/ksengine/geotrack_generate_test.cpp` (include `adapters/assetto_corsa/TrackDataJsonLoader.h` per verificare il manifest con il parser vero del simulatore)

```cpp
#include "KsTest.h"
#include "geotrack/TrackSceneCore.h"
#include "adapters/assetto_corsa/TrackDataJsonLoader.h"
#include <filesystem>
#include <fstream>
#include <sstream>
namespace fs = std::filesystem;

static std::string slurp(const std::string& p) {
    std::ifstream in(p);
    std::stringstream ss; ss << in.rdbuf(); return ss.str();
}

int main() {
    const std::string dir = "geotrack_gen_tmp";
    fs::remove_all(dir);
    fs::create_directories(dir);
    { std::ofstream(dir + "/loop.csv")
          << "50.3355,6.9474\n50.3355,6.96182\n50.3400,6.96182\n50.3400,6.9474\n"; }

    ks::geo::GeotrackConfig cfg;
    cfg.centerlinePath = dir + "/loop.csv";
    cfg.outDir = dir + "/My Track è";
    cfg.trackName = "My Track è";
    ks::geo::GeotrackResult res;
    std::string err;
    KS_CHECK(ks::geo::generateTrack(cfg, &res, &err));
    KS_CHECK(err.empty());

    KS_CHECK_NEAR(res.lengthM, 3000.0, 15.0);
    KS_CHECK(res.sideM > 2400.0);
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/baked/manifest.txt"));
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/baked/ground.nmsh"));
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/baked/track.nmsh"));
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/scene.json"));
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/data/data.json"));

    // Il parser naive del simulatore deve leggere nome, lunghezza e larghezza.
    ks::ac::TrackDataJsonLoader loader;
    KS_CHECK(loader.loadForTrackDir(cfg.outDir));
    KS_CHECK(loader.meta().name == "My Track è");
    KS_CHECK_NEAR(loader.meta().lengthM, res.lengthM, 1.0);
    KS_CHECK_NEAR(loader.meta().widthM, 12.0, 1e-3);

    // Manifest: una riga per mesh, senza estensione, e ogni .nmsh esiste.
    std::ifstream man(cfg.outDir + "/baked/manifest.txt");
    std::string line; int lines = 0;
    while (std::getline(man, line)) {
        if (line.empty()) continue;
        ++lines;
        KS_CHECK(line.find('.') == std::string::npos);
        KS_CHECK(fs::is_regular_file(cfg.outDir + "/baked/" + line + ".nmsh"));
    }
    KS_CHECK(lines >= 2);

    // scene.json dichiara bounds, lato del quadrato e i layer.
    const std::string scene = slurp(cfg.outDir + "/scene.json");
    KS_CHECK(scene.find("\"layers\"") != std::string::npos);
    KS_CHECK(scene.find("\"side_m\"") != std::string::npos);

    fs::remove_all(dir);
    return KS_TEST_RESULT("geotrack_generate_test");
}
```

- [ ] **Step 2: Register the multi-source target** — in `tests/ksengine/CMakeLists_p3.cmake`, stesso stile dei target esistenti:

```cmake
add_executable(geotrack_generate_test
    geotrack_generate_test.cpp
    ../../../tools/geotrack/TrackSceneCore.cpp
    ../../../src/engine/FileFormat/Json.cpp)
target_include_directories(geotrack_generate_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/geotrack")
target_link_libraries(geotrack_generate_test PRIVATE ksengine)
add_test(NAME geotrack_generate_test COMMAND geotrack_generate_test)
```

- [ ] **Step 3: Run to verify it fails**

Run: `cmake -S tests/ksengine -B build_ksengine_tests -DCMAKE_BUILD_TYPE=Release && cmake --build build_ksengine_tests --target geotrack_generate_test -j`
Expected: `geotrack/TrackSceneCore.h` assente.

- [ ] **Step 4: Implement `generateTrack`** — pipeline: `readCenterline` → `Projection::fromOrigin` sul centroide → `toEnu` → `buildStations(stepM, closed)` → `squareAround(padM)` → scrive `ground.nmsh` (`groundQuad`) e `track.nmsh` (`ribbon` con `halfWidth = widthM/2`) → `baked/manifest.txt` con `ground` e `track`, una riga ciascuno → `data/data.json` con `Json` (`name`, `short_name` = nome senza spazi, `length`/`length_m`, `width`/`width_m`, `pitboxes`, `run = 1`, `country`, `city`, `description`, `surfaces`, `pit_pads` = 0, `car_slots` = 0, `garages` = 0, `corner_count`, `marshal_posts` = array vuoto, `safety_car` = oggetto vuoto) → `scene.json` con `bounds`, `square {side_m, min_x, min_z}`, `layers`, `tile {}`. Errori: cartella centerline illeggibile, meno di 4 punti, lunghezza < 100 m, `widthM` non in (3, 40) → `false` con messaggio e nessun file scritto oltre la cartella creata.

- [ ] **Step 5: Run to verify it passes**

Run: `cmake --build build_ksengine_tests --target geotrack_generate_test -j && ctest --test-dir build_ksengine_tests -R geotrack_generate_test --output-on-failure`
Expected: `geotrack_generate_test: OK`.

- [ ] **Step 6: Commit**

```bash
git add tools/geotrack/TrackSceneCore.h tools/geotrack/TrackSceneCore.cpp \
        tests/ksengine/geotrack_generate_test.cpp tests/ksengine/CMakeLists_p3.cmake
git commit -m "feat(geotrack): write loadable track folder from a centerline"
```

---

### Task 5: Pit lane e griglia di partenza

**Files:**
- Create: `tools/geotrack/PitLane.h`
- Modify: `tools/geotrack/TrackSceneCore.cpp` (layer `pitlane`, `griddot`, chiavi manifest)
- Modify: `tests/ksengine/geotrack_generate_test.cpp` (assert su pit lane e griglia)

**Interfaces:**
- Consumes: `Station`, `ribbon`, `gridSlots`, `PitConfig`.
- Produces:
  ```cpp
  namespace ks::geo {
  std::vector<Station> clipStations(const std::vector<Station>& st, double fromS, double toS);
  std::vector<Station> rampStations(const std::vector<Station>& st, double sStart,
                                    double sEnd, double offset, double rampMetres);
  }
  ```

- [ ] **Step 1: Extend the failing test** — in `geotrack_generate_test.cpp`, dopo il blocco esistente, una seconda cartella con la pit lane attiva:

```cpp
    ks::geo::GeotrackConfig pitCfg = cfg;
    pitCfg.outDir = dir + "/pit";
    pitCfg.trackName = "Pit Track";
    pitCfg.pit.enabled = true;
    pitCfg.pit.fromS = 200.0;
    pitCfg.pit.toS = 600.0;
    pitCfg.pit.offset = 10.0;
    pitCfg.pit.halfWidth = 4.0;
    pitCfg.pit.rampMetres = 60.0;
    ks::geo::GeotrackResult pitRes;
    KS_CHECK(ks::geo::generateTrack(pitCfg, &pitRes, &err));
    KS_CHECK(fs::is_regular_file(pitCfg.outDir + "/baked/pitlane.nmsh"));
    KS_CHECK(fs::is_regular_file(pitCfg.outDir + "/baked/griddot.nmsh"));
    const std::string pitMan = slurp(pitCfg.outDir + "/baked/manifest.txt");
    KS_CHECK(pitMan.find("pitlane") != std::string::npos);
    KS_CHECK(pitMan.find("griddot") != std::string::npos);
    KS_CHECK(pitRes.gridSlots >= 10);
```

Aggiungere `double rampMetres = 60.0;` a `PitConfig` e `int gridSlots = 0;` a `GeotrackResult` (Interfaces del Task 4 aggiornate di conseguenza).

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build_ksengine_tests --target geotrack_generate_test -j && ctest --test-dir build_ksengine_tests -R geotrack_generate_test --output-on-failure`
Expected: FAIL su `pitlane.nmsh` inesistente (o errore di compilazione su `rampMetres`).

- [ ] **Step 3: Implement** — `clipStations` taglia le stazioni in `[fromS, toS]` interpolando gli estremi; `rampStations` costruisce tratti a offset costante con raccordo lineare di `rampMetres` a entrambi i capi (offset 0 → `offset`) così la corsia si stacca dal tracciato senza scalino. In `generateTrack`: pit lane se `pit.enabled` (errore se `toS <= fromS` o `offset < widthM/2`), griglia con `gridSlots(st, 0, N, 8.0, 0, 1.2, 4.0, 0.06)` dove `N = min(24, max(8, (int)((pit.fromS - 20.0) / 8.0)))`, e `pit_pads`/`car_slots` in `data.json` = `N`.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build_ksengine_tests --target geotrack_generate_test -j && ctest --test-dir build_ksengine_tests -R geotrack_generate_test --output-on-failure`
Expected: `geotrack_generate_test: OK`.

- [ ] **Step 5: Commit**

```bash
git add tools/geotrack/PitLane.h tools/geotrack/TrackSceneCore.cpp tests/ksengine/geotrack_generate_test.cpp
git commit -m "feat(geotrack): pit lane with ramps and starting grid"
```

---

### Task 6: Vie di fuga, kerb, commissari e piazzola safety car

**Files:**
- Create: `tools/geotrack/Scenery.h`
- Modify: `tools/geotrack/TrackSceneCore.cpp` (layer `runoff`, `kerbs`; oggetti `marshal_posts` e `safety_car` in `data.json`; `scene.json` scenario)
- Modify: `tests/ksengine/geotrack_generate_test.cpp` (assert su `marshal_posts`, safety car, layer)

**Interfaces:**
- Consumes: `Station`, `ribbon`, `countCorners`, `widthM`.
- Produces:
  ```cpp
  namespace ks::geo {
  struct MarshalPost { int corner = 0; double s = 0, lateral = 0, y = 0.0; };
  std::vector<MarshalPost> marshalPosts(const std::vector<Station>& st, double halfWidth,
                                        double minRadiusMetres, double mergeMetres);
  struct SafetyCarBay { double s = 0, lateral = 0, halfWidth = 10.0, halfLength = 20.0; };
  SafetyCarBay safetyCarBay(const std::vector<Station>& st, double halfWidth);
  }
  ```

- [ ] **Step 1: Extend the failing test** — nel blocco della pista base, dopo i controlli esistenti:

```cpp
    KS_CHECK(res.corners >= 3);                 // rettangolo: 4 curve
    KS_CHECK(res.marshalPosts >= 12);           // 4 per curva
    const std::string dj = slurp(cfg.outDir + "/data/data.json");
    KS_CHECK(dj.find("\"marshal_posts\"") != std::string::npos);
    KS_CHECK(dj.find("\"mesh\": \"marshall_A_HI_010\"") != std::string::npos);
    KS_CHECK(dj.find("\"safety_car\"") != std::string::npos);
    KS_CHECK(dj.find("\"corner_count\"") != std::string::npos);
    KS_CHECK(slurp(cfg.outDir + "/baked/manifest.txt").find("runoff") != std::string::npos);
    KS_CHECK(slurp(cfg.outDir + "/baked/manifest.txt").find("kerbs") != std::string::npos);
```

E, con `tests/ksengine/TracksidePosts.h` incluso, la verifica col parser vero:

```cpp
    ks::sim::TracksidePosts posts;
    KS_CHECK(posts.loadFromFile(cfg.outDir + "/data/data.json"));
    KS_CHECK(posts.postCount() == static_cast<std::size_t>(res.marshalPosts));
    KS_CHECK(posts.posts().front().mesh == "marshall_A_HI_010");
    KS_CHECK(posts.hasSafetyCarPad());
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build_ksengine_tests --target geotrack_generate_test -j && ctest --test-dir build_ksengine_tests -R geotrack_generate_test --output-on-failure`
Expected: FAIL su `res.corners >= 3`.

- [ ] **Step 3: Implement** — `marshalPosts` riusa `countCorners` per numerare le curve e piazza 4 posti per curva a `s = sCurva + {-12, -4, 4, 12}` a `lateral = halfWidth + 8` sul lato esterno della curva (segno della curvatura). `safetyCarBay` prende la stazione più vicina a metà giro su un lato rettilineo (`|curvature| < 1/300`), a `lateral = halfWidth + 12`. In `generateTrack`: `runoff.nmsh` = due strisce ai lati (offset `±(halfWidth + 12)`, mezza larghezza 12, `y = 0.01`), `kerbs.nmsh` = due strisce ai bordi (offset `±(halfWidth + 0.5)`, mezza larghezza 0.5, `y = 0.03`), `data.json` con `marshal_posts` (array di `{x,y,z,mesh}`), `safety_car` (`{x,y,z,valid:true,rx,rz}`) e `corner_count`; `scene.json` riceve `"scenery": {"marshal_mesh": "marshall_A_HI_010", "marshal_asset": "objects3d/marshal"}`.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build_ksengine_tests --target geotrack_generate_test -j && ctest --test-dir build_ksengine_tests -R geotrack_generate_test --output-on-failure`
Expected: `geotrack_generate_test: OK`.

- [ ] **Step 5: Commit**

```bash
git add tools/geotrack/Scenery.h tools/geotrack/TrackSceneCore.cpp tests/ksengine/geotrack_generate_test.cpp
git commit -m "feat(geotrack): runoff, kerbs, marshal posts and safety car bay"
```

---

### Task 7: Linea AI di bootstrap

**Files:**
- Modify: `tools/geotrack/TrackSceneCore.cpp` (scrive `ai/fast_lane.ai`)
- Modify: `tests/ksengine/geotrack_generate_test.cpp` (assert con `AiFileReader`)

**Interfaces:**
- Consumes: `ks::ai::AiFileWriter::writeSpline`, `ks::ai::AiSpline` / `AiSplinePoint` / `AiVec3` (`src/engine/AI/AiFileReader.h`), `Station::curvature`.
- Produces: `ai/fast_lane.ai` con un punto ogni `stepM`, `curvature` dalla stazione, `speed` = `min(80, max(25, sqrt(60 / max(|curvature|, 1e-4))))`, `y = 0.02`.

- [ ] **Step 1: Add the failing assertion**

```cpp
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/ai/fast_lane.ai"));
    const ks::ai::AiSpline ai = ks::ai::AiFileReader::readSpline(cfg.outDir + "/ai/fast_lane.ai");
    KS_CHECK(ai.isValid());
    KS_CHECK(ai.size() == static_cast<std::size_t>(res.lapPoints));
    KS_CHECK(ai.points[0].speed >= 20.0f && ai.points[0].speed <= 90.0f);
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build_ksengine_tests --target geotrack_generate_test -j && ctest --test-dir build_ksengine_tests -R geotrack_generate_test --output-on-failure`
Expected: FAIL su `ai/fast_lane.ai` inesistente.

- [ ] **Step 3: Implement** — `res.lapPoints = st.size()`; costruisce `AiSpline` (una stazione su `stepM`, `closed = cfg.closed`) e chiama `AiFileWriter::writeSpline`; errore se `writeSpline` ritorna `false`.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build_ksengine_tests --target geotrack_generate_test -j && ctest --test-dir build_ksengine_tests -R geotrack_generate_test --output-on-failure`
Expected: `geotrack_generate_test: OK`.

- [ ] **Step 5: Commit**

```bash
git add tools/geotrack/TrackSceneCore.cpp tests/ksengine/geotrack_generate_test.cpp
git commit -m "feat(geotrack): bootstrap AI line from the centerline"
```

---

### Task 8: Materiali dei layer e fondo texturizzato

**Files:**
- Create: `tools/geotrack/Materials.h`
- Modify: `tools/geotrack/TrackSceneCore.cpp` (scrive `baked/materials.txt`, copia la texture del fondo)
- Modify: `tests/ksengine/geotrack_generate_test.cpp`

**Interfaces:**
- Consumes: `GeotrackConfig` (nuovo campo `std::string groundTexturePath;`).
- Produces:
  ```cpp
  namespace ks::geo {
  struct LayerMaterial { std::string mesh, albedo, roughness = "0.9", metalness = "0.0"; };
  std::string materialsTxt(const std::vector<LayerMaterial>& rows);   // TSV, newline finale
  }
  ```

- [ ] **Step 1: Add the failing assertions** — in `geotrack_generate_test.cpp`, dopo i controlli base:

```cpp
    // Senza texture del fondo: nessun materials.txt, i layer restano ai default.
    KS_CHECK(!fs::is_regular_file(cfg.outDir + "/baked/materials.txt"));

    // Con una texture del fondo: riga materials.txt + copia in baked/textures.
    { std::ofstream(cfg.outDir + "/ground_src.png", std::ios::binary) << "\x89PNG\r\n"; }
    ks::geo::GeotrackConfig texCfg = cfg;
    texCfg.outDir = dir + "/tex";
    texCfg.groundTexturePath = cfg.outDir + "/ground_src.png";
    KS_CHECK(ks::geo::generateTrack(texCfg, &pitRes, &err));
    KS_CHECK(fs::is_regular_file(texCfg.outDir + "/baked/textures/ground.png"));
    const std::string mats = slurp(texCfg.outDir + "/baked/materials.txt");
    KS_CHECK(mats.find("ground\tground.png") == 0);
    KS_CHECK(mats.find("track\t") != std::string::npos);
    KS_CHECK(slurp(texCfg.outDir + "/scene.json").find("\"uv_repeat_m\"") != std::string::npos);
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build_ksengine_tests --target geotrack_generate_test -j && ctest --test-dir build_ksengine_tests -R geotrack_generate_test --output-on-failure`
Expected: errore di compilazione su `groundTexturePath`.

- [ ] **Step 3: Implement** — `materialsTxt` costruisce le righe `mesh \t albedo \t roughness \t metalness` con albedo vuoto (`-`) per i layer non texturizzati. In `generateTrack`: se `groundTexturePath` è vuoto non scrive nulla; se è valorizzato copia il file in `baked/textures/ground.png` (`fs::copy_file`, errore se manca) e scrive righe per `ground` (albedo `ground.png`, roughness 0.95), `track` 0.85, `pitlane` 0.85, `runoff` 0.95, `kerbs` 0.7, `griddot` 0.8. `scene.json` riceve `"uv_repeat_m": side` così l'immagine copre il quadrato una volta.

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build build_ksengine_tests --target geotrack_generate_test -j && ctest --test-dir build_ksengine_tests -R geotrack_generate_test --output-on-failure`
Expected: `geotrack_generate_test: OK`.

- [ ] **Step 5: Commit**

```bash
git add tools/geotrack/Materials.h tools/geotrack/TrackSceneCore.cpp tests/ksengine/geotrack_generate_test.cpp
git commit -m "feat(geotrack): layer materials and ground texture hook"
```

---

### Task 9: CLI, target CMake, script di cache

**Files:**
- Create: `tools/geotrack/main.cpp`
- Create: `tools/geotrack/fetch_cache.py`
- Create: `tools/geotrack/README.md`
- Modify: `CMakeLists.txt` (accanto a `ks_trackmodules`, che è lo stesso schema)

**Interfaces:**
- Consumes: `ks::geo::generateTrack`, `GeotrackConfig`, `GeotrackResult`.
- Produces: eseguibile `ks_geotrack`; `fetch_cache.py <outdir> --bounds minLat,minLon,maxLat,maxLon --overpass-pass OSM|FOOT --zoom 18` che scrive `<outdir>/vector.geojson`, `<outdir>/tiles/{z}_{x}_{y}.png` e `<outdir>/cache.json`.

- [ ] **Step 1: Add the tool target** — in `CMakeLists.txt`, dopo il blocco `ks_trackmodules`:

```cmake
	# Geotrack (fase 1): satellite + vettoriale -> cartella pista giocabile.
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/geotrack/main.cpp")
		add_executable(ks_geotrack
			tools/geotrack/main.cpp
			tools/geotrack/TrackSceneCore.cpp
			src/engine/FileFormat/Json.cpp)
		target_include_directories(ks_geotrack PRIVATE
			"${CMAKE_CURRENT_SOURCE_DIR}/tools/geotrack")
		target_link_libraries(ks_geotrack PRIVATE ksengine)
		set_target_properties(ks_geotrack PROPERTIES
			RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
	endif()
```

- [ ] **Step 2: Implement the CLI** — `ks_geotrack <centerline> <outDir> [--name N] [--width M] [--pad M] [--step M] [--open|--closed] [--pit from,to,offset,halfWidth] [--ground-texture png]`; stampa a fine lavoro `length`, `side`, `corners`, `marshal posts`, `lap points` e `0` su successo, altrimenti messaggio su `stderr` e `1`.

- [ ] **Step 3: Verify the tool builds and produces a track**

Run: `cmake -S . -B build_geotrack -DKSIMULATOR_QT_FREE=ON && cmake --build build_geotrack --target ks_geotrack -j` (se la configurazione Qt-free della repo non parte per il `SimulationLoop.h` troncato su `master`, verificare con `g++ -std=c++17 -O2 -Isrc/engine -Itools/geotrack -o build_geotrack/ks_geotrack tools/geotrack/main.cpp tools/geotrack/TrackSceneCore.cpp src/engine/FileFormat/Json.cpp src/engine/terrain/TerrainMesh.cpp src/engine/AI/AiFileWriter.cpp`).
Run: `./build_geotrack/bin/ks_geotrack /tmp/loop.csv /tmp/track --name "Demo" --width 12 --pit 200,600,10,4`
Expected: righe `length=...`, `side=...` e `0`; `/tmp/track/baked/manifest.txt` con sei righe.

- [ ] **Step 4: Implement `fetch_cache.py`** — solo `urllib.request` e stdlib: Overpass QL per `way["highway"~"^(raceway|track)$"]` e `way["highway"="service"]` filtrato per `motor_vehicle` nel raggio dei bounds, output GeoJSON; tile da un URL template passato con `--tile-url` (nessun provider cablato); converte ogni tile in PNG con `--png-only` e salta i file già in cache; `cache.json` con bounds, zoom, elenco tile e attribuzione. `python3 -m py_compile tools/geotrack/fetch_cache.py` deve passare senza rete.

- [ ] **Step 5: README** — uso dei due comandi, struttura della cartella di uscita, e la nota che senza `--tile-url` la fase 1 produce la pista con il solo fondo neutro.

- [ ] **Step 6: Commit**

```bash
git add tools/geotrack/main.cpp tools/geotrack/fetch_cache.py tools/geotrack/README.md CMakeLists.txt
git commit -m "feat(geotrack): CLI, build target and imagery cache fetcher"
```

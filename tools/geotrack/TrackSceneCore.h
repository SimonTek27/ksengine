#pragma once
// Geotrack fase 1 — orchestrazione: da una centerline geografica a una cartella
// pista che il simulatore carica (manifest in baked/, data/data.json, scene.json).
//
// Il tool e' Qt-free e deterministico: nessuna rete, nessuna GPU. Le mesh
// escono in NMSH (formato di ks::sim::NativeRenderer::loadMeshFromFile), i
// manifest in JSON con la stessa libreria del resto della pipeline.

#include <string>

namespace ks::geo {

/// Corsia box: una finestra di ascissa curvilinea parallela al tracciato.
struct PitConfig {
    bool enabled = false;
    double fromS = 0.0;     ///< inizio corsia (m di ascissa)
    double toS = 0.0;       ///< fine corsia
    double offset = 8.0;    ///< metri verso sinistra dal centro del tracciato
    double halfWidth = 5.0; ///< mezza larghezza della corsia
    double rampMetres = 60.0; ///< raccordo a entrambi i capi
};

struct GeotrackConfig {
    std::string centerlinePath; ///< .csv lat,lon / .geojson / .gpx
    std::string outDir;         ///< cartella pista da creare
    std::string trackName;      ///< nome pista; vuoto = nome del file
    double widthM = 12.0;       ///< larghezza del tracciato
    double padM = 250.0;        ///< margine del quadrato di terreno per lato
    double stepM = 5.0;         ///< passo delle stazioni
    bool closed = true;         ///< giro chiuso
    PitConfig pit;
    std::string groundTexturePath; ///< PNG del fondo; vuoto = nessun materials.txt
    std::string orthoPath;         ///< immagine georeferenziata sul quadrato (fase 2)
    std::string tileCachePath;     ///< cache.json del mosaico di tile (fase 2)
    double chunkSizeM = 1024.0;    ///< lato dei chunk del fondo quando ci sono tile
    double chunkViewM = 1400.0;    ///< finestra LOD dei chunk (0 = nessun limite)
    bool keepWidth = false;        ///< non sostituire widthM con la misura
    bool correctCenterline = false; ///< riallinea la centerline sullo skeleton dell'asfalto
    double correctSearchM = 6.0;    ///< raggio di ricerca dello snap
};

struct GeotrackResult {
    double lengthM = 0.0;
    double sideM = 0.0;
    int corners = 0;
    int marshalPosts = 0;
    int lapPoints = 0;
    int gridSlots = 0;
    double measuredWidthM = 0.0; ///< 0 = nessuna immagine letta
    int widthSamples = 0;
    double centerlineShiftM = 0.0; ///< spostamento medio della centerline corretta
    int groundChunks = 0; ///< 0 = fondo unico non texturizzato
};

/// Genera la cartella pista. `out` e `error` possono essere nulli.
/// @return false (con `error` valorizzato) se la centerline e' illeggibile,
///         troppo corta o se un layer non si scrive.
bool generateTrack(const GeotrackConfig& cfg, GeotrackResult* out, std::string* error);

} // namespace ks::geo

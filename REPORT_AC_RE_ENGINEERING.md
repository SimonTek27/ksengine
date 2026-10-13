# Reverse engineering AC — acs.exe / acShowroom.exe / Content Manager.exe (REA + Ghidra)

**Data:** 2026-10-13
**Strumento:** REA 6.4.0 (provider Ghidra 12.1.4, JDK 21) — sessioni MCP stdio
(`rea --mcp`) orchestrate dal driver `%TEMP%\opencode\rea\mcp_driver.mjs`.
**Obiettivo:** evidenze verificabili da acs.exe / acShowroom.exe / Content
Manager.exe da tradurre in item di parity per ksengine/kssim.

Binari analizzati:

| Binario | SHA-256 | Formato |
|---|---|---|
| `acs.exe` (Assetto Corsa) | `0df569c840f8303f7018f7891085e3a4c22cf93fb19327c6a0b85325cea23fd1` | PE x86-64, 32990 procedure, 115450 stringhe, 510 classi RTTI |
| `acShowroom.exe` | — | PE x86, 7456 procedure, 108356 stringhe, 66 classi RTTI |
| `Content Manager.exe` | `8586a90598582f2a0d900888019ecc9e8338e5e92f7c4a7d8fbabc7ea1ac93fd` | PE32 CLI .NET Framework (IL-only, 32-bit-required), 2074 tipi / 14824 metodi |

Ogni affermazione di sotto è un'osservazione dal binario (indirizzo
Ghidra a supporto). Le deduzioni sono marcate.

---

## 1 — acs.exe: shared memory (`acpmf_*`)

### 1.1 Le tre pagine pubbliche

`SharedMemoryWriter::SharedMemoryWriter` = `FUN_140185430`
(stringa `"SharedMemoryWriter"` @ `0x1404d5fc0`, vtable `SharedMemoryWriter::vftable`):

- Crea le mapping in ordine **static (slot 0) → graphics (slot 1) → physics (slot 2)**
  via `FUN_140185f70(this, slot, name)`, con nomi `L"acpmf_static"`,
  `L"acpmf_graphics"`, `L"acpmf_physics"`.
- Si sottoscrive a **`OnPhysicsStepCompleted`** (lambda
  `<lambda_6e6ef8fabd1bd688658d1d84b630a61d>`, registrata sul core a
  `*(this+0xb)+0x1b8+0x2d0`) e a **`OnNewCarLoadedEvent`**. La pagina physics
  è quindi scritta **al completamento di ogni step fisico**, non a tick di render.
- Legge `cfg/race.ini`: se esiste `SESSION_0` e in `SESSION_0/SPAWN_SET` compare
  `HOTLAP_START`, imposta il flag `*(this+0x21c)=1` (hotlap mode).
- In caso di errore stampa `"ERROR: SharedMemoryWriter :: ERROR initializing shared memory"`.

`FUN_140185f70` (slot mapper) — **fatto chiave**:

```text
CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0x250, L"Local\\"+name)
MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0x250)
```

**Tutte e tre le pagine `acpmf_*` sono create da 0x250 = 592 byte**, size fissa
uguale per slot 0/1/2. In caso di fallimento: `MessageBoxA("AssettoCorsa", …)`.

Confronto con `src/adapters/assetto_corsa/AcSharedMemory.h` (misurate con MSVC,
`#pragma pack(4)`): `AcPhysicsPage=580`, `AcGraphicsPage=296`, `AcStaticPage=676`.

**Ricaduta ksengine (P1 — FATTO):** per essere intercambiabile al byte con AC
(overlay che mappano la size nota della mapping), `AcSharedMemoryPublisher`
crea ora le tre mapping a `max(sizeof(page), 0x250)` tramite
`acShmMappingSize()` — physics (580 B) e graphics (296 B) sono paddate a
**592 B fissi** come AC, static (676 B) resta alla sua size piena: un lettore
che mappa 592 B espliciti riesce su tutte e tre. Verificato da
`ac_shared_memory_publisher_test` (riapre le mapping per nome e mappa 0x250 B
espliciti, come un client AC).

### 1.2 Quarta mapping: `Local\acpmf`

`SharedFileOut::SharedFileOut` = `FUN_14018e7d0` (stringa `"SHAREDFILE_OUTPUT"`):

```text
name = L"Local\\ac" + L"pmf"  →  "Local\acpmf"
CreateFileMappingW(..., PAGE_READWRITE, 0, 0x19c, name)   // 0x19c = 412 B
```

Mapping separata dalle tre pagine pubbliche, 412 byte, probabilmente usata da
plugin/output interni. ksengine non la espone.

---

## 2 — acs.exe: remote telemetry UDP (porta 9996)

Classe `RemoteTelemetryUDP` (RTTI `.?AVRemoteTelemetryUDP@@`, stringa `L"RT_UDP"`).
Costruttore `FUN_1401468e0`:

- `socket(AF_INET, SOCK_DGRAM, 0)` + `bind(INADDR_ANY, htons(0x270c))` = **porta 9996**;
  porta sovrascrivibile da config (`*(this+0x10) = *(config+0x8cc)`), log
  `"RemoteTelemetryUDP :: creating socket on port %d"`.
- `ioctlsocket(FIONBIO)` → socket **non-blocking**; a fallimento chiude il socket.
- `*(this+0x11) = 0x148` (328) = dimensione pacchetto live; `*((u8*)this+0x84) = 0x61`
  = primo byte 'a' del buffer live.

Pump `FUN_140146cc0` — `recvfrom(max 0x200)`, switch sull'operationId del
client-handshaker (12 B: identifier@0, version@4, operationId@8):

| operationId | comportamento osservato |
|---|---|
| 0 Connect | Rimuove un'eventuale vecchia entry del client e manda la risposta identità: **0x198 = 408 byte** (`sendto @ 0x1401472c6`), con carName e trackName UTF-16[50] terminati da wchar `%` (0x25) e status **0x1092 = 4242** (il magic noto dell'HandshakerResponse). |
| 1 CarInfo | Registra il client (record **28 B / 0x1c**: flag + sockaddr) e da lì in poi riceve il flusso live. |
| 2 LapInfo | Idem, con il byte di famiglia del record posto a 1 (sottoscrizione eventi lap). |
| 3 Disconnect | Rimuove il client dalla registry (shift-compattazione dei record). |
| 4 (car list) | Prima risponde con header **8 B** `[int32 = *(session+0x1fc) — semantica ignota][int32 count = auto totali]`, poi invia un record **0xd0 = 208 B** per auto: `[driverName UTF-16[50] da car+0x10b8, terminata %][carName UTF-16[50] da car+0x138, terminata %][int32 @200 = indice leaderboard (FUN_140048310, 0xFFFFFFFF se assente)][int32 @204 = car+0x1158 (FUN_1400d3500, semantica ignota)]`. NOTA: qui **driverName precede carName** (ordine invertito rispetto al Connect). Il filtro (indice 0 sempre incluso; le altre se `FUN_1400d8b40` = `*(*(car+0x210)+0xd8)` bool) vale per i record, non per il count. |
| 5 | Risponde con **4 B** = indice auto (`FUN_140048310`), solo se `session+0x1c8 != 0` (altrimenti nessuna risposta). |

Broadcast live `FUN_140147680`: per ogni record client con flag attivo →
`sendto(buffer a this+0x84, 0x148 = 328 B)` (RTCarInfo, primo byte 0x61).
Se la porta config cambia a runtime, `FUN_1401473a0` invia ai client un
pacchetto **0xd4 = 212 B** `[op 4B][carName%][trackName%][port-1 4B][ip 4B]`.

`FUN_140147590` = push del record client; `FUN_1401477c0` = pre-pass sul
settore dei client prima del broadcast.

**Ricaduta ksengine:** `AcRemoteTelemetry.h` (reverse precedente, vedi
[REPORT_AC_REMOTE_TELEMETRY.md](REPORT_AC_REMOTE_TELEMETRY.md)) è coerente
sui canali principali (12/408/328/212 B, magic 4242). **P2 FATTO:** il pump
risponde ora anche agli operationId 4 (header 8 B + record 208 B via
`setCarList()`; roster default = auto/driver dell'identità) e 5 (4 B, indice
car 0) — testati in `ac_remote_telemetry_test`. Servono a dashboard/Live
Timers per enumerare le auto in pista.

**Nota terminatori (round 6):** AC termina le stringhe del Connect e della
car list con wchar `%` (0x25), **non** con L'\0' (verificato: dopo il
contenuto scrive la coppia byte `'%','\0'`; stringa vuota = `%` immediato; i
byte di coda restano non inizializzati in AC). I client reali devono essere
scanner su `%`. ksengine ora scrive `%` + zero-fill su Connect e car list;
**RTLap resta NUL-terminated** (open question: non verificato su acs.exe).

**Open question (round 6):** nel Connect di AC `trackName` e `trackConfig`
leggono **lo stesso offset** (`track+0x10`) — AC duplica il nome della pista;
ksengine manda invece i due valori distinti da `setIdentity()` (comportamento
più corretto, ma difforme da AC — da documentare per eventuali client che si
aspettano duplicati). Semantica di `*(session+0x1fc)` (header op 4) e
`car+0x1158` (record op 4) ancora ignota.

---

## 3 — acs.exe: comandi UDP (porta 9666)

`UDPCommandListener::UDPCommandListener` = `FUN_1401d0f30`
(stringa d'errore `"ERROR: BINDING LISTENER SOCKET UdpCommandListener"`):

```text
socket(AF_INET, SOCK_DGRAM, 0)
bind(INADDR_ANY, htons(0x25c2))   // porta 9666
```

Listener separato dalla telemetria. ksengine **non ha alcun listener su 9666**
(grep `9666` → nessun risultato): gli strumenti che inviano comandi UDP a AC
(Content Manager, apps) non parlano con kssim.

### 3.1 Pump e dispatch (round 7)

- Vftable `UDPCommandListener::vftable` @ `0x1404df5e8`: `[0] FUN_1401d1020`
  (dtor: `closesocket(this+0x60)`), `[1] FUN_1401d1570` (**pump, virtuale**),
  `[2..5]` stub `0x140017870`.
- Pump `FUN_1401d1570` (chiamato una volta per frame):
  `ioctlsocket(sock, FIONBIO)` ogni chiamata (socket non bloccante),
  poi **una sola** `recvfrom(sock, buf, 0xfbc /*4028 B*/, ...)`;
  `n < 1` → solo log `"Socket error UDP Command listener %d"` (WSAGetLastError).
- **Dispatch: `switch(payload[0])` su un byte-comando ASCII**, casi
  `'B'`..`'Q'` (16 comandi). Il mittente (`sockaddr` di recvfrom) viene
  **ignorato**: AC non risponde mai (fire-and-forget).

### 3.2 Semantica comandi (round 7 + 8)

`this+0x58` = contesto gioco (possiede la lista auto e un task-queue a
`*(this+0x58)+0x188`); `getCar(app, idx)` = `FUN_1401992e0`.

| Cmd | Evidenza (decompile) | Semantica |
|-----|----------------------|-----------|
| `B` | call virtuale su `*(*(this+0x58)+8)` slot 1 | ignota |
| `C`,`F`,`G`,`H`,`J` | lambda accodata a `*(*(this+0x58)+0x188)` (task queue) | deferred, TBD (round 9) |
| `D` | `RaceManager=FUN_1400e7850(app); FUN_140140d00(rm)` — enqueue con float `1.0f` | TBD (restart-like) |
| `E` | `FUN_14019eb00(app)`: se `car[0]+0xa40==1` enqueue lambda con float da `car+0x118+0x1b4` | TBD |
| `I` | se `*(taskQ+0x120)==1` → `FUN_1400c4710(obj+0xb8)` (cicla indice su lista di record 0x4C B), else enqueue | TBD (ciclo replay/telecamera?) |
| `K` | `goToSpawnPosition(car[0], L"PIT")` (`FUN_1400d3ae0`, log `"goToSpawnPosition %S"`) | **teleport auto 0 alle pit** |
| `L` | `K` + `FUN_140197780(app)` (disabilita 3 subsystem, ne abilita 1) + `FUN_1400ff2b0` | teleport + reset (TBD) |
| `M`,`N` | lambda catturanti `this` + **byte payload[1]** (`local_1007`) | TBD (parametro index) |
| `O` | `goToSpawnPosition(car[payload[1]], L"PIT")` | **teleport auto index alle pit** |
| `P` | `FUN_1400da280(car[0], 1)` — lambda differita con flag | TBD (reset/recover?) |
| `Q` | `RaceManager=FUN_1400e7850(app); FUN_140141350(rm)` — se `idx < (size/0x70)-1` enqueue con float `2.0f` | TBD (next-item?) |

Stringa `DAT_1404aa9a0` (usata da `K`/`L`/`O`) = UTF-16 **`"PIT"`** (verificato
con `read_bytes` round 8).

**Round 9–10–11 (lambda + body decompile):** le 8 funzioni lambda MSVC
(`FUN_1401d13e0`–`FUN_1401d11f0`, round 11A/B) costruiscono `std::function`
oggetti puntate dai rispettivi slot nella vftable di `UDPCommandListener::pump`.
I loro body decompilati mostrano chiamate a `FUN_1400340d0` (enqueue task col
priority 0/1/3), `FUN_140199330`/`FUN_14019e180`/`FUN_14019e6b0`/`FUN_1401b60b0`
(operazioni sulla task‑queue), e `FUN_1400d3500` (lettura da `param_1+0x58+0x1c8`,
usato da case `'I'`). Le relative vftable sono state localizzate con
`find_xrefs_to_name` (round 10): gli hash degli `operator()` puntano agli
indirizzi `0x1404df6a8`–`0x1404df868` (viste in round 10).

Mappatura parziale dei casi switch (`'B'`–`'Q'`) ricavata dai body decompilati:

| Case | Corpo lambda associato (approssimativo) | Conferma |
|------|----------------------------------------|----------|
| `'B'` | `FUN_1401d13e0` – `FUN_1400340d0(...,0,0,0)` | *TBD* |
| `'C'` | `FUN_1401d1250` – operazioni task‑queue | *TBD* |
| `'D'` | `FUN_140140d00` (round 8) – `RaceManager` enqueue | *già noto* |
| `'E'` | `FUN_14019eb00` (round 8) – condizione car[0]+0xa40 | *già noto* |
| `'E'–'F'` | `FUN_1401d1400` – `FUN_1400340d0(...,1,0)` | *TBD* |
| `'G'` | `FUN_1401d1250` – task‑queue (corpo sovrapposto a `'C'`) | *TBD* |
| `'H'` | `FUN_1401d12b0` – `FUN_14019a740` | *TBD* |
| `'I'` | `FUN_1401d1360` – legge `param_1+0x58+0x1c8`, controlla `*(taskQ+0x120)==1` | *TBD* |
| `'J'` | `FUN_1401d11f0` – `FUN_1400340d0(...,3,0)` (priority 3) | *TBD* |
| `'K'` | `goToSpawnPosition(car[0], L"PIT")` | *confermat* (round 7/8) |
| `'L'` | `K` + `FUN_140197780(app)` + `FUN_1400ff2b0` | *TBD* |
| `'M'` | lambda con byte `payload[1]` + `goToSpawnPosition` | *TBD* |
| `'N'` | lambda con byte `payload[1]` | *TBD* |
| `'O'` | `goToSpawnPosition(car[payload[0]], L"PIT")` | *confermat* (round 7/8) |
| `'P'` | `FUN_1401992e0(car[0],0)` + `FUN_1400da280(uVar4,1)` | *TBD* |
| `'Q'` | `RaceManager` enqueue con float `2.0f` | *TBD* |

Stringa `DAT_1404aa9a0` (usata da `K`/`L`/`O`) = UTF-16 **`"PIT"`** (verificato
con `read_bytes` round 8).

**Round 11 (body decompile):** le 8 funzioni lambda `FUN_1401d13e0`–`FUN_1401d11f0`
sono state decompilate con `batch_decompile`. I loro corpi mostrano chiamate a
`FUN_1400340d0` (enqueue col priority 0/1/3), `FUN_140199330`/`FUN_14019e180`
/`FUN_14019e6b0`/`FUN_1401b60b0` (task‑queue) e `FUN_1400d3500` (lettura da
`param_1+0x58+0x1c8`). Gli hash degli `operator()` sono risultati
`b43090e5`/`0ef6d36d`/`580681a2`/`c9308712`/`6fe0215b`/`4d92bba0`/
`bdd0c6e4`/`0304b909` (round 10, `find_xrefs_to_name`). I body decompilati
confermano che il dispatch switch su `payload[0]` ('B'–`'Q'`) mappa a queste
funzioni lambda, ma la corrispondenza lettera‑corpo richiede ulteriore
analisi semantica (ancora `TBD` per la maggior parte dei casi).

### 3.3 ksengine (P3)

`src/adapters/assetto_corsa/UdpCommandListener.h` (header-only, portable
Windows/POSIX):
socket dgram, bind INADDR_ANY, non-blocking, porta default 9666 (0x25C2);
`pump()` drainer burst (max 64/call, AC fa 1/frame) e dispatcha:
`onDatagram` (raw) + `onCommand(cmd, payload, len, ip, port)` solo per
`'B'`..`'Q'` (default ignorato, come AC). `replyTo()` disponibile (AC non
risponde; utile per debug/ksnet). Agganciato a `FeatureHub::udpCmd`
(`startServices`/`stopServices`/`tick`), porta configurabile con
`udp-cmd-port` in `server/kssimserver.ini` o `--udp-cmd-port` (0 = disable).
Test: `tests/ksengine/udp_command_listener_test.cpp` (loopback, porta
effimera) — porta 9666, range `'B'`..`'Q'`, dispatch, replyTo, ignota.

---

## 4 — acs.exe: race control / session server

- `RaceControlSessionServer` (RTTI `.?AVRaceControlSessionServer@@`) +
  `RaceControlSPSession`, `RaceControlVoting`, `RaceControlScreen`,
  `RaceControlSessionInfo` — modulo di sessione/canale race control dedicato.
- `FUN_14025d400` (dispatch) e `FUN_14025d580` (wrapper `sendto`): il listener
  fa `recvfrom(max 0x800)`; se il primo byte del pacchetto è **0xF9** risponde
  al mittente con **0xF8 + timestamp double** (keepalive/ping); altrimenti
  dispaccia il buffer a una lista di callback `std::function` registrate.
- Il bind del socket non è nelle procedure analizzate in questa sessione
  (probabilmente inizializzato in un percorso non decompilato) — porta da
  verificare in un round dedicato.

---

## 5 — acs.exe: penalità

Stringhe e funzioni:

- `"Adding penalty PT: %f"` @ `0x1404f7628`, usata in `FUN_14026f6a0`
  (acccumulatore penalty time): `PT (+0x3d30) += delta`, `PTA (+0x3d28) += delta`
  e `"SUPPRESSING MIN SPEED PENALTY CLEAR"` quando la penalità min-speed è
  attiva (> 0.0) — AC non cancella la penalità di velocità minima se ancora
  dovuta.
- Disqualifica pending penalty `FUN_14009b830` — enum tipi (offset +0xc
  dell'evento): **1 = JumpStart**, **3 = MandatoryPit**, **4 = TrackCut**
  (messaggi `"You have been disqualified for not clearing the JumpStart /
  Track Cut penalty"`, `"...not doing the Mandatory Pit"`); evento di clear
  (tipo 1) azzera il flag. Guardia iniziale `*(car+0x3238) != 0` (server/DQ flag).
- Avatar penalties: `DriveThroughPenaltyAvatar`, stringhe
  `"DRIVE THROUGH PENALTY"`, `"PENALTY, SLOW DOWN FOR"`, `"JUMP_START_PENALTY"`,
  `"DT_PENALTY_AVATAR"`, `"FLAG_PENALTY"`.

**Ricaduta ksengine:** `TrackLimitsMonitor` + penalty hook esistono già
(P1.3 "Done" in [PARITY_STATUS.md](PARITY_STATUS.md)). La parity utile è:
(1) enum tipi 1/3/4 con DQ se non entro N giri/tempo, (2) accumulatori
penalty-time in secondi (PT/PTA) esposti in HUD/SM/UDP, (3) soppressione del
clear per min-speed (pit limiter).

---

## 6 — acs.exe: AI line learning (trace saver)

`FUN_140296ae0` (stringhe `"SAVING SPLINE"`, `"BEST LAP POINT COUNT:%u"`,
`"PITLANE POINT COUNT:%u"`, `"FINISHED SAVING SPLINE"`):

- Opera sotto `CRITICAL_SECTION` (thread dedicato).
- Scrive `/ai/fast_lane.ai.candidate` e `/ai/pit_lane.ai.candidate`;
  con commit (param_2) sovrascrive `/ai/fast_lane.ai` e `/ai/pit_lane.ai`.
- Conta i punti della best-lap spline e della pit-lane spline; errori
  `"NO PIT LANE FOUND: %S"`.

Questo è il meccanismo con cui AC impara la linea del player e la propone
all'AI. ksengine carica `loadAiSpline` ma non salva candidati.

**Ricaduta ksengine:** `AiSpline`/`AIController` — salvataggio
`fast_lane.ai.candidate` dalla best lap del player (formato `.ai` già noto a
ksengine via loadAiSpline) e promozione a linea AI. Effort M.

---

## 7 — acs.exe: replay

- Loader `FUN_140160220`: log `"Loading replay version: %d"`; se versione
  **< 15 (0xf)** → `"Kunos Simulazioni: CRITICAL ERROR"` +
  `"Loading replay with version <15 is no longer supported"`. Frame per auto da
  **0x110 = 272 byte** (contatore letto a 8 B, strutture 0x18 e 0x44 nei
  buffer di canale).
- `ReplayManager`: `cfg/replay.ini`, qualità a Hz
  (`"ERROR: Replay Quality Level %d not supported, using default 30Hz"`,
  `"REPLAY QUALITY AT %f Hz"`), `"ReplayManager MAX FRAMES COUNT %d WHICH IS %f MINS"`,
  autosave, temp in `/Assetto Corsa/replay/temp/AC_*`, `"ReplayManager::onStepCompleted"`.
- Classi: `ReplayManager`, `ReplayRecorder`, `ReplayInterface`, `GhostCar`,
  `GhostCarRecorder`.

**Ricaduta ksengine:** `ReplayRecorder.h` esiste (record + play), frame ~60 B,
formato proprio non compatibile con `.rep` AC v15+. La compatibilità totale
del replay AC (import/export .rep) è un item L, bassa priorità; più utile è
allineare la qualità configurabile (Hz) e l'autosave.

---

## 8 — acs.exe: protocollo multiplayer ACP (server dedicato, porta 9600)

Stringhe `ACP_` (client `ACClient` — 28 occorrenze): `ACP_AUTH_FAILED`,
`ACP_UNSUPPORTED_PROTOCOL`, `ACP_WRONG_PASSWORD`, `ACP_BLACKLISTED`,
`ACP_NO_SLOTS_AVAILABLE`, `ACP_SERVER_STILL_BOOKING`, `ACP_SESSION_CLOSED`,
`ACP_CAR_LIST_RESPONSE` (`"Requesting car list :: UNEXPECTED…"`,
`"TIMEDOUT WAITING FOR ACP_CAR_LIST_RESPONSE"`), `ACP_CE_P2P`
(`"ACP_CE_P2P Car %i Count %i Activate %i"`), `ACP_CE_MANDATORY_PIT`,
`ACP_TEXT_FILE`, `ACP_TYRE_COMPOUND_CHANGED`, `ACP_WEATHER_UPDATE: Ambient=%f
Road=%f GRAPHICS=%S`, `ACP_RACE_START_UPDATE: %f (%f) PING: %d, local
guess:%.1f real:%.1f`, `ACP_DAMAGE_UPDATE`, `ACP_RACE_OVER`,
`ACP_CLIENT_DISCONNECTED`.

ksengine usa `NetworkConfig` con porta **9600** (stessa del server dedicato AC)
ma un protocollo proprietario (ksnet reliable-UDP, `PROTOCOL_VERSION`, no
encryption): **non joinabile con client/server AC**.

**Ricaduta ksengine:** server AC-compatibile/ACP è un item **L/XL**
(allineato a ksnet in GAP_MATRIX P1.1). Non urgente per kssim "definitivo"
offline/LAN, ma è il pezzo che manca per sostituire `acServer.exe` nei
community server.

---

## 9 — acShowroom.exe (x86, 17,2 MB)

66 classi RTTI: `KN5IO` (loader KN5), `GLRenderer`, `SkyBox`,
`CubeMapRenderer`, `HDR*`, `CameraForwardYebis` (Yebis post-fx), mirror
cameras, showroom UI. Conferma che Showroom è un visualizzatore dedicato sullo
stesso formato KN5 + pipeline renderer propria. ksengine ha già bake KN5 →
NMS2 e materiali (roadmap 2A/2.4-2.5): nessun item nuovo emerso; le query xrefs
sui punti d'ingresso non hanno prodotto riferimenti utilizzabili (vtable non
referenziate direttamente).

---

## 10 — Content Manager.exe (.NET)

- `inspect-managed-artifact`: PE32 CLI, runtime v4.0.30319 (.NET Framework),
  flag **IL-only + 32-bit-required**, 2074 TypeDef / 14824 MethodDef /
  9558 Field / 1800 TypeRef, nessun header native né ReadyToRun →
  **nessun motore fisica/graphics proprio**: è un launcher/overlay.
- `inspect-managed-native-boundaries`: **4 soli P/Invoke** — `user32!GetWindowRect`,
  `kernel32!SetDllDirectory`, `kernel32!SetErrorMode`, `kernel32!LoadLibrary`
  (unicode) — più 8 implementazioni native minime. Parla con AC tramite
  shared memory/UDP, non tramite plugin nativi.
- Limite noto: `inspect-managed-members` fallisce per il budget fisso di 64 MB
  del decoder (non configurabile in REA 6.4.0) → inventario membri non
  ottenibile con REA oggi (flag `--type` esiste solo su main).

**Ricaduta ksengine:** CM si collega a qualsiasi build che esponga le
interfacce standard AC (SM `acpmf_*`, UDP 9996, comandi 9666, cartella
contenuti). Parità sufficiente = item sezione 1.1 + 2 + 3.

---

## 11 — Item di parity per ksengine (priorità)

| # | Item | Evidenza | Effort |
|---|---|---|---|
| P1 | `AcSharedMemoryPublisher`: crea le 3 mapping `acpmf_*` a **592 B fissi** (0x250) come AC, non `sizeof(page)` — **FATTO** (`acShmMappingSize()`, test `ac_shared_memory_publisher_test`) | §1.1 | S |
| P2 | `AcRemoteTelemetry`: aggiungi **operationId 4** (car list: 8 B header + record 208 B) e **5** (4 B) — **FATTO** (`setCarList()`, test esteso `ac_remote_telemetry_test`) | §2 | S |
| P3 | Nuovo `UdpCommandListener` su **porta 9666** (socket dgram, INADDR_ANY, non-blocking) con dispatch a FeatureHub — **FATTO** (`UdpCommandListener.h`, dispatch byte-comando `'B'`..`'Q'`, `udp-cmd-port`, test `udp_command_listener_test`; semantica lettere in §3.2, TBD = open question) | §3 | M |
| P4 | Penali: enum **1 JumpStart / 3 MandatoryPit / 4 TrackCut** + accumulatori PT/PTA in secondi + DQ pending + soppressione clear min-speed | §5 | M |
| P5 | AI trace saver: salva `fast_lane.ai.candidate` dalla best lap e promuovi a `fast_lane.ai` | §6 | M |
| P6 | Mapping `Local\acpmf` (412 B) per tool che la leggono | §1.2 | S (opzionale) |
| P7 | Replay: qualità Hz configurabile + autosave (formato .rep v15 resta fuori per ora) | §7 | S |
| P8 | ACP server protocol (9600) — sostituire/affiancare acServer per community server | §8 | L–XL |

Mappatura: P1/P2/P6 → adapter AC (`src/adapters/assetto_corsa/`); P3 →
simulator + FeatureHub; P4 → `RaceSessionManager`/`TrackLimitsMonitor`;
P5 → `AiSpline`; P7 → `ReplayRecorder`; P8 → ksnet.

---

## 12 — Metodologia e limitazioni

- Sessioni REA Ghidra effettuate: acs.exe round 1–6 (`binary_overview`,
  `search_strings`, `search_procedures`, `xrefs`, `procedure_pseudo_code`,
  `batch_decompile`, `resolve_containing_procedure`, `trace_feature`),
  acShowroom round 1–2, CM via provider `rea-dotnet-static`.
- Evidenze grezze: `%TEMP%\opencode\rea\out_acs_all`, `out_acs_script3`,
  `out_acs_script4`, `out_acs_script5`, `out_acs_script6` (semantica op 4/5
  e getter `FUN_140048310`/`FUN_1400d3500`/`FUN_1400d8b40`),
  `out_showroom_script2`, `acs_classes.txt` (510 classi RTTI).
- **Limitazione:** i descrittori RTTI (`.?AV…@@`) non hanno xrefs diretti
  (vtable non referenziate da codice analizzato) — l'ascesa classe → metodi
  passa per stringhe usate dal codice + `trace_feature`.
- **Limitazione:** porta del race-control UDP (§4) e binding ACP (§8) da
  verificare in un round dedicato (i punti di bind non sono nelle procedure
  analizzate).
- **Limitazione:** CM .NET membri non analizzabili con REA 6.4.0 (budget
  decoder 64 MB); per l'inventario tipi servirebbe ILSpy/dnlib locale.

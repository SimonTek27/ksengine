# Track recovery — dalla bandiera gialla al traino

`TracksidePosts` alza il giallo quando un'auto si ferma sulla traiettoria.
`TrackRecovery` (`src/simulator/TrackRecovery.h`) è il resto: chi la porta via.

## La regola

Per ogni auto, a ogni passo di fisica:

| stato dell'auto | cosa succede |
| --- | --- |
| in movimento (`speedMs >= stopSpeedMs`) | niente, il cronometro si azzera |
| motore acceso, anche da ferma | niente: si è fermato per scelta |
| in corsia box o nel proprio box | niente: è un pit stop |
| ferma, motore spento, fuori dalla corsia | **stranded** |
| stranded per `stopSec` | i marshal lo sanno (`armed()`) → è il giallo |
| stranded per `stopSec + marshalSec` | **traino**: `update()` restituisce `true` una volta sola |

Valori in `TrackRecovery::Config`: `stopSpeedMs = 0.5`, `stopSec = 2`,
`marshalSec = 20`. Due secondi distinguono una testacoda che riparte da un'auto
piantata; venti sono il tempo perché i marshal arrivino e aggancino.

## Cosa fa il loop

`SimulationLoop::updateTrackRecovery()` gira allo stesso passo di
`updateTracksidePosts()`, quindi il giallo e il traino vedono la stessa pista
nello stesso istante. Al traino:

1. `snapVehicleToPose(m_vehicle, m_playerBox)` — l'auto è nel suo box, ferma,
   motore ancora spento;
2. `m_garageExit.forceEnterGarage()` — il loop riprende la fase di box, quindi
   valgono pit limiter e hold come in un pit stop normale.

Da lì vale la regola dell'avviamento: `pressStarterByCrew` funziona solo dove la
squadra c'è, cioè in corsia box. Il traino è quindi l'unico modo di riaccendere
una F1 piantata in pista — non c'è un avvio a distanza, e non si resta lì per
sempre. Vedi `docs/PIT_WIRING.md` e `VehicleSimulator::setCrewStarterAvailable`.

## Cosa non copre

- Solo il giocatore: le auto AI non hanno un ciclo di avviamento proprio.
- Solo motore spento: un'auto insabbiata col motore acceso non viene trainata,
  perché non c'è niente da riaccendere; la superficie resta di competenza del
  track-limits.
- Nessun DNF: l'auto recuperata torna in pista dal box, i giri continuano.

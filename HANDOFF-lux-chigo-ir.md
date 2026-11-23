# Handoff: lux-chigo-ir – MUTE, DISPLAY & TCL112AC-Erkenntnisse

**Repo:** https://github.com/klausellus-wallace/lux-chigo-ir (Stand: Commit `85493ad`)
**Maintainer:** klausellus-wallace
**Datum:** 2026-11-22
**Ziel des Folgeagenten:** Die unten belegten Erkenntnisse ins Repo einarbeiten. Die als „offen“ markierten Punkte bleiben Hypothesen, bis Mitschnitte vorliegen.

> ⚠️ `CLAUDE.md` im Repo enthält Regeln, die durch diese Erkenntnisse **widerlegt** sind („DISPLAY and MUTE do NOT emit IR. Never add entities or code paths for them.“, „TIMER is intentionally out of scope“). Laut `CLAUDE.md` braucht jede Änderung daran eine Bestätigung durch den Menschen. **Hol dir vor dem Ändern das OK des Maintainers.** Der Maintainer hat die Untersuchung selbst angestoßen. Ändere CLAUDE.md erst danach mit.

---

## 1. Gerät: tatsächliche Identität

Belegt durch das Typenschild am Innengerät (Foto vom Nutzer):

- **Importeur:** Euromate GmbH, Emil-Lux-Straße 1, 42929 Wermelskirchen. Das ist dieselbe Adresse wie Emil Lux (Marke „LUX“, OBI).
- **Euromate-Art.-Nr. 482255**, S/N <redacted>, „version number V200120“. OBI verkauft es als „SKG 9000-48 QC“, Art.-Nr. 3432531.
- **Inverter:** Kühlleistung 2600 W (940–3300 W), Heizleistung 2610 W (940–3360 W), R32 0,53 kg.
- **Platinen:** Hauptplatine `31101-000222` (Fertigung 2020-12-29, `IC1[V14(0B6C)]`, `IC2[30/8C(71BC3)]`). Display-Platine `31102-000084` (2020-06-06): zwei 7-Segment-Ziffern, Timer-Symbol, Sleep-Symbol (Mond), LED, der IR-Empfänger sitzt auf dieser Platine. Schaltplan-Nr. `211319944G`.
- **Schaltplan:** Er zeigt die Anschlüsse CN16 = WIFI, CN14 = PEOPLE FEEL, CN19 = Feuchtesensor, CN8 = Display, CN5-1..3 = Swing-Motoren und „Generator“ = Ionisator (HEALTH), jeweils mit dem Vermerk „some models“. **Offen:** Ob CN16 auf der Hauptplatine bestückt ist, ist auf den Fotos nicht zu sehen. Ist er bestückt, wäre UART-Steuerung über ein ESP am WLAN-Port eine Alternative mit echtem Status-Rückkanal.
- **Fabrik:** unbekannt. Die Teilenummern liefern online keine Treffer. Das IR-Protokoll gehört zur TCL-Familie (siehe 2). Das beweist aber nicht, dass TCL das Gerät gebaut hat.
- **Bedienungsanleitung:** https://www.manualslib.de/manual/862757/Euromate-482255.html (gilt auch für 482256 und 1351824). Deutsch ab S. 19, Fernbedienung S. 22–23. Englisch S. 51–53.

**README/CLAUDE.md anpassen:** Die Aussagen „Chigo-OEM protocol … NOT covered by any existing ESPHome climate platform“ sind falsch. Richtig ist: Euromate 482255, Protokoll = **TCL112AC-Variante**.

---

## 2. Protokoll = TCL112AC (IRremoteESP8266), leicht erweitert

Referenz: `crankyoldgit/IRremoteESP8266`, Dateien `src/ir_Tcl.h` (`union Tcl112Protocol`), `src/ir_Tcl.cpp` und `test/ir_Tcl_test.cpp`. Die Issues #1528 (Quiet), #1486 (Timer/Teknopoint), #1896 (isTcl) und #2177 (Beep) sind ebenfalls relevant.

- IRremoteESP8266 sendet **LSB-first**, das Repo liest **MSB-first**. Ein Drahtbyte im Repo ist deshalb immer `bit_reverse(logisches Byte)`.
  Header logisch `23 CB 26` → Draht `c4 d3 64`.
  MsgType logisch 1 (Normal) → Draht `0x80` (= Frame B). MsgType 2 (Special) → Draht `0x40` (= Frame A).
- Masken-Umrechnung logisch → Draht: `01→80, 02→40, 04→20, 08→10, 10→08, 20→04, 40→02, 80→01`.

### Bit-Tabelle in Draht-Notation (wie im Repo)

| Byte | Draht-Maske | Bedeutung | Status |
|---|---|---|---|
| 5 (B) | 0x20 | Power | ✓ bereits implementiert |
| 5 (B) | 0x10 | OffTimerEnabled | ✓ im Timer-Mitschnitt gesetzt (b5=0x36) |
| 5 (B) | 0x08 | OnTimerEnabled | laut TCL, nicht mitgeschnitten |
| 5 (B) | 0x04 | „Quiet“-Bit im Normal-Frame. Laut Issue #2177 Beep-Verhalten (gelöscht → kein Piepen) | im Repo „immer gesetzt“. Ohne Bit testen |
| 5 (B) | **0x02** | **Light/DISPLAY, invertiert: gesetzt = Display AUS** | ✓ capture2: 0x26↔0x24 beim DISPLAY-Druck |
| 5 (B) | 0x01 | ECO/Econo | ✓ Code korrekt. **`docs/protocol.md` nennt fälschlich 0x02** |
| 5 (**A**) | **0x04** | **MUTE/Quiet (nur im Special-Frame A)** | ✓ capture2: Frame A b5 0x02→0x06 beim 1. MUTE-Druck, 0x02 beim 2. |
| 6 | 0x08 | Health | ✓ |
| 6 | 0x04 | Turbo **laut TCL** | ✗ Gerät nutzt **0x02** (capture2 eindeutig: c0→c2). Repo-Wert beibehalten |
| 6 | 0x01 | I-FEEL | in TCL nicht vorhanden, Gerätevariante. Beibehalten |
| 7 | 0xF0 | Temperatur (31−T, bitgespiegelt) | ✓ |
| 8 | 0xE0 | Fan: 0x00 Auto, **0x80 Min/„Night“**, 0x40 Low, 0xC0 Med, 0xA0 High | Repo deutet 0x80 als „Sleep“. TCL kennt es als Fan-Min |
| 8 | 0x1C | SwingV: 0x1C Swing. Feste Positionen laut TCL: 0x10 Highest, 0x08 High, 0x18 Middle, 0x04 Low, 0x14 Lowest | Positionen ungetestet |
| 8 | 0x02 | TimerIndicator (gesetzt, wenn ein Timer aktiv ist) | ungetestet |
| 9 / 10 | – | Off- / On-Timer, 6-Bit-Wert n in den logischen Bits 1–6: `wire = bit_reverse((n & 0x3F) << 1)` | siehe 4 |
| 11 | – | I-FEEL-Raumtemperatur | Gerätevariante (in TCL ungenutzt) |
| 12 | 0x10 | SwingH | ✓ |
| 12 | 0x04 | HalfDegree (+0,5 °C) | ungetestet |
| 12 | 0x01 | isTcl, gesetzt in COOL/DRY/FAN | ✓ entspricht dem „mode base“ im Repo |
| 4 | 0x10 | im Timer-Mitschnitt gesetzt | in TCL undefiniert, Gerätevariante. In capture2 ist dieses Bit in einem Frame mit **falscher Checksumme**, vermutlich Rauschen |

### Checksumme von Frame A (wichtig)

`ir_Tcl.cpp::calcChecksum` addiert bei MsgType 2 einen **Offset von 0x0F**:

```
cs_wire = bit_reverse( (Σ bit_reverse(b[0..12]) + (frameA ? 0x0F : 0)) & 0xFF )
```

Nachgerechnet:
- Frame A mit MUTE aus: `c4 d3 64 40 00 02 00 00 01 00 00 00 00` → **`a7`**. Das stimmt mit der Konstante überein.
- Frame A mit MUTE an: `c4 d3 64 40 00 06 00 00 01 00 00 00 00` → **`a0`**.

Das ändert die Frame-B-Formel **nicht**. Sie bleibt korrekt und ist durch die Fixtures belegt. Die „Ground truth“-Regel in CLAUDE.md zur Checksumme bleibt also für Frame B gültig und muss nur um Frame A ergänzt werden.

Byte 8 = 0x01 in Frame A ist bei TCL undefiniert. Unverändert lassen.

---

## 3. Belege aus den Repo-Mitschnitten

Ausgewertet mit dem Repo-Parser (`tests/build_fixtures.py`) über `tests/captures/capture2.log`. Im Log steht nicht, welche Taste wann gedrückt wurde. Die Zuordnung folgt der README-Reihenfolge (ECO/TURBO/I-FEEL/HEALTH/MUTE/DISPLAY/TIMER). 15 Tastendrücke = 1 erster Druck + 6 × 2 + 2 Timer, das geht genau auf.

```
20:45:25/27  ECO      b5 26→27→26
20:45:29/33  TURBO    b6 c0→c2, b7 e0→f0, b8 00→a0, dann zurück
20:45:36/39  I-FEEL   b6 c0→c1, b11 00→d8, dann zurück
20:45:41/44  HEALTH   b6 c0→c8, dann zurück
20:45:47     MUTE #1  Frame B unverändert; Frame A b5 02→06 (Checksum a0 in A wäre zu prüfen)
20:45:50     MUTE #2  Frame B unverändert; Frame A b5 wieder 02
20:45:52     DISPLAY  b5 26→24
20:45:55     DISPLAY  b5 24→26 (+ gekipptes b4-Bit, BADCSUM → Rauschen)
20:46:07     TIMER    b5 26→36, b9 00→24
20:46:08     TIMER    zurück auf 26 / 00
```

Frame A ist in capture.log (19×) und capture3.log (38×) konstant `…00 02 00 00 01…a7`. Es gibt dort nur ein einziges anderes gekipptes Rauschbit. Das spricht dafür, dass das MUTE-Bit echt ist. Dazu kommt die exakte Übereinstimmung mit IRremoteESP8266 Issue #1528: Quiet off `…40…` / on `…60…` logisch = Draht `02`/`06`.

`docs/verification.md` („10 Drücke, nur ~4 Frames → DISPLAY/MUTE senden kein IR“) beruht auf einer Sitzung, die **nicht** im Repo liegt. Die Aussage ist durch capture2 widerlegt.

Hinweis: Die Fixture-Filter in `build_fixtures.py::keep()` werfen Frames mit b5 & 0x02 („DISPLAY-persistent state“) weg. Der Code wusste also schon von dem Bit.

---

## 4. Anleitung Euromate 482255 (manualslib, engl. S. 52–53)

- Die Fernbedienung hat FAN, TIMER, SLEEP, ECO, MODE, TURBO („Super“), SWING (horizontal und vertikal getrennt), **DISPLAY** (Anzeige an/aus), **Mute**, I FEEL.
- **MODE + TIMER gleichzeitig = Kindersicherung.** Vermutlich wirkt das nur lokal auf der Fernbedienung. Ungetestet.
- **I FEEL endet automatisch nach 2 h.** Die Komponente sollte I-FEEL deshalb periodisch neu senden. Ein Intervall wie bei TCL-Remotes (ca. 10 min) ist eine **Annahme**.
- Timer: Bei ausgeschalteter Anlage wird mit TIMER und ▲/▼ in mehreren Schritten gestellt. Bereich und Schrittweite nennt die Anleitung nicht. OBI bewirbt „24 h-Timer“. TCL arbeitet mit 20-min-Schritten bis 12 h. Bei 20-min-Schritten wäre der Mitschnitt b9=0x24 → n=18 → 6 h. **Nicht übernehmen ohne Mitschnitte mit bekannten Zeiten.**
- Die Anzeige am Innengerät zeigt SLEEP, Temperatur/Fehlercode und TIMER.

---

## 5. Aufgaben für den Folgeagenten

### A. Sicher belegt (nach Maintainer-OK umsetzen)

1. **`protocol.{h,cpp}`:**
   - `LuxState` um `display_on` (Default `true`) und `mute` ergänzen.
   - Encoder Frame B: `if (power_on && !display_on) b5 |= 0x02;`
   - Frame A dynamisch bauen, statt die Konstante `FRAME_A` zu nutzen: `b5 = 0x02 | (mute ? 0x04 : 0)`, Checksumme mit +0x0F. Eine Funktion `encode_frame_a(bool mute, uint8_t out[14])` anlegen.
   - Decoder Frame B: b5 & 0x02 → `display_on = false`. Die Validierung `in[5] != 0x04 && (in[5] & 0x24) != 0x24` erlaubt 0x26 bereits.
   - Decoder Frame A: Mute-Zustand aus b5 & 0x04 lesen. Dafür die Frame-A-Checksumme mit Offset prüfen. Die RX-Logik in `lux_chigo_ir.cpp` (ca. Zeile 228) verwirft Frame A heute komplett und muss angepasst werden.
2. **`lux_chigo_ir.cpp`:** `emit_frame(FRAME_A)` ersetzen durch den dynamisch erzeugten Frame A (Zeile 138).
3. **`climate.py` / HA:** Zwei `switch:`-Entities (Display, Mute) nach demselben Template-Muster wie HEALTH anlegen.
4. **Doku:**
   - `docs/protocol.md`: Byte 5 korrigieren (0x01 = ECO, 0x02 = Display aus, 0x04 = Quiet-Bit). Abschnitt Frame A mit MUTE und Checksum-Offset ergänzen. Verweis auf TCL112AC einfügen.
   - `docs/verification.md`: Abschnitt „DISPLAY and MUTE don't emit IR“ korrigieren.
   - README: Gerät = Euromate 482255, Protokoll = TCL112AC-Variante.
   - CLAUDE.md anpassen.
   - CHANGELOG ergänzen.
5. **Tests:** Unit-Tests für Frame A (mute an/aus → a7/a0) und für den DISPLAY-Roundtrip. `keep()` in `build_fixtures.py` so ändern, dass b5 & 0x02 nicht mehr verworfen wird. Fixtures danach neu bauen. Die Fixtures selbst **nicht** von Hand editieren. `make -C tests run` muss grün sein.

### B. Auf Hardware prüfen lassen (Nutzer, nicht raten)

1. Gesendeter Frame A mit MUTE an: Reagiert das Gerät (leiserer Betrieb laut #1528)?
2. Display-Bit senden: Geht die Anzeige aus?
3. **Bit-Mark-Timing:** `protocol.h` sendet `BIT_MARK_US = 243`. In den Rohmitschnitten sind die Marks aber **~520–580 µs**, die 0-Spaces ~240–300 µs und die 1-Spaces ~1065 µs. Die Doku hat Mark und 0-Space offenbar verwechselt. TCL nutzt 500 µs Mark und 325 µs Zero-Space. Laut Code-Kommentar (2026-07-23) funktioniert das Senden live, also toleriert das Gerät die Abweichung. Trotzdem auf ~500 µs Mark / ~280 µs Zero-Space umstellen und **live testen**, bevor das gemerged wird.
4. MUTE etwa 6× mit Zeitprotokoll mitschneiden, um das Frame-A-Bit endgültig zu bestätigen.
5. Byte 5 im Frame B ohne 0x04 senden: Quittiert das Gerät dann ohne Piepen?
6. Timer mit 3 bekannten Zeiten mitschneiden (z. B. 0,5 h / 1 h / 10 h), On- und Off-Timer getrennt. Dabei Byte 8 0x02 (TimerIndicator) und b4 beobachten.
7. Optional: SwingV-Festpositionen, HalfDegree (b12 0x04), Fan-Min 0x80 vs. „Sleep“ klären. Hat SLEEP auf der Fernbedienung ein eigenes Bit, oder ist es nur Fan-Min?
8. Optional: Foto der Hauptplatine mit CN16 (bestückt?) für den UART-Weg.

### C. Nicht tun

- TURBO auf 0x04 umstellen: Der Mitschnitt zeigt eindeutig 0x02 für dieses Gerät.
- Die Frame-B-Checksumme ändern.
- Die Timer-Auflösung aus TCL ungeprüft übernehmen.

---

## 6. Externe Quellen

- IRremoteESP8266 `ir_Tcl.h` / `ir_Tcl.cpp` / `test/ir_Tcl_test.cpp`: https://github.com/crankyoldgit/IRremoteESP8266
- Issues: #1528 (Quiet/Mute, ab v2.7.20), #1486 (Timer, GZ055BE1), #1896 (isTcl), #2177 (Beep-Bit)
- ESPHome `tcl112` (nur Basisfunktionen: kein Mute, Display, ECO, Timer). Taugt als Schnelltest für die Protokollidentität: `climate: - platform: tcl112`
- Tasmota IRHVAC `Vendor: TCL112AC` unterstützt Quiet, Light, Econo, Turbo, SwingH
- ESPHome-PR #17095 (Rheem, auf tcl112 aufbauend, mit Mute-Switch), offen
- Anleitung Euromate 482255: https://www.manualslib.de/manual/862757/Euromate-482255.html

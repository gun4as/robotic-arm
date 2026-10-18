# Elektronika un pieslēgumi

## Asu sadalījums (abām platēm)

| Locītava | Draiveris | RAMPS 1.4 STEP/DIR/EN | SKR V1.4 Turbo STEP/DIR/EN | TMC UART (SKR) |
|---|---|---|---|---|
| J1 | X  | D54 / D55 / D38 | P2_02 / P2_06 / P2_01 | P1_10 |
| J2 | Y  | D60 / D61 / D56 | P0_19 / P0_20 / P2_08 | P1_09 |
| J3 | Z  | D46 / D48 / D62 | P0_22 / P2_11 / P0_21 | P1_08 |
| J4 | E0 | D26 / D28 / D24 | P2_13 / P0_11 / P2_12 | P1_04 |
| J5 | E1 | D36 / D34 / D30 | P1_15 / P1_14 / P1_16 | P1_01 |
| Satvērēja servo | — | D11 (SERVOS) | P2_00 (SERVOS) | — |

Gala slēdži: skat. [zemāk](#gala-slēdži).

---

## RAMPS 1.4 + Arduino Mega 2560

### Mikrosoļi (jumperi zem draivera) — JĀSAKRĪT ar `JOINT_MICROSTEPS` config.h

Firmware: **1/16** (160 soļi/°, tāpat kā Pronterface `M92 X160`) — A4988: visi 3 jumperi.

| Draiveris | 1/8 | 1/16 | 1/32 |
|---|---|---|---|
| A4988 | MS1+MS2 | MS1+MS2+MS3 | — |
| DRV8825 | MS1+MS2 | MS3 | MS1+MS2+MS3 |
| TMC2209 standalone | bez jumperiem | MS1+MS2 | MS1 |

TMC2209 uz RAMPS: **MS3 jumperi neliec** (tajā vietā ir PDN_UART). Strāvu iestata ar Vref
potenciometru: `Vref ≈ I_rms × 1.41` (BTT TMC2209 V1.2, 0.11 Ω) → 1.0 A ≈ 1.41 V.

### ESP32 tilts → RAMPS (Serial1 uz Z-MIN / Z-MAX)

D16/D17 (AUX-4) aizņem ekrāns, tāpēc ESP32 pieslēdz pie **Z-MIN** un **Z-MAX** galviņām
(tur ir arī 5V un GND). Signāla ("S") kontakti: Z-MIN = D18 = TX1, Z-MAX = D19 = RX1.

Mega ir **5 V** loģika, ESP32 — **3.3 V**. Mega TX → ESP32 RX **obligāti** caur līmeņa
pārveidotāju (BSS138 modulis) vai dalītāju 1 kΩ / 2 kΩ.

```
RAMPS Z-MIN S  (D18, TX1) ──[1k]──┬── ESP32 GPIO16 (RX2)
                                 [2k]
                                  │
RAMPS GND ────────────────────────┴── ESP32 GND
RAMPS Z-MAX S  (D19, RX1) ─────────── ESP32 GPIO17 (TX2)   (3.3 V Mega uztver kā "1")
```

**Wemos D1 mini** (kontakti ar uzrakstu **D7/D8**, nevis `RX`/`TX`!). D18 → D7 caur BSS138 moduli
(viens kanāls) vai dalītāju 1 kΩ / 2 kΩ. D8 → D19 **tikai tieši ar vadu** — BSS138 pull-up rezistori
D8 pievilktu uz augšu, un D1 mini nestartētu.

```
                         BSS138 modulis
RAMPS Z-MIN S  (D18, TX1) ── HV1    LV1 ── D1 mini D7 (GPIO13, RX)
RAMPS 5V                  ── HV     LV  ── D1 mini 3V3
RAMPS GND                 ── GND    GND ── D1 mini G
RAMPS Z-MAX S  (D19, RX1) ──────────────── D1 mini D8 (GPIO15, TX)   tieši, bez BSS138!
RAMPS 5V ───────────────────────────────── D1 mini 5V   (barošana)
```

Ar dalītāju BSS138 vietā: `D18 ──[1k]──┬── D7`, `┴[2k]── GND` — tāpat kā ESP32 shēmā augstāk.

**ESP32-S3**: tas pats, kas ESP32, tikai RX = **GPIO18** (caur dalītāju/BSS138), TX = **GPIO17**.

### Ekrāns (RepRapDiscount Smart Controller 20x4) → RAMPS

"smart adapter (c) rrd" plate: garā 18 kontaktu rinda → **AUX-4**, mazais 2×4 bloks → **AUX-3**
(SD karte). Adapterī iesprauž **EXP1** un **EXP2** plakanos kabeļus no ekrāna (EXP1 → EXP1).
Ja pēc ieslēgšanas ekrāns tikai deg zils bez teksta — pagriez kontrasta potenciometru ekrāna
aizmugurē; ja vispār nekas — kabeļi otrādi (atslēgas pusē).
ESP32 barošanai labāk atsevišķs 5 V pazeminātājs (Mega 5 V regulators no 12 V sakarst).

### Barošana
RAMPS 1.4 ir paredzēts **12 V**. 24 V tikai, ja nomainīti kondensatori/polidrošinātāji un
Mega atvienots no 12 V (D1 diode) — citādi neriskē.

---

## BTT SKR V1.4 Turbo + 5x TMC2209 V1.2 (UART)

- **UART režīms:** zem katra draivera viens jumperis UART pozīcijā (savieno PDN_UART ar MCU pinu),
  MS1/MS2/MS3 **bez** jumperiem. Skat. BTT SKR V1.4 rokasgrāmatu, sadaļa "UART mode".
- Mikrosoļus (1/16 ar interpolāciju uz 256) un strāvu ieraksta firmware. Vref potenciometrs netiek izmantots.
- Diagnostika: komanda `TMC` → katram draiverim versija `0x21` un `ok`. "nav atbildes" =
  nav motoru barošanas (VM) vai nepareizi jumperi.
- BTT TMC2209 V1.2 **DIAG** pins ir savienots ar endstop ieeju. Ja nākotnē liksi fiziskos gala slēdžus
  uz tām pašām ieejām, DIAG pinu nolauz/atliec.

### Firmware ielāde
`pio run -e skr14turbo` → `firmware/controller/.pio/build/skr14turbo/firmware.bin` → SD kartes saknē
kā `firmware.bin` → ieslēdz plati. Bootloaderis to ierakstīs un pārsauks par `FIRMWARE.CUR`.

### ESP32 tilts → SKR (TFT galviņa, UART0)

SKR ir 3.3 V loģika — savieno tieši:

```
SKR TFT  TX0 (P0_02) ──── ESP32 GPIO16 (RX2)
SKR TFT  RX0 (P0_03) ──── ESP32 GPIO17 (TX2)
SKR TFT  GND         ──── ESP32 GND
SKR TFT  +5V         ──── ESP32 5V/VIN   (var barot ESP32 no šejienes)
```
Pārbaudi pinu nosaukumus uz plates sietspiedes — galviņas orientācija dažādās revīzijās atšķiras.

### Barošana
12–24 V. 24 V dod labāku momentu lielākos ātrumos.

---

## Gala slēdži

| Locītava | RAMPS 1.4 | SKR V1.4 Turbo |
|---|---|---|
| J1 | X-MIN (D3) | X-STOP (P1_29) |
| J2 | Y-MIN (D14) | Y-STOP (P1_28) |
| J3 | AUX-1 5. kontakts (A3 = D57) | Z-STOP (P1_27) |
| J4 | X-MAX (D2) | E0DET (P1_26) |
| J5 | Y-MAX (D15) | E1DET (P1_25) |

- Mehānisks slēdzis: **COM → GND (–), NO → signāls (S)**. Iekšējais pull-up ir ieslēgts, + vads nav vajadzīgs.
  NC pieslēgums arī der (drošāks — pārrauts vads = "nospiests"), tad konfigurācijā atzīmē **NC**.
- Hall sensors (A3144 u.c., atvērtais kolektors): +5 V, GND, OUT → S. Darbojas kā NO.
- Optiskie / 3-vadu slēdžu plates ar LED: + uz 5 V (RAMPS) vai 3.3 V (SKR, **ne 5 V** ja izeja push-pull!).
- Slēdzi novieto nedaudz aiz programmatūras limita (piem. limits -90°, slēdzis -92°), lai parastā kustība
  līdz limitam to nenospiestu.
- Ieslēgšana un pārbaude: PC programma → Konfigurācija (skat. [pc_programma.md](pc_programma.md)).
- **SKR + BTT TMC2209 V1.2**: draivera DIAG pins ir savienots ar to pašu endstop ieeju — DIAG pinu
  nolauz/atliec, citādi tas traucē slēdzim.

---

## Satvērēja servo (abām platēm)

Servo **signāls** → SERVOS galviņa, **GND** kopējs, bet **barošana no atsevišķa 5–6 V avota**
(MG996R u.c. strāvas pīķī velk 1–2 A, plates 5 V regulators to neizturēs).
Impulsu robežas: `GRIPPER_OPEN_US` / `GRIPPER_CLOSED_US` config.h (vai komanda `SERVO 1500`, lai atrastu).

---

## Tilts uz rokas (ESP32 / ESP32-S3 / Wemos D1 mini)

Der jebkura no trim platēm — firmware vide jāizvēlas atbilstoši (`firmware/esp32_bridge`):

| | ESP32 DevKit (WROOM-32) | ESP32-S3 DevKitC (N16R8) | Wemos D1 mini (ESP8266) |
|---|---|---|---|
| Firmware vide | `-e espnow` / `-e lora` | `-e s3_espnow` / `-e s3_lora` | `-e d1mini` (tikai ESP-NOW) |
| RX ← kontroliera TX (caur dalītāju!) | GPIO16 | GPIO18 | D7 (GPIO13) |
| TX → kontroliera RX | GPIO17 | GPIO17 | D8 (GPIO15) |
| Statusa LED | GPIO2 | RGB GPIO48 (zaļa) | — |
| LoRa SCK, MISO, MOSI, NSS, RST, DIO0 | 18, 19, 23, 5, 14, 26 | 12, 13, 11, 10, 14, 21 | nav |
| Barošana | 5V/VIN | 5V | 5V |

- **ESP32 WROVER** neder (GPIO16/17 aizņem PSRAM).
- **ESP32-S3 N16R8**: GPIO 35–37 aizņem PSRAM, 19/20 — USB, 0/3/45/46 — palaišanas pini, tos neizmanto.
  Firmware ielādei izmanto USB ligzdu ar uzrakstu **"COM"** (vai "UART"), nevis "USB".
- **D1 mini — uzmanību:** vadi iet uz kontaktiem ar uzrakstu **D7** un **D8**, **NEVIS** uz `RX`/`TX`!
  `RX`/`TX` ir USB savienojuma kontakti: ja tur pieslēdz RAMPS, tilts nekomunicē un nevar ielādēt firmware.
- **D1 mini**: aparatūras UART tiek pārslēgts uz D7/D8, tāpēc USB ligzda paliek brīva firmware ielādei,
  bet atkļūdošanas ziņas iet uz **D4** (tikai TX). **D8 → RAMPS D19 savieno tieši ar vadu**, bez līmeņa
  pārveidotāja moduļa: D8 palaišanas brīdī jābūt zemā līmenī (to nodrošina D1 mini paša rezistors), bet
  BSS138 modulim ir savi pull-up rezistori, kas D8 pievilktu uz augšu, un D1 mini nestartētu.
  RAMPS D18 → D7 — caur dalītāju 1 kΩ + 2 kΩ.

LoRa modulis (SX1276/SX1278, piem. Ra-02) ir **3.3 V**! Nekad neieslēdz bez antenas.

---

## Pults: ESP32-S3 Futaba T8J korpusā

ESP32-S3 (N16R8) aizstāj Futabas procesoru un sadegušo raidītāju; sakari ar roku caur **ESP-NOW**.
Futabas pamatplate un RF plate netiek barotas. Futabas **pogu plate** (BU4051, 12 pogas,
potenciometrs, LED, pīkstulis) paliek un tiek pieslēgta caur tās savienotājiem **H1** un **H2**.
Slēdži iet caur **CD74HC4067** (16 kanālu multipleksors), ekrāns — **ST7789 1.69" 240×280** (SPI).

Firmware: `firmware/rc_remote` → `pio run -e s3 -t upload --upload-port COMx`
(ielādei USB ligzda **"COM"** ar CH343 čipu).

**Neizmanto:** GPIO 0 (BOOT = kalibrācija), 35–37 (PSRAM), 43/44 (USB-UART), **46**, 48 (plates RGB LED).

### Barošana
2× 18650 (7.4 V) → Futabas ieslēdzējs (**H2.2 → H2.3**) → **LM2596 IN+**, OUT+ (**noregulēts uz 5.0 V**
pirms ESP pieslēgšanas!) → ESP **5V**. Ieslēdzējs pirms pazeminātāja — izslēgtā pultī tas netērē bateriju.
Pīkstuļa plus (H2.3) tad saņem baterijas spriegumu — tas der (pjezo, paralēli 1 kΩ).
Bateriju bloks ar iebūvētu aizsardzību (piem. 7.4 V 2400 mAh). **Uzlāde no USB:** CN3302 modulis —
IN+/IN− → atsevišķa USB ligzda (lādētājs ≥ 2 A), BAT+/BAT− → tieši bateriju blokam. Pašu
uzlādes stāvokli rāda moduļa LED (ekrāna rādījums izslēgts, `USB_CHANNEL -1`).

### ESP32-S3 kājas

| ESP32-S3 | Kas |
|---|---|
| GPIO 1 / 2 | labais stiks ↔ / ↕ (slīdnis; galos 3V3 un GND) |
| GPIO 5 / 4 | kreisais stiks ↕ / ↔ (pielodēts šādi; programmā `ANALOG_PINS`) |
| GPIO 6 | potenciometrs ← H2.5 |
| GPIO 7 | baterija caur dalītāju 100 kΩ / 33 kΩ |
| GPIO 13 / 14 / 47 | **kopīgās adreses:** BU4051 A/B/C (H2.9 / H2.8 / H2.7) **un** HC4067 S0 / S1 / S2 |
| GPIO 21 | HC4067 S3 |
| GPIO 10 | BU4051 COM ← H2.6, **+ 4.7 kΩ uz 3V3** |
| GPIO 9 | HC4067 SIG (lasa ar ADC), **+ 10 kΩ uz 3V3**; HC4067 EN → GND, VCC → 3V3 |
| GPIO 38 / 39 / 40 / 41 | tiešās pogas ← H1.3 / H1.4 / H1.5 / H1.6 |
| GPIO 3 / 8 / 42 | LED sarkanā / zaļā / zilā → H1.8 / H1.9 / H1.7 (deg pie "0") |
| GPIO 45 | pīkstulis → **1 kΩ** → H2.4 (uz plates bāzes rezistora nav) |
| GPIO 12 / 11 | ekrāna SCL (takts) / SDA (dati) |
| GPIO 15 / 16 / 17 / 18 | ekrāna CS / DC / RES / BLK (spilgtums); ekrāna VCC → 3V3 |
| 3V3 | H1.1 (LED kopējais plus), H1.2 (BU4051 VDD, potenciometrs), stiku galos |
| GND | H1.10, H2.10, HC4067, ekrāns, slēdži |

### HC4067 kanāli (katrs slēdža kontakts → kanāls, otrs → GND)

| Kanāls | Kas |
|---|---|
| C0 | slēdzis **G** = ARM (slēdža COM; kāja 1 uz plates pie GND, kāja 3 nekur) |
| C1 | slēdzis **H** (nefiksējošs, normāli slēgts: miera stāvoklī pie GND, `MOMENT_NC 1`) |
| C2 / C3 / C4 | slēdži **D** / **B** / **A** (A = kreisais ↔ J4/J5) |
| C5 | slēdzis **F** = AVĀRIJA |
| C6 | 3 pozīciju slēdzis **C** (režīms): **COM** |
| C7 | 3 pozīciju slēdzis **E** (ātrums): **COM** |
| C8, C9, C10 | brīvi (C10 rezervē USB uzlādes rādījumam, `USB_CHANNEL 10`) |
| C11 … C15 | brīvi |

**3 pozīciju slēdži:** uz slēdžu plates ON1 jau ir pie GND (kopā ar 2 pozīciju slēdžiem). Tāpēc
COM → kanāls, **ON2 → 4.7 kΩ → GND**. Programma lasa spriegumu: ~0 V = 1. pozīcija, ~3.3 V = vidus,
~1.05 V = 3. pozīcija (robežas `SW3_MID_MV`). Ja kāds slēdzis sanāk otrādi — samaini ON1/ON2 nozīmi
programmā vai pagriez slēdzi.

### Futabas pogu plate (H1, H2)

| Plates kontakts | Kas tur ir |
|---|---|
| H1.1 | LED kopējais plus (caur 120 Ω katrai krāsai) |
| H1.2 | VDD (BU4051, potenciometra augša) |
| H1.3 / H1.4 / H1.5 / H1.6 | tiešās pogas KEY7 / KEY8 / KEY12 / KEY11 (caur 220 Ω) |
| H1.7 / H1.8 / H1.9 | LED zilā / sarkanā / zaļā (mīnuss) |
| H2.1 / H2.2 / H2.3 | ieslēdzējs (H2.2 = kopējais, H2.3 = ieslēgts, arī pīkstuļa plus) |
| H2.4 | pīkstuļa tranzistora bāze |
| H2.5 | potenciometra slīdnis (caur 1 kΩ) |
| H2.6 | BU4051 COM |
| H2.7 / H2.8 / H2.9 | BU4051 C / B / A |
| H2.10 | GND |

BU4051 kanāli X0…X7 = pogas KEY5, KEY2, KEY1, KEY6, KEY3, KEY10, KEY4, KEY9 (uz GND). Uz kanāliem ir
kondensatori pret drebēšanu — tāpēc COM vajag 4.7 kΩ pull-up (ar ESP iekšējo atlaišana kavētos).

**Pogu numuri** (pēc novietojuma, `BTN_ORDER`): P0/P1 kreisā kolonna augšā, P2/P3 kreisā kolonna apakšā,
P4/P5 zem kreisā stika, P6/P7 labā kolonna augšā, P8/P9 labā kolonna apakšā, P10/P11 zem labā stika
(pārī pirmā = "−", otrā = "+"); 12 = nefiksējošā poga.
Nospiežot pogu, ekrāna apakšā parādās tās numurs (**poga P5**), un seriālais ports (115200)
ik sekundi drukā visu ievadu stāvokli — tā var noskaidrot, kura poga ir kura.

### Funkcijas (maina tilta web lapas cilne **"Pults"**; noklusējumi `esp32_bridge/include/config.h`)

Cilnē "Pults" tiešraidē redzami stiki, pogas un slēdži; nospiežot trima pogu, iezīmējas tās rinda.
Tur maina: kura poga kurai locītavai (±), kurš stiks kurai locītavai (SW3 izsl./iesl.), ātrumus,
trima un satvērēja soli. Glabājas tiltā ("Saglabāt").
**Slēdzis H (nefiksējošais) = Shift** (ARM ieslēgts): + trims "+" = nulle šai locītavai, + trims "−" = HOME šai
locītavai; tikai nefiksējošā poga 2 s = HOME visām.

| Vadības elements | Funkcija |
|---|---|
| Labais stiks ↔ / ↕ | J1 / J2 |
| Kreisais stiks ↕ | J3 |
| Kreisais stiks ↔ | J4 (SW3 ieslēgts → J5) — `RC_MAP_BANK_A/B` |
| Potenciometrs | satvērējs (kad to pagriež) |
| G | **ARM** — vadības atļauja (pēc ieslēgšanas jāpārslēdz OFF → ON) |
| F | **AVĀRIJAS STOP**; atslēdzot → `RESET` (citas avārijas atcelšanai: F ieslēgt un izslēgt, vai H 2 s) |
| A | kreisais ↔ = J4 vai J5 |
| B, D | brīvi |
| E (3 poz.) | ātrums lēni 25 % / vidēji 50 % / ātri 100 % — `RC_SPEED_LEVELS` |
| C (3 poz.) | režīms vadība / mācīšana / atskaņošana (ekrānā; mācīšana vēl tiek veidota) |
| Trima pogas 0…11 | J1…J5 ± 0.5° (turot — atkārto), satvērējs ± 5 % — `RC_TRIM_MAP` |
| H turēts 2 s | HOME visām; ja roka avārijas stāvoklī - **RESET** |

Trima pogas darbojas arī locītavām bez nulles (lēni, bez limitiem — komanda `NUDGE`).

### Slēdžu pārbaude ieslēdzot
Ja ieslēdzot kāds slēdzis nav sākuma pozīcijā (G, F, A, B, D izslēgti; E = lēni; C = vadība), pults
neieslēdz radio, pīkst ik sekundi, mirgo sarkani un ekrānā rāda, kurš slēdzis jāpārslēdz. Kad visi ir
pareizi - turpina startu. Iestatījumi: `SW_STARTUP_CHECK`, `SW3_HOME` (rc_remote config.h).

### Ekrāns un gaisma
- **Ekrāns:** saite, ARM, baterija (sarkana, ja zem 6.6 V), ātrums, režīms, rokas stāvoklis;
  katrai locītavai leņķis un josla (−180…180°; pelēka = nav nulles, sarkana = gala slēdzis); satvērējs.
- **LED:** dzeltena mirgo = nav saites · zaļa = saite, ARM izslēgts · zila = pults vada ·
  violeta = mācīšanas režīms · sarkana = avārija · balts uzplaiksnījums = nospiesta poga.
- **Pīkstulis:** saite atrasta / pazudusi, ARM ieslēgts / izslēgts, katrs pogas spiediens.

### Kalibrācija
ARM izslēgts, turi **nefiksējošo pogu 5 s** (vai BOOT 2 s) → (1) atlaid stikus centrā, īsi nospied pogu →
(2) izkustini visus stikus un potenciometru līdz galiem, īsi nospied pogu. Saglabājas pastāvīgi.

### Atjaunināšana pa Wi-Fi (aizvērts korpuss)
Ieslēdz pulti, **turot nefiksējošo pogu** → ekrānā "ATJAUNINĀŠANA" (roku pults tad nevada).
- **Pirmo reizi:** ar telefonu pieslēdzies tīklam **RobotRoka-Pults** (parole `robotroka123`), atver
  `http://192.168.4.1` un ievadi mājas Wi-Fi nosaukumu un paroli (glabājas tikai pultī).
- **Turpmāk** pults pati pieslēdzas mājas tīklam un ekrānā rāda savu IP. No datora:
  `pio run -e s3_ota -t upload --upload-port <IP>` (ielādē caur pults `/update` lapu ar curl) vai pārlūkā
  `http://<IP>/update` ar failu `firmware/rc_remote/.pio/build/s3/firmware.bin`.
- Izslēdz un ieslēdz pulti, lai izietu.

---

## Drošība

- Programmatūras ESTOP **neaizstāj** fizisku avārijas slēdzi, kas atslēdz motoru barošanu.
  Ieliec sēņveida slēdzi 12/24 V līnijā.
- Kopējs GND starp ESP32 un kontrolieri obligāts.
- Motoru vadus vadi atsevišķi no UART vadiem (kontrolsumma noraida bojātas rindas, bet labāk,
  ja to nav).

# Kontroliera komandu protokols

Teksta rindas, `\n` beigās, 115200 bodi. Viens un tas pats protokols USB portā un ESP32 UART portā.
Lielie/mazie burti nav svarīgi. Atdalītāji: atstarpe, komats vai TAB. `;` sāk komentāru.
Locītavas numurē **1..5** (var rakstīt arī `J1`).

## Atbildes

**Katra rindas komanda, izņemot `JOG`, beidzas ar tieši vienu `ok` vai `err <kods>`.**
Dati (CFG, INFO, ES, TMC, HELP, STATUS) tiek izdrukāti *pirms* šīs rindas. Tātad saimniekdators
(PC programma) var sūtīt nākamo komandu tikai pēc atbildes — tāpat kā Printrun ar Marlin.

| Rinda | Nozīme |
|---|---|
| `ok` | komanda pieņemta |
| `err <kods>` | kļūda (kodi zemāk) |
| `<...>` | statuss |
| `[READY] ...` | kontrolieris tikko startēja (pozīcija nav zināma) |
| `[CFG] ...` | iestatījumi / "ielasīti no atmiņas" vai "noklusējuma" pēc starta |
| `[HOME] J2 ok`, `[HOME] pabeigts`, `[HOME] kluda J2: ...`, `[HOME] partraukts` | HOME gaita |
| `[ENDSTOP] J1 apstajas pie sledza` | slēdzis nospiests, braucot uz to → locītava apstājas (bez avārijas) |
| `[ESTOP]` | avārijas apstāšanās (visiem portiem) |
| `[RUN] sakts X`, `[RUN] pabeigts X`, `[RUN] kluda L5: limit`, `[RUN] apturets (...)` | SD programmas gaita |
| `[INFO]`, `[ES]`, `[TMC]`, `[HELP]` | informācija |

`JOG` vienīgais **nekad neatbild ar `ok`/`err`** (to sūta 10–25x sekundē). Kļūdas gadījumā tas izdrukā
notikumu `[JOG] kods` (piem. `[JOG] alarm`), lai tas nesajauktos ar citu komandu atbildēm.

## Statuss

```
<Idle|P:12.50,-30.25,45.00,0.00,90.00|E:1|Z:11111|S:0-1--|Q:0|G:40>
```
| Lauks | Nozīme |
|---|---|
| `Idle` / `Jog` / `Move` / `Home` / `Alarm` | stāvoklis |
| `P:` | locītavu pozīcijas grādos |
| `E:` | 1 = draiveri ieslēgti |
| `Z:` | katrai locītavai 1 = pozīcija zināma (ZERO/SETPOS/HOME) |
| `S:` | gala slēdži: `-` atslēgts, `0` brīvs, `1` nospiests |
| `Q:` | kustības rindā |
| `G:` | satvērējs, % (0 atvērts, 100 aizvērts) |
| `R:` | tikai kad darbojas SD programma: `12/2` = 12. rinda, 2. cikls; `P12/2` = pauze |

Kustība ir pabeigta, kad **pēc komandas `ok`** pienāk statuss `Idle` ar `Q:0`
(pēc `ok` kustība jau ir rindā, tāpēc agrāks "Idle" nevar tikt sajaukts).

## Reāllaika simboli (strādā arī rindas vidū, bez `\n`, bez atbildes)

| | |
|---|---|
| `!` | **ESTOP**: soļi apstājas uzreiz, rinda iztīrīta, stāvoklis `Alarm` |
| `?` | statuss uzreiz |

## Komandas

| Komanda | Apraksts |
|---|---|
| `JOG v1 v2 v3 v4 v5` | Ātrums % no max (-100..100). Trūkstošās = 0. Jāatkārto vismaz ik 300 ms, citādi bremzē līdz 0. |
| `MOVE a1 a2 a3 a4 a5 [S%]` | Absolūta kustība, grādi. `_` = locītavu neaiztikt. `S50` = 50% ātrums (noklusējums 50). Rindā līdz 8 (RAMPS) / 32 (SKR). |
| `MOVEJ n deg [S%]` | Viena locītava |
| `REL d1 .. d5 [S%]` | Relatīvi pret pēdējo mērķi |
| `STOP` | Bremzē ar normālu paātrinājumu, iztīra rindu, pārtrauc HOME |
| `ESTOP` | Tas pats, kas `!` |
| `RESET` | `Alarm` → `Idle` |
| `ENABLE` / `DISABLE` | Draiveri (kustība tos ieslēdz automātiski). DISABLE → roka var nokrist! |
| `HOME [n]` | Gala slēdžu meklēšana: visas ieslēgtās (pēc `HOME_ORDER`) vai viena. `ok` = sākts. |
| `ZERO [n]` | Pašreizējā pozīcija = 0 (visām vai locītavai n). Tikai stāvot. |
| `SETPOS n deg` | Pašreizējā pozīcija = deg |
| `ES` | Gala slēdžu stāvoklis: `[ES] J1=0 J2=- ...` |
| `GRIP %` | Satvērējs 0..100 |
| `SERVO us` | Servo impulss mikrosekundēs (500..2500), iestatīšanai |
| `AUTO ms` | Automātisks statuss šim portam ik ms (0 = izslēgts, min 50) |
| `CFG` | Izdrukā konfigurāciju |
| `CFG <atslēga> n vērtība` | Maina locītavas iestatījumu (tikai stāvot) — skat. tabulu. Mainot `MOTOR_STEPS`/`MICROSTEPS`/`GEAR`, pozīcija grādos paliek tā pati |
| `CFG GRIP_OPEN us` / `CFG GRIP_CLOSED us` | Satvērēja impulsu robežas |
| `CFG LCD 0\|1` / `CFG SD 0\|1` | Vai ekrāns / SD karte ir pieslēgti (saglabā ar `SAVE`). Noklusējums: RAMPS ekrāns ieslēgts, SKR izslēgts |
| `SAVE` | Saglabā visus iestatījumus kontroliera atmiņā (Mega EEPROM / SKR flash) |
| `LOAD` | Ielādē saglabātos (notiek arī automātiski pēc starta) |
| `FACTORY` | Noklusējumi no `config.h` (atmiņa nemainās līdz `SAVE`) |
| `TMC` / `TMC INIT` | TMC2209 diagnostika / pārkonfigurēt (tikai SKR) |
| `SD` | Programmu faili SD kartē: `[SD] PROG1.TXT` |
| `RUN fails [L]` | Palaiž programmu no SD (`L` = atkārtot bezgalīgi). `RUN` bez argumenta = stāvoklis |
| `PAUSE` / `RESUME` | SD programmas pauze (pēc pašreizējā soļa) / turpināt. `STOP`/`!` programmu aptur |
| `PORTS` | Savienojuma diagnostika: cik baitu/rindu saņemts no USB un ESP porta, kontrolsummas kļūdas |
| `LCD` | Izdrukā ekrāna saturu: `[LCD] \|GATAVS ...\|` |
| `KEY +n` / `KEY -n` / `KEY C` / `KEY K` | Ekrāna pogas no attāluma: griezt, klikšķis, STOP poga |
| `INFO`, `STATUS`, `HELP` | |

### CFG atslēgas

| Atslēga | Nozīme |
|---|---|
| `MOTOR_STEPS` | motora pilni soļi apgriezienā: 200 = 1.8° motors, 400 = 0.9° motors |
| `MICROSTEPS` | 1, 2, 4 … 256 (RAMPS: jāsakrīt ar jumperiem; SKR: ieraksta TMC2209) |
| `GEAR` | reduktora attiecība, piem. `9` (9:1) vai `5.18` |
| `MIN`, `MAX` | programmatūras limiti, ° (jābūt min < max) |
| `SPEED`, `ACCEL` | max ātrums °/s, paātrinājums °/s² |
| `INVERT` | 1 = griezt pretēji |
| `CURRENT` | TMC2209 strāva mA RMS (tikai SKR) |
| `ES` | 1 = gala slēdzis ieslēgts |
| `ES_INV` | 1 = NC slēdzis (normāli slēgts) |
| `HOME_DIR` | -1 / 1 — uz kuru pusi atrodas slēdzis |
| `HOME_POS` | leņķis, ko locītava iegūst slēdža nostrādāšanas punktā |
| `HOME_SPEED` | meklēšanas ātrums °/s (lēnā fāze = ¼) |
| `BACKOFF` | atkāpšanās no slēdža, ° |
| `HOME_ORDER` | secība `HOME` komandā (mazākais pirmais) |

### HOME gaita (katrai locītavai pēc kārtas)
1. Ja slēdzis jau nospiests — vispirms atkāpjas.
2. Brauc uz slēdža pusi ar `HOME_SPEED` (ja nobrauc > diapazons + 20° → `[HOME] kluda`, `Alarm`).
3. Atkāpjas par `BACKOFF`, lēnām (¼ ātruma) pieiet vēlreiz → pozīcija = `HOME_POS`.
4. Atkāpjas par `BACKOFF`, lai slēdzis paliek brīvs.

### Drošības loģika
- Kamēr locītavai nav nulles: `MOVE` → `err not_zeroed`, manuālā kustība (`JOG`) ar 30% ātrumu, bez limitiem.
- `JOG` laikā `Move`/`Home` stāvoklī → `err busy` (vispirms `STOP`). `JOG 0 ...` nekad nav kļūda.
- Mērķis ārpus limitiem → `err limit` (nekas netiek izpildīts).
- Ieslēgts slēdzis nostrādā, braucot uz to (manuāla kustība vai MOVE) → šī locītava uzreiz apstājas, MOVE tiek
  pārtraukts (pārējās locītavas nobremzē), `[ENDSTOP]` ziņa, **bez avārijas un RESET**. Kamēr slēdzis nospiests,
  uz to pusi kustēties nevar; prom no slēdža — uzreiz. SD programma šajā brīdī apstājas.
- **Viens slēdzis abos galos** (piem. J1 griežas gandrīz 360°): ja slēdzim uzbrauc pēc vairāk nekā 10° brīvas
  kustības, braucot **prom** no HOME puses, tas ir otrais gals → arī apstājas, un tad bloķēts ir pretējais virziens
  (atpakaļ braukt var). HOME, stāvot uz slēdža otrajā galā, atteic (`[HOME] kluda J1: uz sledza otra gala`) —
  vispirms nobrauc nost uz vidu. Limitiem jābūt tā, lai `MAX − HOME_POS` < 360° − karodziņa platums.

### Kļūdu kodi
`not_zeroed`, `limit`, `queue_full`, `alarm`, `busy`, `bad_arg`, `unknown_cmd`, `line_too_long`,
`checksum`, `no_tmc`, `tmc`, `no_endstop`, `storage`, `no_sd`, `no_file`

## Kontrolsumma

Rindai var pievienot `*HH` — XOR visiem simboliem pirms `*`, divi heksadecimāli cipari:
```
MOVE 10 20*12
```
ESP32 portā kontrolsumma ir **obligāta** (`ESP_REQUIRE_CHECKSUM`), USB portā — pēc izvēles.
Python: `f"{line}*{functools.reduce(lambda a, c: a ^ ord(c), line, 0):02X}"`

---

# ESP tilts: TCP (ports 23)

Tas pats teksta protokols kā USB (PC programmai caur tīklu: `socket://<IP>:23`). Tilts pats pievieno
kontrolsummu un `ok`/`err` nosūta tikai tam klientam, kurš komandu sūtīja; statuss un `[..]` notikumi — visiem.

# ESP tilts: WebSocket (ports 81)

Visas rindas bez `#` tiek pārsūtītas kontrolierim (ar kontrolsummu), visas kontroliera rindas —
visiem web klientiem. Komandas tiltam:

| Komanda | Atbilde |
|---|---|
| `#info` | `#info bridge=… rc=espnow net=0x1234 ap=… sta=… ssid=… ctrl=1` |
| `#rc` | `#rc type=espnow link=1 armed=0 bank=A estop=0 speed=55 grip=40 rssi=0 batt=7900 pkts=…` (sūta arī pats 2x/s) |
| `#wifi<TAB>ssid<TAB>parole` | Saglabā mājas Wi-Fi un restartē |
| `!` | ESTOP |

Ja pults vada roku, web `JOG` tiek ignorēts un klients saņem `#rc_priority`.

# Radio pakete (pults → roka)

[firmware/common/rc_protocol.h](../firmware/common/rc_protocol.h): `magic, version, type, netId, seq,
ch[6] (-1000..1000), switches, flags (ARMED/ESTOP/CAL), battMv, crc16`. Tilts pieņem tikai paketes
ar savu `RC_NET_ID` un derīgu CRC. Saite pazudusi pēc 300 ms (ESP-NOW) / 600 ms (LoRa) → JOG 0.

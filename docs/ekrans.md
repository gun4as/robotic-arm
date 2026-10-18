# Ekrāns ar grozāmo pogu un SD karti

RepRapDiscount Smart Controller (20×4 teksta LCD, grozāmā poga, "X"/STOP poga, pīkstulis, SD karte) —
tas pats ekrāns, kas bija 3D printerim. Pieslēgšana: [elektronika.md](elektronika.md#ekrāns-reprapdiscount-smart-controller-20x4--ramps).

## Ieslēgšana / izslēgšana

Vai ekrāns un SD karte ir pieslēgti, iestata **PC programmā** (Konfigurācija → Aparatūra) vai
**web lapā** (Konfigurācija) → "Ierakstīt + SAVE". Komandas: `CFG LCD 1`, `CFG SD 1`, `SAVE`.
Noklusējums: RAMPS — ieslēgts, SKR — izslēgts (SKR bez pieslēgta ekrāna pogas pins "peld",
tāpēc to ieslēdz tikai, kad ekrāns tiešām pieslēgts).

Kura plate pieslēgta, rāda PC programma (zilā zīme augšā, piem. "RAMPS 1.4 · fw 0.1.0") un web lapa.
To ziņo pats firmware (komanda `INFO`) — RAMPS un SKR ir dažādi procesori, katram savs firmware,
tāpēc sajaukt nevar.

## Statusa ekrāns

```
GATAVS    N11111 M1        stāvoklis, N = kurām locītavām ir nulle, M = motori ieslēgti
J1   30.0  J2   20.0       pozīcijas grādos
J3  -15.0  J4    0.0
J5    0.0  G100% SD        satvērējs, SD = karte ielikta
```
Stāvokļi: `GATAVS`, `JOGS`, `KUSTIBA`, `HOME...`, `PAUZE`, `AVARIJA!` (spied RESET).
Kad darbojas programma no SD: `KUSTIBA   L12 c2` (rinda 12, 2. cikls).
Notikumi (HOME, gala slēdzis, kļūdas) uz 3 s parādās pirmajā rindā.

Ekrānā nav garumzīmju — HD44780 ekrāns tās nemāk.

## Vadība

- **Griež** pogu — kustas pa sarakstu / maina vērtību.
- **Spied** pogu — izvēlas. No statusa ekrāna atver izvēlni.
- **X / STOP poga** — avārijas apstāšanās (RAMPS). Uz **SKR** šī poga ir pieslēgta plates RESET,
  tāpēc plate pārstartējas (motori atslēdzas, pozīcija jāatjauno ar HOME/ZERO).
- Pēc 30 s bez darbības ekrāns atgriežas statusā.

## Izvēlne

| Punkts | Ko dara |
|---|---|
| < Atpakal | uz statusa ekrānu |
| Progr.: pauze / turpinat, Progr.: apturet | tikai kad darbojas programma |
| Kustinat locitavu | izvēlies J1..J5 → griežot pogu, locītava kustas pa soļiem. Strādā arī **bez nulles** (ar ierobežotu ātrumu) — tā roku var nolikt sākuma pozā un tad "Nulle visam". |
| Solis | 0.1° → 1° → 5° → 10° (katrs klikšķis maina) |
| Atrums | 5…100 % kustībām no ekrāna |
| Satverejs | 0…100 %, mainās uzreiz |
| Programma no SD | *.TXT faili kartē → "Palaist vienreiz" vai "Atkartot bezgaligi" |
| HOME visas | gala slēdžu meklēšana (ja iestatīti) |
| Nulle visam | pašreizējā poza = 0° (ar apstiprinājumu) |
| Motori | ieslēgt / atslēgt (atslēgšana ar apstiprinājumu — roka var nokrist) |
| RESET (avarija) | pēc avārijas apstāšanās |
| Info | firmware versija, plate, soļi/° |

## Programmas no SD kartes

1. PC programmā sastādi programmu → **Eksportēt tekstu** → nosaukums **līdz 8 burtiem + .TXT**
   (piem. `PROG1.TXT`, bez atstarpēm un garumzīmēm).
2. Iekopē failu SD kartes saknes mapē (FAT32), ieliec karti ekrānā.
3. Ekrānā: izvēlne → Programma no SD → fails → Palaist.

Faila saturs ir parastas kontroliera komandas, katra savā rindā (var rakstīt arī ar roku):
```
; komentārs
HOME
MOVE 30 20 -15 0 0 S60      ; visas locītavas uzreiz, 60 % ātrums
GRIP 100
WAIT 500                    ; pauze ms
MOVEJ 2 45 S30
REL 10 _ _ _ _ S50
```
Pēc kustības komandas programma gaida, kamēr roka apstājas. Kļūda (piem. mērķis ārpus limita,
nav nulles) → programma apstājas un ekrānā parādās `RUN kluda L5: limit` (5. rinda).

To pašu var vadīt caur USB/Wi-Fi: `SD` (saraksts), `RUN PROG1.TXT`, `RUN PROG1.TXT L` (atkārtot),
`PAUSE`, `RESUME`, `STOP`.

Pagaidām SD karte strādā ar **RAMPS**. Uz SKR ekrāns un izvēlne strādā, SD — vēlāk.

## Virtuālā roka
Virtuālajai rokai "SD karte" ir mape `sdcard` tās darba mapē (PC programmas datu mapē:
`%APPDATA%\RobotRoka\RobotRokaPC\sdcard`). Tur jau ir `DEMO.TXT`. Ekrāna saturu var redzēt ar
komandu `LCD`, pogas spiest ar `KEY +1`, `KEY -1`, `KEY C` (klikšķis), `KEY K` (STOP).

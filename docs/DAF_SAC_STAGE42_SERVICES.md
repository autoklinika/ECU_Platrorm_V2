# ECU Platform V2 — DAF SAC Stage 4.2: parametry i DTC

Data: 2026-10-07
Gałąź: `stage4.2/sac-parameters-dtc`
Status: **SOFTWARE + PHYSICAL READ PASS / DTC CLEAR PHYSICAL NOT RUN / DEDICATED CI PENDING**
Zakres: **BEZ API, BEZ GUI, BEZ zmian CORE V2/Bench Runtime**.

## Podział odpowiedzialności

- `src/dut_profiles/daf_sac/service_program.*` — program usług SAC, nieblokująca maszyna stanów UDS oraz odrębny `PressureMonitor` (ICanFrameSink przez Core V2). Wszystkie formaty, DID i skalowania są lokalne dla SAC.
- `src/applications/daf_sac/application.*` — wybór operacji i orkiestracja istniejącą sesją Bench Runtime: start, service, safe-stop, deadline, recovery, rezerwacja CAN i zasobu DUT.
- `src/platform/linux/v2` — istniejący adapter fizyczny SocketCAN. Żaden sprzęt nie jest na stałe wpisany do profilu ani modułu aplikacji.
- `tests/daf_sac_stage42_read_probe.cpp` oraz `scripts/run_stage42_daf_sac_read_gate.sh` — operatorowy fizyczny dowód odczytowy. Nie ma możliwości wywołania ClearDiagnosticInformation przez ten runner.

Rewizja DUT Profile: **2** (wcześniejsza identyfikacja na fizycznym SAC
została przyjęta z rewizją 1). Rewizja 2 deklaruje już oba odbiorniki CAN:
dokładny `0x18DAF930 / 0x1FFFFFFF` dla UDS i maskowany
`0x18FEAE30 / 0x03FFFFFF` dla ciśnienia. Ta zmiana nie modyfikuje
żadnego kontraktu CORE V2 ani Bench Runtime. Rewizja 2 została potwierdzona dwoma odczytami na fizycznym SAC
(2026-10-07), z zastrzeżeniem niedostępnych wartości ciśnienia.

## Funkcje

| Funkcja | Mechanizm | Status dowodowy |
|---|---|---|
| Identyfikacja SAC | 22 F190/F188/F192 | Fizycznie PASS w Stage 4.1 |
| Ciśnienie 1 i 2 | Pasywnie PGN `65198` / `0xFEAE`, SA=`0x30`; bajty 2,3 × 0,08 bar | Fizyczny RX PASS; dwa pola niedostępne (`0xFE`) — skalowanie dla rzeczywistej wartości jeszcze niepotwierdzone |
| Napięcie permanentne i ignition | UDS `22 FE96`, bajty 7–8 i 9–10 odpowiedzi `62 FE 96 ...`, big-endian /10 V | Fizyczny odczyt PASS: 28,1 V / 28,1 V |
| DTC | UDS `10 03` potem `19 02 FF` (dowolna maska ustawiana jawnie) | Fizyczny odczyt PASS: 12 rekordów, maska dostępności `0x8B` |
| Kasowanie DTC | UDS `10 03` potem `14 FF FF FF`; pozytywna odpowiedź `54` | Implementacja/testy offline PASS, **obsługa i warunki OEM nieweryfikowane fizycznie** |

Wszystkie działania z wyjątkiem kasowania DTC są odczytowe, jednak przejście
w rozszerzoną sesję `10 03` **zmienia stan sesji diagnostycznej**.
Po zakończeniu aplikacja zamyka transport; nie obiecuje automatycznego
`10 01` ani resetu ECU. Nie wysyłamy TesterPresent w tle.

## Semantyka wartości

W zaobserwowanej fizycznie ramce `0x18FEAE30` wystąpiły
`FF FF FE FE FF FF FF FF`. Zatem oba pola ciśnienia mają kod
**niedostępności**, NIE wartość `20,32 bar`. Dekoder rozróżnia dla
każdego kanału: `received`, `valid`, wartość. Raw `0xFB..0xFF`
jest nieprawidłowy. Z ramki z innym SA, PGN, skróconej lub innego typu
nie wyciągamy pomiaru.

Odczyt FE96 ma osobny status `valid`, format i zakres 0..60 V
(jest to konserwatywna kontrola dla sterownika niskonapięciowego,
a nie zatwierdzona przez producenta tolerancja elektryczna).
Syntetyczne `FEFE`/`FFFF` i krótkie odpowiedzi są odrzucane.

Pomiar parametrów zaczyna jedną sesję Bench Runtime, wysyła tylko
`22 FE96`, jednocześnie odbiera J1939 PGN przez osobny filtr Core V2.
Po otrzymaniu napięcia czeka **maksymalnie 800 ms** na broadcast ciśnienia.
Niedostępne ciśnienie nie powoduje fałszywego sukcesu pola ciśnienia,
ale samo napięcie może być odczytane poprawnie.

## DTC

`ReadDTCInformation reportDTCByStatusMask` wymaga w evidence legacy
`10 03`, a następnie `19 02 <mask>`. Odpowiedź `59 02` zawiera
`statusAvailabilityMask` i rekordy `3 bajty DTC + 1 bajt statusu`.
Maksymalna pojemność wynosi 128 rekordów; zbyt długa lub niepełna
odpowiedź jest błędem, bez częściowej listy. NRC i błędy transportowe
zostają zachowane diagnostycznie.

**Kasowanie** jest osobną, świadomie zatwierdzoną operacją o konsekwencjach
serwisowych. Przed dopuszczeniem wywołania wymagane są:
1. Poprawny, zakończony odczyt DTC z tego profilu.
2. Jawny wniosek `prepare_clear_dtcs()`, zwracający jednorazowe wyzwanie
   z profilem i liczbą rozpoznanych DTC.
3. Osobne `clear_dtcs(challenge, true)`. Brak potwierdzenia, fałszywy token,
   inny profil albo użyty token => **bez transmisji**.
4. Po pozytywnej odpowiedzi `54` oznaczamy operację jako
   `acknowledged`, NIE jako gwarancję, że ECU ma teraz zero błędów.
   Poprzednia lista jest nieważna i trzeba wykonać nowy odczyt DTC.
5. Po timeout/NRC wynik kasowania jest **niepewny**. Brak automatycznego
   ponawiania `0x14` i brak obchodzenia zabezpieczeń.

Powyższy mechanizm chroni przed przypadkowym wywołaniem w kodzie.
Nie stanowi mechanizmu uwierzytelniania przyszłych klientów sieciowych.
**Żadna operacja fizycznego kasowania DTC nie została uruchomiona
w tym etapie.** Przed testem kasowania na realnym SAC wymagana jest
osobna decyzja operatora (z uwzględnieniem utraty danych diagnostycznych).

## Testy i zależności

`bash scripts/validate_daf_sac_application.sh`:
- testy Linux Debug / Release / Generic (bez Linux) / ASan+UBSan;
- testy neutralnego Bench i DUT Profile oraz SAC;
- identyfikacja bez regresji;
- symulowane FE96, ciśnienie poprawne i `FE/FF` niedostępne;
- DTC read (z `10 03`), NRC, błędne rekordy, zakres i status;
- DTC clear (z `10 03`, `14 FF FF FF`), brak wywołania przed
  dwustopniowym potwierdzeniem, jednorazowy token, NRC po kasowaniu;
- brak niezwolnionych zasobów i wyłączenie CAN przy błędzie;
- tryby błędów CAN, timeout, recovery i ponowny odczyt.

`cmake --build build/daf-sac-app-linux --target ecu_daf_sac_stage42_read_probe`
kompiluje fizyczny runner z Core V2 Linux SocketCAN, **nie uruchamia go**.
`ctest` również nigdy nie wykonuje fizycznych transmisji.

## Test fizyczny nowych ODCZYTÓW — operator

Po podłączeniu poprawnie zasilonego SAC, jeśli CAN jest DOWN:

```bash
cd ~/ECU_Platrorm_V2
sudo ./scripts/run_stage42_daf_sac_read_gate.sh parameters
sudo ./scripts/run_stage42_daf_sac_read_gate.sh dtc
```

Każdy test konfiguruje CAN na 250 kbit/s Classic, działa przy normalnym
ACK i zawsze wraca do DOWN. Tryb `dtc` powoduje wejście w sesję
diagnostyczną `10 03`, ale nie kasuje błędów.

`clear` nie jest żadną opcją tego skryptu; nie należy dopisywać
fizycznego erase do read-only smoke. Po odczycie trzeba ocenić,
czy ECU obsługuje `14 FF FF FF` i jakie ma warunki bezpieczeństwa.

## Bramy release

- Program/testy lokalne Stage 4.2: osobno raportować wynik.
- Test fizyczny odczytu FE96 / PGN / DTC: **PASS** 2026-10-07 (dowód poniżej).
- Fizyczne kasowanie DTC: **NOT RUN / OEM UNVERIFIED**; zachować 12 odczytanych DTC jako materiał do dalszej analizy.
- WebGUI/API: **NIE ROBIMY w Stage 4.2**.
- CI GitHub dla nowego zakresu: wymaga zatwierdzonego workflow; aktualny
  token nie ma uprawnienia edycji workflow. Zmiany CI pozostają w patchu
  `docs/DAF_SAC_STAGE4_CI_WORKFLOW.patch`.
- CORE V2, Bench Runtime, produkcyjny `main`: **bez zmian**.
- Merge do `main`: wyłącznie po Twojej wyraźnej zgodzie.

## Physical validation evidence — 2026-10-07

Source: the operator's console output for both commands
`sudo ./scripts/run_stage42_daf_sac_read_gate.sh parameters` and
`sudo ./scripts/run_stage42_daf_sac_read_gate.sh dtc` on the real DAF SAC.

**Both operator-triggered physical read gates PASS** at Classic CAN 250 kbit/s,
normal ACK mode, 29-bit addressing and DUT Profile revision 2.
The output pasted into the conversation had the two sessions interleaved;
the measurements below were assigned using their individual
`Mode: parameters` / `Mode: dtc` and `SAC_STAGE42_READ_GATE` markers.

### Voltage and pressure

- `SAC_FE96_PERMANENT_V=28.1`
- `SAC_FE96_IGNITION_V=28.1`
- `SAC_PGN_FEAE_OBSERVED=1`
- `SAC_PRESSURE1_BAR=UNAVAILABLE`
- `SAC_PRESSURE2_BAR=UNAVAILABLE`
- `SAC_BENCH_RESOURCE_LEASES=0`
- `SAC_STAGE42_READ_PHYSICAL=PASS`
- `SAC_STAGE42_READ_GATE=PASS mode=parameters`
- `SAC_STAGE42_LINK_CLEANUP=DOWN`

Measured kernel counters during this single active test:
RX packets `12805 -> 12810` (+5), TX `12 -> 14` (+2);
RX errors `0 -> 0`, TX errors `0 -> 0`, no new dropped TX.

Both pressure channels have unavailable raw values, consistent with the earlier
passively captured PGN payload `FF FF FE FE FF FF FF FF`. This proves correct
handling of absent sensor values, **not pressure scaling against a measured
physical pressure**. A known-pressure test will be needed for full calibration.

### DTC read

- `SAC_DTC_AVAILABILITY_MASK=0x8b`
- `SAC_DTC_COUNT=12`
- `SAC_BENCH_RESOURCE_LEASES=0`
- `SAC_STAGE42_READ_PHYSICAL=PASS`
- `SAC_STAGE42_READ_GATE=PASS mode=dtc`
- `SAC_STAGE42_LINK_CLEANUP=DOWN`

Received 12 raw three-byte DTC identifiers with their UDS status bytes:

| DTC (hex, 24-bit) | Status byte |
|---|---|
| `3A0002` | `0A` |
| `DFF7E9` | `8B` |
| `DCF7E9` | `8B` |
| `DEF7E9` | `8B` |
| `D9F7E9` | `8B` |
| `DBF7E9` | `8B` |
| `DAF7E9` | `8B` |
| `DDF7E9` | `8B` |
| `77F9E5` | `8B` |
| `74F9E3` | `8B` |
| `08F9E2` | `8B` |
| `09F9E2` | `89` |

UDS statusAvailabilityMask `0x8B` indicates supported bits 0, 1, 3 and 7.
Observed status `0x8B`: testFailed, testFailedThisOperationCycle,
confirmedDTC and warningIndicatorRequested. Status `0x89`: testFailed,
confirmedDTC and warningIndicatorRequested. Status `0x0A`:
testFailedThisOperationCycle and confirmedDTC, without current testFailed bit.
These are ISO-14229 status interpretations only; **no OEM-specific DAF fault
names, causes or required repair actions have been validated**.
A `confirmedDTC` does not imply that the fault is currently present.

Measured kernel counters for this single active DTC test:
RX packets `12814 -> 12829` (+15), TX `14 -> 17` (+3);
RX errors `0 -> 0`, TX errors `0 -> 0`, no new dropped TX.

Because this read included `10 03` (extended diagnostic session), it changed
the ECU diagnostic session temporarily. No DTC clearing, write, flash,
security access or actuator command was executed. Neither diagnostic session
restoration nor physical DTC clearing is asserted by this test.

The CM5 was independently inspected after these tests:
`can0 state DOWN / CAN STOPPED`, clocked at the configured 250 kbit/s.

### Acceptance boundary

**Stage 4.2 physical READ gate: PASS** (UDS voltage, presence/NA handling
of pressure broadcast, DTC query and parsing).
**DTC clear: NOT TESTED on real ECU.** Do not automatically issue
`14 FF FF FF` to a unit with 12 recorded DTCs; preserve evidence
and obtain an explicit operator decision before any destructive trial.
Full analog pressure scaling and DAF OEM fault-code interpretation remain
separate follow-up verification tasks.

The VIN was not written into this public evidence file.

## Stage 4.2 — opcja kasowania DTC z terminala (operator CLI)

Gałąź: `stage4.2/sac-dtc-clear-cli`. To **osobny, fizyczny**
punkt wejścia; wcześniejsze `run_stage42_daf_sac_read_gate.sh`
pozostaje wyłącznie do odczytów i nadal nie oferuje `clear`.
Ciśnienie pozostaje do późniejszej walidacji — bez zmian w dekoderze.

Po świadomej decyzji operatora uruchomienie na ECU Platform:

```bash
cd ~/ECU_Platrorm_V2
sudo ./scripts/run_stage42_daf_sac_clear_gate.sh
```

Jest to potencjalnie **destrukcyjna operacja serwisowa**, która usuwa
informacje diagnostyczne zapisane w SAC. Narzędzie wymaga interaktywnego
terminala (TTY), odmawia uruchomienia przez stdin/pipe i nie działa
automatycznie ani podczas CI.

Przebieg w jednej aplikacji SAC, przez te same neutralne Bench Runtime
oraz Core V2 co już fizycznie sprawdzone odczyty:

1. Konfiguracja `can0` Classic CAN 250000 normal i start sesji Bench.
2. UDS `10 03` -> `19 02 FF`: ponowne odczytanie pełnej listy DTC,
   ich 24-bitowych kodów, statusów i maski dostępności.
3. Trwały lokalny zapis listy PRZED kasowaniem w katalogu
   `~/.ecu-platform-v2/daf-sac/dtc-clear/`, z dostępem
   tylko dla operatora (katalog `0700`, plik `0600`). Plik zapisany
   i zsynchronizowany przed wystawieniem możliwości kasowania.
   Jest to *kopia kodów/statusów*, a nie całej diagnostyki ECU
   (nie obejmuje freeze frames / rozszerzonych danych OEM).
4. Operator w ciągu 120 s wpisuje dokładnie tekst podany na ekranie,
   np. `KASUJ SAC 12 DTC`. Niepoprawny tekst, brak TTY, brak zapisu
   lub brak potwierdzenia => **zero żądań ClearDiagnosticInformation**.
5. Aplikacja konsumuje jednorazowe wyzwanie powiązane z bieżącym
   profilem SAC i liczbą właśnie odczytanych DTC. Dopiero teraz
   wykonuje `10 03` -> `14 FF FF FF`. Nie ma automatycznego retry.
6. Gdy przyjdzie poprawne `54`, operator widzi
   `SAC_DTC_CLEAR_UDS_ACK=YES`; to **potwierdzenie przyjęcia
   żądania**, nie dowód braku usterek.
7. Następuje ponowny odczyt `10 03` -> `19 02 FF`: drukowana
   i archiwizowana jest lista DTC pozostałych lub ponownie aktywnych.
   Błąd ponownego odczytu jest raportowany osobno, bez ukrywania ACK.
8. Każde zakończenie (również NRC, błąd, anulowanie) przełącza
   systemowe `can0` na DOWN. Stan błędu z nieznanym efektem
   kasowania **nie powoduje automatycznego ponowienia**.

Logi w `~/.local/state` nie są kopiowane do publicznego repozytorium.
Nie zmieniono CORE V2, ogólnego Bench Runtime, API ani WebGUI.

### Walidacja bez kasowania fizycznego

- Linux builder `ecu_daf_sac_stage42_clear_probe`: kompilacja PASS.
- `ecu.sac.clear_operator`: PASS — dokładna fraza operatora,
  zapis oryginalnych DTC z uprawnieniami `0600`, odmowa użycia
  niebezpiecznego katalogu i ochrony przed nadpisaniem istniejącego
  archiwum.
- `DTC_CLEAR_NONINTERACTIVE_DENIAL=PASS`: CLI uruchomione bez TTY
  odrzuca operację jeszcze przed otwarciem CAN.
- Istniejące testy SAC: syntetyczne `10 03`/`14`/`54`,
  NRC, timeouty, błędne odpowiedzi, rezerwacje, stop/recover.
- Testy portable Debug/Release/Generic/ASan-UBSan: PASS.
- Na rzeczywistym SAC: **kasowania nie uruchamiano**.
  Potwierdzony fizyczny odczyt 12 DTC pozostaje niezniszczony.
- Dedykowane CI Stage 4.2 nadal wymaga zaakceptowanego patcha
  workflow; nie zmienia się `main`.

Fizyczna akceptacja procedury kasowania wymaga przyszłego
świadomego uruchomienia przez operatora i obserwacji odpowiedzi ECU.

### Korekta uprawnień archiwum DTC — 2026-10-07

Pierwsze ręczne wywołanie opcji kasowania zakończyło się przed włączeniem
CAN: `mkdir: cannot create directory /home/ecu/.local/state/ecu-platform/daf-sac: Permission denied`.
Zdalny audyt wykazał, że istniejące `~/.local/state` oraz
`~/.local/state/ecu-platform` mają właściciela `root:root` i tryb `0755`.
To stan istniejącego katalogu innych funkcji systemu, którego nie zmieniamy.

Poprawiony skrypt tworzy archiwum wyłącznie jako konto operatora w osobnym
`~/.ecu-platform-v2/daf-sac/dtc-clear`, ustawiając `0700` wszystkim nowym
katalogom. Odrzuca ścieżki będące symlinkami i sprawdza zapis przed
konfiguracją CAN. Archiwum nadal powstaje z uprawnieniami `0600`
i jest synchronizowane na dysk przed zaoferowaniem kasowania.

Odmowa dostępu podczas pierwotnego przygotowania katalogu nastąpiła przed
wszelką transmisją CAN — **nie skasowano żadnych błędów SAC**.

## Stage 4.2 — niepewny wynik fizycznego kasowania (2026-10-07)

Operator potwierdził usunięcie po odczycie 12 DTC. Żądanie kasowania zostało
podjęte, lecz nie nadeszła potwierdzona odpowiedź `54`. Zgłoszono
`SAC_DTC_CLEAR_OUTCOME=UNKNOWN`, `uds=6` (UDS timeout P2), `nrc=0`,
`can=0`. Nie ma podstaw do twierdzenia, że DTC skasowano albo że kasowanie
nie zadziałało. Ponownego `14 FF FF FF` NIE wysłano.

Lokalne prywatne archiwum ma tryb `0600`, zawiera 12 kodów/statusów oraz
`CLEAR_OUTCOME=UNKNOWN`; `can0` po próbie jest DOWN.

Korekta po audycie: tylko dla potwierdzonego przez operatora `0x14`
klient SAC ma minimum **3000 ms** P2 i **5000 ms** P2*, przy zachowaniu
większych czasów zgłoszonych przez ECU. Pozostałe usługi zachowują swoje
oryginalne timingi. Ta zmiana to tolerancja czasu oczekiwania klienta,
nie dowód poprawności/spełnienia czasów OEM. Testy obejmują opóźnioną
odpowiedź `54` po 650 ms i całkowity brak odpowiedzi po 3000 ms;
po braku odpowiedzi wynik pozostaje **UNKNOWN** bez automatycznego retry.

Dodatkowo skrypt fizycznego kasowania blokuje ponowną próbę, jeśli
zobaczy nierozstrzygnięty `CLEAR_OUTCOME=UNKNOWN` w dotychczasowym
archiwum — sprawdzenie następuje przed włączeniem CAN.

**Następny dozwolony test fizyczny: WYŁĄCZNIE odczyt**:

`sudo ./scripts/run_stage42_daf_sac_read_gate.sh dtc`

Dopiero po porównaniu aktualnej listy z wcześniej archiwizowaną
podejmujemy odrębną decyzję o ewentualnej nowej próbie. Taka lista
nie pozwala jednoznacznie odróżnić „nie skasowano” od „skasowano,
ale usterki natychmiast powróciły”. Nie kasować archiwum.

## DTC re-read after uncertain ClearDiagnosticInformation — 2026-10-07

Źródło: wynik operatora z fizycznego SAC przez **stały agent**
(`python3 scripts/ecu_bench.py sac-dtc`), bez sudo i bez `0x14`.
`ECU_BENCH_AGENT_STATUS=PASS`,
`SAC_STAGE42_READ_PHYSICAL=PASS`,
`SAC_BENCH_RESOURCE_LEASES=0`,
`CAN0_CLEANUP=DOWN`. Ilość rekordów: **12 przed i 12 po**.

Porównano z prywatnym archiwum
`~/.ecu-platform-v2/daf-sac/dtc-clear/sac-dtc-1791405524-408769.txt`.
Archiwum zachowuje `CLEAR_OUTCOME=UNKNOWN`. **Nie wolno utożsamiać
odczytu następczego z potwierdzeniem wykonanego kasowania** — brak
pozytywnej odpowiedzi `0x54` w poprzedniej sesji.

| DTC (hex, 24-bit) | Status przed | Status po |
|---|---|---|
| `3A0002` | `0A` | `02` |
| `DFF7E9` | `8B` | `8B` |
| `DCF7E9` | `8B` | `8B` |
| `DEF7E9` | `8B` | `8B` |
| `D9F7E9` | `8B` | `8B` |
| `DBF7E9` | `8B` | `8B` |
| `DAF7E9` | `8B` | `8B` |
| `DDF7E9` | `8B` | `8B` |
| `77F9E5` | `8B` | `8B` |
| `74F9E3` | `8B` | `8B` |
| `08F9E2` | `8B` | `8B` |
| `09F9E2` | `89` | `89` |

W `3A0002` zmienił się `0x0A -> 0x02` (wyzerowany bit 3
`confirmedDTC`); bit 1 `testFailedThisOperationCycle` pozostaje
ustawiony. Pozostałe 11 kodów i ich statusów są identyczne.
To nie dowodzi ani powodzenia, ani niepowodzenia wcześniejszego
`0x14`; możliwa jest ponowna rejestracja trwających usterek.

**Bramka następnego etapu:** wstrzymać kolejne kasowanie.
Dalsza analiza: interpretacja kodów OEM, źródłowe warunki SAC
i ewentualna pasywna obserwacja odpowiedzi na osobnej, autoryzowanej
próbie w przyszłości. Nie zmieniono Core V2, Bench Runtime ani
operacyjnego zakresu agenta.

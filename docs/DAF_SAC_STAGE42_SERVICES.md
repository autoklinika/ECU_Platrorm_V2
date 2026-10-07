# ECU Platform V2 — DAF SAC Stage 4.2: parametry i DTC

Data: 2026-10-07
Gałąź: `stage4.2/sac-parameters-dtc`
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
żadnego kontraktu CORE V2 ani Bench Runtime. Rewizja 2 wymaga osobnego
odczytowego potwierdzenia na fizycznym SAC.

## Funkcje

| Funkcja | Mechanizm | Status dowodowy |
|---|---|---|
| Identyfikacja SAC | 22 F190/F188/F192 | Fizycznie PASS w Stage 4.1 |
| Ciśnienie 1 i 2 | Pasywnie PGN `65198` / `0xFEAE`, SA=`0x30`; bajty 2,3 × 0,08 bar | Poprawny odbiór PGN potwierdzony fizycznie; skala z legacy, pomiar ciśnienia do weryfikacji |
| Napięcie permanentne i ignition | UDS `22 FE96`, bajty 7–8 i 9–10 odpowiedzi `62 FE 96 ...`, big-endian /10 V | Software/testy PASS, fizyczny odczyt czeka |
| DTC | UDS `10 03` potem `19 02 FF` (dowolna maska ustawiana jawnie) | Software/testy PASS, fizyczny odczyt czeka |
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
- Test fizyczny odczytu FE96 / PGN / DTC: **PENDING** do wyniku operatora.
- Fizyczne kasowanie DTC: **NOT RUN / OEM UNVERIFIED**.
- WebGUI/API: **NIE ROBIMY w Stage 4.2**.
- CI GitHub dla nowego zakresu: wymaga zatwierdzonego workflow; aktualny
  token nie ma uprawnienia edycji workflow. Zmiany CI pozostają w patchu
  `docs/DAF_SAC_STAGE4_CI_WORKFLOW.patch`.
- CORE V2, Bench Runtime, produkcyjny `main`: **bez zmian**.
- Merge do `main`: wyłącznie po Twojej wyraźnej zgodzie.

# ECU Platform V2 — DAF SAC jako pierwsze ECU aplikacji

Data: 2026-10-07
Gałąź: `app/daf-sac-first-ecu`
Zakres: projekt docelowy oraz implementacja pionowego przekroju Stage 4.0.
Status: Stage 4.0 i 4.1 PASS; Stage 4.2 = parametry, odczyt i obsługa kasowania DTC, bez API/WebGUI. Odczyty parametrów i DTC potwierdzone fizycznie 2026-10-07; kasowanie DTC pozostaje nieweryfikowane na realnym sterowniku.

## 1. Nieprzekraczalne granice

CORE V2 jest stabilnym, niezależnym od OS i sprzętu silnikiem transportów,
protokołów, arbitrażu i bezpieczeństwa. Nie zawiera marki DAF.

Bench Runtime jest wspólnym właścicielem sesji jednego badanego urządzenia,
rezerwacji zasobów, cyklu życia, zatrzymania, watchdog i obserwowalności.
Nie przechowuje DID/PGN ani semantyki DAF.

DUT Profile opisuje CAN (250 kbit/s Classic CAN 29-bit), adresowanie, wymagane
zasoby i protokoły. SAC posiada osobną implementację programu UDS
`IdentificationProgram`, bez sterownika CAN i bez zależności od Linux.

`src/applications/daf_sac` to kompozycja: łączy istniejący program SAC z
Bench Session, Host Service, planem DUT i Core CanBusRuntime. Nie zawiera
obsługi sprzętu, Qt, HTTP lub cyklicznego wątku.

Adapter Linux SocketCAN i fizyczny CLI proof należą wyłącznie do osobnej
warstwy platformowej i uruchomieniowej.

Przy dodaniu nowego ECU/EGR/VGT domyślnie zmienia się jego DUT Profile oraz
moduł funkcjonalny. Bench Runtime zmieniamy wyłącznie dla nowej potrzeby
wspólnej, CORE V2 wyłącznie dla potwierdzonej luki architektonicznej z
pełnym re-gate. Żaden automatyczny merge do produkcyjnego main.

## 2. Pionowy przekrój

```text
WebGUI (etap przyszły) -> autoryzowany backend API (etap przyszły)
                                    |
                                    v
                    ECU application: DAF SAC (Stage 4.0)
                    - katalog dostępnych operacji
                    - identyfikacja i bezpieczne DTO stanu
                    - jawne start/stop/recover
                    - absolutny limit operacji: 10 s
                                    |
                                    v
                 BenchSessionHostRuntime (watchdog)
                                    |
                                    v
                       BenchSession (zasoby)
                                    |
                                    v
                   BenchEndpoint: adapter aplikacyjny
               /                                         \
      CanBusRuntime                                   DUT Profile
       + ISO-TP                                   IdentificationProgram
            \                                            /
               UDS Client / diagnostic transport
                                    |
                            ICanDriver (platform)
                                    |
                        Linux SocketCAN -> can0
```

Kolejność: rejestracja profilu -> wybór `profile_id` -> rejestracja DUT ->
wiążący fizyczne zasoby `DutProfileBinding` -> walidacja planu i sesji ->
rezerwacja zasobów przez Bench -> otwarcie CAN podczas `prepare()` ->
`activate()` -> bounded poll + program UDS podczas `service()` ->
`safe_stop()` -> `stop()` -> zwolnienie lease zasobów.

Kod aplikacyjny nie wywołuje bezpośrednio `try_send` ani `try_receive`.

## 3. Udowodniony zakres SAC

| Operacja | Dostępność Stage 4.0 | Mechanizm |
|---|---|---|
| Identyfikacja VIN | TAK, read-only | UDS 0x22 F190 |
| Identyfikacja SW | TAK, read-only | UDS 0x22 F188 |
| Identyfikacja HW | TAK, read-only | UDS 0x22 F192 |
| Stop/Recovery | TAK | Bench lifecycle |
| Odczyt DTC | TAK — Stage 4.2 fizyczny PASS | UDS 10 03 → 19 02 FF; 12 rzeczywistych rekordów |
| Ciśnienie | TAK — Stage 4.2 w kodzie | Pasywne J1939 PGN 65198, wartości FE/FF są niedostępne |
| Napięcie zasilania | TAK — Stage 4.2 fizyczny PASS | UDS 22 FE96; 28,1 V permanent i 28,1 V ignition |
| Kasowanie DTC | TAK — testowo i z potwierdzeniem | UDS 10 03 -> 14 FF FF FF, brak zgody na automatyczne fizyczne wykonanie |
| Testy wyjść/aktywatory | NIE | Brak zweryfikowanej procedury oraz warstwy kontroli energii |
| Flash/programowanie | NIE | Poza zakresem pierwszego etapu |

Katalog `kOperationCatalog` jawnie oznacza operacje niedostępne. Nie wolno
włączać funkcji na podstawie samej obecności DID/PGN w materiałach legacy.

Wybrany i potwierdzony profil: `0xDAF00025` przy 250000 bit/s; 500000 bit/s
zostaje w typie DUT Profile jako wariant historyczny, lecz nie jest
udostępniany pierwszej aplikacji na tym egzemplarzu.

## 4. Kontrakt cyklu życia i bezpieczeństwa

Stan aplikacji: `unconfigured -> ready -> identifying -> identified`.
Błąd przechodzi do `faulted`. Operacje aktywne mają osobne `stop`
i `recover`. Wznawianie po błędzie wymaga jawnego polecenia.

`BenchSession` rezerwuje dwa zasoby: logiczny DUT i fizyczny CAN; nie
dopuszcza drugiej rezerwacji. Podstawowy profil nie wymaga lokalnego
sterownika zasilania, ignition lub wake: SAC jest zasilony zewnętrznie
przez operatora, nie przez ten program. Nie obiecujemy automatycznego
power-off bez działającej warstwy elektrycznej.

`BenchEndpoint` jest jedyną kompozycją otwierającą i zamykającą
`CanBusRuntime` w sesji. Czyści faulted CAN przez świadomy `recover`
i zachowuje status ostatniego błędu protokołu. Krok `service()` pobiera
maksymalnie osiem ramek CAN. Deklarowany budżet wykonania obejmuje
maksymalne czasy sterownika, callbacks oraz obsługi programu.

Host ma osobny watchdog braku wywołań serwisowych (w fizycznym runnerze:
250 ms). Aplikacja nakłada maksymalny czas operacji odczytu: 10 s.
Oba mechanizmy są software'owe i zależą od pracy procesu/schedulera;
nie zastępują niezależnego sprzętowego wyłącznika awaryjnego.

Po zakończeniu `IdentificationProgram.stop()` zeruje jego wewnętrzny
wynik, dlatego aplikacja przed stop kopiuje rekord do pamięci sesji,
a następnie weryfikuje zwolnienie zasobów. Rekord jest zerowany przy
kolejnym uruchomieniu i błędzie; nie jest automatycznie zapisywany do pliku.

## 5. WebGUI — projekt odłożony na późniejszy etap

Ścieżka aplikacji: `Urządzenia -> TRUCK -> DAF -> SAC`.
Ekran zawiera:

- nagłówek: DAF SAC, profil `0xDAF00025`, CAN Classic 250 kbit/s, status;
- sekcję „Połączenie”: stan Bench, CAN, ostatni błąd UDS/NRC i recovery;
- kartę „Identyfikacja”: pole VIN, HW, SW; odczyt na żądanie;
- akcje: „Rozpocznij identyfikację”, „Zatrzymaj”, „Odzyskaj sesję”;
- przyszłe zakładki: DTC, parametry, testy laboratoryjne — oznaczone
  „niedostępne / nieweryfikowane” bez fałszywych danych.

WebGUI nie ustawia prędkości CAN, nie posiada socketów, nie wysyła
ramek i nie uruchamia bezpośrednio przekaźników. Jest klientem
uwierzytelnionego API, które ma egzekwować uprawnienia i serializować
operacje sesji. Nie tworzymy niezależnego „drugiego Bench Runtime”
w frontendzie.

## 6. Proponowana powierzchnia backend API (nieudostępniona jeszcze w sieci)

Kontrakt typowany z `AppSnapshot::kSchemaVersion = 2` (po audycie 2026-10-08: jawny `vin_unprogrammed`), tłumaczony
do JSON w osobnym serwerze. Native C++ layout nie jest wire ABI.

- `GET /api/v2/duts`: katalog wybranych i dopuszczonych DUT Profile,
  `profile_id`, typ i deklarowany katalog możliwości;
- `POST /api/v2/bench/sessions`: inicjacja jednej autoryzowanej sesji
  z walidowanym `profile_id` i wiązaniem zasobów; bez swobodnych CAN ID;
- `GET /api/v2/bench/sessions/{id}`: snapshot (wersja, state, status,
  CAN/UDS, maskowany VIN, flagi dostępu);
- `POST /api/v2/bench/sessions/{id}/identify`: read-only identyfikacja;
- `POST /api/v2/bench/sessions/{id}/stop`: bezpieczne zatrzymanie;
- `POST /api/v2/bench/sessions/{id}/recover`: jawne recovery po błędzie.

Pełny VIN należy udostępnić tylko po osobnym sprawdzeniu uprawnienia,
nie w domyślnym snapshotcie, logach CI, GitHub lub telemetryce. W aplikacji
`AppSnapshot` dostępne są tylko ostatnie cztery znaki. Powyższe
ścieżki API są projektem kontraktu, a nie działającymi URL-ami.

## 7. Dowody i bramki akceptacji

Stage 3.3A fizycznie udowodnił CAN 250 kbit/s, ISO-TP, UDS
`0x22 F190/F188/F192` na DAF SAC (zob. `DAF_SAC_STAGE3_3A_PROOF.md`).

Stage 4.0 jednostkowo weryfikuje:
- wybór poprawnego profilu 250 kbit/s i odrzucenie 500 kbit/s;
- rezerwację zasobów przed CAN open i ich zwolnienie;
- identyfikację przez prawdziwe komponenty Bench/UDS/Profile z transportem
  testowym, bez symulowania prawdziwego ECU;
- brak aktywnych komend innych niż identyfikacja;
- ponowne wykonanie identyfikacji oraz ręczny stop;
- NRC, błędy RX/CAN, zajętość zasobów, watchdog i cleanup;
- kompilację i testy Debug/Release/Generic/ASan-UBSan;
- odrębną budowę Linux SocketCAN runnera, bez automatycznego TX w CI.

Stage 4.1: operator uruchomił fizyczny proof:
`sudo ./scripts/run_stage4_daf_sac_bench_gate.sh`.
**PHYSICAL PASS 2026-10-07** — szczegółowe, zanonimizowane dowody poniżej.
Runner potwierdził docelową ścieżkę z Bench oraz `can0` 250000.
Nie wykonano write, diagnostic session control, flash ani output testu.
Skrypt przywrócił CAN DOWN, co sprawdzono niezależnie po uruchomieniu.

Stage 4.2 (zmieniony zakres decyzją właściciela projektu): wyłącznie
obsługa ciśnienia PGN 65198, napięcia UDS FE96, odczytu DTC 19 02 i
jawnie potwierdzanego kasowania DTC 14. Bez uruchamiania API i WebGUI.
Szczegóły, testy i ograniczenia: `DAF_SAC_STAGE42_SERVICES.md`.

Po Stage 4.2 ECU SAC jest obsługiwane programowo w Bench Runtime.
Zdalny interfejs API/WebGUI pozostaje niezależnym przyszłym zadaniem.

## 8. Wymagania utrzymaniowe

Każda nowa funkcja SAC:
1. najpierw ma dokument dowodowy DID/PGN/format + test negatywny;
2. trafia do SAC Profile/programu oraz katalogu akcji;
3. ma testy normalnego wykonania, abortu, timeoutu i recovery;
4. rozszerza API/UI tylko przy gotowym kontrakcie;
5. nie modyfikuje Bench lub CORE bez formalnego powodu i re-gate.

Zabronione są ukryte automatyczne testy wyjść, flashe lub zmiany sesji.
Zezwolenie na prace na gałęzi nie oznacza zgody na merge do main.

## 9. Dostęp do zmian GitHub Actions

Nowe joby CI dla Stage 4.0 zostały przygotowane jako
`docs/DAF_SAC_STAGE4_CI_WORKFLOW.patch`: Linux GCC + ASan/UBSan +
Generic, Clang Linux oraz Windows MSVC x64/Win32 Debug/Release.
Patch nie jest jeszcze zastosowany do `.github/workflows/ecu-platform-ci.yml`,
ponieważ bieżący token GitHub odmawia zapisu workflow bez uprawnienia
`workflow`. Nie obchodzimy tej granicy autoryzacji.

Lokalne bramki `bash scripts/validate_daf_sac_application.sh` są
dostępne od razu. Zgodność całej gałęzi z nowymi jobami GitHub CI
pozostaje niezatwierdzona do czasu autoryzowanego zastosowania patcha
i pozytywnego przebiegu. Nie scalać do `main` bez oddzielnej zgody.

## 10. Stage 4.1 — zapis wyniku fizycznego Bench Runtime (PASS)

Data pomiaru: 2026-10-07; źródło: wynik operatora z rzeczywistego DAF SAC
podłączonego do ECU Platform V2 na stanowisku. Fizyczny runner:
`sudo ./scripts/run_stage4_daf_sac_bench_gate.sh`.

- `SAC_BENCH_APP=START profile=0xdaf00025 bitrate=250000 tx=0x18da30f9 rx=0x18daf930 service=read-only-0x22`
- `SAC_BENCH_VIN_LAST4=1236` (pełny VIN nie jest publikowany)
- `SAC_BENCH_SOFTWARE=1973214`
- `SAC_BENCH_HARDWARE=K075169` (spacje końcowe pominięte)
- `SAC_BENCH_RESOURCE_LEASES=0`
- `SAC_BENCH_APP_PHYSICAL=PASS CORE_V2_BENCH_DUT_PROFILE`
- `DAF_SAC_STAGE4_BENCH_PHYSICAL_GATE=PASS`
- `STAGE4_CAN_LINK_AFTER=DOWN`; niezależna kontrola zdalna:
  `can0 state DOWN, CAN STOPPED`

Kontrola CAN przed i po aktywnej próbie:

| Miara | Przed | Po (przed DOWN) | Różnica |
|---|---:|---:|---:|
| RX frames | 12781 | 12801 | +20 |
| RX bytes | 102221 | 102354 | +133 |
| TX frames | 6 | 12 | +6 |
| TX bytes | 21 | 42 | +21 |
| RX errors | 0 | 0 | 0 |
| TX errors | 0 | 0 | 0 |
| TX dropped (total) | 1 | 1 | 0 |

Interfejs w trakcie próby: Classic CAN / 29-bit / 250 kbit/s, normal mode
(z ACK), ERROR-ACTIVE bez bus-off, liczniki błędów CAN TX 0, RX 0.
Zakończenie: zwolnione wszystkie logiczne zasoby Bench i zamknięta sesja CAN;
systemowy interfejs `can0` bezpiecznie przełączony do DOWN przez skrypt.

**Decyzja o bramkach:** Stage 4.1 fizyczny test identyfikacji — PASS.
Nie oznacza to gotowości WebGUI/API ani zatwierdzenia zmian do `main`.
Odczyty fizyczne Stage 4.2: PASS (2026-10-07); fizyczne kasowanie DTC: NOT RUN. Dedykowana matryca CI Stage 4.2 wymaga wdrożenia osobnego patcha workflow.
Nie wykonywano programowania, sterowania wyjściami, kasowania błędów
ani zmian sesji UDS.

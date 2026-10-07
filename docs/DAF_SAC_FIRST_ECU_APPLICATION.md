# ECU Platform V2 — DAF SAC jako pierwsze ECU aplikacji

Data: 2026-10-07
Gałąź: `app/daf-sac-first-ecu`
Zakres: projekt docelowy oraz implementacja pionowego przekroju Stage 4.0.
Status: implementacja i testy automatyczne; fizyczny Bench Runtime proof (4.1) wymaga oddzielnego uruchomienia; WebGUI/API (4.2) nie są jeszcze podłączone.

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
WebGUI (Stage 4.2)  ->   autoryzowany backend API (Stage 4.2)
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
| DTC odczyt | NIE | Potrzebna osobna weryfikacja |
| Parametry bieżące | NIE | Potrzebna osobna weryfikacja |
| Kasowanie DTC | NIE | Operacja zmieniająca stan |
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

## 5. WebGUI — projekt pierwszego ekranu (Stage 4.2)

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

Kontrakt typowany z `AppSnapshot::kSchemaVersion = 1`, tłumaczony
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

Stage 4.1: operator uruchamia fizyczny proof:
`sudo ./scripts/run_stage4_daf_sac_bench_gate.sh`.
Runner weryfikuje docelową ścieżkę z Bench oraz `can0` 250000.
Nie wykonuje write, diagnostic session control, flash ani output testu.
Skrypt zostawia CAN DOWN także przy błędzie.

Stage 4.2: osobny backend API i rzeczywisty interaktywny WebGUI;
przed udostępnieniem trzeba domknąć auth, DTO, CSRF/origin,
kontrolę konkurencyjności sesji i recovery po restarcie serwera.

Po Stage 4.2 będzie można mówić o pełnej pierwszej funkcji ECU
obsługiwanej przez GUI. Sam Stage 4.0 nie oznacza, że WebGUI już działa.

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

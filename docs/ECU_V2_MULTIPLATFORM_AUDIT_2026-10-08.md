# ECU Platform V2 — przedprodukcyjny audyt multiplatformowy

Data: 2026-10-08

Branch: `release/ecu-v2-multiplatform-audit-20261008`

Punkt odniesienia produkcji: `origin/main` = `5600282`

Status: **CANDIDATE / NIE SCALAĆ DO MAIN PRZED AKCEPTACJĄ WŁAŚCICIELA**

## Zakres audytu

Zakres funkcjonalny release candidate: przenośny CORE V2,
Bench Runtime, DUT Profile, DAF SAC Application (250 i 500 kbit/s),
Linux V2 SocketCAN adapter, read-only operator tooling oraz
decyzja WebGUI jako ściśle izolowanego klienta. Bez implementacji
HTTP API/WebGUI, bez aktywacji nowych DUT, bez merge do main.

Zakres testowy: TRUCK/AGRI/OHV i urządzenia inne niż ECU
(architektura surowego CAN i EGR/VGT w CORE; konkretnych profili
EGR/VGT fizycznie w tym wydaniu nie akceptujemy).

Rozdzielone środowiska: Linux ARM64 (fizyczny CM5), Linux x86_64
GCC/Clang (GitHub), Windows MSVC x64/Win32 (GitHub),
symulowany CMake Generic i sanitizery ASan/UBSan.

## Wykryte problemy i postępowanie

| ID | Waga | Ustalenie | Działanie / status |
|---|---|---|---|
| A-001 | HIGH — ROZWIĄZANY | CI obejmował CORE/Bench/DUT na Windows, ale pomijał konkretną aplikację SAC | Wdrożono GCC/Clang/MSVC Debug/Release x64/Win32 oraz buildy domyślne; pełne CI 27/27 PASS dwukrotnie na `84292c6` |
| A-002 | HIGH | Świeży CMake domyślnie wybierał legacy Core/SAC/SocketCAN, a pomijał aplikację V2 | Zmiana wartości domyślnych na V2, legacy opt-in, Linux V2 warunkowy, test grafu default |
| A-003 | HIGH dla WebGUI | Obecny kiosk Cage/Chromium działa pod tym samym `ecu` UID/grupą co klient socketu uprzywilejowanego agenta | Obowiązkowa izolacja osobnego konta `ecu-kiosk` PRZED wdrożeniem realnego WebGUI; Stage D placeholder bez zmian |
| A-004 | HIGH operacyjny | Pierwsze fizyczne kasowanie DTC poprzedniego SAC nie udowodniło trwałego wymazania; poprzedni wynik UNKNOWN | Nie resetować jednorazowego biletu; w tym audycie nie wykonywać `14`; restrykcje pozostają |
| A-005 | MEDIUM | Z CAN passive PGN FEAE jest widoczny, ale podczas odczytu napięć monitor go nie obserwował | Status ciśnień pozostaje niepotwierdzony; nie dopisywać fikcyjnych wartości |
| A-006 | HIGH dla WebGUI | Brak produkcyjnej warstwy API z autoryzacją, a kiosk wciąż korzysta z pliku placeholder | Nie podłączać GUI do agenta; przygotować osobną warstwę API po akceptacji architektury |
| A-007 | WYMÓG | `main` ma osobną bramkę właściciela | Draft PR #18 do `main` jest otwarty; bez automatycznego merge, wymagana osobna akceptacja właściciela |
| A-008 | HIGH portability | Pierwszy GitHub MSVC build DUT Profile 4/4 FAIL na C4244 (konwersja `unsigned int` na `uint8_t` w wektorach testowych) | Wymuszony typ `std::array<std::uint8_t, 3U>`; ponowny Windows MSVC x64/Win32 Debug/Release **4/4 PASS**, workflow `37755575051` **18/18 PASS** |
| A-009 | MEDIUM DTO | Snapshot identyfikacji nie przenosił jawnie informacji o niezaprogramowanym VIN F190/FF17 | `vin_unprogrammed` w AppSnapshot, schema 2, testy 250k/500k i pustego suffix |
| A-010 | BLOCKER CI — ROZWIĄZANY | Początkowo GitHub OAuth CLI miał `repo`, ale nie `workflow` | Operator odnowił autoryzację 2026-10-08; zakres `workflow` potwierdzony, pełny RC opublikowany, Draft PR #18 utworzony |
| A-011 | HIGH API safety | `kOperationCatalog` publikował `clear_dtc` jako dostępne, choć fizycznego skutku kasowania nie potwierdzono | Publiczna dostępność `clear_dtc=false`, osobna ręczna procedura z ograniczeniami bez zmian; dodatkowy test katalogu |
| A-012 | HIGH portability — ROZWIĄZANY | Pierwszy nowy test aplikacji MSVC x64 Debug zakończył się `SEGFAULT`; pozostałe 26/27 zadań PR CI przeszło | 28 kolejnych przypadków testowych DUT/Bench rozdzielono na 7 funkcji po 4 przypadki; poprawka **MSVC x64 Debug PASS**, pełna macierz 27/27 PASS na commicie `84292c6` |

## Zmiany bez naruszenia CORE freeze

- Domyślna konfiguracja CMake: V2 Core + Bench + DUT + SAC Application
  i Linux V2 adapter tylko na Linux; bez legacy domyślnie.
- Zależne opcje CMake nie wymuszają DUT/App przy `BENCH_RUNTIME=OFF`
  (ważne dla „Core-only” na Linux i Windows).
- `-DECU_BUILD_LINUX_V2_PLATFORM=ON` na nie-Linux musi fail-closed,
  zamiast milcząco ignorować żądanie.
- Oddzielny legacy hardening CI używa jawnych legacy build flags.
- Skrypt `scripts/check_v2_default_build.py` weryfikuje
  `CMakeCache.txt`, tożsamość źródła i brak Linux platform graph
  na Windows/Generic.
- Rozszerzenie `.github/workflows/ecu-platform-ci.yml` o testy
  aplikacji i domyślnych konfiguracji wszystkich systemów.
- Kontrakt GUI-client-only z `docs/WEBGUI_CLIENT_ONLY_ARCHITECTURE.md`
  oraz reguła `PROJECT_GOVERNANCE.md` włączone do RC z odrębnego ADR.
- `README.md` opisuje prawdziwy build V2 oraz ograniczenia.

Nie modyfikowano kodu `src/core_v2`, `src/bench`
ani transmisji do fizycznego DUT w ramach audytu.

## Walidacja — wyniki oraz otwarte bramki

| Gate | Status |
|---|---|
| Git diff / workflow YAML / kontrola architektury | PASS lokalnie |
| CM5 ARM64 GCC: domyślny Linux build | PASS: konfiguracja, kompilacja Application/Profile/LinuxV2 i CTest |
| CM5 ARM64 GCC: Core V2 Debug/Release/Generic/ASan+UBSan | PASS, 20/20 Core V2 testów |
| CM5 ARM64 GCC: Bench + DUT + Application | PASS: Bench Stage 2, DUT i Application Debug/Release/Generic/ASan+UBSan |
| Symulowany non-Linux Generic CMake | PASS: domyślna konfiguracja + kompilacja i CTest |
| Linux x86_64 GCC / Clang GitHub CI | **PASS** — pełny workflow PR #18 `37760830998`, 27/27 PASS dla kodu `84292c6` |
| Windows MSVC x64 + Win32 / Debug + Release | **PASS** — Application, DUT, Bench, CORE i domyślny build na Windows x64 + Win32 / Debug + Release; PR CI `37760830998`, 27/27 PASS na `84292c6` |
| WebGUI security isolation kiosk vs agent | **BLOCKED dla uruchomienia WebGUI** |
| Hardware CAN/UDS 250/500 read-only | PASS we wcześniejszych, zachowanych dowodach; bez nowego TX |
| DTC clear / EGR-VGT / flash / output control | poza zakresem release read-only |

Nie zastępować `CI PASS` samym lokalnym buildem.
Konkretną zgodę na produkcyjny merge można rozważać
dopiero po udokumentowaniu wyników, zakresu i pozostałych blokad.

## Evidence finalnej macierzy dla kodu aplikacji

Commit zawierający ostatnią poprawkę kodu/testów: `84292c6dddd9190a7a04823ad1476421604841eb`.

- Push CI: `https://github.com/autoklinika/ECU_Platrorm_V2/actions/runs/37760826277` — **27/27 PASS**.
- Draft PR #18 CI: `https://github.com/autoklinika/ECU_Platrorm_V2/actions/runs/37760830998` — **27/27 PASS**, `MERGEABLE/CLEAN`.
- Wszystkie scenariusze Windows MSVC aplikacji SAC: **x64 Debug PASS, x64 Release PASS, Win32 Debug PASS, Win32 Release PASS**.
- CM5 ARM64 GCC: Application Debug/Release/Generic/ASan+UBSan **PASS**; CORE V2, Bench Runtime i opcjonalne narzędzia legacy przeszły wcześniejsze niezależne gates.
- Przenośne moduły V2 nie zostały zmodyfikowane dla potrzeb systemu Windows; naprawa dotyczyła wyłącznie niezależnych testowych zakresów lokalnych o dużych strukturach sesji.
- Brak użycia destrukcyjnego `14`, brak testów wyjść/flash i brak wymuszonego CAN TX podczas CI.

Kandydat wydania jest technicznie gotowy **w zakresie obecnego backendu laboratoryjnego i diagnostyki odczytowej**. Nie jest to akceptacja przyszłego WebGUI ani API: izolacja procesu kiosku od Bench oraz autoryzacja API pozostają obowiązkowymi osobnymi bramkami.

Raport podlega wersjonowaniu jako ostatnia korekta dokumentacji po powyższych wynikach; finalny SHA po korekcie raportu powinien mieć osobny zielony przebieg CI przed przyjęciem do produkcyjnego `main`.

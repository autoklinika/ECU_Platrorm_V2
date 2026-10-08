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
| A-001 | HIGH | CI obejmował CORE/Bench/DUT na Windows, ale pomijał konkretną aplikację SAC | Dodać osobne GCC, Clang, MSVC Debug/Release x64/Win32; PR CI wymagany |
| A-002 | HIGH | Świeży CMake domyślnie wybierał legacy Core/SAC/SocketCAN, a pomijał aplikację V2 | Zmiana wartości domyślnych na V2, legacy opt-in, Linux V2 warunkowy, test grafu default |
| A-003 | HIGH dla WebGUI | Obecny kiosk Cage/Chromium działa pod tym samym `ecu` UID/grupą co klient socketu uprzywilejowanego agenta | Obowiązkowa izolacja osobnego konta `ecu-kiosk` PRZED wdrożeniem realnego WebGUI; Stage D placeholder bez zmian |
| A-004 | HIGH operacyjny | Pierwsze fizyczne kasowanie DTC poprzedniego SAC nie udowodniło trwałego wymazania; poprzedni wynik UNKNOWN | Nie resetować jednorazowego biletu; w tym audycie nie wykonywać `14`; restrykcje pozostają |
| A-005 | MEDIUM | Z CAN passive PGN FEAE jest widoczny, ale podczas odczytu napięć monitor go nie obserwował | Status ciśnień pozostaje niepotwierdzony; nie dopisywać fikcyjnych wartości |
| A-006 | HIGH dla WebGUI | Brak produkcyjnej warstwy API z autoryzacją, a kiosk wciąż korzysta z pliku placeholder | Nie podłączać GUI do agenta; przygotować osobną warstwę API po akceptacji architektury |
| A-007 | WYMÓG | `main` ma osobną bramkę właściciela | RC na oddzielnym branchu, ewentualny draft PR; bez automatycznego merge |

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

## Walidacja — do wypełnienia wynikami

| Gate | Status |
|---|---|
| Git diff / workflow YAML / kontrola architektury | wykonywane |
| CM5 ARM64 GCC: domyślny Linux build | wykonywane |
| CM5 ARM64 GCC: Core V2 Debug/Release/Generic/ASan+UBSan | wykonywane |
| CM5 ARM64 GCC: Bench + DUT + Application | planowane |
| Symulowany non-Linux Generic CMake | skonfigurowany, build/test oczekuje |
| Linux x86_64 GCC / Clang GitHub CI | oczekuje |
| Windows MSVC x64 + Win32 / Debug + Release | oczekuje |
| WebGUI security isolation kiosk vs agent | **BLOCKED dla uruchomienia WebGUI** |
| Hardware CAN/UDS 250/500 read-only | PASS we wcześniejszych, zachowanych dowodach; bez nowego TX |
| DTC clear / EGR-VGT / flash / output control | poza zakresem release read-only |

Nie zastępować `CI PASS` samym lokalnym buildem.
Konkretną zgodę na produkcyjny merge można rozważać
dopiero po udokumentowaniu wyników, zakresu i pozostałych blokad.

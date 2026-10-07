# ECU Platform V2 — stały delegowany dostęp do stanowiska CAN

Data: 2026-10-07. Gałąź rozwojowa:
`platform/permanent-bench-agent-v1` (stały dostęp delegowany;
bez dodawania możliwości kasowania DTC).

## Cel

Asystent ma móc wykonać dozwolony test na podłączonym ECU **bez
powtarzania hasła sudo i bez otwartego terminala operatora**. Usługa
pozostaje dostępna po restarcie Raspberry Pi przez `systemd`.

To nie jest ogólny dostęp root ani nieograniczona zdalna administracja.
Po jednorazowym, lokalnym zatwierdzeniu instalacji hasłem administratora
asystent korzysta ze zwykłego konta `ecu` i prywatnego gniazda Unix.

## Architektura

```text
Remote Desktop Commander (konto ecu, bez sudo)
     |
     +-- scripts/ecu_bench.py status
     +-- scripts/ecu_bench.py sac-dtc
     +-- scripts/ecu_bench.py sac-parameters
                      |
                  AF_UNIX
           /run/ecu-platform-v2-bench/request.sock
                      |
           root-owned permanent agent
           /usr/local/libexec/ecu-platform-v2/agent_server.py
                      |
          [root] zarządza can0 (250k, Classic, normal)
                      |
          [ecu] uruchamia skopiowany przez root
                nierozszerzalny zdalnie read-only probe
                      |
          Core V2 -> Bench Runtime -> DUT Profile DAF SAC
                      |
         [root] can0 DOWN, również przy błędzie/timeout
```

Gniazdo `0660 root:ecu`; katalog `0750 root:ecu`, a serwer
sprawdza również rzeczywisty UID klienta przez `SO_PEERCRED`.
Serwer i probe są kopiowane z repozytorium do katalogu
`root:root` i nie mogą być modyfikowane przez konto `ecu`.
Serwer nie wykonuje dowolnych komend z parametru użytkownika i nie
używa `sh -c` ani kodu Python z katalogu roboczego.

Nie nadajemy `sudo NOPASSWD: ALL`, `CAP_NET_ADMIN` wszystkim procesom
konta `ecu`, ani pliku zawierającego hasło.
Ograniczone uprawnienie systemowe istnieje wewnątrz usługi root, która
zarządza wyłącznie `can0`; program diagnostyczny działa jako `ecu`.

## Jednorazowa instalacja

Na lokalnym terminalu CM5/SSH (przez użytkownika `ecu`, bez wysyłania
hasła do ChatGPT) wykonaj:

```bash
cd ~/ECU_Platrorm_V2
sudo bash scripts/install_ecu_bench_agent.sh
```

Warunki:
- budowa `build/daf-sac-app-linux/tests/ecu_daf_sac_stage42_read_probe`
  już istnieje; testy Linux Stage 4.2 przeszły;
- `can0` jest DOWN; instalator odmawia ingerencji przy aktywnym CAN;
- `sudo` jest podane **tylko raz, przez operatora lokalnie**;
- instalator włącza `ecu-platform-v2-bench-agent.service` na każdym
  uruchomieniu systemu (`systemctl enable --now`);
- instalator kopiuje wersję root-owned agenta i odczytowego probe.
  Repo nie zawiera plików uwierzytelniających i nie przejmuje root
  w późniejszych pracach.

**Nie jest wymagana stała sesja terminala ani laptop pozostawiony
włączony**, pod warunkiem działającego CM5 i zdalnego dostępu do niego.

## Polecenia bez hasła — także po restarcie

Asystent może później uruchamiać zdalnie:

```bash
cd ~/ECU_Platrorm_V2
python3 scripts/ecu_bench.py status
python3 scripts/ecu_bench.py sac-dtc
python3 scripts/ecu_bench.py sac-parameters
```

`status` nie zmienia sprzętu. Obie pozostałe operacje mają
stałe adresowanie DAF SAC na 250000 bit/s i wykonują jedynie
odczyty z istniejącego Stage 4.2 (z wyłączeniem komendy kasowania).
**Odczyt DTC zawiera UDS `10 03`**, czyli zmianę aktywnej sesji
diagnostycznej, choć samych błędów nie usuwa.

Usługa:
- odmawia pracy, kiedy `can0` już jest UP;
- wprowadza CAN tylko podczas pojedynczego żądania;
- uruchamia stałą kopię odczytowego binarium z uprawnieniami `ecu`;
- zamyka `can0` po odpowiedzi albo niepowodzeniu;
- odrzuca nieznane operacje oraz próby arbitralnego wykonywania shell;
- ma timeout pojedynczego probe, loguje swój status przez systemd;
- nie wysyła pełnego VIN ani zapisów DTC do zewnętrznych serwisów.

## Granice uprawnień

W tym wydaniu agent **nie umożliwia**:
- kasowania DTC `UDS 0x14`, resetowania, flashowania czy kodowania;
- sterowania nastawnikami EGR/VGT lub wyjściami ECU;
- dowolnych komend `ip`, `sudo`, `systemctl`, powłoki root;
- uruchamiania modyfikowanego przez `ecu` programu z repo z prawami root.

Do kolejnego rodzaju ECU lub operacji dodajemy wyraźny kontrakt i osobną
zgodę właściciela. Nie ma automatycznego rozszerzania uprawnień przez
edycję źródeł. Uaktualnienie zaufanej, instalowanej root-owned wersji
agenta/probe wymaga ponownego świadomego uruchomienia instalatora —
natomiast normalne odczyty i rozwój kodu nie wymagają hasła.

Nierozstrzygnięty wcześniejszy DTC clear `UNKNOWN` pozostaje w prywatnym
archiwum; to uprawnienie pozwala na **bezpieczny odczyt jego obecnego
stanu, bez ponawiania `0x14`**.

## Wycofanie stałego dostępu

Operator w dowolnej chwili może wyłączyć usługę:

```bash
sudo systemctl disable --now ecu-platform-v2-bench-agent.service
```

Wyłączenie kończy dostęp bezhasłowy. Root-owned artefakty oraz usługa
pozostają na dysku do ewentualnej ponownej aktywacji lub ręcznego usunięcia.

## Ograniczenia pomiarowe i bezpieczeństwa

Kontrakt podstawowych modułów CORE V2 / Bench Runtime pozostaje bez zmian.
Dowód fizyczny DAF SAC odczytu FE96/DTC jest już dostępny, ale wersja
usługi permanentnej wymaga **osobnego testu po instalacji przez operatora**.
Nigdy nie twierdzimy, że agent został uruchomiony przed wynikiem
`ecu_bench.py status` od rzeczywistej usługi.

Automatyczny cleanup zależy od działania systemu operacyjnego; nagłe
odcięcie zasilania lub SIGKILL mogą uniemożliwić wykonanie kodu cleanup.
To nie zastępuje fizycznego E-stop i prawidłowego stanowiska zasilania.

## Poprawka bootstrapu agent service (2026-10-07)

Pierwsza instalacja utworzyła i włączyła systemd unit, jednak nie
powstało gniazdo: dziennik systemowy zgłosił `Operation not permitted`
podczas próby zmiany grupy katalogu `/run/ecu-platform-v2-bench`.
Pierwotny serwer wywoływał `chown`, chociaż zgodnie z zasadą najmniejszych
uprawnień nie posiadał `CAP_CHOWN`. Usługa wchodziła w cykl restartów;
żadne żądanie diagnostyczne nie zostało uruchomione. `can0` pozostał DOWN.

Poprawka:
- systemd `User=root` i `Group=ecu` ustawia właściciela katalogu
  RuntimeDirectory (`root:ecu`, `0750`) bez `chown` wykonywanego przez
  ograniczony proces;
- gniazdo dziedziczy grupę `ecu`, ma tryb `0660`;
  serwer weryfikuje typ, właściciela, grupę i uprawnienia obu obiektów;
- `CapabilityBoundingSet` pozostaje bez `CAP_CHOWN`;
- instalator przy aktualizacji najpierw zatrzymuje usługę, także gdy
  jej stan to `activating (auto-restart)`;
- dodano regresyjne testy dokładnego kontraktu własności i trybów
  oraz walidację szablonu systemd.

**Dla aktualnego CM5:** po publikacji poprawki należy jeszcze raz
uruchomić lokalnie `sudo bash scripts/install_ecu_bench_agent.sh`.
Zdalne narzędzie nie ma prawa wprowadzać tej zmiany za operatora.
Dopiero pozytywny wynik `python3 scripts/ecu_bench.py status` na żywym
systemie pozwala uznać stałego agenta za uruchomionego.

## Druga bramka uruchomienia: brak efektywnego CAP_SETUID (2026-10-07)

Po poprawnym uruchomieniu systemd i utworzeniu gniazda `root:ecu 0660`
operator zweryfikował wynik `ECU_BENCH_AGENT_STATUS=READY`.
Zdalny, **wyłącznie odczytowy** test `sac-dtc` został jednak odrzucony
`Operation not permitted` zanim doszło do odczytu DTC. Archiwum
sprzed wcześniejszego kasowania jest zachowane, a `can0` został
wyłączony. Weryfikacja aktywnego procesu usługi pokazała
`CapEff=0x1040` (SETGID + NET_ADMIN) i `CapBnd=0x10c0` —
`CAP_SETUID` było dozwolone przez bounding set, ale **nieaktywne**.
Uruchomienie diagnostycznego probe pod UID `ecu` potrzebuje
jednorazowej redukcji uprawnień przez `setuid`.

Poprawka Stage Bench Agent:
- `AmbientCapabilities=CAP_SETUID` w jednostce systemd;
- bounding set bez zmian: `CAP_NET_ADMIN CAP_SETUID CAP_SETGID`;
- probe nadal wykonuje się z UID/GID `ecu`, bez nadanych uprawnień
  do root oraz bez możliwości kasowania DTC;
- agent weryfikuje wymagane bity `CapEff` **przed** otwarciem
  gniazda i ogłoszeniem gotowości; brak prawa => fail-closed;
- nowy test regresyjny dla `CapEff=0x1040` (odmowa) i
  `CapEff=0x10c0` (dopuszczone).

Powyższa konfiguracja wymaga jeszcze autoryzowanej reinstalacji przez
operatora; statyczne testy same w sobie nie potwierdzają fizycznego
uruchomienia. Nie jest nadawany `CAP_SETUID` całemu kontu `ecu`,
nie ma reguły `NOPASSWD` dla dowolnych programów.

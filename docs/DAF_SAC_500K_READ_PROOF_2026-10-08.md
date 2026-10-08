# ECU Platform V2 — DAF SAC 500 kbit/s — pierwszy odczyt

Data: 2026-10-08
Gałąź: `stage4.3/daf-sac-500k-read-proof`
Status: **PASYWNY CAN 500 KBIT/S PASS / IDENTYFIKACJA UDS FAIL (ODPOWIEDŹ PRZYJĘTA, WERYFIKACJA NIEUDANA) / PARAMETRY I DTC NIEODCZYTANE**

## Sytuacja wyjściowa

Operator podłączył nowy SAC, dla którego deklarowana prędkość CAN to
**500 kbit/s**. Na CM5 `can0` był **DOWN**, lecz nadal skonfigurowany
na **250000 bit/s** po poprzednim ECU. Sam ten stan nie pozwala wnioskować
o rzeczywistym bitrate nowego SAC. Uprzywilejowany, stale zainstalowany
agent `ecu-platform-v2-bench-agent` obsługuje tylko read-only SAC
250 kbit/s i celowo nie oferuje ogólnego konfigurowania CAN.

Stary test fizyczny w dniu 2026-10-07 przy 500k dotyczył **innego,
wcześniej podłączonego sterownika**: brak odpowiedzi nie jest dowodem,
że obecny SAC nie komunikuje się przy 500k.

## Warianty DUT i zakres kodu

Kontrakt DUT Profile posiada dwa warianty:
- `0xDAF00025` / CAN Classic 250000 bit/s;
- `0xDAF00050` / CAN Classic 500000 bit/s.

Poprawiono tylko **DAF SAC Application** i fizyczny operatorowy read-only
probe, nie CORE V2 i nie Bench Runtime. Walidacja aplikacji sprawdza
jednocześnie profil ID, bitrate oraz format CAN. Usługa identyfikacji
udostępnia `22 F190/F188/F192`; parametry `22 FE96`, odczyt DTC
`10 03 -> 19 02 FF`. Przejście `10 03` zmienia sesję diagnostyczną.

Wariant 500k ma **wprost zablokowane kasowanie DTC w Application**
(`prepare_clear_dtcs` i `clear_dtcs` nie wydają tokenu ani nie
wysyłają poleceń). Stały root-owned agent nie jest przebudowywany
ani instalowany ponownie i nadal obsługuje tylko znany wariant 250k.
Operatorowe narzędzia kasowania dla poprzedniego SAC nie są uruchamiane.

## Test operatora 500 kbit/s (wyłącznie odczyty)

**Pierwszy test fizyczny został już wykonany 2026-10-08 i ujawnił błąd identyfikacji.** Poniższe polecenie służy teraz do jego powtórnej, wyłącznie odczytowej diagnostyki z rozszerzoną obserwowalnością na CM5:

```bash
cd ~/ECU_Platrorm_V2
sudo bash scripts/run_stage42_daf_sac_500k_read_gate.sh all
```

To jest sekwencja bezpieczna dla historii DTC:
1. Sprawdza `can0 DOWN`; odmowa przejęcia aktywnej magistrali.
2. Ustawia 500 kbit/s, Classic CAN, **listen-only** i nasłuchuje 5 s.
   `0` ramek przy pojedynczym ECU jest nierozstrzygające (np. brak
   cyklicznego broadcast); błędy fizyczne i nieoczekiwany TX przerywają gate.
3. Przełącza na Classic CAN normal, 500 kbit/s i wykonuje ograniczony
   czasowo, **wyłącznie odczytowy** proof Core V2: DID VIN, software,
   hardware. Jeśli identyfikacja się nie uda — przerywa pozostałe testy.
4. Dopiero po potwierdzonej identyfikacji uruchamia **osobne** sesje
   odczytu `22 FE96` i `19 02 FF`. Wyniki raportuje oddzielnie;
   nie zakłada zgodności formatów DID/PGN innej rewizji OEM.
5. Zachowuje raporty w prywatnym katalogu
   `~/.ecu-platform-v2/daf-sac/500k-proof/` (`0700`, pliki `0600`).
   VIN na standardowym wyjściu oraz w raporcie identyfikacji jest anonimizowany.
   **Surowy log aktywnego UDS może zawierać VIN i musi pozostać prywatny**;
   skrypt drukuje tylko SHA-256 i ścieżkę tego logu.
   `can0` pozostaje DOWN w każdym zwykłym zakończeniu.
6. Nie wykonuje `0x14` ClearDiagnosticInformation, resetowania,
   adaptacji, procedur zabezpieczających, sterowania wyjściami ani flash.

Opcjonalnie dostępny jest etap tylko pasywny:

```bash
sudo bash scripts/run_stage42_daf_sac_500k_read_gate.sh passive
```

**Nie uruchamiać** agenta `python3 scripts/ecu_bench.py sac-dtc`
przed testem 500k: będzie on rekonfigurował `can0` na 250 kbit/s,
ponieważ jego kontrakt jest nadal przypięty do poprzedniego profilu.

## Brama programowa

- C++ symulator: identyfikacja 500k, napięcie FE96 i odczyt
  `19 02 FF` z 24-bitowymi kodami DTC; testy obejmują niezależne
  sesje oraz poprawne zwolnienie zasobów.
- C++ bezpieczeństwo: na profilu 500k `0x14` jest niemożliwe nawet
  po syntetycznym „potwierdzeniu”.
- Fizyczny read-only probe: stary, trójargumentowy kontrakt 250k dla
  zainstalowanego agenta zachowany; jawny czwarty argument `500000`
  służy tylko nowemu operatorowemu gate.
- Test błędnego stanu `can0`: gdy interfejs jest DOWN, probe 500k
  odrzuca próbę **przed transmisją**, zachowując licznik TX.
- Debug/Release/Generic/ASan+UBSan, testy architektury, brak TTY
  i inne regresje: **PASS lokalnie**.

## Granice akceptacji

Pasywny odbiór CAN 500 kbit/s został potwierdzony na fizycznym SAC,
ale nie potwierdzono jeszcze poprawnej identyfikacji DID.
Test fizyczny wymaga interaktywnego uruchomienia skryptu z `sudo` przez
operatora — zdalny terminal nie udostępnia uprawnienia
`CAP_NET_ADMIN` ani polecenia sudo. Nie wolno deklarować pełnego PASS
nowego sterownika bez identyfikacji i dalszych odczytów.
Po uzyskaniu wyniku rozwój powinien pozostać w DUT Profile / Bench
Application, o ile nie wystąpi rzeczywisty błąd CORE V2.

## Pierwszy fizyczny test nowego SAC — 2026-10-08 09:48 (operator)

`sudo bash scripts/run_stage42_daf_sac_500k_read_gate.sh all`:
- `SAC_500K_PASSIVE_RX_PACKETS_DELTA=15743`, 5 s,
  `SAC_500K_PASSIVE_DATA_FRAMES=15749`;
- `RX_ERRORS_DELTA=0`, `TX_PACKETS_DELTA=0`, `ERROR_FRAMES=0`;
- wszystkie zarejestrowane dane CAN miały
  `0x18FEAE30 [8] FF FF FE FE FF FF FF FF`.
  Obecność ruchu potwierdza poprawny fizyczny bitrate 500 kbit/s,
  ale kod `0xFE` na pozycjach ciśnień oznacza „niedostępne”,
  nie potwierdza sprawnych torów ciśnieniowych;
- aktywny profil: CAN Classic, nominal 500000 bit/s,
  `fd=0`, `listen_only=0`, `bus_off=0`;
- `SAC_PROFILE_SERVICE=FAIL uds_status=0 transport_failure=0 nrc=0x0`.
  W V2 `uds_status=0` jest `UdsStatus::ok`. Błąd nastąpił
  **po zaakceptowanej odpowiedzi UDS**, przy ścisłej walidacji
  odpowiedzi `22`: pozytywny SID, DID, długość i drukowalne ASCII.
  Nie wiadomo jeszcze, **który** z `F190/F188/F192` był przyczyną.
  Brak zarejestrowanej aktywnej odpowiedzi UDS w pierwszym runie;
- `SAC_500K_IDENTIFY=FAIL`, dlatego parametry i DTC nie były
  odczytane (prawidłowy fail-closed). `can0 DOWN` po teście.

Prywatny plik dowodowy nasłuchu:
`~/.ecu-platform-v2/daf-sac/500k-proof/read-500k-20261008T074846Z-420754.passive.log`.
Nie udostępniać pełnego VIN ani surowych logów aktywnego UDS publicznie.

## Korekta obserwowalności po pierwszym failu

Dodano do `IdentificationProgram` wyłącznie **metadane**
`IdentificationReplyDiagnostic`, bez przechowywania surowych
wartości VIN/software/hardware. Na błędzie probe wypisuje
`SAC_IDENT_REPLY_META`: `requested_did`, `observed_did`,
`response_length`, `issue` oraz, tylko dla niedrukowalnego bajtu,
jego `invalid_octet_offset` i `invalid_octet_value`.
Powody: `invalid-positive-header`, `unexpected-did`,
`invalid-text-length`, `non-printable-character` lub `none`
dla błędów transportowych/NRC. Parsowanie zostało **bez zmian** —
żadnego automatycznego akceptowania nieznanych danych OEM.

Aktualny runner 500k dodatkowo przechwytuje w trybie normal
aktywny ruch `0x18DA30F9/0x18DAF930` **przed wysłaniem** UDS,
zachowuje plik `*.active-uds.log` w prywatnym katalogu
`500k-proof` i wypisuje jedynie SHA-256 i lokalną ścieżkę.
Nie należy kopiować tego pliku do repo GitHub ani wklejać
niezamazanych wartości VIN w komunikatorach.

Testy symulacyjne: `62 F188` z NUL, `62 F188` bez danych
i `62` z nieoczekiwanym DID, w każdym przypadku UDS=OK,
NRC=0 i jednoznaczny powód odmowy; testy kończą się
bezpiecznym zamknięciem zasobów. Kompilacja/testy
Linux Debug, Release, Generic i ASan/UBSan: PASS lokalnie.

Kolejny **wyłącznie odczytowy** run tej samej komendy
`sudo bash scripts/run_stage42_daf_sac_500k_read_gate.sh all`
powinien dostarczyć konkretne metadane pozwalające
ustalić rzeczywisty format DID. Dopiero na podstawie
wyniku można rozważyć zmianę parsera konkretnego
wariantu SAC, bez rozluźniania walidacji 250k.

## Odczyt surowego F190 i poprawka dla braku VIN — 2026-10-08

Operator wykonał drugi fizyczny test 500k. CAN passive:
15742 pakiety / 5 s, zero RX errors, zero transmisji;
CAN Classic 500000 i `bus_off=0`. Aktywne UDS:
`SAC_IDENT_REPLY_META requested_did=0xF190 observed_did=0xF190
response_length=20 issue=non-printable-character
invalid_octet_offset=3 invalid_octet_value=0xFF`.
Dzięki rejestracji znamy przyczynę odrzucenia pierwszego DID.

Prywatny surowy ślad:
`~/.ecu-platform-v2/daf-sac/500k-proof/read-500k-20261008T075918Z-421777.active-uds.log`.
SHA-256: `0277343dd53b40d6e936fa58b9834d21173a9b305f0cdbcdeed4b064de1c8c57`.
Przeprowadzona wyłącznie lokalnie reassemblacja ISO-TP daje:
**jedną pozytywną odpowiedź `62 F1 90` długości 20 i dokładnie
17 bajtów `FF` jako całe pole VIN**, bez błędów sekwencji.
Poprzedza ją jedna ujemna odpowiedź przejściowa na `22`.
Nie ma odpowiedzi F188 ani F192, ponieważ stary parser przerwał
pracę po F190. Zawartość samego VIN nie była eksportowana ani
publikowana.

### Zasada interpretacji

`FF` razy 17 jest stanem **brak dostępnego/zaprogramowanego VIN**
w odpowiedzi DID, a **nie poprawnym numerem VIN**.
Przy 500k, wyłącznie na `F190`, dokładnie `62 F1 90` i 17x`FF`
otrzymuje status `vin_unprogrammed_ff17=true`,
z pozostawieniem pustego `TextField`.
Wariant 250k nie akceptuje tego stanu. Niedrukowalne bajty
o innych wzorcach, same zera, częściowe `FF`, zły DID czy zła
długość nadal dają błąd.

Dla poprawnie rozpoznanego znacznika w profilu 500k program
odczytuje dalej F188 i F192, zachowując ścisłą walidację tych
wartości. Brak VIN nie jest już powodem przerwania osobnych,
wyłącznie odczytowych sprawdzeń FE96 i DTC; wymagamy nadal
poprawnej identyfikacji pozostałych dwóch DID.

Operatorowy proof wypisuje:
`SAC_VIN_STATUS=UNPROGRAMMED_FF17`,
`SAC_IDENTIFICATION_COMPLETENESS=PARTIAL_NO_VIN`,
a **nie** `SAC_VIN=` z fikcyjnym identyfikatorem.
Aplikacja nie produkuje też w takim przypadku `vin_suffix`.

### Walidacja i granice

Symulator UDS/DUT i aplikacji sprawdza kompletne FF17 500k,
akceptację odczytów następnych DID, odrzucenie innych
niepoprawnych wariantów, zachowanie strict 250k, pusty
sufiks VIN GUI, brak wycieku zasobów i brak transmisji kasowania.
Brama Debug/Release/Generic/ASan+UBSan: **PASS lokalnie**.
Nie zmieniano CORE V2, Bench Runtime ani chronionego stałego
agenta 250k.

Pozostałe do ustalenia w fizycznym teście: odpowiedzi
`F188`, `F192`, `FE96`, `19 02 FF`.
**Nie twierdzimy, że fizyczne te usługi już działają**.
Można wykonać ponownie tę samą operatorową bramę
`sudo bash scripts/run_stage42_daf_sac_500k_read_gate.sh all`,
ponieważ jest ograniczona do odczytu, nie resetuje,
nie kasuje DTC ani nie steruje wyjściami.

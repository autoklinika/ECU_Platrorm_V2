# ECU Platform V2 — DAF SAC 500 kbit/s — pierwszy odczyt

Data: 2026-10-08
Gałąź: `stage4.3/daf-sac-500k-read-proof`
Status: **SOFTWARE PASS / FIZYCZNY ODCZYT 500 kbit/s JESZCZE NIEURUCHOMIONY**

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

## Jedyny fizyczny test operatora 500 kbit/s (odczyty)

**Po potwierdzeniu zasilania i gotowości nowego ECU** uruchomić na CM5
z interaktywnego terminala:

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
   VIN na standardowym wyjściu oraz w logach jest anonimizowany.
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
- Test błędnego stanu `can0`: obecny interfejs 250k/DOWN odrzuca
  probe 500k **przed transmisją**.
- Debug/Release/Generic/ASan+UBSan, testy architektury, brak TTY
  i inne regresje: wyniki odnotować po wykonaniu pełnej walidacji.

## Granice akceptacji

Sukces testów offline nie dowodzi fizycznej łączności z drugim SAC.
Test fizyczny wymaga jednorazowego uruchomienia skryptu z `sudo` przez
operatora — interfejs zdalnego terminala nie udostępnia uprawnienia
`CAP_NET_ADMIN` ani polecenia sudo. Nie wolno deklarować PASS na
500 kbit/s bez wyniku aktywnego testu i identyfikacji nowego ECU.
Po uzyskaniu wyniku rozwój powinien pozostać w DUT Profile / Bench
Application, o ile nie wystąpi rzeczywisty błąd CORE V2.

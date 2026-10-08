# DAF SAC — kontrolowana ponowna próba UDS ClearDiagnosticInformation

Data: 2026-10-08
Status: **PHYSICAL UDS 0x54 ACK CONFIRMED; AUTOMATIC POST-READ TIMEOUT; INDEPENDENT READ PASS (12 DTC); CLIENT PATCH REQUIRES FUTURE PHYSICAL VERIFICATION**
Repo: `stage4.2/dtc-read-clear-audit-20261008` (separate branch; no main merge).

## Znane fakty

- Poprzednia autoryzowana próba `14 FF FF FF` zakończyła się `CLEAR_OUTCOME=UNKNOWN`, bez potwierdzonego `54`. Archiwum jest nienaruszone: `~/.ecu-platform-v2/daf-sac/dtc-clear/sac-dtc-1791405524-408769.txt` (`0600`).
- UDS `10 03; 19 02 FF` działa na fizycznym SAC; 12 DTC, maska `0x8B`.
- Odczyt fizyczny FE96 2026-10-08: `permanent 28.2 V`, `ignition 28.1 V`, oba valid; `can0` po odczytach `DOWN`, brak sesji zasobów.
- Stały agent systemowy ograniczony do `sac.read_dtc` i `sac.read_parameters`; **brak** uprawnienia do `sac.clear_dtc` i nie należy rozszerzać go na stałe.
- Próba wystartowania `candump` gdy `can0` był DOWN skończyła się `read: Network is down`. W nowym runnerze sniffer startuje **po `can0 UP`, przed poleceniami diagnostycznymi**.
- UDS odpowiada `0x54` na pozytywne `0x14`, `7F 14 xx` przy odmowie, `0x78` ResponsePending nie jest końcową odmową. Nie wykazano dodatkowej OEM sekwencji DAF. Działania ograniczone do wcześniej stosowanego `10 03 + 14 FF FF FF`.

## Jednorazowy program testu — WYKONANY 2026-10-08

**UWAGA: ten etap jest już wykonany, a jednorazowy bilet został zużyty. Nie uruchamiać żadnego ponownego 0x14.** Poniższy zapis dokumentuje historyczną sekwencję, NIE jest instrukcją ponowienia.

**Nie** uruchamiać zwykłego `run_stage42_daf_sac_clear_gate.sh` przez obchodzenie archiwum. Jego ochrona nadal poprawnie zwraca `DTC_CLEAR_BLOCKED=PREVIOUS_OUTCOME_UNKNOWN`.

Zamiast tego istnieje odrębny, audytowalny punkt operatorowy:

```bash
cd ~/ECU_Platrorm_V2
bash scripts/validate_daf_sac_application.sh
sudo bash scripts/run_stage42_daf_sac_controlled_retest.sh
```

**Wymaga lokalnego interaktywnego terminala z `sudo` i człowieka przy stanowisku.** Nie jest częścią CI ani stałego agenta i nie można go wywołać przez polecenia stdin, automat lub API.

1. Sprawdzenie `can0 DOWN`, archiwum `UNKNOWN`, integralności wcześniejszego pliku (SHA-256, profil `0xDAF00025`, pełna maska `0xFF`) i braku wcześniejszego biletu retestu.
2. Świeży odczyt napięcia i pełnej listy DTC przez **read-only** agenta. Jeśli odczyty zawodzą lub DTC nie ma, przerwać bez kasowania.
3. Operator wpisuje `PONOW SAC 12 DTC PO UNKNOWN` (liczba pochodzi z archiwum). Powstaje trwale zapisany `controlled-retest-once.txt` (`0600`, `O_EXCL`, `fsync`), którego obecność blokuje powtórne użycie retestu nawet po crash/SIGKILL.
4. Dopiero teraz `can0` jest konfigurwany: 250 kbit/s, Classic normal, a `candump` rejestruje TX/RX obu stron diagnostycznego ISO-TP `0x18DA30F9` i `0x18DAF930`. Zrzuty tylko w prywatnym `~/.ecu-platform-v2/daf-sac/dtc-capture/`.
5. Operatorowy program fizyczny wykonuje ponownie `10 03 -> 19 02 FF`, zapisuje nową kopię DTC przed kasowaniem, żąda frazy `KASUJ SAC <liczba> DTC`. Dopiero po niej **jedno** `10 03 -> 14 FF FF FF`.
6. Analiza `0x54` versus `7F 14 xx` oraz `0x78` i timeout. Nie ma automatycznego retry i nie są zmieniane limity CORE.
7. Po zakończeniu programu: 8 s pasywnej obserwacji ewentualnej spóźnionej odpowiedzi, wyłączenie CAN i zamknięcie logu. Jeżeli `0x14` był rozpoczęty, dodatkowy wyłącznie odczytowy `sac.read_dtc` i zapis statusów po operacji.
8. Offline parser analizuje kompletną sekwencję pojedynczych i wieloramkowych UDS, raportuje liczby `0x14`, `0x54`, NRC `0x78`, inne NRC i liczbę DTC. Raport wraz z surowym zrzutem i konsolą pozostaje prywatny. Wynik ACK sam w sobie **nie dowodzi zera DTC**.

Przy zamknięciu zrzutu `can0 DOWN`. Nawet jeśli retest będzie pozytywny, poprzednie archiwum `UNKNOWN` pozostaje nienaruszone. Ticket pozostaje zużyty, aby zapobiec niekontrolowanemu ponowieniu.

## Wyniki walidacji offline

- `test_daf_sac_controlled_retest.py`: poprawny i błędny stan archiwum, jednoznaczne `UNKNOWN`, próby wielokrotne, symlink, zły tryb pliku, odmowa bez TTY, jednorazowy ticket z `fsync`, parser CAN przy `0x54`, NRC `0x22/0x31/0x33/0x72`, `0x78`, brak odpowiedzi, dublowanie `0x14`, 12-rekordowy ISO-TP FF/CF i ucięcie transferu.
- `validate_daf_sac_application.sh` nadal wykonuje dotychczasowe testy Debug/Release/Generic/ASan/UBSan i nowy negatywny test runnera w trybie bez TTY.
- Żaden test offline nie otwiera `can0` ani nie wysyła `0x14`.

## Zakończenie fizycznego testu — operator, 2026-10-08

Interaktywny operator uruchomił przygotowany runner ze swojego terminala. Potwierdzone jest rzeczywiste pojedyncze żądanie `14 FF FF FF`, pozytywna odpowiedź UDS `54`, a następnie nieudane natychmiastowe ponowne wejście w sesję w celu odczytu DTC. Rezultat testu destrukcyjnego: **UDS ACK, lecz brak dowodu trwałego usunięcia DTC**. Dalsza analiza i poprawki opisane niżej.

Stały agent nadal pozwala wyłącznie na odczyt. Użyty bilet jednorazowy jest zużyty, a poprzednie archiwa pozostają nienaruszone. Nie wykonuje się kolejnego `0x14` w celu sprawdzenia poprawki programowej. Nie modyfikowano CORE V2, Bench Runtime, ustawień mocy/ignition ani aktywnego `main`.

## Analiza śladu fizycznego i porównanie z legacy (2026-10-08)

Źródło: prywatny, niepublikowany zapis operatora:
`~/.ecu-platform-v2/daf-sac/dtc-capture/sac-clear-retest-20261008T065648Z-418611.candump`.
Istnieją też `.console.txt`, `.analysis.txt`, `.post-read.txt`, archiwum przed drugim kasowaniem
`sac-dtc-1791442609-418731.txt` i zużyty bilet
`controlled-retest-once.txt`. Nie przenosimy tych materiałów do publicznego repo.

Sekwencja rzeczywista: `10 03 -> 50 03 00 19 00 C8` (P2=25 ms,
P2*=2000 ms), `19 02 FF -> 7F 19 78 -> 59 02` z 12 DTC,
następnie **jedno** `14 FF FF FF -> 54`. Po ACK natychmiastowy
odczyt kontrolny wysłał `10 03`, a sniffer zarejestrował `50 03`,
lecz **nie było żadnego następnego `19 02 FF`**. Klient raportował
`UdsStatus::timeout_p2`, kod NRC `0x00`; nie był to odmowny
`7F 14 xx`. Surowy ślad obejmuje 19 ramek i nie zawiera błędów parsera.
`SAC_TRACE_RESULT=UDS_54_OBSERVED_POST_READ_NOT_REQUESTED` jest
precyzyjniejszym opisem niż historyczne `UDS_54_OBSERVED_VERIFY_POST_READ`.

Niezależny odczyt `sac.read_dtc` po nieudanej automatycznej
weryfikacji zakończył się **PASS, 12 DTC, maska 0x8B**.
Same obecne błędy nie pozwalają ustalić, czy poprzednia
kasacja usunęła historię i błędy pojawiły się ponownie. Brak gwarancji
„zero DTC”. Zasilanie przed próbą: 28,2 V / 28,1 V.

Porównanie referencyjne (tylko analiza, **nie kopiowano kodu**) z
repozytorium `autoklinika/ecu_platform`:
- `src/ecu/sac/SAC_DTC_Module.cpp`: te same żądania `10 03`,
  `19 02 FF`, `14 FF FF FF`; po `54` kończy operację, nie
  wysyła automatycznie nowego `19`;
- `src/uds/UDS_Core.h`: sztywny 3000 ms timeout;
- `src/QML/SACDTCPage.qml`: ponowny odczyt jest osobnym
  działaniem operatora (REFRESH).

W V2 zachowujemy osobne sesje Bench i stan UDS. Naprawa w warstwie
DUT Profile / operator SAC:
- client-side minimum P2 1000 ms i P2* 5000 ms **tylko dla SAC ReadDTC**
  (także po odpowiedzi `50 03`; dane OEM pozostają obserwacją),
  `ClearDTC` nadal ma swoje minimum P2 3000 ms;
- po pozytywnym `54` odczekanie 1000 ms przed nową sesją
  odczytową; jest to ostrożnościowy czas klienta, a nie
  zweryfikowany czas kasowania OEM;
- jeśli pierwszy odczyt kontrolny zawiedzie, zapis diagnostyki,
  `recover()` po zwolnieniu zasobów oraz **maksymalnie jeden**
  ponowny **ODCZYT** `10 03 + 19 02 FF` po dodatkowej 1000 ms pauzie;
  nigdy automatyczny retry `14 FF FF FF`;
- parser CAN osobno raportuje `POST_CLEAR_SESSION_ACKS`,
  `POST_CLEAR_READ_REQUESTS` i `POST_CLEAR_READ_RESPONSES`.
  `0x54` bez zarejestrowanej odpowiedzi `59` to ACK,
  a nie potwierdzony odczyt kontrolny.

Testy syntetyczne: odpowiedź SAC `50 03 00 19 00 C8`, 200 ms
opóźniona odpowiedź `59`, zgubiona pierwsza odpowiedź na sesję
po kasowaniu, timeout, `recover()`, bez ponownego `0x14` oraz
regresja analizatora na sekwencji ACK / sesja bez `19`.
CORE V2 i Bench Runtime pozostają bez zmian.

**Stan bramy po tej korekcie:**
software unit/gate PASS po walidacji; fizyczna naprawa
bez nowego destrukcyjnego testu pozostaje niezweryfikowana.
Oryginalny bilet jest zużyty; nie kasować ponownie w celu testowania kodu.

## Po aktualizacji klienta SAC — brama odczytu 2026-10-08

Po przebudowaniu `ecu_daf_sac_stage42_read_probe` z nowym
`ServiceProgram` wykonano jeszcze jeden test **tylko odczytowy**
przez stałego agenta. Wynik:
`ECU_BENCH_AGENT_STATUS=PASS`,
`SAC_STAGE42_READ_PHYSICAL=PASS`, `SAC_DTC_COUNT=12`,
`SAC_DTC_AVAILABILITY_MASK=0x8B`,
`SAC_BENCH_RESOURCE_LEASES=0`, `CAN0_CLEANUP=DOWN`.
`3A0002` zachował `0x02`; brak dowodu trwałego skasowania historii.

Dodatkowo doprecyzowano nazwy raportów: awaria fizycznego operatorowego
programu nosi teraz `SAC_DTC_CLEAR_CLI=FAIL`, a przypadek poprawnie
zarejestrowanego `54` z błędnym odczytem następczym jest klasyfikowany
`CLEAR_ACKNOWLEDGED_POST_READ_INCOMPLETE`, nie „niepotwierdzone kasowanie”.
To rozróżnia **pozytywną odpowiedź usługi** od **rezultatu weryfikacji**.

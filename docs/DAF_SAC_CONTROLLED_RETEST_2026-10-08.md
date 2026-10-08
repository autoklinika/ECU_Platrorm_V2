# DAF SAC — kontrolowana ponowna próba UDS ClearDiagnosticInformation

Data: 2026-10-08
Status: **PRE-FLIGHT READ PASS; SOFTWARE/SIMULATION PASS; PHYSICAL CLEAR NOT RUN**
Repo: `stage4.2/dtc-read-clear-audit-20261008` (separate branch; no main merge).

## Znane fakty

- Poprzednia autoryzowana próba `14 FF FF FF` zakończyła się `CLEAR_OUTCOME=UNKNOWN`, bez potwierdzonego `54`. Archiwum jest nienaruszone: `~/.ecu-platform-v2/daf-sac/dtc-clear/sac-dtc-1791405524-408769.txt` (`0600`).
- UDS `10 03; 19 02 FF` działa na fizycznym SAC; 12 DTC, maska `0x8B`.
- Odczyt fizyczny FE96 2026-10-08: `permanent 28.2 V`, `ignition 28.1 V`, oba valid; `can0` po odczytach `DOWN`, brak sesji zasobów.
- Stały agent systemowy ograniczony do `sac.read_dtc` i `sac.read_parameters`; **brak** uprawnienia do `sac.clear_dtc` i nie należy rozszerzać go na stałe.
- Próba wystartowania `candump` gdy `can0` był DOWN skończyła się `read: Network is down`. W nowym runnerze sniffer startuje **po `can0 UP`, przed poleceniami diagnostycznymi**.
- UDS odpowiada `0x54` na pozytywne `0x14`, `7F 14 xx` przy odmowie, `0x78` ResponsePending nie jest końcową odmową. Nie wykazano dodatkowej OEM sekwencji DAF. Działania ograniczone do wcześniej stosowanego `10 03 + 14 FF FF FF`.

## Jednorazowy program testu

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

## Znany blocker fizycznego wykonania

Zdalne narzędzie terminalowe ECU nie dopuszcza wykonania polecenia `sudo`. Stały agent poprawnie nie posiada operacji kasowania. **Nie obchodzimy ograniczeń uprawnień**. W rezultacie nie da się uczciwie oznaczyć fizycznego retestu jako PASS bez uruchomienia interaktywnego operatorowego polecenia na stanowisku. Skrypt jest gotowy do jednorazowego wykonania w tym kontekście; przed nim warto potwierdzić stabilne zasilanie ECU i przechowywanie archiwum.

Nie modyfikowano CORE V2, Bench Runtime, ustawień mocy/ignition ani aktywnego `main`.

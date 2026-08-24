# Scanmatik 2 + Sonceboz EGR — J2534 PoC

Minimalny test laboratoryjny sprawdzający, czy **Scanmatik 2** może być transportem J2534 dla sterowania MAN/Sonceboz EGR w ECU Platform V2.

## Cel

Test ma odpowiedzieć na cztery pytania:

1. Czy przez Scanmatik 2/J2534 widzimy ramki statusowe Sonceboz EGR na klasycznym CAN 29-bit?
2. Czy `PassThruStartPeriodicMsg` potrafi utrzymywać dwie wymagane ramki TX co 20 ms?
3. Czy EGR reaguje na zadaną pozycję i nadal regularnie wysyła feedback?
4. Czy zatrzymanie testu/timeout poprawnie kończy periodic TX?

To **nie jest jeszcze kod ECU Platform V2 ani finalny ActuatorEngine**. To izolowany PoC transportu J2534.

## Parametry protokołu przeniesione z legacy ECU Platform

- CAN extended 29-bit
- automatyczne sprawdzenie: 250 kb/s, potem 500 kb/s
- TX co 20 ms:
  - `0x18FF2700` — `01 02 <RAW_L> <RAW_H> 7D 00 00 FF`
  - `0x1AFFB400` — `01 01 <RAW_L> <RAW_H> 7D 00 00 FF`
- RX:
  - `0x1AFFB580` — primary/actual position
  - `0x1AFFB680` — auxiliary status
- aktualna pozycja: LE16 z bajtów payload `[4..5]`, `percent = raw / 10`
- zadanie pozycji: `raw = min(percent * 10, 999)`
- EGR ONLINE po minimum 3 ramkach statusowych
- timeout obecności: 1500 ms
- safe stop: zatrzymanie obu periodic TX

## Wymagania

- Windows 10/11
- Scanmatik 2 podłączony **przez USB**
- zainstalowany aktualny pakiet Scanmatik / J2534-RP1210
- zamknięta aplikacja diagnostyczna Scanmatik oraz inne programy używające J2534
- EGR prawidłowo zasilony na stanowisku laboratoryjnym
- CAN-H/CAN-L Scanmatika połączone z EGR
- mechanicznie zabezpieczony aktuator i łatwo dostępne fizyczne odcięcie jego zasilania

Scanmatik 2 ma wbudowane rezystory CAN 1 kΩ; producent podaje, że na stole dodatkowe 120 Ω często nie jest konieczne, ale nie zabrania jego zastosowania. Terminację dobieramy do rzeczywistej topologii stanowiska.

## 1. Najpierw self-test bez sprzętu

Z katalogu repo:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\tools\scanmatik2_egr_test\scanmatik2_egr_test.ps1 -SelfTest
```

Oczekiwane:

```text
SELFTEST PASS: Sonceboz frame encoding is consistent with legacy protocol.
```

## 2. Pierwsze uruchomienie — tylko nasłuch

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\tools\scanmatik2_egr_test\scanmatik2_egr_test.ps1
```

Skrypt:

- znajdzie `FunctionLibrary` Scanmatika w rejestracji J2534 v04.04,
- jeśli wykryje typową 32-bitową bibliotekę z `Program Files (x86)`, sam uruchomi 32-bit Windows PowerShell,
- otworzy adapter,
- sprawdzi 250 i 500 kb/s,
- uzna EGR za ONLINE po 3 ramkach `0x1AFFB580/0x1AFFB680`,
- pokaże aktualną pozycję,
- **nie wyśle żadnej ramki sterującej**,
- zapisze CSV do `tools/scanmatik2_egr_test/logs/`.

## 3. Pierwszy krótki test ruchu

Najpierw odczytaj pozycję w trybie listen-only. Dla pierwszej próby wybierz cel blisko aktualnej pozycji, np. o około 5 punktów procentowych.

Przykład:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\tools\scanmatik2_egr_test\scanmatik2_egr_test.ps1 -TargetPercent 15 -DurationSec 3
```

Przed rozpoczęciem transmisji skrypt pokaże pozycję aktualną, cel i dokładne ramki oraz wymaga wpisania **dokładnie `START`**. Dopiero wtedy uruchomi dwie wiadomości okresowe J2534 co 20 ms.

Po pierwszej udanej walidacji można pominąć ręczne `START` przez `-Yes`.

## Parametry opcjonalne

Wymuszenie bitrate:

```powershell
... -Bitrate 250000
```

lub:

```powershell
... -Bitrate 500000
```

Wymuszenie konkretnej biblioteki J2534:

```powershell
... -DllPath 'C:\Program Files (x86)\Scanmatik\smj2534.dll'
```

## Zachowanie bezpieczeństwa

Sterowanie nie wystartuje, dopóki adapter nie zostanie otwarty, nie zostanie znaleziony prawidłowy bitrate, EGR nie wyśle co najmniej 3 ramek statusowych i operator nie potwierdzi `START` (chyba że jawnie użyto `-Yes`).

Po uruchomieniu skrypt zatrzymuje periodic TX, gdy:

- przez >1500 ms nie przychodzi status EGR,
- J2534 zwróci błąd,
- skończy się `DurationSec`,
- skrypt wychodzi przez wyjątek / blok `finally`.

Dodatkowo podczas cleanup wykonywane jest `CLEAR_PERIODIC_MSGS`.

**Uwaga:** żaden program na PC nie zastępuje fizycznego zabezpieczenia stanowiska. Pierwsze próby wykonujemy z krótkim czasem i celem blisko pozycji aktualnej.

## Pomiar timingu

`PassThruStartPeriodicMsg` zleca okresową transmisję sterownikowi/adapterowi, więc Windows nie generuje każdej ramki osobnym timerem aplikacji.

Jeżeli sterownik Scanmatika zwróci echo własnych ramek TX z timestampami J2534, tester policzy maksymalny zaobserwowany odstęp. Jeśli echo nie jest dostępne, dokładny jitter 20 ms zweryfikujemy drugim analizatorem CAN / oscyloskopem.

## Co wysłać po teście

Po teście potrzebne są:

1. cały tekst z konsoli PowerShell,
2. wygenerowany plik `logs/sm2_egr_*.csv`,
3. informacja, czy ruch fizyczny odpowiadał zadanej pozycji.

Na tej podstawie ocenimy, czy J2534/Scanmatik nadaje się również do warstwy Actuator Runtime wersji Desktop, czy tylko do diagnostyki i zwykłego transportu CAN.

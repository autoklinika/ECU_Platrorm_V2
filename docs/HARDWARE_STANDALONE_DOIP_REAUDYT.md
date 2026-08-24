# ECU Platform V2 — reaudyt hardware standalone z DoIP

> **Status:** dokument roboczy / research, 2026-08-24
>
> Cel: ponowna ocena możliwie najtańszego hardware dla wersji standalone ECU Platform V2 po dodaniu wymagania DoIP / Ethernet diagnostycznego. Dokument nie oznacza wyboru platformy produkcyjnej.

## 1. Wniosek główny

- **[USTALONE]** DoIP nie wymusza użycia NXP i.MX95. Obsługa ISO 13400 może działać na znacznie tańszych MPU/SoM z Linuxem i Ethernetem.
- **[DO WERYFIKACJI]** Najbardziej obiecujący kosztowo wariant standalone to obecnie TI AM62 / AM62P.
- **[DO WERYFIKACJI]** NXP i.MX95 pozostaje wariantem premium z dużym zapasem mocy i I/O, ale nie jest obecnie wyborem kosztowo optymalnym.
- **[DO WERYFIKACJI]** STM32MP257F jest bardzo mocnym kandydatem do przyszłej własnej płyty bez SoM, szczególnie przy większym wolumenie.

## 2. Minimalne wymagania wynikające z obecnych założeń V2

Hardware standalone powinien docelowo umożliwiać:

- Linux / system aplikacyjny dla Core, GUI i WebGUI,
- DoIP / ISO 13400 po Ethernet,
- co najmniej 2–3 CAN-FD, docelowa liczba kanałów nadal do ustalenia,
- wykonywanie sterowania czasowo-krytycznego poza GUI i poza niedeterministyczną częścią Linuxa,
- hardware Root of Trust / Secure Boot / bezpieczne klucze,
- lokalny HMI,
- możliwość dodania 100BASE-T1 / 1000BASE-T1 przez zewnętrzny automotive PHY,
- długi lifecycle produkcyjny.

## 3. Kandydat kosztowy nr 1 — TI AM62 / VAR-SOM-AM62

Aktualne cechy:

- do 4× Cortex-A53 1.4 GHz,
- Cortex-M4F 400 MHz,
- PRU 333 MHz do zadań real-time,
- 3× CAN/CAN-FD na SoM,
- 2× GbE,
- USB 2.0,
- zintegrowana grafika i wyjścia display,
- Secure Boot i HSM / security subsystem,
- warianty industrial,
- Linux + SDK dla M4F.

Publiczna cena Variscite:

- baseline VAR-SOM-AM62: **od 53 USD**,
- publiczna konfiguracja 2 GB RAM / 16 GB eMMC / Wi-Fi: **240 USD**,
- producent oferuje konfiguracje cost-optimized od 25 szt. po RFQ.

Ocena:

- **najtańszy sensowny SoM**, który nadal daje Linux + dual Ethernet + 3 CAN + osobny rdzeń real-time,
- DoIP może działać na Cortex-A53/Linux,
- M4F/PRU może zostać wykorzystany do schedulerów CAN, watchdogów i sterowania aktuatorami,
- wymaga praktycznej walidacji, czy zasoby M4F/PRU oraz dostęp do wymaganych CAN wystarczą dla naszego docelowego Actuator Runtime.

## 4. Kandydat koszt/funkcje nr 1 — TI AM62P / VAR-SOM-AM62P

Aktualne cechy:

- 4× Cortex-A53 1.4 GHz,
- Cortex-R5F 800 MHz,
- 4× CAN-FD,
- 2× GbE/TSN,
- mocniejsza grafika i multimedia niż AM62,
- Hardware Root of Trust, Secure Boot, HSM, TEE/TrustZone, anti-rollback, secure storage,
- wariant AM62P-Q1 automotive / AEC-Q100,
- Linux + niezależny rdzeń real-time.

Publiczna cena Variscite:

- baseline VAR-SOM-AM62P 1 GB RAM / 8 GB eMMC: **od 69 USD**,
- publiczna konfiguracja 4 GB / 16 GB / Wi-Fi: **299 USD**,
- konfiguracje cost-optimized dostępne przez RFQ.

Ocena:

- dopłata do baseline AM62 wynosi tylko **16 USD**, a otrzymujemy 4 CAN-FD i znacznie mocniejszy rdzeń R5F,
- z punktu widzenia ECU Platform V2 jest obecnie najbardziej racjonalnym kompromisem koszt / realtime / CAN / DoIP / security,
- dla produktu standalone byłby dziś pierwszym kandydatem do realnego prototypu sprzętowego.

## 5. NXP i.MX95 / DART-MX95 — wariant premium

- do 6× Cortex-A55,
- Cortex-M7 + Cortex-M33,
- 5× CAN-FD,
- 2× GbE + 10GbE,
- EdgeLock Secure Enclave,
- bardzo duży zapas CPU, RAM i I/O.

Publiczna cena baseline DART-MX95: **od 93 USD**.

Ocena po reaudytcie:

- nadal technicznie znakomity,
- ale jego przewagi (6×A55, 10GbE, bardzo duża moc) nie są potrzebne do samego DoIP,
- sensowny jako wersja premium / bardzo rozwojowa, nie jako najtańsza baza produktu.

## 6. STM32MP257F — bardzo ciekawy dla własnej płyty produkcyjnej

STM32MP257F oferuje:

- 2× Cortex-A35 do 1.5 GHz,
- Cortex-M33 400 MHz,
- 3× Ethernet,
- 3× CAN-FD,
- USB 3.0/2.0,
- 3D GPU,
- Secure Boot, kryptografię, DRAM encryption/decryption, PKA,
- OpenSTLinux i STM32CubeMP2.

Publiczna cena samego MPU STM32MP257F wynosi obecnie około **20 USD przy 100 szt.** i około **19 USD przy 250 szt.** w sklepie ST dla jednego z wariantów przemysłowych.

Istnieją gotowe SoM-y, np. MYIR MYC-LF257:

- STM32MP257F 1.5 GHz,
- 1 GB LPDDR4,
- 8 GB eMMC,
- -40…+85°C,
- publiczna cena detaliczna około **150–156 EUR/szt.** przed rabatem seryjnym.

Ocena:

- gotowy SoM nie jest obecnie tańszy od baseline AM62P,
- ale **własna płyta z bezpośrednio montowanym STM32MP257F** może być bardzo konkurencyjna przy większej serii,
- wymaga jednak pełnego projektu DDR/PMIC/eMMC/high-speed PCB i większego NRE.

## 7. NXP i.MX91 / i.MX93

### i.MX91

- 1× Cortex-A55,
- 2× CAN-FD,
- 2× GbE,
- EdgeLock Secure Enclave,
- baseline SoM około 54–55 USD.

Minus: brak osobnego mocnego rdzenia real-time dla actuator runtime; wymagałby dodatkowego MCU lub kompromisu architektonicznego.

### i.MX93

- 2× Cortex-A55,
- Cortex-M33,
- 2× CAN-FD,
- 2× GbE,
- EdgeLock,
- baseline DART-MX93 od **59 USD**.

Ocena: atrakcyjny cenowo, ale 2 CAN-FD ograniczają zapas rozwojowy. Po dodaniu zewnętrznych CAN/MCU koszt i złożoność zbliżają się do AM62P.

## 8. Microchip SAMA7D65 — dużo CAN, ale słabszy model realtime

- Cortex-A7 do 1 GHz,
- 5× CAN-FD,
- 2× GbE/TSN,
- USB HS,
- LCD/2D GPU do klasy 720p/WXGA,
- Secure Boot, TrustZone, OTP, PUF i hardware crypto.

Ocena:

- bardzo ciekawy jako tani gateway/VCI,
- brak niezależnego Cortex-M/R oznacza, że dla naszego actuator runtime prawdopodobnie potrzebny byłby osobny MCU,
- po dodaniu MCU przestaje być oczywistym zwycięzcą kosztowym nad AM62P.

## 9. Rewidowany ranking standalone + DoIP

### Najlepszy koszt całkowity / najprostszy produkt

1. **TI AM62P / VAR-SOM-AM62P** — obecny faworyt.
2. **TI AM62 / VAR-SOM-AM62** — wariant budżetowy do walidacji.
3. **STM32MP257F** — szczególnie mocny kandydat przy własnej płycie i większym wolumenie.
4. **NXP i.MX95** — wariant premium z dużym zapasem.
5. **NXP i.MX93** — dobry, ale ograniczony do 2 CAN-FD.
6. **Microchip SAMA7D65** — ciekawy, jeśli real-time przeniesiemy do osobnego MCU.

## 10. Wniosek kosztowy

Nie należy obecnie projektować V2 standalone wokół i.MX95 tylko dlatego, że posiada DoIP-capable Ethernet i wiele CAN. DoIP nie wymaga takiej mocy.

Najbardziej racjonalna ścieżka jest obecnie następująca:

- **prototyp / pierwsza wersja:** AM62P SoM + własny carrier,
- **wariant budżetowy do porównania:** AM62 SoM + własny carrier,
- **przy większym wolumenie:** rozważyć odejście od SoM i projekt własnej płyty wokół AM62P lub STM32MP257F,
- **i.MX95:** pozostawić jako opcję premium / high-end, jeśli dalsze wymagania rzeczywiście wykorzystają jego dodatkową moc, CAN i I/O.

## 11. Otwarte decyzje przed wyborem hardware

- **[DO USTALENIA]** minimalna i docelowa liczba fizycznych CAN-FD,
- **[DO USTALENIA]** wymagania izolacji każdego CAN,
- **[DO USTALENIA]** wymagania dla 100BASE-TX, 100BASE-T1 i 1000BASE-T1,
- **[DO USTALENIA]** rozdział zadań Linux ↔ Cortex-R/M/PRU,
- **[DO WERYFIKACJI]** jitter i deterministyczność actuator runtime na AM62 M4F/PRU oraz AM62P R5F,
- **[DO WERYFIKACJI]** rzeczywiste konfiguracje SoM 2 GB / 16–32 GB bez zbędnych Wi-Fi/audio po RFQ dla 100/500/1000 szt.,
- **[DO WERYFIKACJI]** koszt NRE i unit BOM dla własnej płyty bez SoM przy 500/1000/5000 szt.

# ECU Platform V2 — kandydaci hardware i koszty robocze

> **Status:** dokument roboczy / research, 2026-08-22
>
> Nie oznacza wyboru docelowego hardware. Ceny seryjne SoM dla 100–1000 szt. są zwykle ofertowe; wszystkie widełki produkcyjne poniżej są estymacją do potwierdzenia formalnymi RFQ. Kwoty BOM podano orientacyjnie netto i bez kosztu lokalnego wyświetlacza, obudowy, okablowania, certyfikacji, montażu końcowego i marży.

## 1. Kryteria dla hardware produkcyjnego

- [USTALONE] Docelowy hardware nie jest jeszcze wybrany.
- [USTALONE] Aktualny rozwój może odbywać się na Linuxie, ale Core pozostaje platform-agnostic.
- [USTALONE] Hardware produktu komercyjnego musi być oceniany również pod kątem ochrony produktu: Secure Boot / hardware Root of Trust / bezpieczne klucze / możliwość hardware-bound device identity.
- [DO USTALENIA] Docelowa liczba fizycznych kanałów CAN/CAN-FD.
- [DO USTALENIA] Czy wszystkie kanały CAN mają być izolowane galwanicznie.
- [DO USTALENIA] Docelowa ilość RAM/eMMC, obecność Wi-Fi/Bluetooth i wymagany zakres temperatury.
- [DO USTALENIA] Czy funkcje real-time/actuator pozostają w Linux Core, czy część trafi na dedykowany Cortex-R/M.

## 2. Aktualny shortlist

### A. TI AM62P-Q1 / VAR-SOM-AM62P

Najbardziej zbalansowany kandydat koszt/funkcje.

- do 4× Cortex-A53 1.4 GHz
- Cortex-R5F 800 MHz do real-time
- 4× CAN-FD do 8 Mb/s
- 2× zewnętrzny GbE/TSN
- Hardware Root of Trust, Secure Boot, HSM, TEE/TrustZone, anti-rollback, secure storage
- AM62P-Q1: AEC-Q100, automotive
- SoM Variscite: do 8 GB RAM, do 64 GB eMMC, industrial -40…+85°C

Publiczne punkty cenowe Variscite (nie są ceną naszego docelowego wariantu):

- baseline 1 GB RAM / 8 GB eMMC: **69 USD ≈ 254 PLN**
- stock/prototype 4 GB / 16 GB / Wi-Fi: **299 USD ≈ 1 102 PLN**
- Starter Kit: **389 USD ≈ 1 433 PLN** przed lokalnymi podatkami/wysyłką

Robocza estymacja docelowego SoM 4 GB / 16–32 GB, wersja przemysłowa, po RFQ:

| Wolumen | Estymacja SoM / szt. |
|---:|---:|
| 100 | 380–600 PLN |
| 500 | 340–520 PLN |
| 1000 | 320–480 PLN |

### B. NXP i.MX95 / DART-MX95

Największy zapas mocy i najlepsza heterogeniczność CPU.

- do 6× Cortex-A55
- Cortex-M7 + Cortex-M33
- 5× CAN-FD
- EdgeLock Secure Enclave / Root of Trust / Secure Boot
- 2× GbE + 10GbE, PCIe
- SoM Variscite z longevity deklarowanym do 2039

Publiczne punkty cenowe Variscite:

- baseline 4×A55 / 4 GB RAM / 8 GB eMMC / 0…70°C: **93 USD ≈ 343 PLN**
- stock/prototype 6×A55 / 8 GB / 32 GB / Wi-Fi / extended temp: **455 USD ≈ 1 676 PLN**
- Starter Kit: **569 USD ≈ 2 096 PLN** przed lokalnymi podatkami/wysyłką

Robocza estymacja docelowego SoM 4–8 GB / 32 GB, przemysłowego, po RFQ:

| Wolumen | Estymacja SoM / szt. |
|---:|---:|
| 100 | 600–900 PLN |
| 500 | 520–800 PLN |
| 1000 | 480–730 PLN |

### C. Renesas RZ/G3E

Bardzo mocny kandydat, szczególnie jeśli priorytetem stanie się duża liczba natywnych CAN-FD.

- 4× Cortex-A55 1.8 GHz
- Cortex-M33
- **6× CAN-FD**
- 2× GbE
- PCIe Gen3
- Secure Boot, Device Unique ID, OTP, TRNG, Secure Crypto Engine, JTAG disable
- Renesas deklaruje 15-letni Product Longevity Program
- dostępne/ogłaszane produkcyjne SoM-y ekosystemu m.in. MXT OSM-L

Publiczna seryjna cena odpowiedniej konfiguracji SoM nie została znaleziona — wymaga RFQ.

Robocza estymacja SoM 4 GB / 16–32 GB:

| Wolumen | Estymacja SoM / szt. |
|---:|---:|
| 100 | 500–800 PLN |
| 500 | 450–700 PLN |
| 1000 | 420–650 PLN |

### D. STM32MP257F / PHYTEC phyFLEX-STM32MP25x

Konserwatywny kandydat przemysłowy z mocnym ekosystemem ST.

- 2× Cortex-A35 do 1.5 GHz
- Cortex-M33
- 3× CAN-FD
- do 3× Ethernet
- Secure Boot, cryptography, DRAM encryption/decryption, PKA
- PHYTEC SoM: do 4 GB RAM / do 256 GB eMMC / -40…+85°C
- PHYTEC development kit: **329 USD ≈ 1 212 PLN** przed lokalnymi podatkami/wysyłką

Publiczna cena samego aktualnego SoM w wymaganej konfiguracji nie została znaleziona — wymaga RFQ.

Robocza estymacja SoM 4 GB / 16–32 GB:

| Wolumen | Estymacja SoM / szt. |
|---:|---:|
| 100 | 400–650 PLN |
| 500 | 360–580 PLN |
| 1000 | 330–540 PLN |

## 3. Roboczy koszt naszej płyty carrier / elektroniki poza SoM

Założenie kalkulacyjne do porównania platform, a nie finalny BOM:

- własna wielowarstwowa carrier PCB,
- 4 fizyczne kanały CAN-FD z transceiverami, ochroną ESD/surge i roboczo zakładaną izolacją,
- wejście zasilania klasy warsztat/pojazd 9–32 V z zabezpieczeniami,
- Ethernet,
- USB,
- RTC/watchdog,
- ignition/sense i podstawowe I/O,
- złącza,
- PCB + SMT/THT assembly + test podstawowy.

| Element wspólny | 100 szt. | 500 szt. | 1000 szt. |
|---|---:|---:|---:|
| PCB + montaż | 220–330 PLN | 150–230 PLN | 120–190 PLN |
| 4× CAN-FD + izolacja + protection | 120–200 PLN | 100–160 PLN | 90–140 PLN |
| zasilanie 9–32 V + automotive protection | 90–150 PLN | 75–125 PLN | 65–110 PLN |
| Ethernet/USB/RTC/watchdog/I/O/złącza | 120–170 PLN | 95–135 PLN | 85–120 PLN |
| **Razem carrier poza SoM** | **550–850 PLN** | **420–650 PLN** | **360–560 PLN** |

## 4. Szacowany koszt kompletnej elektroniki urządzenia

SoM + nasza płyta carrier wg powyższych założeń. **Bez ekranu, obudowy, przewodów, końcowego montażu, certyfikacji i marży.**

| Platforma | 100 szt. | 500 szt. | 1000 szt. |
|---|---:|---:|---:|
| **TI AM62P-Q1** | **930–1 450 PLN** | **760–1 170 PLN** | **680–1 040 PLN** |
| **STM32MP257F** | **950–1 500 PLN** | **780–1 230 PLN** | **690–1 100 PLN** |
| **Renesas RZ/G3E** | **1 050–1 650 PLN** | **870–1 350 PLN** | **780–1 210 PLN** |
| **NXP i.MX95** | **1 150–1 750 PLN** | **940–1 450 PLN** | **840–1 290 PLN** |

Wniosek na tym etapie: różnica między platformami SoM jest istotna, ale nie dominuje całkowicie ceny gotowego urządzenia. Przy serii 1000 szt. przejście z AM62P do i.MX95 może podnieść koszt elektroniki orientacyjnie o ok. 160–250 PLN/szt. w dolnej części widełek, a daje znacznie większy zapas CPU i dodatkowy CAN-FD. Decyzji nie należy więc podejmować wyłącznie na podstawie ceny samego SoM.

## 5. Koszty nieuwzględnione w powyższym BOM

- panel LCD/touch lub inne lokalne HMI,
- obudowa i mechanika,
- przewody / adaptery / złącza diagnostyczne klienta,
- zewnętrzne akcesoria,
- opakowanie,
- montaż końcowy i test produkcyjny pełnego urządzenia,
- NRE projektu carrier PCB i prototypów,
- EMC/RED/CE i inne badania/certyfikacja,
- narzędzia produkcyjne / test fixture,
- provisioning kryptograficzny i infrastruktura kluczy,
- software development i utrzymanie produktu,
- VAT, cło, transport i marża dystrybucyjna.

## 6. Stan decyzji

- **[DO USTALENIA]** Nie wybieramy jeszcze SoM produkcyjnego.
- **[DO WERYFIKACJI]** TI AM62P-Q1 i NXP i.MX95 pozostają obecnie najmocniejszymi kandydatami do bezpośredniego porównania.
- **[DO WERYFIKACJI]** Renesas RZ/G3E pozostaje kandydatem ze względu na 6× CAN-FD i mocne I/O, ale wymaga ustalenia ceny/ekosystemu SoM i dostępności.
- **[DO WERYFIKACJI]** STM32MP257F pozostaje kandydatem konserwatywnym; wymaga RFQ na konkretny SoM.
- **[DO WERYFIKACJI]** Przed decyzją należy wysłać RFQ dla tej samej funkcjonalnie konfiguracji (RAM/eMMC/temp/no-Wi-Fi lub Wi-Fi) przy 100/500/1000 szt., aby porównanie cen było uczciwe.

## 7. Źródła cen/specyfikacji użyte do bieżącej estymacji

- Variscite VAR-SOM-AM62P product/shop — publiczne ceny baseline, prototype i starter kit.
- Variscite DART-MX95 product/shop — publiczne ceny baseline, prototype i starter kit.
- Texas Instruments AM62P-Q1 — oficjalna specyfikacja CAN-FD/security/automotive.
- Renesas RZ/G3E — oficjalna specyfikacja i Product Longevity Program.
- ST STM32MP257F — oficjalna specyfikacja CAN-FD/security.
- PHYTEC phyFLEX-STM32MP25x — oficjalna specyfikacja SoM i cena development kit.
- Kurs przeliczeniowy: tabela A NBP z 2026-08-21, **1 USD = 3,6839 PLN**.

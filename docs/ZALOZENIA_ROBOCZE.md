# ECU Platform V2 — założenia robocze

> **Status dokumentu:** żywy dokument roboczy
>
> Ten dokument nie jest finalną specyfikacją ani dokumentem architektury. Powstaje stopniowo podczas rozmów projektowych. Zapisujemy tutaj wyłącznie ustalenia, otwarte decyzje i kwestie wymagające weryfikacji. Dopiero po wyczerpaniu założeń zostanie z niego zbudowana właściwa dokumentacja projektu.

## Oznaczenia

- **[USTALONE]** — decyzja zaakceptowana i traktowana jako aktualne założenie projektu.
- **[DO USTALENIA]** — temat pozostaje otwarty.
- **[DO WERYFIKACJI]** — wymaga sprawdzenia starego repozytorium, dokumentacji, protokołu lub fizycznego sprzętu.

---

## 1. Charakter projektu

- **[USTALONE]** ECU Platform V2 powstaje jako nowy projekt w nowym repozytorium.
- **[USTALONE]** ECU Platform V2 ma być ogólną, rozwijalną platformą inżyniersko-diagnostyczną, a nie aplikacją zbudowaną pod jeden konkretny ECU, pojazd lub aktuator.
- **[USTALONE]** Platforma ma obsługiwać pracę zarówno ze sterownikami/modułami na stole, jak i — tam gdzie jest to technicznie i bezpiecznie uzasadnione — w pojeździe.
- **[USTALONE]** Zakładany zakres platformy obejmuje co najmniej: komunikację z ECU/modułami, identyfikację sterowników, diagnostykę, odczyt i kasowanie DTC, dane live/runtime, sterowanie aktuatorami, automatyczne procedury i testy, skanowanie i analizę CAN, rejestrację komunikacji oraz generowanie raportów.
- **[USTALONE]** Platforma ma posiadać lokalny interfejs użytkownika oraz WebGUI.
- **[USTALONE]** Rozwijalność jest wymaganiem fundamentalnym: dodanie w przyszłości nowego ECU, modułu, urządzenia wykonawczego, protokołu, transportu, klienta lub innej klasy obsługiwanych elementów nie może wymagać przebudowy całej platformy.
- **[USTALONE]** Architektura ma zapewniać stabilne granice odpowiedzialności i kontrakty między Core a modułami funkcjonalnymi, tak aby rozszerzenia można było dodawać lokalnie zamiast zmieniać wiele niezwiązanych części systemu.
- **[USTALONE]** Nie zakładamy z góry zamkniętej listy obsługiwanych ECU, protokołów ani urządzeń. V2 ma być bazą do dalszego wieloletniego rozwoju.
- **[USTALONE]** Nie wykonujemy mechanicznej migracji starego repozytorium `ecu_platform`.
- **[USTALONE]** Stare repozytorium traktujemy jako źródło wiedzy, zweryfikowanych protokołów, parametrów komunikacji, wyników testów oraz wybranych implementacji referencyjnych.
- **[USTALONE]** Architektura V2 jest projektowana od podstaw, bez obowiązku zachowania historycznej struktury starego projektu.
- **[USTALONE]** Projekt będzie od początku prowadzony razem z dokumentacją.

## 2. Sposób prowadzenia założeń i dokumentacji

- **[USTALONE]** Nie tworzymy od razu pełnej, zamkniętej specyfikacji.
- **[USTALONE]** Najpierw przeprowadzamy wyczerpującą rozmowę o założeniach projektu.
- **[USTALONE]** W trakcie rozmowy kolejne zaakceptowane ustalenia są dopisywane do tego dokumentu punktowo.
- **[USTALONE]** Tematy mogą być omawiane w dowolnej kolejności; dokument ma być porządkowany według obszarów, a nie kolejności rozmowy.
- **[USTALONE]** Kwestie nierozstrzygnięte są jawnie oznaczane jako `[DO USTALENIA]`.
- **[USTALONE]** Wiedza wymagająca potwierdzenia w legacy repo, dokumentacji lub na sprzęcie jest oznaczana jako `[DO WERYFIKACJI]`.
- **[USTALONE]** Dopiero po zamknięciu głównych założeń powstaną docelowe dokumenty: architektura, wymagania, dokumentacja protokołów, ADR-y, strategia testów itd.

## 3. Fundamentalne zasady architektury

- **[USTALONE]** Core jest źródłem prawdy o stanie systemu.
- **[USTALONE]** GUI nie może być właścicielem logiki sterowania urządzeniem.
- **[USTALONE]** GUI lokalne ma być klientem Core.
- **[USTALONE]** WebGUI ma być klientem Core.
- **[USTALONE]** Krytyczne czasowo sterowanie nie może zależeć od GUI, renderowania, event loop warstwy prezentacji ani od aktywności użytkownika.
- **[USTALONE]** Klient wysyła polecenia wysokiego poziomu; sposób bezpiecznego wykonania polecenia należy do Core.
- **[USTALONE]** Fizyczny interfejs CAN / hardware musi mieć jednoznacznego właściciela i kontrolowany mechanizm arbitrażu dostępu.
- **[USTALONE]** Awaria lub restart GUI nie może powodować destabilizacji warstwy komunikacji i sterowania.
- **[USTALONE]** Funkcje specyficzne dla konkretnego ECU, aktuatora lub protokołu nie mogą wymuszać zmian w niezwiązanych modułach systemu.
- **[DO USTALENIA]** Dokładny podział procesów: osobny `ecu-platform-core.service` i oddzielne klienty vs inny model wdrożeniowy.
- **[DO USTALENIA]** Czy lokalne Qt GUI korzysta z dokładnie tego samego zdalnego API co WebGUI, czy z lokalnego adaptera do tej samej warstwy application.
- **[DO USTALENIA]** Dokładny mechanizm rozszerzeń: statyczne moduły kompilowane z Core, rejestrowane moduły runtime, pluginy lub model hybrydowy.

## 4. Podejście do starego ECU Platform

- **[USTALONE]** Nie przenosimy całych katalogów ani starej architektury aplikacji w ciemno.
- **[USTALONE]** Każdy element legacy jest oceniany przed wykorzystaniem.
- **[USTALONE]** Wartość starego projektu stanowią przede wszystkim zdobyta wiedza, działające protokoły, parametry komunikacji i rozwiązania zweryfikowane na fizycznym sprzęcie.
- **[DO WERYFIKACJI]** SocketCAN / konfiguracja interfejsu / filtry / RX-TX.
- **[DO WERYFIKACJI]** ISO-TP.
- **[DO WERYFIKACJI]** UDS.
- **[DO WERYFIKACJI]** DAF SAC: identyfikacja, DID-y, DTC, runtime i przebiegi sesji.
- **[DO WERYFIKACJI]** ActuatorEngine: scheduler, kolejki, safety, rampy, timeouty i zachowanie STOP.
- **[DO WERYFIKACJI]** MAN Sonceboz EGR: protokół, identyfikatory, sekwencje i wyniki testów sprzętowych.
- **[DO WERYFIKACJI]** CAN Scanner.
- **[USTALONE]** Nie przenosimy jako wzorca starego uzależnienia logiki od QML / Qt Bridge.

## 5. Testowalność

- **[USTALONE]** Testowalność jest wymaganiem architektonicznym od początku projektu.
- **[USTALONE]** Większość logiki powinna być możliwa do testowania bez fizycznego ECU i Raspberry Pi.
- **[DO USTALENIA]** Interfejs abstrakcji CAN i sposób implementacji transportu symulowanego.
- **[DO USTALENIA]** Format zapisu i odtwarzania rzeczywistych sesji CAN do testów regresyjnych/replay.
- **[USTALONE]** Fizyczny sprzęt ma służyć przede wszystkim do walidacji integracyjnej i końcowej, a nie do wykrywania podstawowych błędów programistycznych.

## 6. Klienci systemu

- **[USTALONE]** Architektura V2 od początku musi uwzględniać więcej niż jeden typ klienta.
- **[USTALONE]** Planowane są co najmniej lokalne GUI oraz WebGUI.
- **[USTALONE]** Dodanie nowego klienta w przyszłości nie może wymagać kopiowania logiki sprzętowej lub diagnostycznej.
- **[DO USTALENIA]** Zakres funkcjonalny lokalnego GUI.
- **[DO USTALENIA]** Zakres funkcjonalny WebGUI.
- **[DO USTALENIA]** Model autoryzacji, ról i dostępu zdalnego.

## 7. Obszary do omówienia

Poniższa lista jest roboczym indeksem pierwszej fazy projektowania. Nie oznacza kolejności prac.

- **[USTALONE]** Ogólny cel produktu i jego rola jako rozwijalnej platformy inżyniersko-diagnostycznej.
- **[DO USTALENIA]** Docelowe tryby pracy i granice odpowiedzialności platformy.
- **[DO USTALENIA]** Docelowy hardware platformy.
- **[DO USTALENIA]** System operacyjny i środowisko uruchomieniowe.
- **[DO USTALENIA]** Architektura Core.
- **[DO USTALENIA]** Lifecycle aplikacji i usług.
- **[DO USTALENIA]** CAN ownership i arbitraż zasobów.
- **[DO USTALENIA]** Abstrakcja transportów.
- **[DO USTALENIA]** ISO-TP / UDS / J1939 i inne protokoły.
- **[DO USTALENIA]** Model ECU i modułów diagnostycznych.
- **[DO USTALENIA]** Model aktuatorów i sterowania czasowo-krytycznego.
- **[DO USTALENIA]** Model rozszerzania platformy o nowe klasy urządzeń i funkcji.
- **[DO USTALENIA]** Scanner CAN.
- **[DO USTALENIA]** Model Command / State / Event.
- **[DO USTALENIA]** API Core.
- **[DO USTALENIA]** Lokalny klient GUI.
- **[DO USTALENIA]** WebGUI.
- **[DO USTALENIA]** Raporty i eksport danych.
- **[DO USTALENIA]** Logging, telemetry i audit trail.
- **[DO USTALENIA]** Bezpieczeństwo funkcjonalne i fail-safe.
- **[DO USTALENIA]** Uprawnienia użytkowników i bezpieczeństwo sieciowe.
- **[DO USTALENIA]** Symulator i replay CAN.
- **[DO USTALENIA]** Strategia testów.
- **[DO USTALENIA]** CI/CD, release i aktualizacje.
- **[DO USTALENIA]** Instalacja i wdrożenie na urządzeniu docelowym.

---

## 8. Historia ważnych powodów powstania V2

- **[USTALONE]** Jednym z fundamentalnych problemów legacy ECU Platform było zbyt silne sprzężenie sterowania EGR z GUI.
- **[USTALONE]** Późniejsza przebudowa do ActuatorEngine pokazała potrzebę trwałego oddzielenia procesów czasowo-krytycznych od warstwy prezentacji.
- **[USTALONE]** Doświadczenia z projektu Workshop Ventilation Controller potwierdziły skuteczność modelu, w którym Core jest autonomiczny, a GUI pełni wyłącznie rolę klienta.
- **[USTALONE]** ECU Platform V2 ma wykorzystać wiedzę zdobytą na legacy Platform oraz późniejszych projektach, zamiast odtwarzać historyczne błędy architektoniczne.

---

## 9. Następne ustalenia

Pierwsza faza projektu to rozmowa projektowa i uzupełnianie niniejszego dokumentu. Implementacja Core rozpocznie się dopiero wtedy, gdy podstawowe założenia architektury, odpowiedzialności warstw oraz zachowania systemu będą wystarczająco jasno ustalone.

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
- **[USTALONE]** Obsługa nowych ECU nie może ograniczać się do CAN/CAN-FD. ECU Platform V2 musi od początku uwzględniać diagnostykę **DoIP (Diagnostics over Internet Protocol, ISO 13400)** jako równorzędny, pierwszoplanowy transport diagnostyczny.
- **[USTALONE]** Platforma ma posiadać lokalny interfejs użytkownika oraz WebGUI.
- **[USTALONE]** Rozwijalność jest wymaganiem fundamentalnym: dodanie w przyszłości nowego ECU, modułu, urządzenia wykonawczego, protokołu, transportu, klienta lub innej klasy obsługiwanych elementów nie może wymagać przebudowy całej platformy.
- **[USTALONE]** Architektura ma zapewniać stabilne granice odpowiedzialności i kontrakty między Core a modułami funkcjonalnymi, tak aby rozszerzenia można było dodawać lokalnie zamiast zmieniać wiele niezwiązanych części systemu.
- **[USTALONE]** Nie zakładamy z góry zamkniętej listy obsługiwanych ECU, protokołów ani urządzeń. V2 ma być bazą do dalszego wieloletniego rozwoju.
- **[USTALONE]** Docelowo ECU Platform V2 ma być urządzeniem komercyjnym. Docelowa platforma sprzętowa i system operacyjny nie są jeszcze wybrane.
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

### 3.1. Niezależność od docelowej platformy sprzętowej i systemu operacyjnego

- **[USTALONE]** ECU Platform V2 będzie obecnie rozwijana i uruchamiana na Linuxie, ponieważ jest to dostępne, dobrze znane i praktyczne środowisko rozwojowe dla aktualnego sprzętu testowego.
- **[USTALONE]** Linux jest pierwszą wspieraną implementacją platformy uruchomieniowej, ale nie może stać się częścią logiki domenowej ECU Platform V2.
- **[USTALONE]** Core ma być projektowany jako `platform-agnostic`: logika diagnostyczna, protokoły, modele ECU, sterowanie aktuatorami, state machine, safety, model Command/State/Event oraz pozostała logika domenowa nie mogą zależeć bezpośrednio od Linuxa ani od konkretnej płyty sprzętowej.
- **[USTALONE]** Zależności specyficzne dla systemu operacyjnego i hardware — m.in. SocketCAN, sockety systemowe, GPIO, zegary/timery systemowe, filesystem, procesy/usługi systemowe, konfiguracja interfejsów i inne I/O — muszą być odseparowane za stabilnymi interfejsami/adapterami platformowymi.
- **[USTALONE]** Kod domenowy nie może bezpośrednio używać nagłówków/API specyficznych dla Linuxa, takich jak `linux/can.h`, systemowych socketów CAN ani poleceń typu `ip link`; takie zależności należą wyłącznie do implementacji platformowej dla Linuxa.
- **[USTALONE]** Qt/QML nie jest częścią Core i nie może być wymaganiem dla działania logiki domenowej. Qt może być technologią klienta lokalnego, ale jego ewentualna wymiana nie może wymagać przebudowy Core.
- **[USTALONE]** Docelowy produkt komercyjny może w przyszłości używać innej platformy sprzętowej lub systemu operacyjnego. Zmiana platformy powinna wymagać przede wszystkim dostarczenia nowych adapterów warstwy platformowej, a nie przepisywania logiki ECU Platform.
- **[USTALONE]** Nie zakładamy pełnej przenośności na dowolny typ urządzenia. Celem jest niezależność w rozsądnym zakresie dla klasy urządzeń zdolnych uruchomić Core; przejście na bardzo ograniczony mikrokontroler bez systemu operacyjnego może wymagać osobnej adaptacji architektury.
- **[DO USTALENIA]** Minimalny formalny kontrakt warstwy platformowej: CAN, clock/scheduler, storage, networking, system lifecycle, hardware I/O oraz pozostałe zależności od OS.
- **[DO USTALENIA]** Czy CI będzie od początku kompilować i testować Core w więcej niż jednym środowisku/adapterze (np. Linux + platforma symulowana), aby wykrywać przypadkowe zależności od Linuxa.

**Dlaczego przyjmujemy to założenie:**

1. **Nie znamy jeszcze docelowego hardware produktu komercyjnego.** Związanie logiki z Raspberry Pi, SocketCAN lub konkretną dystrybucją Linuxa mogłoby wymusić kosztowny rewrite po wyborze platformy produkcyjnej.
2. **Największa wartość projektu leży w logice domenowej i zdobytej wiedzy.** Implementacje ISO-TP, UDS/J1939, obsługa ECU, procedury diagnostyczne, sterowanie aktuatorami, safety i testy powinny pozostać użyteczne niezależnie od późniejszego wyboru komputera przemysłowego czy systemu operacyjnego.
3. **Produkt komercyjny musi mieć możliwość ewolucji sprzętowej.** Dostępność podzespołów, koszty BOM, wymagania EMC, temperatura pracy, certyfikacja, wymagania klienta lub cykl życia komponentów mogą w przyszłości wymusić zmianę platformy sprzętowej.
4. **Oddzielenie OS od Core poprawia testowalność.** Te same interfejsy, które pozwolą później zmienić Linux na inną platformę, umożliwią teraz uruchamianie symulatorów, mocków i testów bez fizycznego CAN/ECU.
5. **Zapobiega to powtórzeniu błędu legacy.** Tak jak wcześniej logika sterowania została zbyt mocno związana z GUI, tak samo nie chcemy teraz związać logiki produktu z aktualnym środowiskiem Linux/Raspberry Pi.
6. **Koszt tej separacji jest najmniejszy na początku projektu.** Wprowadzenie abstrakcji platformowej przed powstaniem dużej ilości kodu jest znacznie prostsze niż późniejsze wydzielanie zależności systemowych z działającego produktu.

### 3.2. Ochrona produktu, integralność i licencjonowanie

- **[USTALONE]** Ochrona ECU Platform V2 przed nieautoryzowanym kopiowaniem, modyfikacją i uruchamianiem na nieautoryzowanym sprzęcie jest wymaganiem architektonicznym produktu komercyjnego, a nie funkcją dodawaną po zakończeniu developmentu.
- **[USTALONE]** Architektura musi umożliwiać powiązanie instalacji/licencji z kryptograficzną tożsamością konkretnego urządzenia (`hardware-bound device identity`).
- **[USTALONE]** Docelowy hardware powinien zapewniać sprzętowy Root of Trust lub równoważny mechanizm bezpiecznego przechowywania i używania kluczy urządzenia. Prywatny klucz tożsamości urządzenia nie powinien być możliwy do zwykłego skopiowania razem z filesystemem/nośnikiem danych.
- **[USTALONE]** System musi umożliwiać kryptograficzne podpisywanie i weryfikację software, modułów oraz aktualizacji producenta.
- **[USTALONE]** Projekt powinien umożliwiać zapewnienie integralności łańcucha uruchamiania (`boot chain`), tak aby nieautoryzowana modyfikacja systemu lub Core nie była traktowana jak prawidłowy software producenta.
- **[USTALONE]** Egzekwowanie licencji, uprawnień i dostępności funkcji należy do Core / warstwy bezpieczeństwa, nigdy do lokalnego GUI ani WebGUI. Klient może jedynie prezentować wynik decyzji podjętej przez Core.
- **[USTALONE]** Skopiowanie kompletnego nośnika danych z jednego urządzenia do drugiego nie powinno wystarczać do uzyskania działającej, licencjonowanej kopii produktu.
- **[USTALONE]** Architektura ma umożliwiać podpisane licencje/capabilities przypisane do konkretnego urządzenia oraz — w przyszłości — niezależne licencjonowanie pakietów funkcjonalnych, np. rodzin ECU, sterowania aktuatorami, zaawansowanych narzędzi CAN lub innych rozszerzeń.
- **[USTALONE]** Podstawowa praca urządzenia nie powinna wymagać stałego połączenia z Internetem. Mechanizm licencyjny musi umożliwiać lokalną kryptograficzną weryfikację uprawnień. Internet może być wykorzystywany pomocniczo do aktywacji, aktualizacji, zarządzania licencjami lub dystrybucji nowych funkcji.
- **[USTALONE]** Klucze producenta służące do podpisywania wydań, aktualizacji i licencji są zasobem krytycznym i nie mogą być przechowywane w kodzie źródłowym, repozytorium ani zwykłym pliku dołączonym do procesu build/release.
- **[USTALONE]** Mechanizmy bezpieczeństwa muszą być niezależne od konkretnego GUI oraz — na poziomie kontraktów Core — od konkretnego systemu operacyjnego. Dostęp do Root of Trust, tożsamości urządzenia i bezpiecznych kluczy ma być realizowany przez abstrakcję platformową.
- **[USTALONE]** Wybierając w przyszłości hardware produkcyjny, oceniamy nie tylko CPU/RAM/CAN/I/O i koszty, ale także dostępność mechanizmów Secure Boot, sprzętowej tożsamości, bezpiecznego przechowywania kluczy i możliwości budowy zaufanego łańcucha uruchamiania.
- **[DO USTALENIA]** Konkretny mechanizm Root of Trust: TPM 2.0, Secure Element, DICE/eFuse/TEE lub inne rozwiązanie zależne od docelowego hardware.
- **[DO USTALENIA]** Szczegółowy model licencjonowania: licencja urządzenia, edycje produktu, pakiety funkcjonalne, licencje czasowe/bezterminowe i sposób aktywacji offline/online.
- **[DO USTALENIA]** Szczegółowy model Secure/Verified Boot oraz ochrony systemu plików dla pierwszej implementacji Linux.
- **[DO USTALENIA]** Docelowa infrastruktura przechowywania kluczy producenta i podpisywania wydań, np. HSM/KMS lub inne rozwiązanie sprzętowo chronione.
- **[DO USTALENIA]** Polityka reakcji na naruszenie integralności, nieprawidłową licencję, utratę lub wymianę hardware oraz procedury serwisowe/recovery.

**Dlaczego przyjmujemy to założenie:**

1. **V2 ma być produktem komercyjnym.** Ochrona własności intelektualnej, know-how komunikacyjnego i płatnych funkcji jest częścią wartości produktu.
2. **Sama licencja programowa nie wystarcza.** Jeżeli cały system można skopiować lub dowolnie zmodyfikować, możliwe byłoby również usunięcie kontroli licencji. Dlatego licencjonowanie musi współpracować z tożsamością hardware i mechanizmami integralności software.
3. **Zabezpieczenia najłatwiej zaprojektować na początku.** Hardware identity, boot chain, podpisywanie wydań i abstrakcje bezpieczeństwa wpływają na podział odpowiedzialności Core i wybór docelowego hardware; późniejsze dokładanie tych mechanizmów mogłoby wymagać głębokiej przebudowy produktu.
4. **Nie znamy jeszcze docelowej platformy sprzętowej.** Definiujemy więc wymagany kontrakt bezpieczeństwa, ale nie przywiązujemy architektury do TPM, Secure Element czy innego konkretnego rozwiązania, dopóki nie zostanie wybrany hardware produkcyjny.
5. **Egzekwowanie licencji w GUI byłoby błędem architektonicznym.** GUI jest klientem i może zostać zamknięte, wymienione lub zastąpione WebGUI. Autoryzacja funkcji musi należeć do Core, podobnie jak pozostała logika krytyczna produktu.
6. **Celem nie jest obietnica niemożliwej do złamania ochrony.** Przy fizycznym dostępie i odpowiednio dużych zasobach każde urządzenie może być przedmiotem analizy. Celem jest wielowarstwowa ochrona, która znacząco podnosi koszt, trudność i nieopłacalność klonowania lub modyfikacji produktu.

### 3.3. DoIP i Ethernet diagnostyczny

- **[USTALONE]** ECU Platform V2 musi obsługiwać **DoIP (Diagnostics over Internet Protocol)** zgodnie z rodziną ISO 13400. Jest to wymaganie podstawowe wynikające z kierunku rozwoju nowych ECU i architektur pojazdów.
- **[USTALONE]** Punktem odniesienia dla nowej implementacji będzie aktualna edycja warstwy transportowej/networkowej **ISO 13400-2:2025**; implementacja musi jednak uwzględniać interoperacyjność z ECU zgodnymi ze starszymi wersjami standardu, jeśli będzie to wymagane w praktyce.
- **[USTALONE]** DoIP traktujemy jako transport diagnostyczny oparty na IP, TCP i UDP, a nie jako odmianę CAN. Architektura diagnostyczna nie może zakładać, że UDS zawsze działa przez ISO-TP/CAN.
- **[USTALONE]** Warstwa UDS ma być niezależna od transportu tak, aby ta sama logika diagnostyczna mogła działać m.in. przez DoCAN/ISO-TP oraz DoIP bez duplikowania modułów ECU.
- **[USTALONE]** Hardware produkcyjny/interfejs diagnostyczny musi zapewniać Ethernet odpowiedni do komunikacji DoIP. Kandydat sprzętowy bez realnej możliwości obsługi Ethernetu diagnostycznego nie spełnia pełnych wymagań ECU Platform V2.
- **[USTALONE]** Dla klasycznego połączenia testera DoIP należy uwzględnić co najmniej standardowy Ethernet 10/100 Mb/s, w szczególności 100BASE-TX zgodny z wymaganiami fizycznej warstwy diagnostycznej ISO 13400-3.
- **[USTALONE]** Ponieważ platforma ma służyć również do pracy laboratoryjnej bezpośrednio z nowymi ECU, architektura hardware ma przewidywać możliwość obsługi **Automotive Ethernet** (co najmniej 100BASE-T1; możliwość 1000BASE-T1 pozostaje do oceny) poprzez odpowiedni PHY/moduł/rozszerzenie bez przebudowy całego urządzenia.
- **[USTALONE]** W wariancie produktu `Windows + inteligentny interfejs USB` interfejs sprzętowy nie może być projektowany jako wyłącznie wielokanałowy adapter CAN. Musi również posiadać ścieżkę Ethernet/DoIP do pojazdu lub ECU.
- **[USTALONE]** Tak jak dla CAN, odbiór, buforowanie i pomiary czasu dla ruchu Ethernet realizowane w interfejsie nie mogą zależeć od chwilowych opóźnień GUI Windows.
- **[DO USTALENIA]** Dokładne miejsce implementacji stosu DoIP w wariancie Windows + USB: w Core na PC przy interfejsie pełniącym rolę mostu Ethernet, w firmware inteligentnego interfejsu albo model hybrydowy. Decyzję podejmiemy po analizie prostoty sterowników, bezpieczeństwa, testowalności i wymaganej funkcjonalności standalone.
- **[DO USTALENIA]** Docelowe fizyczne interfejsy Ethernet urządzenia: 100BASE-TX/OBD DoIP, 100BASE-T1, 1000BASE-T1 oraz ewentualne wymienne adaptery kablowe/moduły PHY.
- **[DO USTALENIA]** Wymagania dotyczące DoIP activation line, sposobu jej sterowania oraz mapowania na docelowe złącza pojazdu.
- **[DO USTALENIA]** Zakres obsługi zabezpieczonego DoIP/TLS oraz wymagań OEM dotyczących uwierzytelniania i bezpiecznej diagnostyki.

**Dlaczego przyjmujemy to założenie:**

1. **DoIP jest istotnym transportem diagnostycznym nowych generacji ECU.** Projekt, który od początku zakłada wyłącznie CAN/CAN-FD, szybko ograniczyłby możliwość rozwoju Platformy na nowsze pojazdy.
2. **DoIP zmienia wymagania sprzętowe.** Samo zwiększanie liczby kanałów CAN nie rozwiązuje diagnostyki IP; potrzebne są odpowiednie kontrolery Ethernet, PHY, złącza i warstwa sieciowa.
3. **UDS i transport muszą być rozdzielone.** Dzięki temu wiedza o ECU, DID-ach, DTC i procedurach diagnostycznych nie będzie kopiowana osobno dla CAN i Ethernetu.
4. **Praca na stole wymaga większej elastyczności niż typowy tester serwisowy.** ECU Platform ma diagnozować również pojedyncze ECU bez kompletnej infrastruktury pojazdu, dlatego możliwość pracy z Automotive Ethernet jest ważnym elementem przyszłej rozbudowy.
5. **Uwzględnienie DoIP teraz wpływa na wybór MCU.** Przykładowo i.MX RT1180, wcześniej rozważany dla interfejsu USB, posiada rozbudowany wieloportowy Ethernet/TSN, przez co po dodaniu wymagania DoIP staje się jeszcze bardziej interesującym kandydatem niż MCU pozbawione natywnego Ethernetu.

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
- **[DO USTALENIA]** Symulacja/replay ruchu Ethernet i sesji DoIP do testów regresyjnych bez fizycznego ECU.
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
- **[DO USTALENIA]** Docelowy hardware platformy komercyjnej.
- **[USTALONE]** Aktualne środowisko rozwojowe: Linux; architektura Core nie może uzależniać produktu od Linuxa ani obecnego hardware.
- **[DO USTALENIA]** Docelowy system operacyjny / środowisko uruchomieniowe produktu.
- **[DO USTALENIA]** Architektura Core.
- **[DO USTALENIA]** Lifecycle aplikacji i usług.
- **[DO USTALENIA]** CAN ownership i arbitraż zasobów.
- **[DO USTALENIA]** Abstrakcja transportów.
- **[USTALONE]** DoIP / ISO 13400 musi być pełnoprawnym transportem diagnostycznym V2 obok DoCAN/ISO-TP.
- **[DO USTALENIA]** Fizyczna obsługa Ethernet/Automotive Ethernet: 100BASE-TX, 100BASE-T1 i ewentualnie 1000BASE-T1.
- **[DO USTALENIA]** ISO-TP / UDS / J1939 i inne protokoły.
- **[DO USTALENIA]** Model ECU i modułów diagnostycznych.
- **[DO USTALENIA]** Model aktuatorów i sterowania czasowo-krytycznego.
- **[DO USTALENIA]** Model rozszerzania platformy o nowe klasy urządzeń i funkcji.
- **[DO USTALENIA]** Scanner CAN.
- **[DO USTALENIA]** Analiza i monitoring Ethernet/DoIP/Automotive Ethernet.
- **[DO USTALENIA]** Model Command / State / Event.
- **[DO USTALENIA]** API Core.
- **[DO USTALENIA]** Lokalny klient GUI.
- **[DO USTALENIA]** WebGUI.
- **[DO USTALENIA]** Raporty i eksport danych.
- **[DO USTALENIA]** Logging, telemetry i audit trail.
- **[DO USTALENIA]** Bezpieczeństwo funkcjonalne i fail-safe.
- **[DO USTALENIA]** Ochrona produktu przed kopiowaniem i modyfikacją: Root of Trust, integralność boot chain, podpisywanie wydań i aktualizacji oraz licencjonowanie funkcji.
- **[DO USTALENIA]** Uprawnienia użytkowników i bezpieczeństwo sieciowe.
- **[DO USTALENIA]** Symulator i replay CAN/DoIP.
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

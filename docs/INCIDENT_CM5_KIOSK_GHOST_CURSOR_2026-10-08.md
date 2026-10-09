# INC-WEBGUI-2026-10-08-01 — nieruchomy kursor w kiosku CM5

**Status:** ZAMKNIĘTY — użytkownik potwierdził brak kursora i poprawne działanie dotyku (2026-10-08, po wdrożeniu poprawki).
**Waga:** niska — problem wizualny; nie stwierdzono wpływu na CAN, ECU, CORE, Bench Runtime ani działanie dotyku.
**Środowisko:** Raspberry Pi CM5, Debian 13 (trixie), Cage/Wayland, Chromium, dotyk WaveShare USB 0712:0009.
**Zakres:** wyłącznie kiosk/WebGUI, bez zmian API oraz bez integracji z AI Platform.

## Objawy i przebieg

1. Po prawidłowym wdrożeniu WebGUI V1 oraz przejściu testu uruchamiania po restarcie na środku fizycznego ekranu pozostał nieruchomy wskaźnik myszy. Dotyk działał.
2. Zastosowano globalne **cursor: none !important** w arkuszu WebGUI (commity 84c940c, 5e39a49). Test Chromium potwierdził wartość none również na przyciskach, ale **fizyczny kursor pozostał widoczny**.
3. Sprawdzono urządzenia wejściowe na CM5. Udev zgłaszał dwa wejścia HDMI-CEC:
   - **vc4-hdmi-0** — /dev/input/event2, ID_INPUT_POINTINGSTICK=1,
   - **vc4-hdmi-1** — /dev/input/event4, ID_INPUT_POINTINGSTICK=1.
   WaveShare to osobny /dev/input/event1, ID_INPUT_TOUCHSCREEN=1, ID_VENDOR_ID=0712, ID_MODEL_ID=0009.
4. Wprowadzono ograniczoną regułę udev/libinput ignorującą tylko dwa wymienione wirtualne urządzenia wskazujące. Po zastosowaniu poprawki użytkownik potwierdził: **kursor zniknął, dotyk nadal działa**.

## Przyczyna

Urządzenia wejściowe vc4-hdmi-0 i vc4-hdmi-1 były raportowane jako ID_INPUT_POINTINGSTICK=1. Powodowało to utrzymywanie przez warstwę kompozytora Wayland/Cage wskaźnika, którego nie usuwa deklaracja CSS — CSS steruje kursorem klienta przeglądarki, nie wszystkimi wskaźnikami zarządzanymi przez kompozytor.

Diagnozę potwierdza zarówno identyfikacja dwóch wirtualnych wejść, jak i ustąpienie objawu po ich wykluczeniu z libinput.

## Rozwiązanie

**Commit naprawczy:** d20f702 — fix(kiosk): ignore HDMI CEC phantom pointer devices in libinput.

- Reguła: [deploy/webgui/90-ecu-kiosk-ignore-hdmi-pointer.rules](../deploy/webgui/90-ecu-kiosk-ignore-hdmi-pointer.rules).
- Instalacja/wycofanie: [scripts/fix_cm5_ghost_pointer.sh](../scripts/fix_cm5_ghost_pointer.sh).
- Ustawienie: LIBINPUT_IGNORE_DEVICE=1 tylko dla wejść o nazwach vc4-hdmi-0 i vc4-hdmi-1, z warunkiem ID_INPUT_POINTINGSTICK=1.
- Nie usunięto urządzeń z kernela, nie wyłączono interfejsu CAN, nie zmieniono usługi Bench Agenta ani uprawnień kiosku.
- **Ograniczenie:** w tej konfiguracji libinput pomija również ewentualne klawisze HDMI-CEC z dwóch wykluczonych wejść. Panel WaveShare, prawdziwa mysz USB i inne wejścia nie są objęte regułą.

## Weryfikacja

| Kontrola | Wynik |
| --- | --- |
| Składnia reguły (udevadm verify) | PASS |
| Testy WebGUI na etapie wdrożenia (12/12) | PASS |
| Testy kontraktowe i instalacyjne na etapie wdrożenia (12/12) | PASS |
| CI WebGUI Linux + Windows dla commita naprawczego | PASS |
| /dev/input/event2: LIBINPUT_IGNORE_DEVICE=1 | PASS na CM5 |
| /dev/input/event4: LIBINPUT_IGNORE_DEVICE=1 | PASS na CM5 |
| /dev/input/event1: WaveShare, bez LIBINPUT_IGNORE_DEVICE=1 | PASS na CM5 |
| ecu-kiosk.service: aktywny jako ecu-kiosk, bez restart-loop | PASS na CM5 |
| ecu-webgui-static.service: aktywny, nasłuch wyłącznie na 127.0.0.1:8877 | PASS na CM5 |
| Fizyczny kursor niewidoczny; dotyk działa | **PASS — potwierdzenie użytkownika** |
| Trwałość tej konkretnej poprawki po kolejnym restarcie CM5 | **Jeszcze niezweryfikowana osobnym testem** |

**Granica zaufania pozostaje bez zmian:** WebGUI to klient. Brak połączenia z API, CAN, CORE lub Bench Runtime. Dostęp do zasobów stanowiska nadal należy wyłącznie do autoryzowanych warstw backendu.

## Przywrócenie poprzedniego zachowania

W terminalu CM5:

    cd /home/ecu/ECU_WebGUI_Home_V1
    sudo bash scripts/fix_cm5_ghost_pointer.sh --rollback

Polecenie usuwa dodaną regułę, odświeża właściwości obu wejść HDMI i restartuje tylko kiosk. Nie należy go wykonywać bez potrzeby.

## Wnioski dla kolejnych wdrożeń

- Jeżeli CSS deklaruje cursor: none, lecz fizyczny wskaźnik pozostaje, najpierw sprawdzić udevadm info, ID_INPUT_POINTINGSTICK i urządzenia wejściowe kompozytora.
- Ograniczać reguły libinput do **ściśle zidentyfikowanych urządzeń**, zamiast wyłączać wszystkie urządzenia wskazujące lub dotyk.
- Nie dokładać nowych warstw i mechanizmów pośrednich w API/CORE dla problemów czysto prezentacyjnych.
- Poprawkę traktować jako zamkniętą po weryfikacji fizycznej; ponowny reboot testować przy najbliższym planowym restarcie, bez wymuszania go na aktywnym stanowisku.

Zobacz również: [CM5 WebGUI deployment V1](CM5_WEBGUI_KIOSK_DEPLOYMENT_V1.md) i [WebGUI client-only architecture](WEBGUI_CLIENT_ONLY_ARCHITECTURE.md).

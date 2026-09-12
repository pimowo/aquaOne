# FACTORY_RESET_ONBOARDING_STANDARD.md

**Status:** DRAFT
**Scope:** aquaOne ecosystem
**Version:** 1.0
**Last reviewed:** 2026-09-12

## Terminologia normatywna

- **MUSI** — wymaganie obowiązkowe.
- **POWINNO** — zalecenie, od którego można odstąpić wyłącznie z udokumentowanym uzasadnieniem.
- **MOŻE** — opcja.

## 1. Cel

Ten dokument definiuje wspólny standard pierwszego uruchomienia, onboardingu i factory reset dla całego ekosystemu **aquaOne**.

Dotyczy wszystkich urządzeń korzystających z `aquaOneCore`, m.in.:

- `aquaOneLuma`
- `aquaOneDoser`
- `aquaOneHydro`
- `aquaOneClima`
- `aquaOneGas`
- `aquaOneFauna`

Standard określa:

- zachowanie świeżego urządzenia,
- tworzenie lokalnego AP,
- konfigurację Wi-Fi,
- dostęp do WWW przed dołączeniem do sieci,
- fallback przy błędnej konfiguracji,
- factory reset,
- bezpieczeństwo resetu,
- odtwarzanie wartości domyślnych,
- zachowanie po utracie Wi-Fi,
- odpowiedzialność `aquaOneCore` i projektów domenowych.

## Stan implementacji

### Implementation: CURRENT

AquaCore ma mechanizmy Network i Config/Storage używane selektywnie przez projekty. Nie ma
jeszcze wspólnego workflow factory reset, onboarding API, captive portal ani koordynatora
atomowego resetu dla całego ekosystemu.

### Implementation: TARGET

Semantyka kasowania i zachowania danych, atomowy i powtarzalny reset, obsługa przerwanego
resetu, fizyczny i zdalny trigger, autoryzacja, fallback AP, timeout AP, onboarding state i
zapis credentials opisane niżej są kontraktem docelowym.

Każdy projekt MUSI przed implementacją jawnie wymienić dane kasowane i zachowywane. Reset
nie może kasować danych spoza zatwierdzonego zakresu. Identity i wersje definiuje
[NAMING_VERSIONING_STANDARD.md](NAMING_VERSIONING_STANDARD.md), persistence
[CONFIG_STORAGE_STANDARD.md](CONFIG_STORAGE_STANDARD.md), a bezpieczeństwo
[SAFETY_STANDARD.md](SAFETY_STANDARD.md).

---

# 2. Zasada nadrzędna

Nowe lub zresetowane urządzenie musi dać się skonfigurować lokalnie bez:

- Home Assistant,
- MQTT,
- Internetu,
- aplikacji producenta,
- chmury.

Pierwsze uruchomienie ma być proste, przewidywalne i odporne na błędy.

---

# 3. Stan świeżego urządzenia

Świeże urządzenie lub urządzenie po factory reset:

```text
config = factory defaults
mode = NORMAL
buzzer_enabled = ON
network credentials = missing
MQTT = not configured
HA Discovery = disabled until MQTT is usable
```

Funkcje domenowe, które wymagają konfiguracji bezpieczeństwa, pozostają nieaktywne do czasu poprawnej konfiguracji.

---

# 4. Brak Wi-Fi credentials

Jeśli urządzenie nie posiada poprawnej konfiguracji Wi-Fi, uruchamia lokalny onboarding AP.

To jest normalny tryb konfiguracji, nie błąd alarmowy.

---

# 5. Nazwa AP

Rekomendowany wzorzec:

```text
aquaOne-<type>-<MAC6>
```

Przykłady:

```text
aquaOne-luma-A1B2C3
aquaOne-doser-7F21AC
```

SSID ma być jednoznaczny i techniczny.

---

# 6. Hasło AP

AP onboarding nie powinien być otwarty bez potrzeby.

Preferowane jest hasło lokalne.

Może być:

- stałe dla development,
- wygenerowane per-device,
- wydrukowane/udostępnione w dokumentacji urządzenia.

Jeżeli urządzenie jest projektem prywatnym w zaufanej sieci, dopuszczalne jest uproszczenie, ale Core powinien umożliwiać ochronę AP.

---

# 7. AP nie jest trybem normalnej pracy

AP onboarding służy do:

- konfiguracji,
- odzyskania dostępu,
- naprawy sieci.

Nie powinien być głównym interfejsem urządzenia podczas normalnej eksploatacji.

---

# 8. Captive portal

Captive portal jest opcjonalny.

Nie jest wymagany do poprawnego działania onboardingu.

Najważniejsze jest, aby lokalne WWW było dostępne pod znanym adresem IP AP.

---

# 9. Adres WWW w AP

Core powinien używać przewidywalnego adresu AP, np.:

```text
192.168.4.1
```

lub innego ustalonego w implementacji.

Adres powinien być wspólny w całym ekosystemie.

---

# 10. Onboarding flow

Rekomendowany przebieg:

```text
boot
-> no valid Wi-Fi config
-> start AP
-> open local WWW
-> scan/select SSID
-> enter password
-> validate
-> save
-> attempt STA connection
-> if success, continue normal boot
```

---

# 11. Scan Wi-Fi

WWW może oferować skan dostępnych sieci.

Nie jest to wymagane do zapisania SSID ręcznie.

---

# 12. Ukryte sieci

Użytkownik musi mieć możliwość ręcznego wpisania SSID.

---

# 13. Walidacja Wi-Fi

Przed zapisaniem można sprawdzić podstawowy format danych.

Pełna walidacja odbywa się dopiero przy próbie połączenia.

---

# 14. Zapis Wi-Fi

Nowe credentials są zapisywane przez wspólny mechanizm `CONFIG_STORAGE_STANDARD`.

Nie zapisujemy haseł w logach.

---

# 15. Apply bez restartu

Jeśli platforma pozwala, nowe Wi-Fi powinno być zastosowane bez restartu.

Restart jest dopuszczalny tylko wtedy, gdy upraszcza bezpieczną implementację i jest jasno komunikowany.

---

# 16. Błędne credentials

Jeśli urządzenie nie może połączyć się z zapisanym Wi-Fi:

- nie kasuje od razu credentials,
- wykonuje normalne retry,
- po rozsądnym czasie może uruchomić fallback AP,
- nadal działa autonomicznie w zakresie funkcji lokalnych.

---

# 17. Fallback AP

Jeśli skonfigurowane Wi-Fi jest niedostępne przez dłuższy czas, Core może uruchomić fallback AP.

Fallback AP nie oznacza factory reset.

---

# 18. STA + AP

Dozwolony jest tryb równoległy:

```text
STA + AP
```

jeśli jest stabilny na danej platformie.

Preferencja: AP ma być pomocniczy, a urządzenie nadal próbuje pracować jako STA.

---

# 19. Fallback AP nie zmienia configu

Samo uruchomienie AP nie modyfikuje zapisanych credentials.

---

# 20. Auto-disable AP

Po poprawnym połączeniu STA AP powinien zostać wyłączony, chyba że projekt świadomie wspiera stały tryb STA+AP.

Domyślna polityka:

```text
AP onboarding/fallback -> temporary
```

---

# 21. AP timeout

Core może wyłączyć fallback AP po okresie bez aktywnego klienta.

Nie jest to wymaganie dla v1, ale architektura powinna to umożliwiać.

---

# 22. Utrata Wi-Fi po starcie

Jeśli urządzenie działało poprawnie i później straci Wi-Fi:

- domena działa dalej autonomicznie,
- Core próbuje reconnect,
- nie uruchamiamy od razu AP,
- AP może pojawić się dopiero po dłuższym czasie braku połączenia.

---

# 23. Brak Internetu

Brak Internetu przy działającym LAN nie jest powodem do onboardingu.

---

# 24. MQTT nie jest częścią podstawowego onboardingu Wi-Fi

Najpierw urządzenie musi mieć lokalną sieć i WWW.

MQTT może być skonfigurowane później w normalnym WWW.

---

# 25. HA Discovery nie jest wymagane do onboardingu

HA nie uczestniczy w pierwszym uruchomieniu urządzenia.

---

# 26. Pierwszy ekran WWW

Przy świeżym urządzeniu WWW powinno jasno pokazać:

```text
Device type
Device ID
Wi-Fi not configured
Configuration required
```

oraz prowadzić użytkownika bez zbędnych sekcji.

---

# 27. Onboarding wizard

Core może mieć prosty wizard:

```text
1. Network
2. Device essentials
3. Save
4. Ready
```

Nie powinien być długi ani wymagać konfiguracji każdej opcjonalnej funkcji.

---

# 28. Minimalna konfiguracja domenowa

Projekt domenowy może zadeklarować minimalne ustawienia wymagane do bezpiecznego startu.

Przykłady:

- kalibracja pompy,
- typ sensora,
- limit bezpieczeństwa,
- konfiguracja kanałów.

Jeśli ich brak, funkcja pozostaje nieaktywna.

---

# 29. Onboarding complete

Urządzenie może oznaczyć onboarding jako ukończony dopiero po spełnieniu minimalnych wymagań.

Można przechowywać:

```text
onboarding_complete = true
```

jeśli realnie upraszcza logikę.

Nie jest wymagane, jeśli stan można jednoznacznie wywnioskować z configu.

---

# 30. Brak niepotrzebnych flag

Preferujemy wyliczanie stanu z rzeczywistej konfiguracji zamiast mnożenia trwałych flag.

---

# 31. Factory reset

Factory reset usuwa dane konfiguracyjne użytkownika i przywraca factory defaults.

---

# 32. Factory reset usuwa

Co najmniej:

```text
Wi-Fi credentials
MQTT config
MQTT credentials
HA Discovery config
domain config
user preferences
buzzer setting -> reset to ON
latched alarm persistence
onboarding state
```

---

# 33. Factory reset nie usuwa

Danych niemodyfikowalnych sprzętowo, np.:

```text
MAC
hardware revision
factory calibration, jeśli jest produkcyjna i wymagana
immutable device identifiers
```

---

# 34. Factory reset nie zmienia firmware

Factory reset resetuje config, nie firmware.

Nie wykonuje rollbacku OTA.

---

# 35. Factory reset z WWW

Akcja musi:

- być w sekcji System,
- wymagać wyraźnego potwierdzenia,
- jasno informować, że Wi-Fi i config zostaną usunięte,
- wykonać kontrolowany restart po zakończeniu.

---

# 36. Potwierdzenie resetu

Preferowane:

```text
Factory Reset
-> confirmation dialog
-> second explicit confirm
```

Dla szczególnie ryzykownego UI można wymagać wpisania krótkiego słowa, ale nie jest to obowiązkowe.

---

# 37. Factory reset fizycznym przyciskiem

Core może wspierać reset przez fizyczny przycisk.

Musi to być akcja trudna do przypadkowego wykonania.

Przykład:

```text
hold 10 s during defined boot window
```

Dokładny timing pozostaje implementacyjny.

---

# 38. Reset fizyczny wymaga jednoznaczności

Nie używamy prostego krótkiego kliknięcia do factory reset.

---

# 39. Feedback resetu

Jeśli sprzęt ma buzzer/LED, można zasygnalizować reset.

Nie jest to wymaganie standardu.

---

# 40. Controlled reset sequence

Sekwencja:

```text
request factory reset
-> validate authorization/confirmation
-> stop risky domain actions
-> clear config
-> clear persistent user state
-> restore defaults
-> reboot
-> start onboarding AP
```

---

# 41. Reset przy safety_lock

Factory reset może być dozwolony przy safety_lock jako akcja serwisowa.

Przed resetem domena pozostaje w safe state.

---

# 42. Reset nie może przypadkowo uruchomić domeny

Po factory reset funkcje wymagające konfiguracji nie mogą wystartować na przypadkowych defaults.

---

# 43. Reset i alarm latched

Factory reset usuwa trwałe latched alarms, ponieważ usuwa cały user/device state.

Po boot alarm manager ponownie ocenia aktualne warunki fizyczne.

Jeśli przyczyna CRITICAL nadal istnieje, alarm pojawi się ponownie.

---

# 44. Recovery bez factory reset

Factory reset jest ostatecznością.

Jeśli problem dotyczy tylko Wi-Fi, użytkownik powinien móc poprawić Wi-Fi bez kasowania całej konfiguracji domenowej.

---

# 45. Network reset

Core może wspierać osobną akcję:

```text
Reset Network
```

która usuwa tylko konfigurację sieci.

Jest to zalecane, jeśli upraszcza odzyskanie dostępu.

---

# 46. MQTT reset

Nie potrzebujemy osobnej globalnej funkcji „MQTT factory reset”.

MQTT można wyłączyć lub poprawić z WWW.

---

# 47. AP recovery trigger

Po nieudanych reconnectach urządzenie może uruchomić fallback AP automatycznie.

Możliwy jest również jawny lokalny trigger sprzętowy.

---

# 48. Onboarding status w diagnostyce

Core może raportować:

```text
network_configured
onboarding_required
ap_active
```

Nie muszą być encjami HA.

---

# 49. Web availability podczas AP

WWW powinno działać w AP nawet wtedy, gdy:

- MQTT jest wyłączone,
- NTP nie działa,
- HA jest niedostępny.

---

# 50. Czas w onboarding

Brak poprawnego czasu nie blokuje konfiguracji sieci.

---

# 51. DNS captive portal

Jeśli captive portal jest używany, DNS redirect ma być lekki i lokalny.

Nie komplikujemy systemu tylko po to, aby wymusić automatyczne otwarcie strony.

---

# 52. Bezpieczeństwo danych podczas onboardingu

Hasła:

- nie są logowane,
- nie są publikowane przez MQTT,
- nie są zwracane w zwykłym API w plaintext.

---

# 53. Network config atomic save

Wi-Fi credentials zapisujemy atomowo jako logiczny zestaw.

Nie może powstać np. nowe SSID ze starym hasłem wskutek częściowego zapisu.

---

# 54. Test po zapisie Wi-Fi

Preferowana sekwencja:

```text
save candidate
-> attempt connect
-> success -> commit/keep
-> fail -> allow correction
```

Jeżeli backend nie obsługuje candidate config, dopuszczalne jest zapisanie i późniejsza korekta przez fallback AP.

---

# 55. Last known good network

Core może przechowywać last known good Wi-Fi config, jeśli realnie zwiększa niezawodność.

Nie jest obowiązkowe w v1.

---

# 56. Multiple Wi-Fi profiles

W v1 nie wymagamy obsługi wielu profili Wi-Fi.

Jedno aktywne SSID jest wystarczające.

---

# 57. Static IP

Static IP jest opcjonalne.

Domyślnie:

```text
DHCP
```

jest preferowane jako prostsze i bezpieczniejsze.

---

# 58. Hostname

Core może używać przewidywalnego hostname:

```text
aquaone-<type>-<MAC6>
```

Jeśli mDNS jest wspierane, można umożliwić dostęp po:

```text
<hostname>.local
```

Nie jest to wymagane do onboardingu.

---

# 59. Device identity

Onboarding nie pozwala zmieniać technicznego device ID, MAC6 ani `device_type`.

---

# 60. Friendly name

Core nie wymaga własnej friendly name.

Użytkownik może nazwać urządzenie w HA.

---

# 61. Błędna konfiguracja domenowa po onboardingu

Jeśli Wi-Fi działa, ale konfiguracja domenowa jest niekompletna:

- WWW działa normalnie,
- urządzenie pokazuje potrzebę konfiguracji,
- niebezpieczne funkcje są zablokowane,
- nie uruchamiamy ponownie AP tylko z tego powodu.

---

# 62. Onboarding i OTA

OTA może być dostępne w recovery AP, jeśli pozwala bezpiecznie naprawić urządzenie.

Jest to zalecane dla safe boot.

---

# 63. Onboarding i Diagnostics

Recovery AP powinien udostępniać co najmniej podstawową diagnostykę.

---

# 64. Safe boot + AP

Jeśli urządzenie wejdzie w safe boot z powodu crash loop, może uruchomić recovery AP nawet przy istniejącej konfiguracji Wi-Fi, jeśli STA nie daje dostępu serwisowego.

---

# 65. No cloud ownership

Onboarding nie wiąże urządzenia z żadnym kontem/chmurą.

---

# 66. Re-onboarding

Zmiana Wi-Fi nie powinna resetować pozostałej konfiguracji.

---

# 67. Factory reset i backup

Przed factory reset WWW może przypomnieć o możliwości eksportu backupu.

Nie blokuje to resetu.

---

# 68. Import po factory reset

Po przywróceniu sieci użytkownik może zaimportować kompatybilny backup zgodnie z `CONFIG_STORAGE_STANDARD`.

---

# 69. Recovery priority

Przy problemach z dostępem preferowana kolejność:

```text
normal STA
-> reconnect
-> fallback AP
-> network correction
-> config restore
-> factory reset
```

Factory reset nie jest pierwszą reakcją.

---

# 70. Core API — odpowiedzialność

`aquaOneCore` powinien docelowo zapewnić mechanizmy odpowiadające za:

```text
OnboardingManager
AccessPointManager
NetworkRecovery
FactoryResetService
NetworkConfig
RecoveryMode
```

Nazwy klas mogą się różnić.

---

# 71. OnboardingManager

Powinien odpowiadać za:

- wykrycie braku configu,
- start/stop AP,
- onboarding state,
- przejście do normalnej pracy,
- fallback/recovery.

---

# 72. FactoryResetService

Powinien centralnie koordynować:

- czyszczenie Core config,
- czyszczenie Domain config,
- trwały alarm state,
- reset defaults,
- restart.

Domena nie powinna ręcznie kasować losowych namespace poza wspólnym procesem.

---

# 73. Domena — odpowiedzialność

Projekt domenowy definiuje:

- minimalną bezpieczną konfigurację,
- domenowe defaults,
- dane czyszczone przy factory reset,
- czy wymagany jest dodatkowy krok onboardingu domenowego.

---

# 74. Integracja z WEB_STANDARD

WWW jest podstawowym interfejsem onboardingu i recovery.

UI ma być lekkie i działać bez Internetu/CDN.

---

# 75. Integracja z CONFIG_STORAGE_STANDARD

Onboarding i reset korzystają ze wspólnego config/storage.

Nie ma osobnej „tymczasowej konfiguracji” bez mechanizmu walidacji.

---

# 76. Integracja z SAFETY_STANDARD

Brak konfiguracji nie może aktywować ryzykownych funkcji.

Safe defaults i interlocki mają pierwszeństwo.

---

# 77. Integracja z MQTT_STANDARD

MQTT nie jest wymagane do onboardingu.

Po skonfigurowaniu MQTT urządzenie stosuje standardowy reconnect/snapshot/availability.

---

# 78. Integracja z OTA_STANDARD

Recovery mode może udostępniać OTA, ale tylko przez bezpieczny mechanizm standardu OTA.

---

# 79. Testy

Należy testować co najmniej:

- fresh device boot,
- no Wi-Fi credentials,
- wrong Wi-Fi password,
- unavailable AP,
- fallback AP,
- STA reconnect,
- network reset,
- factory reset from WWW,
- interrupted reset,
- reset during safety_lock,
- recovery after corrupt config,
- import backup after reset.

---

# 80. Reguła końcowa

Urządzenie aquaOne musi dać się uruchomić, skonfigurować i odzyskać lokalnie bez chmury i bez HA.

**Wspólny onboarding, AP recovery i factory reset pozostają TARGET. Domena definiuje
minimalną konfigurację wymaganą do bezpiecznego działania oraz dokładny zakres resetu.**

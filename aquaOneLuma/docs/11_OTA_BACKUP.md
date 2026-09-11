# LumaSense — kontrakt OTA i backupu

## 1. Stan implementacji

OTA, rollback, eksport, import i factory reset nie są jeszcze zaimplementowane. Są opcjonalnymi funkcjami administracyjnymi. Ich brak nie wpływa na autonomiczną pracę Core.

## 2. OTA

Przyszłe OTA może być uruchamiane przez WWW, lecz nie może modyfikować aktywnej konfiguracji podczas zapisu firmware. Błąd pobierania, weryfikacji albo zapisu nie może uszkodzić zapisanej konfiguracji.

Docelowy przebieg:

1. sprawdzenie obrazu i zgodności urządzenia;
2. zapis do nieaktywnej partycji;
3. weryfikacja kompletności;
4. wybór nowej partycji dopiero po sukcesie;
5. restart;
6. potwierdzenie poprawnego startu;
7. rollback, jeśli potwierdzenie nie nastąpi.

Wymagana tablica partycji, kryteria potwierdzenia startu, podpis obrazu i polityka rollback pozostają TODO.

## 3. Zachowanie po restarcie

Po każdym restarcie, także po OTA lub rollback:

- `LumaCore::begin()` przywraca NORMAL;
- żaden tryb chwilowy ani OFF nie jest odtwarzany;
- hardware rozpoczyna od fizycznego OFF;
- błędna inicjalizacja hardware blokuje normalne PWM;
- nieważny RTC utrzymuje requested i actual na 0%;
- poprawny czas rozpoczyna start 60 s od zera do bieżącego celu.

OTA nie może omijać tych zasad.

## 4. Rozdzielenie firmware i konfiguracji

Partycje aplikacji i dane konfiguracji muszą być rozdzielone. Aktualizacja ani rollback nie wykonują factory reset. Jeśli nowy firmware wymaga innego schematu, Storage migruje kopię danych i aktywuje ją dopiero po pełnym sukcesie.

Niepowodzenie migracji pozostawia ostatnią poprawną konfigurację oraz umożliwia bezpieczny rollback firmware.

## 5. Eksport

Przyszły eksport tworzy wersjonowany dokument z pełną trwałą konfiguracją obsługiwaną w danym wydaniu. Nie eksportuje RuntimeState ani trybów chwilowych.

Jeżeli przyszła struktura obejmie sekrety Wi-Fi lub MQTT, UI musi jasno określić, czy są zawarte. Format, szyfrowanie i polityka sekretów są TODO.

## 6. Import

Import przebiega transakcyjnie:

```text
plik
→ parser z limitami
→ schemaVersion
→ migracja kopii
→ ConfigValidator
→ podsumowanie i potwierdzenie
→ atomowy zapis
→ aktywacja po sukcesie
→ restart, jeśli wymagany
```

Niepoprawny lub niekompletny plik nie może zmienić aktywnej konfiguracji. Błąd zapisu także pozostawia Core na dotychczasowej wersji.

Jeżeli import zmienia `pwmInverted`, nowa polaryzacja obowiązuje dopiero po restarcie.

## 7. Factory reset

Factory reset jest osobną, jawną operacją z potwierdzeniem. Musi bezpiecznie utworzyć defaults zgodne z ConfigValidator. Nie jest automatyczną reakcją na błąd OTA, migracji, RTC ani sieci.

Sposób wejścia w pierwszą konfigurację po resecie pozostaje TODO.

## 8. Odporność integracji

Obsługa OTA i importu nie może blokować sterowania w sposób pozostawiający wyjścia w nieokreślonym stanie. Przed restartem hardware powinien otrzymać fizyczny OFF. Dokładna polityka świecenia podczas długiego transferu OTA pozostaje TODO i wymaga testu na urządzeniu.
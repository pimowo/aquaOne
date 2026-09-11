# LumaSense — Storage

## 1. Stan implementacji

ETAP 11 dodał działający `StorageService` oparty na Preferences/NVS oraz funkcję `createDefaultConfig()`. Warstwa pozostaje niezależna od Wi-Fi, WWW, MQTT, NTP i OTA. Od ETAPU 13 produkcyjny `FirmwareApp` używa jej podczas startu do wyboru konfiguracji NVS albo defaults.

Publiczne API:

```cpp
bool begin();
bool load(DeviceConfig& config);
bool save(const DeviceConfig& config);
bool hasValidConfig() const;
```

`begin()` otwiera namespace NVS i sprawdza oba sloty. Pusty NVS nie jest błędem inicjalizacji: `begin()` zwraca true, `hasValidConfig()` jest false, a `load()` zwraca false.

## 2. NVS i sloty

Storage używa namespace:

```text
lumasense
```

oraz dwóch niezależnych blobów:

```text
cfg_a
cfg_b
```

Bezpieczeństwo zapisu wynika z naprzemiennych slotów oraz pełnej weryfikacji po zapisie. Poprzedni poprawny slot nie jest kasowany po sukcesie nowego zapisu.

## 3. Format rekordu

Wszystkie liczby nagłówka są kodowane little-endian. Nagłówek ma 24 bajty:

| Offset | Rozmiar | Pole |
|---:|---:|---|
| 0 | 4 | magic `0x4C534346` |
| 4 | 2 | record format version = 1 |
| 6 | 2 | `DeviceConfig::schemaVersion` |
| 8 | 4 | długość payloadu |
| 12 | 4 | generation |
| 16 | 4 | CRC32 payloadu |
| 20 | 4 | CRC32 pierwszych 20 bajtów nagłówka |

Payload jest bitową kopią całego `DeviceConfig`. `static_assert` wymaga, aby struktura była trivially copyable. Długość musi być dokładnie równa `sizeof(DeviceConfig)`.

Zmiana układu pól, typów, wyrównania albo ABI wymaga podniesienia `DEVICE_CONFIG_SCHEMA_VERSION`. Bieżąca wersja schematu wynosi 1. Migracje nie są jeszcze obsługiwane.

## 4. CRC32

CRC payloadu obejmuje wszystkie bajty zapisanej konfiguracji. Dodatkowe CRC nagłówka chroni także magic, format, schemaVersion, długość, generation i CRC payloadu.

Algorytm:

- CRC-32 z odwróconym wielomianem `0xEDB88320`;
- wartość początkowa `0xFFFFFFFF`;
- końcowe XOR `0xFFFFFFFF`.

CRC wykrywa przypadkowe uszkodzenie rekordu; nie jest podpisem kryptograficznym.

## 5. Polityka load

`load()` czyta oba sloty. Każdy kandydat musi przejść kolejno:

1. dokładną kontrolę długości blobu;
2. magic;
3. record format version;
4. aktualny schemaVersion;
5. deklarowaną długość payloadu;
6. CRC nagłówka;
7. CRC całego payloadu;
8. zgodność schemaVersion nagłówka i payloadu;
9. `ConfigValidator::validate()`.

Wynik:

- tylko A poprawny → A;
- tylko B poprawny → B;
- oba poprawne → rekord z nowszym generation;
- oba niepoprawne lub puste → false.

Konfiguracja przekazana do `load()` jest podmieniana dopiero po pełnej walidacji wybranego rekordu. Błąd nie modyfikuje obiektu wyjściowego.

## 6. Generation i overflow

Pierwszy zapis otrzymuje generation 1. Każdy następny zwiększa numer modulo `uint32_t`.

Rekord A jest nowszy od B, gdy:

```cpp
difference = generationA - generationB;
difference != 0 && difference < 0x80000000
```

Dzięki temu `0` jest poprawnie uznawane za nowsze od `UINT32_MAX`. Różnica dokładnie połowy przestrzeni numerów jest stanem nieosiągalnym przy normalnej naprzemiennej sekwencji; przy równych lub niejednoznacznych wartościach wybór jest deterministycznie po stronie A.

## 7. Polityka save

Przed zapisem Storage wymaga:

- `config.schemaVersion == DEVICE_CONFIG_SCHEMA_VERSION`;
- pełnego sukcesu `ConfigValidator`.

Niepoprawna konfiguracja nie jest zapisywana.

Sekwencja:

1. przeskanuj oba sloty i wybierz aktualnie najnowszy poprawny;
2. wybierz drugi slot; przy pustym NVS zacznij od A;
3. zbuduj pełny rekord z następnym generation i CRC;
4. zapisz blob przez `Preferences::putBytes()`, które wykonuje operację NVS wraz z commit;
5. wymagaj zwrócenia pełnej długości;
6. ponownie odczytaj zapisany slot;
7. wykonaj wszystkie kontrole load;
8. sprawdź generation i bitową zgodność payloadu z argumentem.

Dopiero po tej weryfikacji nowy slot jest uznawany za bieżący. Stary poprawny slot pozostaje nietknięty. Przerwany zapis, krótki write, błąd NVS lub uszkodzenie wykryte po zapisie powodują `save() == false`; poprzednia poprawna konfiguracja pozostaje dostępna.

`save()` przyjmuje const reference i nie modyfikuje przekazanego `DeviceConfig`. StorageService nie zmienia konfiguracji używanej przez LumaCore.

## 8. Bufory robocze

Bufor pełnego rekordu i bufor kandydata są alokowane w `begin()` przez `new (std::nothrow)`. Ogranicza to użycie stosu zadania ESP32. Brak pamięci powoduje `begin() == false`.

StorageService nie jest kopiowalny. Destruktor zamyka Preferences i zwalnia bufory.

## 9. Defaults

`createDefaultConfig()` tworzy konfigurację, która przechodzi ConfigValidator:

- schemaVersion 1;
- profile aktywny i serwisowy 0;
- globalny limiter 100%;
- kanały enabled, bez inwersji, hardMax 100%, kalibracja 0–100%, gamma 1;
- profile 08:00–19:00, poziomy DAY i NIGHT równe 0;
- poprawne identyfikatory ośmiu etapów;
- NIGHT wyłączone;
- Europe/Warsaw;
- bezpieczne zerowe dane akwarium.

Pusty NVS nie powoduje automatycznego zapisu defaults. Warstwa wyżej wybierze defaults i zdecyduje o ich zapisaniu.

## 10. Granica z Core

StorageService nie zna LumaCore i nie aktywuje konfiguracji. `FirmwareApp`:

1. wywołuje `storage.begin()`;
2. próbuje `storage.load(config)`;
3. przy braku poprawnych danych używa defaults bez zapisu do NVS;
4. waliduje wybrany config;
5. przekazuje ten sam obiekt do `hardware.begin()` oraz `LumaCore::begin()`.

Błąd zapisu nie może zmienić działającego Core. Runtime edycji i transakcyjne zastosowanie nowej konfiguracji pozostają późniejszym zakresem.
## 11. Zakres pozostający na później

- migracje starszych schemaVersion;
- factory reset;
- import i eksport;
- polityka debounce zapisów z WWW;
- test zaniku zasilania podczas rzeczywistego zapisu NVS;
- test trwałości na fizycznym NVS przez pełny restart i odłączenie zasilania.
## 12. AC4 — migracja infrastruktury do Aqua Core

**AC4 IMPLEMENTED.** Mechanika trwałego zapisu znajduje się od tej wersji w AquaCore::Config. Aqua Core udostępnia neutralny StorageBackend, adapter PreferencesStorageBackend, opis StorageRecord, funkcję crc32() oraz generic StorageService. Usługa operuje na nieprzezroczystym buforze, rozmiarze payloadu, całkowitym numerze schematu i wskaźniku funkcji walidującej.

Aqua Core odpowiada za sloty A/B, rekord, CRC32, numer generacji, wybór nowszej poprawnej kopii, pełny zapis blobu i weryfikację po zapisie. Nie zna DeviceConfig, kanałów, profili, defaults ani reguł lampy.

LumaSense::StorageService zachowuje dotychczasowe API i jest cienkim adapterem. Przekazuje sizeof(DeviceConfig), DEVICE_CONFIG_SCHEMA_VERSION oraz adapter walidatora wywołujący lokalny ConfigValidator. ConfigTypes, ConfigValidator i ConfigDefaults nadal należą wyłącznie do LumaSense. Aqua Core nie zapisuje defaults automatycznie i nie wykonuje migracji schematu.

### 12.1 Zgodność istniejącego NVS

AC4 zachowuje zgodność binarną z rekordami zapisanymi przed migracją. Bez zmian pozostały:

- namespace lumasense;
- klucze cfg_a i cfg_b;
- 24-bajtowy układ nagłówka i kodowanie little-endian;
- magic 0x4C534346 i record format version 1;
- 16-bitowe pole schemaVersion;
- 32-bitowe pola długości, generacji i CRC;
- surowy payload DeviceConfig;
- algorytm CRC-32 z wielomianem 0xEDB88320;
- porównanie generacji odporne na przejście UINT32_MAX -> 0.

Nie jest potrzebna migracja danych NVS. Zmiana ABI samego DeviceConfig nadal wymaga osobnego podniesienia jego wersji schematu.
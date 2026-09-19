# Architecture vNext — decyzje

**Status:** DRAFT ACCEPTED dla FAZY 0, F1.1 CONTRACT GATE, F1.5 SYS-102 DESIGN GATE, F1.6 SYS-104 DESIGN GATE, F1.8 SYS-105 RECOVERY DESIGN GATE, F1.9 SYS-106 WRITER OWNERSHIP DESIGN GATE, F1.10 SYS-107 RESTART REQUEST POLICY DESIGN GATE, F1.11 SYS-101 RUNTIME IDENTITY DESIGN GATE i F1.12 IDN-101 DEVICE IDENTITY DESIGN GATE
**Scope:** cała platforma aquaOne
**Zasada:** Architecture vNext jest TARGET. CURRENT wynika z kodu i macierzy projektu. Legacy code is not architecture.

## ACCEPTED

### ARCH-001 — autonomia urządzenia
Każde urządzenie działa autonomicznie. Podstawowa funkcja nie zależy od Home Assistant, MQTT, WWW/HTTP, WebSocket/realtime, Panelu, Internetu ani innego aquaOne.

### ARCH-002 — granica Core / Domain
Core dostarcza mechanizmy wspólne, Domain znaczenie funkcjonalne. Core nie zawiera logiki produktowej ani rozgałęzień po `device_type`.

### ARCH-003 — równorzędni klienci
Luma, Doser, Hydro, Clima, Gas i Fauna są równorzędnymi klientami platformy. Panel jest klientem, nie zależnością krytyczną domeny.

### ARCH-004 — Composition Root
Composition Root zna konkretny skład urządzenia, hardware, BoardProfile, Core i adaptery. Domain otrzymuje jawne, wąskie zależności.

### ARCH-005 — ocena legacy
Istniejący kod może zostać oceniony jako KEEP, ADAPT, REWRITE albo REMOVE. Nie zmieniamy Architecture vNext tylko po to, aby zachować kompatybilność z istniejącym projektem.

### ARCH-006 — Composition Root i ApplicationRuntime
Composition Root zna konkretne implementacje i składa aplikację. ApplicationRuntime wyłącznie
koordynuje startup i runtime przez wąskie kontrakty. Nie posiada konkretnych usług, nie zna
konkretnych klas Domain, Network, Web ani MQTT, nie jest service locatorem ani Registry i nie
zawiera semantyki domenowej. Dokładny ApplicationPlan, hooks i ownership definiuje decyzja
SYS-102 poniżej.

### SYS-001 — lifecycle
Docelowy lifecycle to: POWER ON, BOOT, CORE INIT, LOAD/VALIDATE CONFIG, HARDWARE INIT, DOMAIN INIT, SAFETY VALIDATION, NETWORK INIT, INTERFACES INIT, RUNNING.

### SYS-002 — niezależne osie stanu
OperationalState, HealthState, SafetyState, DomainMode, alarms i Action Locks są rozdzielone. Nie tworzymy jednego ogromnego enum.

### SYS-003 — StartupPhase
Publiczny `StartupPhase` opisuje wyłącznie startup: `BOOT`, `CORE_INIT`,
`LOAD_VALIDATE_CONFIG`, `HARDWARE_INIT`, `DOMAIN_INIT`, `SAFETY_VALIDATION`,
`NETWORK_INIT`, `INTERFACES_INIT`, `RUNNING`. Nie obejmuje Maintenance, Recovery ani
Restart i nie konkuruje z OperationalState. Early safe outputs są technicznym krokiem na
początku `BOOT`, a nie osobną publiczną fazą.

### SYS-004 — wynik startupu
`StartupRequirement` rozróżnia `REQUIRED` i `OPTIONAL`, a `StartupOutcome` rozróżnia
`SUCCEEDED`, `DISABLED` i `FAILED`. Requirement należy do composition/deklaracji
participanta, nie do wyniku operacji. `REQUIRED + FAILED` zatrzymuje normalny startup i
prowadzi do `ERROR + FAULT + LOCKED`. `OPTIONAL + FAILED` nie blokuje startupu i może
prowadzić do `DEGRADED`. `OPTIONAL + DISABLED` nie degraduje health. `REQUIRED + DISABLED`
jest kombinacją nieprawidłową.

### SYS-005 — stabilny startup error code
Każdy startup failure posiada krótki, stabilny error code. Długi dynamiczny tekst nie jest
podstawowym kontraktem błędu. Reprezentację i zasady katalogów definiuje SYS-104.

### SYS-006 — stan systemu w Fazie 1
`OperationalState` ma wartości `BOOTING`, `RUNNING`, `MAINTENANCE`, `ERROR`;
`HealthState`: `OK`, `DEGRADED`, `FAULT`; `SafetyState`: `CLEAR`, `LOCKED`.
ApplicationRuntime jest authoritative writer dla OperationalState. HealthState wynika w
Fazie 1 z minimalnej koordynacji startup/runtime. SafetyState startuje jako `LOCKED`, a
startup safety gate może przejść do `CLEAR`. Nie powstaje ogólne `setSafetyState()`,
SafetyManager ani Action Locks. `MAINTENANCE` istnieje w typie, ale jego workflow należy do
późniejszej fazy.

### SYS-007 — granice restartu
`RestartReason` opisuje, dlaczego urządzenie wystartowało, `RestartRequestReason` — dlaczego
bieżący runtime żąda restartu, a `RestartExecutor` wykonuje efekt platformowy. Domain i
transport otrzymują wyłącznie `RestartRequester`. Zakres Fazy 1 to: request, pending request,
safe point w runtime loop, RestartExecutor. RestartPreparation, Maintenance transition,
MQTT offline, timeouty i OTA workflow pozostają poza Fazą 1.

### SYS-101 — RuntimeIdentity

**Status:** ACCEPTED — TARGET contract; F1.11 nie implementuje value type ani generatora.

#### Nazwa, semantyka i wybór reprezentacji

Wybrana nazwa to RuntimeIdentity, zgodna z kategorią IDN-001; docelowy neutralny value type
należy do AquaCore::Identity. RuntimeInstanceId jest precyzyjny, ale wprowadza drugą nazwę
tej samej kategorii; RuntimeId jest mniej jednoznaczny. BootIdentity/BootInstanceId wiążą
nazwę z resetem zamiast instancją runtime, która obejmuje także startup i ERROR recovery.
RuntimeIdentity identyfikuje jedną konkretną instancję runtime, nie urządzenie, MAC,
wersję firmware, reset cause ani friendly name.

| Reprezentacja | Ocena |
|---|---|
| Random 32-bit integer | Zero heap i prosty format, ale zbyt mała przestrzeń kolizji dla historii/fleet diagnostics. |
| Random 64-bit integer | 8 bajtów, proste equality i fixed hex, bez czasu/persistence; wystarcza dla przyjętego probabilistycznego modelu. |
| Random 128-bit / UUID | Znacznie mniejszy collision risk, ale większy storage i tekst; UUID wnosi dodatkowe format/version rules bez wymagania globalnego indeksowania. |
| Fixed random byte array | Zero heap, ale to forma storage, nie collision model; osiem random bytes ma tę samą przestrzeń co 64-bit value. |
| Persistent boot counter | Deterministyczny w obrębie device tylko przy poprawnym atomic storage, wrap i reset policy; wymaga persistence i zwiększa flash wear. |
| Timestamp-based identity | Zależy od dostępności, rozdzielczości i cofania czasu; RTC/NTP nie są obowiązkowe. |
| Device-derived + random | Wymaga dodatkowych danych/mappingu i może zmniejszyć przestrzeń losową; device identity można publikować osobno. |

Wybrane jest losowe 64-bit nonzero value. 128-bit byłby właściwszy przy wymaganiu ogromnego
globalnego zbioru z jeszcze mniejszym collision risk; takiego wymagania nie ma. Counter nie uzasadnia
persistence dla identyfikatora diagnostycznego. Wszystkie przyszłe transports mogą serializować
ten sam value bez zależności od sieci, czasu lub platformy w samym value type.

#### Value, validity, equality i format

Storage jest konceptualnie unsigned uint64-sized value, bez heap, pointer identity ani
resource ownership. Zero oznacza invalid/not generated i jest stanem default konstrukcji.
Legalna RuntimeIdentity zawsze jest nonzero. Jawna factory z pełnej binary value waliduje
nonzero; nie używa implicit conversion ani zastępczej wartości przy błędzie. Typ jest tanio
copyable; trivial copyability nie ustanawia ABI. Equality porównuje pełne 64 bity, bez adresów
i bez porównywania tekstu, gdy binary value jest dostępne. Równość invalid sentinel nie oznacza
równości legalnych runtime instances.

Canonical machine serialization legalnej wartości to dokładnie 16 uppercase ASCII hex chars,
most-significant digit first, bez 0x, separatorów i locale, z zachowaniem leading zeros,
np. 0000000000000001. W implementacji formattera C-string buffer może wymagać 17 bajtów (16 chars + terminator);
jest to konsekwencja tego formattera, nie wire/storage representation requirement. Formatter
używa caller-provided storage, nie truncates i jawnie zgłasza zbyt mały buffer albo invalid value.
Zero nie jest publikowane jako legalna identity. Format jest niezależny od host endianness;
nie ustanawia się raw-memory binary wire format. Skrót w UI jest wyłącznie display,
nie identity ani kluczem do equality. Parsing API pozostaje implementacyjne.

#### Generator, RNG i collision model

Wybrany wąski koncept RuntimeIdentityGenerator::generate() zwraca legalną nonzero identity
albo jawne failure. Platform generator generuje candidates i wykonuje bounded retry zgodnie
z backendem; caller nie otrzymuje surowych candidates. Identity startup participant/adapter
wywołuje generator, defensywnie waliduje wartość, nie publikuje zero i przy zero/invalid zwróconym
jako success zwraca zwykły participant failure z przyszłym identity-specific error code.
ApplicationRuntime widzi wyłącznie StartupStepResult: nie losuje i nie interpretuje binary identity.
Nie rozszerza się znaczenia SYS-104 RESULT_CONTRACT_FAILURE. Composition Root przekazuje generator
jawnie; neutralny kontrakt nie importuje ESP32 API, Time, Network, NVS, Registry ani crypto frameworka.

Algorytm v1 platform generatora pobiera pełne 64 random bits. Przy zero wykonuje bounded retry
zgodnie z lokalnie zdefiniowanym limitem backendu; nie może wykonywać nieskończonej pętli. Po
wyczerpaniu tego limitu, source failure albo braku bezpiecznie dostępnego źródła zwraca explicit
failure bez stałego fallbacku, millis seed, countera lub zależności od loggera. Dokładna liczba
prób nie jest kontraktem SYS-101; test backendu musi sprawdzać jego własny limit. Generator
kończy się w bounded platform execution, nie czeka na Network ani RTC/NTP. Native tests używają
jawnie wstrzykniętego deterministic fake. Nie trzeba projektować entropy health frameworka.

Platform backend używa źródła losowości odpowiedniego dla danego SoC/frameworka, o jakości
wystarczającej dla probabilistic collision resistance. Nie seeduje PRNG wyłącznie millis()/uptime,
nie wymaga RTC/NTP ani nie tworzy architektonicznej zależności od Network, AP lub Internetu.
Warunki entropy readiness, RF preparation, SDK calls i cleanup należą do backendu. ESP32 jest
przykładem backendu, nie uniwersalną procedurą; sama obecność esp_random() nie dowodzi jakości
przed NETWORK_INIT. Backend nie może kolidować z ADC/I2S/RF ani early safe outputs, a brak
preconditions oznacza failure, nie przesunięcie generowania do Network. Dokładne SDK calls są
przyszłą implementacją.
Źródło wymagań platformowych: [Espressif ESP32 RNG documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.5/esp32/api-reference/system/random.html).

Przyjęta jest praktyczna probabilistyczna unikalność, przy niezależnych, równomiernych
losowaniach z 2^64-1 legalnych wartości; nie jest to globalna absolutna gwarancja.
Dla n losowań birthday approximation wynosi n(n-1)/(2(2^64-1)), gdy ryzyko jest małe:
dla miliona instancji około 2.7e-8. Ryzyko rośnie z liczbą instancji; nie deklaruje się
bezkolizyjności nieograniczonej floty. Consumers odnoszą identity do konkretnego device,
nie używają samej RuntimeIdentity jako gwarantowanego globalnego primary key.
V1 nie wykrywa kolizji i nie przechowuje historii. Nowe losowanie po reboot może teoretycznie
dać tę samą wartość; nowa instancja oznacza świeżą generację, nie matematycznie wymuszoną
nierówność. Zmiana wartości jest sygnałem nowego runtime, równość nie jest dowodem braku rebootu.

#### Generation point, ownership i failure

Nie generuje się w constructor ApplicationRuntime: hardware/RNG effect nie może wyprzedzić
early safe outputs. BOOT byłby możliwy po early action, ale CORE_INIT odpowiada inicjalizacji
identity Core. INTERFACES_INIT jest za późno i sprzęgałby obowiązkową identity z optional interfaces.

Composition Root posiada jeden runtime-scoped identity owner/storage i generator platformowy,
a ApplicationRuntime koordynuje generowanie przez pierwszy REQUIRED participant CORE_INIT, po
earlySafeOutputs, pełnej walidacji planu i ordinary BOOT participants. Composition Root MUSI
deklarować identity participant przed wszystkimi zwykłymi CORE_INIT participantami, które
wymagają RuntimeIdentity; kolejność wynika z declaration-order contract SYS-102, bez dependency
DAG. BOOT participants nie mogą wymagać już wygenerowanej identity ani uniemożliwić bezpiecznego
RNG window. Nie dodaje się special action, fazy, nowych pól ApplicationPlan ani callback access
do całego runtime.

Owner jest przypisany do jednej instancji ApplicationRuntime; nowa instancja dostaje świeży
owner w invalid state, bez reuse już zainicjalizowanego storage. Jest jedynym authoritative
storage, raz publikuje legalną wartość i nie udostępnia setterów/regenerate consumers.
Read-only identity views i kopie nie są drugim mutable źródłem prawdy. Owner i generator/context
żyją co najmniej przez okres używania przez runtime oraz pożyczone read views zgodnie z SYS-102.
Nie tworzy się IdentityService frameworka ani service locatora; dokładny holder/read API
pozostaje implementacyjne. ApplicationRuntime nie staje się właścicielem concrete generatora.

Generate jest wywołane raz podczas pierwszego start(), jeśli osiągnięto identity participant;
lokalne retry zero są częścią tego jednego wywołania. Failure generatora oznacza REQUIRED
participant failure w CORE_INIT ze stable Core error code RUNTIME_IDENTITY_GENERATION_FAILED
w namespace AQUA.CORE; sukces z invalid oznacza contract violation tego kroku, bez publikacji zero.
Normalny startup zatrzymuje się jako ERROR + FAULT + LOCKED według SYS-102/104/105.
RuntimeIdentity jest fundamentalnym invariantem normalnej instancji aquaOne runtime, nie
wygodnym dodatkiem diagnostics/Realtime. Normalny RUNNING musi mieć legalną identity, ponieważ
wyznacza granicę życia instancji, namespace przyszłych per-runtime event/sequence semantics,
sygnał odróżniający stan przed i po reboot oraz część systemowego lifecycle/identity contract.
Dlatego brak legalnej identity przed RUNNING jest REQUIRED startup failure → ERROR + FAULT + LOCKED.
To invariant lokalnego Core lifecycle, bez zależności od Network; brak Network nie jest failure RNG.

Jeżeli fatal wystąpi przed legalnym RuntimeIdentity, identity pozostaje unavailable, zero nie jest
publikowane jako legalne ID, a ERROR recovery shell i lokalna diagnostyka mogą działać bez identity.
Recovery nie generuje jej później, a drugi start() nie generuje jej ponownie; dopiero nowa runtime
instance może ponownie przejść normalny generation step. Jeśli legalna identity powstała przed
późniejszym fatal failure, ERROR shell, restart request i RestartExecutor failure jej nie zmieniają.
StartupReport i cztery osie RuntimeStatus nie są rozszerzane; identity jest osobnym read-only
identity snapshotem, dostępnym lokalnym status/diagnostics consumers niezależnie od transportów.
Invalid/unavailable jest jawnie odróżnione od legalnej wartości; exact schema pozostaje otwarte.

#### Lifetime, relacje i consumers

Legalna wartość jest niezmienna aż do końca instancji, także przez RUNNING, MAINTENANCE,
ERROR, reconnect i recovery activity. Kolejne start() nie generuje jej ponownie; odczyt jest
bez side effects. Pending RestartRequest, executor failure, config repair oraz zakończenie
factory reset/restore/OTA nie zmieniają identity. Faktyczny reboot/power cycle/brownout lub
nowa instancja lifecycle rozpoczyna nową generację. SYS-101 nie definiuje semantyki deep sleep:
po wake nowa identity powstaje tylko wtedy, gdy future lifecycle tworzy NOWĄ instancję runtime;
kontynuacja tej samej instancji pozostaje nierozstrzygnięta. Polityka wielu instancji runtime nie
jest tu implementowana.

RuntimeIdentity nie jest persistent jako current identity, nie trafia do NVS, backupu ani
restore jako wartość do ponownego użycia. Nie wymaga flash writes ani RTC/NTP.
Ewentualny boot history i next-boot metadata są osobnymi future contracts.

DeviceIdentity jest stabilną kategorią technical device identity według IDN-001; CURRENT
Identity::DeviceIdentity przechowuje tylko deviceType i nie jest jeszcze finalnym unikalnym
device_id. SYS-101 nie zmienia tego ani IDN-101. BuildIdentity opisuje firmware/Core versions,
HardwareIdentity fizyczny wariant; reboot przy tych samych wartościach nadal losuje nową
RuntimeIdentity. MAC nie jest RuntimeIdentity i nie jest wymaganym składnikiem RuntimeIdentity generatora. Nie łączy się
identity categories w jeden canonical string; presentation może pokazać je obok siebie.

Future event sequence jest scoped do runtime instance: para RuntimeIdentity + per-runtime seq
rozróżnia ten sam numer przed/po reboot z przyjętym probabilistycznym zastrzeżeniem.
Device context pozostaje osobny. Seq width, overflow, allocation i payload API pozostają otwarte.
Zgodnie z EVT-002 nie tworzy to replay; reconnect/full resync może publikować identity,
a zmiana identity unieważnia poprzedni runtime state/seq consumers. Unavailable identity nie
udaje legalnego event stream ID. Dokładne zachowanie protokołu przy unavailable jest przyszłe.

MQTT może publikować RuntimeIdentity w retained state, ale topic root pozostaje oparty na
device identity/MAC standardzie, bez zmiany root przy każdym reboot. HTTP, Realtime, MQTT,
logs i diagnostics używają tego samego canonical value. Nie wymaga się wpisywania ID w każdym
logu; logger formatting pozostaje przyszłe. Identity jest publiczna i nie jest credential,
auth token, security nonce ani anti-replay primitive; nie może stanowić podstawy autoryzacji.

#### Testability, HIL i scope

Przyszłe host tests obejmują default/zero invalid, nonzero factory, pełne binary equality,
copy independence i 16 uppercase hex chars; backend generatora: legal nonzero, invalid candidates,
bounded retry zgodnie z własnym limitem i failure po jego wyczerpaniu; participant: legal success,
generator failure, defensive invalid jako participant failure i brak interpretacji identity przez
ApplicationRuntime; lifecycle: jedno generate na normalną instancję, stałe odczyty, drugi start bez
regeneracji, fatal przed identity unavailable, fatal po identity zachowuje wartość oraz nowa
instancja wywołuje generator ponownie bez wymagania różnej wartości. Deep sleep testuje się tylko
zgodnie z future lifecycle. Bez testów/buildów w F1.11.

HIL później sprawdza real platform RNG/entropy readiness i resource cleanup, startup bez zależności
od Network service, reboot/brownout/reset jako nową instancję i nowy generation attempt oraz
realne SoC behavior. Deep sleep/wake tylko wtedy, gdy future lifecycle definiuje nową instancję
runtime. Kilka różnych wartości nie dowodzi jakości rozkładu ani braku kolizji;
value-type correctness pozostaje host-testable.

SYS-101 nie zamyka IDN-101, ARCH-101, current-boot RestartReason catalog, event payload API,
Realtime protocol, MQTT payload/topic standard, Diagnostics full API, logger formatting,
boot history, next-boot metadata ani security/session identifiers. F1.11 nie zmienia
ApplicationRuntime, istniejących Device/Build/Hardware identity ani kodu produkcyjnego.

### SYS-102 — ApplicationPlan i participanty startupu
`ApplicationRuntime` używa hybrydowego planu: publiczna kolejność `StartupPhase` jest stała,
a zwykłe kroki startupu są płaską, niemodyfikowalną tablicą descriptorów przypisanych do faz.
Każdy descriptor zawiera stabilny identyfikator participanta, fazę, `StartupRequirement`
oraz callback i opcjonalny opaque context. Requirement i phase należą do composition, więc
callback nie zna fazy i nie może sam ogłosić się jako `REQUIRED`. Callback nie otrzymuje
runtime, registry ani zestawu usług. Zwraca minimalny wynik zawierający `StartupOutcome` i
stabilny error code. `FAILED` wymaga poprawnego kodu, a `SUCCEEDED` i `DISABLED` nie niosą
błędu; dokładny typ wyniku, reprezentację kodu i raport definiuje SYS-104.

Konceptualny kontrakt planu; typy wyniku definiuje SYS-104:

```cpp
using StartupCallback = StartupStepResult (*)(void* context);

struct StartupAction {
    const char* participantId;   // non-null, static lifetime
    StartupCallback callback;    // non-null
    void* context;               // nullable tylko dla funkcji bez instancji
};

struct StartupParticipant {
    StartupAction action;
    StartupPhase phase;
    StartupRequirement requirement;
};

struct ApplicationPlan {
    StartupAction earlySafeOutputs; // zawsze wymagany; no-op jest jawną implementacją
    StartupAction safetyGate;       // zawsze wymagany
    const StartupParticipant* participants;
    size_t participantCount;
};
```

Normatywna kolejność uruchomienia jest następująca: runtime ustawia `BOOTING`, `OK`,
`LOCKED` i bieżącą fazę `BOOT`; wykonuje minimalną strukturalną walidację wyłącznie
`earlySafeOutputs`; uruchamia ten krok dokładnie raz; dopiero po jego legalnym
`SUCCEEDED` wykonuje pełną strukturalną walidację pozostałego planu i rozpoczyna zwykły
startup. Minimalna walidacja early action wymaga non-null callbacka, non-null i niepustego
`participantId`; lifetime wskazywanego storage jest kontraktem Composition Root i nie jest
runtime-checkable. Walidacja strukturalna nie jest participantem, nie używa configu ani
hardware i nie zmienia publicznej `StartupPhase`. Jeśli early action jest nielegalny,
zwraca nielegalny wynik albo wynik inny niż `SUCCEEDED`, startup kończy się jako
`ERROR + FAULT + LOCKED`; ten stan logiczny nie jest dowodem osiągnięcia bezpiecznego stanu
fizycznych wyjść. Recovery pozostaje SYS-105.

Inwarianty `ApplicationPlan` są częścią SYS-102. `earlySafeOutputs.callback` i
`safetyGate.callback` oraz callback każdego zwykłego participanta muszą być non-null.
`context` może być `nullptr`; jeśli jest non-null, jego lifetime musi obejmować co najmniej
życie `ApplicationRuntime`. Każdy action, włącznie z obiema special actions, wymaga
non-null, niepustego i stabilnego przez runtime `participantId`; ID muszą być unikalne w
całym planie. Runtime przechowuje wyłącznie `const char*`, a storage ID należy do
Composition Root albo static storage. Dla pustej tablicy obowiązuje `participantCount == 0`
i `participants == nullptr`; dla niepustej `participantCount > 0` i `participants != nullptr`.
Tablica participantów żyje co najmniej tak długo jak runtime. Każdy descriptor musi mieć
poprawny `StartupPhase` i `StartupRequirement`, a tablica musi być uporządkowana
niemalejąco według phase. Kolejność deklaracji w tej samej fazie jest kolejnością wykonania;
nie ma priority, sortowania w runtime ani grafu zależności.

Zwykły participant może mieć wyłącznie fazę `BOOT`, `CORE_INIT`, `LOAD_VALIDATE_CONFIG`,
`HARDWARE_INIT`, `DOMAIN_INIT`, `NETWORK_INIT` albo `INTERFACES_INIT`. `BOOT` jest legalny
i oznacza wykonanie po early safe outputs. `SAFETY_VALIDATION` jest zarezerwowane dla
special `safetyGate`, a `RUNNING` jest wyłącznie terminalnym markerem i nie przyjmuje
zwykłych participantów. Network i Interfaces mogą zawierać wyłącznie participanty
`OPTIONAL`.

Obie special actions są wymagane przez kształt planu, nie mają `StartupRequirement`
i wykonują się dokładnie raz. `SUCCEEDED` kontynuuje startup, `FAILED` jest fatal failure,
a `DISABLED` jest contract failure i fatal failure. Zwykłe wyniki podlegają macierzy
`REQUIRED + SUCCEEDED` → continue, `REQUIRED + FAILED/DISABLED` → fatal,
`OPTIONAL + SUCCEEDED/DISABLED` → continue bez degradacji oraz
`OPTIONAL + FAILED` → continue z `DEGRADED`. Nieznany `StartupOutcome`, `FAILED` bez
stable error code oraz error code przy `SUCCEEDED` lub `DISABLED` są contract failure.

Brak descriptorów Network lub Interfaces oznacza intentional disabled przez Composition
Root. Obecny descriptor z `DISABLED` oznacza config-driven disabled, a obecny descriptor
z `FAILED` oznacza `DEGRADED`; osobny marker expected feature nie jest potrzebny. `void*`
context służy wyłącznie participant-specific context i nie może wskazywać service locatora,
Registry ani całego `AquaOneCore`; `nullptr` jest legalny dla callbacka bez instancji.

Podczas startupu `StartupPhase` wskazuje aktualnie wykonywaną fazę. Po błędzie pozostaje
fazą, w której startup został zatrzymany; structural failure przed zwykłymi fazami pozostaje
w `BOOT`. Po pełnym sukcesie phase wynosi `RUNNING`. `ApplicationRuntime` nie posiada
concrete services, kopiuje tylko mały plan view, nie kopiuje descriptorów i nie alokuje
heap; Composition Root posiada participant array, contexty, concrete services i storage ID.
Plan może mieć wiele participantów w jednej zwykłej fazie albo nie mieć żadnego. Przyszłe kroki
runtime są osobnym `RuntimePlan`, a restart safe point przypada na koniec kompletnego `tick()`;
runtime scheduling oraz RestartRequester/Executor nie są częścią SYS-102.

### SYS-104 — wynik kroku i raport startupu

Rozważone reprezentacje stable error code:

- jeden globalny `enum class : uint16_t` jest mały i prosty w testach, ale wymaga centralnego
  katalogu wszystkich błędów Core i Domain, sprzęga wydania oraz sprzyja rozgałęzieniom
  produktowym w Core;
- numeric value type z namespace/category usuwa kolizje globalnego enum i jest mały, ale
  wartości są słabo czytelne w diagnostyce, wymagają osobnego mapowania oraz trwałej polityki
  numerów; arbitralne zakresy zarezerwowane nie rozwiązują ownership katalogów;
- tekst kopiowany do bufora o stałej pojemności jest czytelny i łatwy w serializacji, ale
  pojemność byłaby arbitralnym limitem publicznego kontraktu, a każdy rekord powiela storage;
- wybrany model to mały value type z dwiema referencjami do niemodyfikowalnych tokenów:
  namespace właściciela i lokalnego kodu. Nie alokuje heap, kopiuje tylko wskaźniki, pozostaje
  czytelny i nie wymaga jednego globalnego enum ani zakresów liczbowych.

Oba tokeny muszą być non-null i niepuste. Local code zawiera wyłącznie wielkie litery ASCII,
cyfry i `_`, a jego pierwszy znak musi być literą. Namespace dopuszcza te same znaki oraz `.`
wyłącznie jako separator niepustych segmentów; każdy segment zaczyna się literą. `:`, whitespace
i małe litery są niedozwolone. Runtime waliduje oba tokeny i nie normalizuje tekstu.

Tokeny mają static storage duration i stabilne znaczenie przez kompatybilne wersje firmware.
Nie wolno używać stack-local ani temporary buffers, mutable config storage ani dynamic storage
o lifetime krótszym niż `ApplicationRuntime` i jego `StartupReport`. Namespace używa
hierarchicznego, kontrolowanego przez właściciela klucza, na przykład `AQUA.CORE` albo
`AQUA.DOSER`; local code używa nazwy takiej jak `PLAN_INVALID` albo `CONFIG_INVALID`. Core
rezerwuje `AQUA.CORE`. Każdy Domain/provider posiada własny, globalnie unikalny namespace
i własny katalog local codes. Core nie zna listy domen, nie prowadzi registry namespace,
sprawdza wyłącznie format i nie interpretuje katalogu Domain. Namespace i kodu nie wolno
zmieniać ani ponownie użyć dla innego znaczenia.

Równość `StartupErrorCode` jest semantic i case-sensitive: wymaga byte-for-byte equality tekstu
namespace oraz byte-for-byte equality tekstu local code. Nie zależy od adresów wskaźników;
dwa różne wskaźniki do identycznych tekstów oznaczają ten sam kod. Postać maszynowa to
jednoznaczne `namespace:local`, na przykład `AQUA.CORE:PLAN_INVALID`; `:` nie może wystąpić
w żadnym tokenie. Human-readable text może być dodany później i nie jest kontraktem.
Długość tokenów nie jest częścią kontraktu, a implementacja ich nie kopiuje.

Konceptualny publiczny kontrakt wyniku jest następujący; nazwy metod są normatywne na poziomie
designu, a reprezentacja pól pozostaje prywatna:

```cpp
class StartupErrorCode {
public:
    StartupErrorCode() = delete;
    static StartupErrorCode fromStatic(
        const char* ownerNamespace,
        const char* localCode
    );

    bool isValid() const;
    const char* ownerNamespace() const;
    const char* localCode() const;
    bool equals(const StartupErrorCode& other) const;

private:
    // Prywatny absent sentinel jest dostępny tylko StartupStepResult.
    StartupErrorCode(const char* ownerNamespace, const char* localCode);
    const char* ownerNamespace_;
    const char* localCode_;
    friend class StartupStepResult;
};

enum class StartupOutcome : uint8_t {
    SUCCEEDED,
    DISABLED,
    FAILED
};

class StartupStepResult {
public:
    StartupStepResult() = delete;
    static StartupStepResult succeeded();
    static StartupStepResult disabled();
    static StartupStepResult failed(StartupErrorCode code);

    StartupOutcome outcome() const;
    bool isValid() const;
    bool hasErrorCode() const;
    const StartupErrorCode* errorCode() const;

private:
    StartupStepResult(StartupOutcome outcome, StartupErrorCode code);
    StartupOutcome outcome_;
    StartupErrorCode code_; // prywatny absent sentinel dla SUCCEEDED/DISABLED
};
```

`succeeded()` i `disabled()` tworzą wynik bez kodu. `failed(code)` tworzy poprawny wynik tylko
dla poprawnego kodu. Nie ma publicznego konstruktora ani setterów, więc zwykły kod nie może
zbudować sprzecznej kombinacji. `isValid()` jest obowiązkową defensywną granicą callbacka:
akceptuje dokładnie `FAILED` z poprawnym kodem albo `SUCCEEDED`/`DISABLED` bez kodu.
`errorCode()` zwraca `nullptr`, gdy `hasErrorCode()` jest false. Błędny kod przekazany do
`failed()` daje wykrywalny invalid result, a nie zastępczy błąd Domain. Typy nie używają heap,
są tanio kopiowalne, a wymóg trivial copy nie jest kontraktem ABI.

Core posiada własny stabilny katalog w namespace `AQUA.CORE`. Minimalny katalog implementacji
ma następujące local codes:

- `PARTICIPANT_STORAGE_INVALID` — niepoprawna relacja pointer/count participant array;
- `REPORT_STORAGE_INVALID` — niepoprawna relacja pointer/capacity report storage;
- `CALLBACK_MISSING` — action nie ma callbacka;
- `PARTICIPANT_ID_MISSING` — action ma null albo pusty participant ID;
- `PARTICIPANT_ID_DUPLICATE` — participant ID nie jest unikalny w planie;
- `PHASE_INVALID` — wartość `StartupPhase` leży poza legalnym enum domain;
- `PHASE_NOT_ALLOWED` — poprawna wartość enum jest zakazana dla danego descriptora, na przykład
  ordinary participant używa `SAFETY_VALIDATION` albo `RUNNING`;
- `PHASE_ORDER_INVALID` — participant array nie ma niemalejącej kolejności phase;
- `REQUIREMENT_INVALID` — wartość `StartupRequirement` leży poza legalnym enum domain;
- `OUTCOME_INVALID` — wartość `StartupOutcome` leży poza legalnym enum domain;
- `ERROR_CODE_MISSING` — outcome `FAILED` nie zawiera wymaganego kodu;
- `ERROR_CODE_INVALID` — kod jest obecny, ale jego tokeny lub format są niepoprawne;
- `ERROR_CODE_UNEXPECTED` — kod występuje przy `SUCCEEDED` albo `DISABLED`;
- `REQUIRED_DISABLED`, `SPECIAL_DISABLED`.

`REQUIRED_DISABLED` oznacza `DISABLED` zwrócone przez ordinary participant `REQUIRED`,
a `SPECIAL_DISABLED` — `DISABLED` zwrócone przez jedną z special actions. Każdy Core code ma
dokładnie jedno powyższe znaczenie. Symbole i znaczenia są stałe, nie mogą być ponownie użyte
i podlegają testom stabilności; nie tworzą globalnego katalogu błędów urządzenia. Awaria
special action zwracająca poprawne
`FAILED` jest identyfikowana przez `PARTICIPANT_FAILURE` i source tej action, a jej error code
pozostaje kodem callbacka. Nie dodajemy drugiego ogólnego kodu Core, który ukrywałby przyczynę.
Domain/provider definiuje we własnym namespace kody dla config, hardware, sensor, actuator
i domain init; Core ich nie zna, nie mapuje i nie zastępuje.

Każdy startup failure tworzy rekord z poprawnym stable error code.
Rozróżnione są trzy przyczyny:

- `PARTICIPANT_FAILURE` — callback legalnie zwrócił `FAILED`; zachowany zostaje jego kod;
- `PLAN_CONTRACT_FAILURE` — struktura planu narusza SYS-102; runtime używa kodu Core;
- `RESULT_CONTRACT_FAILURE` — callback zwrócił nielegalny wynik lub kombinację; runtime używa
  kodu Core i nie próbuje naprawiać wyniku.

Contract failure zawsze jest fatal, również gdy wadliwy descriptor ma requirement `OPTIONAL`.
Nie rzuca wyjątku, nie uruchamia recovery i kończy startup stanem `ERROR + FAULT + LOCKED`.
Zwykły `REQUIRED + FAILED` i `FAILED` special action są fatal participant failures.
`OPTIONAL + FAILED` jest niefatalnym participant failure i ustawia finalne health na
`DEGRADED`. `REQUIRED + DISABLED` oraz `DISABLED` special action są result contract failures.

Raport lokalizuje błąd niezależnie od poprawności ID. `StartupFailureSource` rozróżnia
`PLAN`, `EARLY_SAFE_OUTPUTS`, `PARTICIPANT` i `SAFETY_GATE`. Dla zwykłego participanta rekord
zachowuje jego indeks w tablicy; dla pozostałych źródeł indeks nie występuje. `participantId`
jest pożyczonym wskaźnikiem z SYS-102 i może być `nullptr` tylko wtedy, gdy naruszenie planu
uniemożliwia wskazanie poprawnego ID. `phase` oznacza publiczną fazę w chwili błędu:
`BOOT` dla pełnej walidacji strukturalnej, bieżącą fazę dla callbacka, `SAFETY_VALIDATION`
dla safety gate. Source jest potrzebny, aby jednoznacznie odróżnić special actions i plan
failure bez wymyślania zarezerwowanego participant ID. Requirement i observed outcome nie są
kopiowane: źródło, kind, miejsce rekordu i error code wystarczają do interpretacji.
`PLAN` wskazuje błąd plan-wide, na przykład relację pointer/count; błąd konkretnego descriptora
używa source `PARTICIPANT`, jego indeksu i phase `BOOT`, ponieważ wykryła go walidacja przed
wykonaniem zwykłych faz.

Normatywna macierz legalnych kombinacji kind/source jest następująca:

| Failure kind | Legalne source |
|---|---|
| `PARTICIPANT_FAILURE` | `EARLY_SAFE_OUTPUTS`, `PARTICIPANT`, `SAFETY_GATE`; nigdy `PLAN` |
| `RESULT_CONTRACT_FAILURE` | `EARLY_SAFE_OUTPUTS`, `PARTICIPANT`, `SAFETY_GATE`; nigdy `PLAN` |
| `PLAN_CONTRACT_FAILURE` | `PLAN` albo konkretne `EARLY_SAFE_OUTPUTS`, `PARTICIPANT`, `SAFETY_GATE`, jeżeli błąd można przypisać do action/descriptora |

Pozostałe kombinacje są niepoprawne. `participantId` jest wymagany dla
`PARTICIPANT_FAILURE`. Jest również wymagany dla callback `RESULT_CONTRACT_FAILURE`, jeżeli
action ma poprawny ID. Może być `nullptr` wyłącznie przy contract failure, gdy nie istnieje
poprawny participant/action ID do wskazania. `participantIndex` ma znaczenie tylko dla ordinary
source `PARTICIPANT`; special actions i plan-wide failure nie mają indeksu. Każdy present
record ma poprawny `StartupPhase`, odpowiadający fazie zatrzymania według SYS-102, oraz poprawny
`StartupErrorCode`. Requirement i raw outcome nie są przechowywane, ponieważ kind, source
i error code jednoznacznie opisują problem.

Konceptualny rekord ma read-only API:

```cpp
enum class StartupFailureKind : uint8_t {
    PARTICIPANT_FAILURE,
    PLAN_CONTRACT_FAILURE,
    RESULT_CONTRACT_FAILURE
};

enum class StartupFailureSource : uint8_t {
    PLAN,
    EARLY_SAFE_OUTPUTS,
    PARTICIPANT,
    SAFETY_GATE
};

class StartupFailureRecord {
public:
    StartupFailureRecord(); // pusty slot storage, nie publiczny failure
    bool isPresent() const;
    StartupFailureKind kind() const;       // precondition: isPresent()
    StartupFailureSource source() const;   // precondition: isPresent()
    StartupPhase phase() const;            // precondition: isPresent()
    const char* participantId() const;     // może być nullptr jak opisano wyżej
    bool hasParticipantIndex() const;
    size_t participantIndex() const;       // precondition: hasParticipantIndex()
    const StartupErrorCode& errorCode() const; // precondition: isPresent()

private:
    // Tylko ApplicationRuntime zapisuje rekord i nie tworzy present bez valid code.
    // Dokładne prywatne pola nie są publicznym ABI.
    friend class ApplicationRuntime;
};

struct StartupFailureStorage {
    StartupFailureRecord* records;
    size_t capacity;
};
```

Pusty slot służy wyłącznie do statycznej lub stosowej deklaracji bufora. `StartupReport` nigdy
nie udostępnia go jako failure. Dla `capacity == 0` wymagane jest `records == nullptr`; dla
`capacity > 0` wymagany jest non-null writable buffer o lifetime co najmniej runtime. Zła
relacja pointer/capacity jest plan contract failure wykrywanym podczas pełnej walidacji po
early safe outputs. Zapisu i lifetime pamięci nie da się sprawdzić w runtime i pozostają
precondition Composition Root. Storage należy do Composition Root i żyje przez cały
`ApplicationRuntime` oraz jego `StartupReport`; runtime nie posiada go ani nie zwalnia.
`StartupReport` przechowuje tylko view/pointer i stored count, bez kopiowania całej tablicy.
Po zakończeniu startupu wykorzystany prefix jest częścią immutable reportu: Composition Root
nie może go modyfikować, a runtime nie może go ponownie nadpisać.

Rozważone modele raportu:

- pełna kopia całego przyszłego `RuntimeState` miesza startup z danymi runtime i tworzy
  niepotrzebne ownership;
- raport zawierający wyłącznie błędy startupu jest mały, lecz po późniejszej zmianie statusu
  nie zachowuje jednoznacznie finalnego stanu osiągniętego przez startup;
- wybrany model przechowuje failure data oraz mały snapshot czterech istniejących osi
  `RuntimeStatus` po zakończeniu startupu. Nie kopiuje service state ani przyszłego dużego
  `RuntimeState`. To świadoma, mała kopia potrzebna do niezmiennego wyniku historycznego.

`RuntimeStatus` jest bieżącym snapshotem `OperationalState`, `HealthState`, `SafetyState`
i `StartupPhase`, zwracanym przez wartość. Może zmienić się po starcie zgodnie z przyszłymi
decyzjami. `StartupReport` opisuje wyłącznie ostatnią i w Fazie 1 jedyną próbę startupu.
Istnieje od konstrukcji runtime i przed pierwszym `start()` może być bezpiecznie pobrany przez
`startupReport()`. Ma wtedy `isComplete() == false`, `hasFatalFailure() == false`,
`fatalFailure() == nullptr`, `optionalFailureCount() == 0`,
`storedOptionalFailureCount() == 0`, `optionalFailuresTruncated() == false` oraz
`totalFailureCount() == 0`.
`finalStatus()` jest dostępny dopiero dla complete report, a wcześniejsze wywołanie narusza
precondition.

Pierwszy `start()` wykonuje startup i finalizuje report. Po jego powrocie report jest complete
i niezmienny: `finalStatus` jest kopią stanu końcowego, fatal record, optional failure counts
oraz stored records nie zmieniają się. Późniejsze przejście bieżącego `RuntimeStatus` do
`MAINTENANCE`, runtime fault ani innego stanu nie zmienia `StartupReport::finalStatus()`.

Każde kolejne `start()` nie wywołuje callbacków, nie czyści reportu, nie resetuje bieżącego
statusu, nie zmienia fatal failure ani liczników i nie modyfikuje caller-provided buffer;
zwraca istniejący report. Jego complete state wystarcza runtime do rozpoznania zakończonego
startu, bez nowej publicznej osi lifecycle. `ApplicationRuntime::start()` jest non-reentrant;
wywołanie go w trakcie trwającego `start()` narusza precondition. Mutex, thread safety
i recursive handling nie są częścią tego kontraktu. SYS-102 nie udostępnia runtime callbackom
w normalnej ścieżce. Retry/recovery pozostają SYS-105.

Konceptualny kontrakt raportu i dostępu:

```cpp
struct RuntimeStatus {
    OperationalState operational;
    HealthState health;
    SafetyState safety;
    StartupPhase startupPhase;
};

class StartupReport {
public:
    bool isComplete() const;
    RuntimeStatus finalStatus() const; // precondition: isComplete()

    bool hasFatalFailure() const;
    const StartupFailureRecord* fatalFailure() const;

    size_t optionalFailureCount() const;       // wszystkie zaobserwowane
    size_t storedOptionalFailureCount() const; // prefix obecny w buforze
    bool optionalFailuresTruncated() const;
    const StartupFailureRecord* optionalFailure(size_t index) const;
    size_t totalFailureCount() const;

private:
    // Mutowany wyłącznie przez ApplicationRuntime do zakończenia start().
    friend class ApplicationRuntime;
};

class ApplicationRuntime {
public:
    ApplicationRuntime(
        ApplicationPlan plan,
        StartupFailureStorage optionalFailureStorage
    );

    const StartupReport& start();
    RuntimeStatus status() const;
    const StartupReport& startupReport() const;
};
```

`StartupReport` zawiera najwyżej jeden fatal record, przechowywany inline przez runtime, więc
nie konkuruje on o caller buffer. `fatalFailure()` zwraca `nullptr`, gdy fatal failure nie
wystąpił. Po fatal failure normalny startup natychmiast się zatrzymuje, a final status wynosi
`ERROR + FAULT + LOCKED` z fazą zatrzymania. Optional failures zaobserwowane wcześniej
pozostają w raporcie wraz z niezmienionymi total/stored/truncated; fatal failure nie czyści
wcześniejszej diagnostyki.

`optionalFailure(index)` zwraca wskaźnik tylko dla
`index < storedOptionalFailureCount()`, w przeciwnym razie `nullptr`.
`totalFailureCount()` to suma optional failure count oraz zera albo jedynki dla fatal failure.
Pełny sukces ma complete report, final status
`RUNNING + OK + CLEAR + RUNNING` oraz zero failures. Sukces z błędami opcjonalnymi ma
`RUNNING + DEGRADED + CLEAR + RUNNING`. Fatal startup zachowuje fazę błędu i ma
`ERROR + FAULT + LOCKED`.

Strategie przechowywania optional failures oceniono następująco: tylko pierwszy lub tylko
ostatni błąd traci informację o pozostałych; wewnętrzna tablica o stałej pojemności narzuca
całej platformie arbitralny limit; callback/sink wiąże raport z obserwatorem i jego lifetime;
sam count plus first/last utrudnia diagnostykę wielu niezależnych awarii. Wybrany jest
caller-provided fixed buffer: Composition Root dobiera pojemność do konkretnego planu, także
zero, a runtime nie używa heap i zachowuje pierwsze N błędów opcjonalnych w kolejności
wykonania.

`optionalFailureCount()` jest total count wszystkich optional failures wykrytych podczas
startupu, a `storedOptionalFailureCount()` jest stored count rekordów zapisanych w caller
buffer. Zawsze obowiązuje `stored <= capacity`, `stored <= total` oraz
`optionalFailuresTruncated() == (total > stored)`.

Każdy optional failure zwiększa total count. Dopóki jest miejsce, jego rekord jest dopisywany
do kolejnego slotu. Po zapełnieniu bufora kolejne rekordy nie są zapisywane ani nie nadpisują
wcześniejszych, ale total nadal rośnie. Przechowywane są dokładnie pierwsze N optional failures
w deterministycznej kolejności wykonania participantów, bez sortowania. Dla capacity zero
i co najmniej jednego optional failure zachodzi `total > 0`, `stored == 0` i truncated jest
true; finalny stan wynosi `RUNNING + DEGRADED`, jeżeli później nie wystąpi fatal failure.
Overflow raportowania nie jest fatal failure i nie zmienia Operational ani Safety. Pojemność
nie jest globalnym limitem API.

Publiczne API nie daje mutowalnego dostępu do stanu raportu ani użytego prefixu bufora.
Referencje do reportu i failure records pozostają ważne przez lifetime runtime, lecz wyłącznie
przy braku równoczesnego `start()`; wielowątkowa synchronizacja nie jest częścią Fazy 1.
Composition Root posiada pamięć caller buffer, ale po przekazaniu jej do runtime nie może
modyfikować użytego prefixu przez cały lifetime runtime.
Pożyczone participant IDs i tokeny error code zachowują preconditions static/runtime lifetime
opisane wyżej. Serializacja porównuje i emituje tokeny, outcome oraz nazwy enum bez budowania
dynamicznego tekstu; dokładny JSON schema nie jest częścią SYS-104.

Host tests implementacji SYS-104 muszą objąć:

- fabryki `succeeded()`, `disabled()`, `failed(validCode)`, usunięty default constructor oraz
  invalid code/result na granicy callbacka;
- stabilne Core codes i porównanie namespace/local bez porównywania adresów wskaźników;
- required failure, special failure, required/special disabled i każdy rodzaj contract failure;
- wiele optional failures, wariant z buforem zero, prefix przechowany przy overflow, dokładne
  total/stored counts i brak zmiany na fatal;
- zachowanie participant ID i indeksu, source oraz phase dla zwykłego kroku, special action
  i plan contract failure;
- pełny sukces, degraded success i fatal final status w raporcie oraz niezależność późniejszego
  bieżącego `RuntimeStatus` od niezmiennego final status w raporcie;
- one-shot `start()` i niezmienność raportu po jego zakończeniu.

SYS-104 nie definiuje recovery/retry, persistence, loggera, alarmów, command errors, błędów po
startupie ani JSON schema. Nie zmienia SYS-105, SYS-106, SYS-107, ARCH-101 ani RuntimeIdentity.

### SYS-105 — recovery po krytycznym błędzie startupu

Rozważone modele recovery:

- permanentny `ERROR` do fizycznego resetu albo power cycle jest najprostszy, nie tworzy boot
  loop ani flash wear, ale ogranicza diagnostykę, autonomię urządzenia headless i możliwość
  naprawy konfiguracji;
- ERROR recovery shell zachowuje bezpieczną blokadę domeny i umożliwia diagnostykę oraz naprawę,
  lecz wymaga małej, testowalnej granicy wykonywania usług systemowych;
- automatyczny restart po każdym fatal failure może pomóc przy błędzie przejściowym lub reakcji
  watchdoga, ale utrudnia diagnostykę i grozi boot loop, powtarzaniem niebezpiecznej inicjalizacji
  oraz flash wear;
- model hybrydowy używa ERROR recovery shell jako zachowania domyślnego, a jawny restart lub
  przyszłą policy traktuje jako opcję. Zapewnia diagnostykę, config repair i autonomię urządzenia
  headless bez domyślnego ryzyka restart loop, pozostając prostym do testowania.

Wybrany jest model hybrydowy. Fatal startup failure nie uruchamia automatycznego restartu;
domyślnym zachowaniem jest pozostanie w ERROR recovery shell.

Po fatal startup failure normalny startup kończy się ostatecznie w
`ERROR + FAULT + LOCKED`, a `StartupReport` jest complete i niezmienny. `StartupPhase`
pozostaje fazą zatrzymania. Runtime pozostaje aktywny wyłącznie jako ograniczony ERROR recovery
shell. Normalny runtime domeny nie startuje, domain loop i domain commands nie są wykonywane,
a actuators nie mogą zostać normalnie aktywowane. Częściowa inicjalizacja hardware lub domeny
nie daje permission do normalnej pracy.

ERROR recovery shell służy diagnostyce, bezpiecznemu pozostaniu w `ERROR`, naprawie konfiguracji
i kontrolowanemu restartowi. Nie jest `MAINTENANCE`: maintenance jest świadomym, planowanym
workflow operacyjnym, a recovery shell jest reakcją na fatal startup failure. Nie ma
automatycznego przejścia `ERROR → MAINTENANCE`. Istniejący `OperationalState::ERROR`
wystarcza; SYS-105 nie dodaje stanów `RECOVERY`, `SAFE_MODE` ani `FAILED_STARTUP`.

Recovery services są explicit opt-in w Composition Root. Mogą działać wyłącznie usługi
systemowe jawnie uznane za bezpieczne w `ERROR` i tylko wtedy, gdy ich zależności są dostępne.
Logging i lokalna diagnostyka mogą być dostępne niezależnie od sieci. Status, diagnostics,
Config read/write, factory reset, restore, OTA, restart, Network, Web, Realtime i MQTT nie są
automatycznie dostępne: każda capability wymaga osobnego bezpiecznego kontraktu i poprawnie
uruchomionej infrastruktury. Failure przed `NETWORK_INIT` nie pozwala zakładać Network ani Web.
Brak Network, Web lub MQTT nie jest kolejnym startup failure. Recovery pozostaje autonomiczne
i nie może wymagać sieci. Lokalna diagnostyka może działać bez Network, jeżeli konkretne
urządzenie jawnie ją udostępnia.

Rozważone warianty recovery bootstrapu:

- brak specjalnego bootstrapu jest prosty, ale uzależnia recovery od przypadkowo ukończonych faz
  normalnego startupu i nie zapewnia drogi naprawy urządzenia headless;
- pełny `RecoveryPlan` daje jawny lifecycle, lecz w Fazie 1 tworzyłby drugi framework bez
  potwierdzonej potrzeby;
- minimalna granica recovery services umożliwia uruchomienie tylko jawnie bezpiecznych usług
  systemowych i system/recovery processing bez wiązania ich z domeną;
- reuse zwykłego `ApplicationPlan` grozi ponownym uruchomieniem participantów bez kontraktu
  reentrancy, idempotency, dependencies i legalności działania w `ERROR`.

Faza 1 wybiera wyłącznie konceptualną, minimalną granicę recovery services. Nie definiuje jej
publicznej reprezentacji jako `RecoveryPlan`, `RecoveryServicesPlan`, handlera ani publicznego
`RecoveryCapability`. Dokładna reprezentacja bootstrapu pozostaje osobną przyszłą decyzją.
Composition Root jawnie udostępnia każdą usługę, która musi być bezpieczna w `ERROR`, mieć
dostępne zależności i nie może zależeć od normalnego domain runtime. Zwykli participanty
`ApplicationPlan` nie są ponawiani ani używani jako recovery bootstrap.

Participant, który zakończył inicjalizację przed późniejszym failure, pozostaje initialized,
lecz nieużywany przez normalny runtime. Faza 1 nie wprowadza reverse rollback stack,
obowiązkowych shutdown/deinit hooks ani ogólnego cleanup frameworka. Takie mechanizmy mogą zostać
dodane później dla konkretnych zasobów, gdy ich bezpieczne i idempotentne zwolnienie będzie
wymagane.

Fatal failure zawsze ponownie ustanawia logiczne `SafetyState::LOCKED`, także gdy safety gate
wcześniej zwrócił sukces. `EarlySafeOutputInitializer` jest idempotentną techniczną operacją,
ale idempotency nie jest kontraktem emergency shutdown ani safe stop i nie daje permission do
automatycznego ponownego wywołania po fatal failure. Runtime nie wywołuje ponownie
`earlySafeOutputs`. `LOCKED` nie jest dowodem fizycznego bezpiecznego stanu wyjść. Ewentualne
fizyczne safe shutdown actions należą do przyszłego jawnego kontraktu Safety/recovery i wymagają
testów sprzętowych; nie wolno ich zastępować Action Locks ani ponownym wywołaniem startup action.

Fatal startup failure domyślnie nie tworzy restart request. Jawny restart może zostać zażądany
przez dozwoloną usługę recovery albo przyszłą osobną politykę systemową i przechodzi przez
granicę `RestartRequester` → safe point → `RestartExecutor` z SYS-007. Restart rozpoczyna pełny
BOOT. Ponieważ normalny domain loop nie działa, ERROR recovery shell posiada minimalną granicę
system/recovery processing. Jej safe point przypada na koniec kompletnej iteracji, gdy nie trwa
krytyczny zapis, OTA, restore ani factory reset, a bieżąca operacja recovery osiągnęła bezpieczny
punkt. Jest to system/recovery safe point, nie domain-loop safe point.

Automatyczny restart jest dozwolony dopiero po przyjęciu osobnej jawnej polityki dla wybranych
kategorii błędów wraz z ochroną przed boot loop, backoff/safe mode i uwzględnieniem flash wear
oraz watchdoga. Persistent boot-attempt counter, persistence i dokładna polityka pozostają poza
SYS-105.

W `ERROR` policy może dopuścić wyłącznie komendy system/recovery, na przykład status,
diagnostics, config repair, restart, factory reset, restore lub OTA, jeśli wspierająca
infrastruktura jest dostępna i dana operacja ma bezpieczny kontrakt. Normalne domain commands
i uruchamianie actuators są zabronione, w tym dosing, lighting control, CO2 control, pumps
i feeding. Dokładny Command API i authorization policy pozostają poza SYS-105.

Jeżeli przyczyną failure jest konfiguracja, dostępna recovery capability może umożliwić jej
odczyt, zapis poprawki, restore albo factory reset. Zmiana konfiguracji nie powoduje przejścia
`ERROR → RUNNING`. Ponowna próba normalnego startupu wymaga restartu urządzenia i pełnego BOOT
z nową instancją runtime. `ApplicationRuntime::start()` pozostaje one-shot; drugie wywołanie
nadal jest no-op i zwraca ten sam report.

Recovery activity nie modyfikuje `StartupReport`, jego final status ani failure records.
Bieżąca diagnostyka i logi recovery są osobnym strumieniem informacji. Host tests przyszłej
implementacji muszą potwierdzić: fatal → ERROR shell, brak wejścia do domain runtime,
`ERROR + FAULT + LOCKED`, blokowanie normalnych domain commands, brak automatycznego restart
request, obsługę jawnego restart request przy dostępnej capability przez system/recovery safe
point, opcjonalność usług recovery, brak usług bez ich infrastruktury, możliwość pozostania
w `ERROR` bez Network, brak `ERROR → RUNNING` po config repair, niezmienność reportu oraz
one-shot/no-op drugiego `start()`. HIL pozostaje wymagany dla fizycznego zachowania wyjść,
watchdog/reset, rzeczywistego `RestartExecutor`, network recovery, brownout, utraty zasilania
i zachowania hardware po częściowej inicjalizacji. Host tests nie dowodzą fizycznego safety.

SYS-105 nie rozstrzyga SYS-101, SYS-106, SYS-107, ARCH-101, Maintenance workflow, SafetyManager,
Action Locks, physical emergency shutdown, publicznego RecoveryPlan API, OTA, Backup/Restore,
Command API/policy ani RuntimePlan scheduling. Określa tylko ich granicę względem ERROR recovery
shell.

### SYS-106 — writer ownership i agregacja Health/Safety po startupie

**Status:** ACCEPTED — TARGET contract; F1.9 nie implementuje coordinatora ani writerów.

Rozważone modele ownership:

- ApplicationRuntime jako jedyny writer wszystkich osi jest najprostszy strukturalnie, ale
  łączy lifecycle z agregacją wielu niezależnych faultów i policy Health/Safety;
- osobny HealthManager i SafetyManager zachowują single writer dla każdej osi, ale zwiększają
  liczbę lifecycle/handoff boundaries i utrudniają spójny snapshot obu agregatów;
- jeden SystemStateCoordinator agregujący facts oddziela źródła od wyniku i wspiera wiele
  alarmów oraz domain policy, ale bez jawnego handoff nie rozstrzyga ownership w startupie;
- hybryda oddziela startup od runtime: ApplicationRuntime jest writerem podczas startupu,
  potem jeden RuntimeStateCoordinator przejmuje wyłącznie Health/Safety. Zapewnia single-writer
  property, testowalność, separację Core/Domain i prostą statyczną kompozycję bez service locatora.

Wybrany jest model hybrydowy z jednym RuntimeStateCoordinator i provider aggregation.
OperationalState i StartupPhase pozostają własnością ApplicationRuntime/lifecycle owner;
SYS-106 nie przenosi ich do coordinatora. Maintenance transition ani event publication nie
są częścią tej decyzji. Coordinator zna wąskie kontrakty, nie konkretne usługi, klasy domen,
transporty ani device_type.

#### Single source of truth i writer authority

Rozważone miejsca przechowywania stanu:

- nowy SystemStateStore/SystemStateModel może neutralnie posiadać cztery osie, ale wymaga
  zmiany F1.7 i dodatkowej granicy dostępu bez obecnej potrzeby;
- coordinator posiadający Health/Safety po handoff wymagałby delegowanego read model i dwóch
  wariantów storage zależnych od lifecycle;
- shared state owner pozwala uniknąć kopii, ale ogólnie dostępne write interface byłoby
  niebezpieczne;
- zachowanie prywatnego RuntimeStatus storage w ApplicationRuntime umożliwia najmniejszą
  zmianę F1.7: zmienia się prawo zapisu dwóch osi, a nie miejsce przechowywania stanu.

Wybrane jest zachowanie jednego authoritative storage odpowiadającego obecnemu status_.
ApplicationRuntime jest właścicielem jego lifetime; fizyczne posiadanie pól nie oznacza prawa
do zapisu Health/Safety po handoff. Coordinator otrzyma prywatną, ograniczoną do tych osi
write authority, aktywną tylko po successful handoff. Nie istnieje publiczne setHealthState(),
setSafetyState() ani ogólne write interface dla Domain, Alarm, Diagnostics, Commands,
transportów lub driverów. Single source of truth pozostaje jednym prywatnym state storage odpowiadającym obecnemu ApplicationRuntime::status_. Przyszła implementacja może zachować ten storage i przekazać coordinatorowi wąską prywatną/internal write authority do Health/Safety; nie wymaga to publicznego SystemStateStore, publicznych setterów ani drugiej kopii Health/Safety. RuntimeStatus::status() musi czytać ten sam live authoritative storage, a StartupReport::finalStatus() pozostaje osobnym immutable snapshotem historycznym. Dokładny mechanizm egzekwowania dostępu należy do implementacji.

Coordinator nie przechowuje niezależnego authoritative health_/safety_ snapshotu. Lokalne
wartości robocze podczas agregacji nie są drugim źródłem prawdy. ApplicationRuntime::status()
nadal zwraca przez wartość cztery osie z tego samego storage, bez side effects i bez query
providerów podczas odczytu. StartupReport::finalStatus() pozostaje niezmiennym historycznym
snapshotem, nie drugim bieżącym stanem.

#### Contributions, providerzy i agregacja

Konceptualne kontrakty, bez ustalania finalnego C++ API:

- HealthContribution: bieżący, poprawny poziom OK, DEGRADED albo FAULT;
- SafetyContribution: informacja, czy provider posiada co najmniej jeden aktywny systemowy
  powód blokady; brak powodu jest neutralny;
- HealthProvider i SafetyProvider: wąskie read-only query bieżących contributions;
- RuntimeStateCoordinator: jawny successful-startup handoff i deterministyczny refresh obu
  agregatów, bez prawa zmiany OperationalState/StartupPhase.

Composition Root posiada i statycznie składa providerów Core oraz application/domain adapters.
Domain może implementować wąskie DomainHealthProvider/DomainSafetyProvider; Core nie zna
konkretnych klas Domain ani ich katalogów reasonów. Diagnostics może obserwować aggregate lub
dostarczać własny niezależny fact, ale nie może zwrotnie traktować odczytanego aggregate jako
nowego źródła tego samego stanu. Registry nie służy do wyszukiwania providerów przez coordinatora.

Health = najpoważniejszy aktywny contribution: FAULT > DEGRADED > OK. Brak contributions daje
OK. Provider może wewnętrznie agregować wiele swoich conditions. Powstanie/usunięcie condition
zmienia jego query result; refresh zawsze przelicza cały zestaw, więc nie ma last-writer-wins.
Ustąpienie FAULT odsłania nadal aktywne DEGRADED, a OK jest możliwe dopiero po ustąpieniu
wszystkich contributions poważniejszych od OK.

Safety = LOCKED, gdy co najmniej jeden provider lub unresolved startup restriction wskazuje
aktywny systemowy powód blokady; inaczej CLEAR. Usunięcie jednego z wielu reasonów nie daje
CLEAR. Nie istnieje globalne unlock ani zerowanie wszystkich providerów przez coordinatora.
LOCKED jest agregatem logicznym, nie physical emergency shutdown ani dowodem stanu wyjść.

Rozważone reprezentacje conditions:

- setters setDegraded()/setFault()/setLocked() nie zachowują ownership i przyczyny;
- reference-counted flags zależą od poprawnego parowania zmian i mogą pozostawić stale locks;
- token/handle contributions dają osobne ownership, ale wymagają bookkeeping oraz release
  protocol; nie są potrzebne przy statycznej kompozycji;
- statyczni providerzy z query mają przewidywalny lifetime, zero heap i jawnych właścicieli;
- snapshots dostarczane do coordinatora ograniczają query, ale wymagają cache, freshness
  i reguł aktualizacji.

Wybrani są statyczni providerzy z query. Conditions i ich latches należą do źródła/provider
policy, nie do centralnego rejestru. Lista providerów jest immutable pointer/count; zero
providerów jest legalne, ale nie usuwa unresolved startup conditions. Nie ma dynamic registration,
global capacity, event bus ani wyszukiwania przez service locator. Composition Root posiada provider objects, provider contexts, tablice pointer/count oraz state storage, do którego ograniczoną write authority otrzymuje coordinator. Providerzy i ich contexts
żyją co najmniej tak długo jak coordinator i runtime. Każdy provider, context i każda tablica provider pointers żyją co najmniej tak długo jak coordinator, a pointer/count view pozostaje stabilny przez cały okres używania. W baseline SYS-106 providerzy nie są dynamicznie usuwani, podmieniani ani rejestrowani w trakcie życia coordinatora. Authoritative state storage żyje co najmniej tak długo jak ApplicationRuntime i RuntimeStateCoordinator, które z niego korzystają. Coordinator nie posiada ani nie zwalnia provider objects, provider arrays ani drugiej authoritative kopii Health/Safety.

#### Push/pull i execution boundary

PUSH wymaga protokołu dostarczania zmian, lifetime i synchronizacji; utrata update może
pozostawić nieaktualny aggregate. PULL jest prostszy w embedded loop i testach. HYBRID
dirty notification + deterministic refresh może później ograniczyć pracę, ale wymaga dodatkowego
kontraktu i nie jest potrzebny jako baseline.

Wybrany baseline to jawny PULL refresh w serializowanej system/runtime execution boundary.
Providerzy są pytani w kolejności statycznej deklaracji, bez side effects, blocking hardware I/O
i zmiany lifecycle. Oba wyniki są liczone przed publikacją spójnego snapshotu. Nie udostępnia
się częściowo przeliczonej pary Health/Safety. Query musi obejmować nadal istotne conditions;
krótki sygnał wymagający reakcji źródło musi zachować zgodnie ze swoją policy, zamiast polegać
na trafieniu w chwilowy event. Brak poprawnego, aktualnego query nie jest dowodem recovery
i nie może zwolnić istniejącej blokady.

Źródła z innych tasks/ISR muszą dostarczać spójny snapshot przez własną jawną synchronizację.
SYS-106 nie projektuje threading, częstotliwości refresh, scheduler ani RuntimePlan. Odczyt
statusu i egzekwowanie command policy używają ostatniego kompletnego snapshotu; wykonanie
physical safety actions nie może zależeć wyłącznie od tego pollingu. Publikacja eventów jest
osobną przyszłą granicą po zatwierdzeniu snapshotu, nigdy warunkiem lokalnej agregacji.

#### Successful startup handoff i startup DEGRADED

Podczas startupu ApplicationRuntime jest jedynym writerem HealthState i SafetyState według SYS-102/104. Przed handoffem nie ma innego writera.
Handoff jest jawny, one-shot, dopiero po complete successful StartupReport, przed pierwszym
normalnym domain runtime processing lub command execution. Do tego momentu coordinator nie
ma write authority, a bieżący startup status pozostaje widoczny. Bez zainstalowanego coordinatora
nie ma handoff; foundation F1.7 zachowuje swój startup status. W punkcie handoff authority jest przekazywana atomowo/logicznie jako jedna serializowana operacja: nie może istnieć chwila z dwoma writerami ani chwila bez authoritative writera live Health/Safety. Po successful handoff RuntimeStateCoordinator jest jedynym writerem HealthState i SafetyState, a ApplicationRuntime traci write authority dla tych osi i nie może zapisywać ich ponownie w tej samej instancji runtime. Authority nie wraca bez pełnego rebootu albo nowej instancji runtime; handoff jest nieodwracalny w ramach życia instancji. OperationalState i StartupPhase pozostają poza tym handoffem.

Przed przekazaniem authority muszą być gotowe listy providerów (także puste) i startup health
conditions. Każdy OPTIONAL + FAILED pozostawia osobny unresolved DEGRADED condition związany
ze swoim właścicielem/participantem. DISABLED jest neutralne. Conditions są zachowywane
niezależnie od caller-provided failure-report buffer: capacity zero albo truncated report
nie może zgubić powodu degradacji. Nie odtwarza się ich z przechowanych failure records ani
nie interpretuje Domain error codes jako runtime policy. Przyszła implementacja musi zapewnić
pełne przekazanie tych facts przez statyczną kompozycję/startup bridge; jego storage/API nie
jest tutaj definiowane i nie zmienia immutable StartupReport. StartupReport nie jest authoritative store aktywnych runtime facts. Nawet przy capacity zero albo overflow aktywny condition powodujący DEGRADED musi istnieć niezależnie w odpowiednim provider/service state. Handoff nie może wyczyścić DEGRADED tylko dlatego, że raport diagnostyczny nie przechował rekordu. Recovery do OK następuje dopiero po potwierdzeniu recovery przez ownera condition, gdy contribution przestaje być aktywne.

Handoff przygotowuje kompletne provider snapshots i startup conditions, następnie w jednej
serializowanej boundary przekazuje authority bez zerowania statusu. Brak nowych facts zachowuje
startup OK/CLEAR lub DEGRADED/CLEAR. Pierwszy aggregate może natychmiast wzmocnić Health lub
ustanowić LOCKED, gdy istnieje uzasadniający fact; nie publikuje się default OK ani przejściowego
LOCKED wynikającego tylko z konstrukcji coordinatora.

Unresolved startup condition może być zastąpiony bieżącym provider contribution wyłącznie po
jawnym przejęciu odpowiedzialności za tę samą przyczynę. Jego usunięcie wymaga pozytywnego
potwierdzenia recovery przez właściciela, nie samego istnienia providera. Zastąpienie jest
spójne, bez luki i bez podwójnego niezależnego latcha. Gdy brak takiego providera/potwierdzenia,
condition pozostaje aktywny; brak providerów nie zamienia startup DEGRADED w OK.

Przykład: optional Network init FAILED daje RUNNING + DEGRADED + CLEAR. Późniejsze potwierdzone
recovery tego samego condition usuwa jego DEGRADED contribution. Health wraca do OK tylko gdy
nie istnieją inne aktywne contributions. StartupReport nadal opisuje DEGRADED startup.
Startup DEGRADED nie jest globalnym immutable latchem.

#### Fatal startup, latching i granice policy

Fatal startup nie wykonuje normalnego handoff. ApplicationRuntime zachowuje authority i
ERROR + FAULT + LOCKED z fazą zatrzymania przez cały ERROR recovery shell z SYS-105.
Recovery system facts mogą służyć osobnej diagnostyce, ale nie zastępują tego fatal condition,
nie zdejmują LOCKED i nie obniżają FAULT. Config repair nie aktywuje coordinatora ani nie daje
ERROR → RUNNING. Ponowny startup wymaga restartu/nowej instancji runtime; start() nadal jest
one-shot, a StartupReport immutable. RuntimeStateCoordinator nie otrzymuje normalnej write authority, a recovery shell nie uruchamia normalnej runtime aggregation, która mogłaby obniżyć FAULT albo zdjąć LOCKED.

Health FAULT i Safety LOCKED nie mają jednego globalnego latch behavior. Latching, wymagany
ACK, safe-resume conditions i reset należą do konkretnego fact/alarm/domain policy.
Coordinator agreguje nadal aktywne reasons, nie obsługuje ACK i nie kasuje latchy. ACK != CLEAR;
potwierdzenie nie usuwa trwającej przyczyny ani innych powodów blokady. Fatal startup jest
szczególnym condition utrzymanym do nowego pełnego startupu. Sam Health FAULT nie wymusza
OperationalState ERROR, a LOCKED nie wymusza MAINTENANCE; te transition policies są poza SYS-106.

Alarm framework nie jest Health ani Safety coordinatorem. Alarm może dostarczać contributions
przez Core/Domain mapping policy; nie każdy alarm degraduje Health lub blokuje Safety.
Istniejące wymagania dla CRITICAL i latched alarms pozostają obowiązujące. Action Locks
blokują konkretne akcje; tylko safety-critical lock wskazany przez policy może stanowić
systemowy SafetyContribution. SafetyState nie jest drugim Action Locks frameworkiem.
Maintenance może przez lifecycle owner wpływać na OperationalState, blokować wybrane akcje
i dostarczać SafetyContribution, lecz Maintenance != Safety.

Command pipeline czyta OperationalState, HealthState, SafetyState i Action Locks, ale nie
pisze bezpośrednio globalnych osi. Komenda może zmienić condition przez jego dozwolony workflow;
dopiero coordinator aktualizuje aggregate. HTTP, Realtime, MQTT i Panel czytają/publikują stan,
nie otrzymują write authority. Diagnostics również nie jest alternatywnym writerem.

#### Migration, testability i scope

F1.9 jest docs-only. Nie zmienia F1.7 ani ApplicationPlan, start() i StartupReport API.
Przyszła implementacja doda ograniczoną write authority, handoff, startup facts boundary
i coordinator bez drugiej kopii stanu. Oddzielny SystemStateStore nie jest wymagany.

Przyszłe host tests muszą objąć:

- Health: brak contributions → OK, DEGRADED, FAULT, dominację FAULT oraz powrót do niższego
  poziomu po recovery tylko odpowiedniej przyczyny;
- Safety: brak reasons → CLEAR, jeden/wiele reasons → LOCKED, usunięcie jednego z wielu
  nie daje CLEAR, usunięcie wszystkich daje CLEAR;
- handoff: startup OK/CLEAR bez glitcha, startup DEGRADED bez false OK, recovery jednej
  z wielu startup przyczyn, unresolved condition bez providera, report capacity zero/truncation,
  nowe uzasadnione facts podczas handoff, fatal startup bez handoff i trwałe FAULT/LOCKED;
- ownership: brak publicznych setterów dla Domain/transportów, jeden runtime writer,
  jeden bieżący storage, spójny snapshot, deterministic refresh bez last-writer-wins, dokładnie jeden writer przed i po handoff, brak zapisu ApplicationRuntime po handoff, one-shot i brak powrotu authority bez reboot/new runtime;
- kompozycja: statyczny provider array/lifetime contract oraz capacity zero/overflow bez utraty aktywnych runtime facts;
- granice: latched condition po ustąpieniu przyczyny, ACK bez clear aktywnej przyczyny,
  one-shot start() i immutable StartupReport mimo późniejszego recovery.

Agregacja i ownership są host-testable. HIL jest potrzebny później dla real hardware facts,
recovery sensorów/actuatorów, timing/races urządzenia i rzeczywistego physical safety.
Host tests agregatu nie dowodzą fizycznego stanu wyjść.

SYS-106 nie rozstrzyga SYS-107, ARCH-101, pełnego Alarm API, Action Locks API, Maintenance
workflow, Command implementation, RuntimePlan scheduling, SafetyManager implementation,
Diagnostics providers ani event publication. Ustala tylko writer ownership, lifecycle handoff,
single source of truth i aggregation boundary.

### SYS-107 — restart request policy

**Status:** ACCEPTED — TARGET contract; F1.10 nie implementuje requestera ani executora.

RestartReason opisuje przyczynę BIEŻĄCEGO bootu. RestartRequestReason opisuje intencję
BIEŻĄCEGO runtime wykonania PRZYSZŁEGO restartu. Są osobnymi kontraktami; accepted request
nie zmienia current-boot RestartReason ani immutable StartupReport.

#### Katalog RestartRequestReason v1

Wybrany jest mały, stabilny machine-readable enum z poniższymi tokenami i znaczeniami.
Tokenów nie zmienia się ani nie używa ponownie dla innego znaczenia. Binary enum encoding
i prywatny bit layout nie są publicznym wire formatem. Każdy legalny reason ma stabilną bit identity wynikającą z jawnie zdefiniowanego mapowania reason → bit; nie wolno polegać na przypadkowej kolejności deklaracji enum, chyba że wartości enum są jawnie przypisane jako te stabilne identity. Reason musi być zwalidowany przed użyciem w shift, index albo bit lookup; invalid value nie może spowodować out-of-range shift/index. Accepted-reasons mask musi mieć szerokość obejmującą cały legalny katalog v1. Rozszerzenie katalogu wymaga sprawdzenia zgodności reprezentacji maski. Ten kontrakt nie wybiera konkretnego typu integer ani finalnego układu enum.

| Reason | Stabilne znaczenie |
|---|---|
| USER_REQUEST | Jawne żądanie restartu użytkownika, zaakceptowane przez właściwy workflow/policy, również w ERROR shell. |
| CONFIG_APPLY | Owner pomyślnie przygotował/zapisał zaakceptowaną konfigurację wymagającą pełnego startupu; nie dotyczy live apply bez restartu. |
| FACTORY_RESET | Pomyślnie zakończony factory reset wymaga nowego bootu. Nie oznacza rozpoczęcia kasowania danych. |
| RESTORE_COMPLETE | Pomyślnie zakończony restore wymaga nowego bootu. Nie oznacza rozpoczęcia restore ani jego błędu. |
| OTA_COMPLETE | Pomyślnie zakończony OTA workflow przygotował firmware do aktywacji przez restart; sam upload nie wystarcza. |
| RECOVERY_ACTION | Jawna, pomyślnie zakończona recovery action wymaga pełnego startupu i nie jest objęta bardziej konkretnym reason. |
| SYSTEM_POLICY | Osobna, jawnie przyjęta polityka systemowa żąda restartu, gdy żaden konkretny reason katalogu nie opisuje tej intencji. Nie jest catch-all dla błędów. |

CONFIG_APPLY, FACTORY_RESET, RESTORE_COMPLETE i OTA_COMPLETE zachowują swoje znaczenie także
w ERROR shell. Sama lokalizacja w recovery nie zamienia ich na RECOVERY_ACTION. Bezpośredni
restart użytkownika w ERROR nadal ma USER_REQUEST. SYSTEM_POLICY nie zastępuje żadnego z tych
reasonów; może powstać wyłącznie z jawnie zaakceptowanej policy z określonym triggerem, authority i workflow/boundary. Nie jest catch-all typu „wystąpił błąd → restart”, nie omija safe point, Safety ani przyszłych policy boundaries.
SYS-107 nie ustanawia takiej polityki, automatycznego restartu ani boot-loop protection.

MAINTENANCE nie jest reasonem: opisuje workflow, który może użyć właściwego reason.
WATCHDOG jest obserwowaną przyczyną resetu bieżącego bootu, nie zwykłym requestem.
FATAL_ERROR i NETWORK nie są reasonami: failure sam nie żąda restartu. UNKNOWN nie istnieje
dla poprawnego requestu. Request bez legalnego reason jest contract error, odrzucany bez
modyfikacji pending state i bez wywołania executora; nie mapuje się go na SYSTEM_POLICY.

Arbitrary text nie jest reasonem ani częścią wymaganej reprezentacji pending requestu.
Opcjonalne source/detail może służyć osobnej diagnostyce przy jawnym bounded storage/lifetime
contract i bez sekretów. Nie jest warunkiem przyjęcia, agregacji lub wykonania requestu.

#### Wiele requestów, primary reason i idempotency

Rozważone modele:

- first request wins jest prosty i deterministyczny, ale traci późniejsze causes;
- last request wins nadpisuje historię i uzależnia diagnostykę od ostatniego callera;
- priority wins wymaga rankingu, który miesza diagnostic importance z urgency/safety;
- merge zachowuje jeden pending restart, first primary oraz fixed bitmask różnych causes,
  bez heap i bez utraty zaakceptowanej intencji;
- queue wszystkich requestów wymaga storage/overflow policy, choć jeden reboot realizuje
  wszystkie przyjęte intencje; nie tworzy się kolejki kolejnych rebootów.

Wybrany jest merge: pierwszy poprawny accepted request ustanawia sticky pending i primary
reason. Fixed bitmask zawiera wszystkie różne zaakceptowane reasons, włącznie z primary;
additional causes to ten zbiór bez primary. Primary pozostaje first accepted reason przez
całe życie pending requestu. Kolejny różny reason dodaje tylko swój bit; nie zastępuje primary,
nie przyspiesza restartu i nie resetuje kolejności. Maska ma stały rozmiar wynikający z katalogu
v1, bez arbitralnej global capacity, dynamic array lub rosnącej listy tekstów.

Duplikat tego samego reason jest idempotentny: nie zmienia primary, maski, kolejności ani czasu,
nie zwiększa occurrence counter. V1 nie wymaga timestampu, sekwencji ani licznika wystąpień.
Nie ma severity/priority reasonów ani escalation na podstawie kolejnego requestu. Semantycznie request może być accepted-new-cause, accepted-duplicate albo invalid; nie ustanawia to finalnego C++ result type. Completion
OTA/restore/factory reset nie ma wyższej safety severity i nie może ominąć safe point.

#### Pending state, authority, cancellation i lifetime

Konceptualny RestartRequestState zawiera pending yes/no, primary obecny tylko gdy pending,
oraz accepted reason mask. Empty state ma pending=false i pustą maskę; nie ma legalnego
UNKNOWN/default reason. Snapshot jest read-only, spójny i nie wykonuje efektów.
Obowiązują inwarianty: przy pending=false primary nie jest ważny, accepted mask jest pusta, a additional causes są puste; przy pending=true primary jest legalnym reasonem, jego bit jest ustawiony w accepted mask, maska zawiera wyłącznie legalne reasons, a maska jest jedynym mutable zbiorem causes. Additional causes są wyliczane jako accepted mask minus primary; nie istnieje osobna mutable lista additional causes. Primary jest pierwszym accepted reason i nie zmienia się do końca życia pending requestu.
Nie rozszerza RuntimeStatus o nowe osie ani nie zmienia Health/Safety writer ownership.

Jeden lifecycle/system owner posiada mutable pending state. Composition Root składa ten
owner oraz jawnie przekazuje wąski RestartRequester do uprawnionych system/recovery workflows,
config ownera i przyszłych maintenance/OTA/restore/factory reset workflows. User command
adapter może dostać requester przez właściwy workflow; reason nie zastępuje Auth/policy.
Domain nie dostaje tej capability automatycznie. Jawnie skomponowany domain adapter może
zgłosić intencję tylko w ramach przyjętej policy, bez dostępu do executora lub całego runtime.
State owner żyje przez cały okres używania pożyczonych requesterów i read views.

RestartRequester zapisuje intencję i nigdy nie wykonuje platformowego resetu. RestartExecutor
jest efektem platformowym dostępnym wyłącznie lifecycle/system orchestration; Domain,
HTTP, Realtime, MQTT, Panel i arbitrary services nie otrzymują executora ani nie wykonują
bezpośrednio ESP.restart(). Transport może inicjować autoryzowany workflow przez requester.

V1 nie ma cancellation, także przez ownera pierwotnego requestu. Sticky request nie znika
samoczynnie przy disconnect, ustąpieniu błędu lub zakończeniu callera. Caller rozstrzyga
potrzebę restartu przed requestem; raz zaakceptowana intencja nie wymaga token ownership.
Usunięcie dodatkowego cause również nie jest dostępne. Pending trwa do terminalnego wykonania
restartu albo końca bieżącego runtime przez inny reset/power loss.

Acceptance i aktualizacja pending są serializowane przez system runtime. First accepted
oznacza pierwszy w tej serializowanej kolejności, bez last-call-wins. ISR nie wywołuje
bezpośrednio request API; cross-task/ISR delivery wymaga osobnego kontraktu. Nie projektuje
się tu mutexów, kolejki eventów ani scheduler implementation.

#### Safe point i terminalne wykonanie

Obowiązuje SYS-007: request → pending → legal safe point → RestartExecutor. Sam accepted
request nigdy nie wywołuje executora. W RUNNING safe point jest na końcu kompletnej, bezpiecznej iteracji runtime/system processing;
w ERROR shell jest to system/recovery safe point z SYS-105, niezależny od domain loop.
Normalny startup pozostaje one-shot; request nie przerywa go ani nie wznawia callbacków.
Execution następuje dopiero po zakończonym startupie w legalnej system execution boundary. Pojęcie tick użyte w SYS-102 opisuje przyszłą execution boundary i nie ustanawia publicznego RuntimePlan ani tick API; exact scheduling i RuntimePlan pozostają otwarte.

Pending pozostaje aktywny i może agregować kolejne causes, gdy trwa critical persistent write, OTA, restore,
factory reset albo dowolna operacja deklarująca restart unsafe. Owner operacji musi jawnie
udostępnić aktualny stan safe/unsafe orchestration; brak dowodu legalnego safe point nie daje
permission do resetu. SYS-107 nie implementuje Operation Manager, physical safe shutdown ani
RestartPreparation. Sam pending request nie wprowadza MAINTENANCE ani nie zmienia SafetyState.

W legalnym safe point lifecycle owner ponownie sprawdza warunki i przechodzi do terminalnego
dispatch jako jednej serializowanej operacji. Do momentu przekazania do RestartExecutor pending pozostaje authoritative: primary i accepted mask pozostają ważne, a rozpoczęcie próby nie może zgubić intencji. Executor jest platformowym handoffem; kontrakt nie wymaga bool/result, a prawidłowy restart może nie wrócić. Zablokowana próba sprawdzenia safe point nie konsumuje requestu.
Jeżeli executor zgłosi failure albo wróci bez wykonanego restartu, pending MUSI pozostać aktywny, primary bez zmian, a accepted mask bez zmian; intencja nie została skonsumowana. Nie oznacza to automatycznej kolejnej próby na następnym safe point. Retry loop, counter, delay, backoff, forced restart i escalation pozostają przyszłą execution/retry policy i nie mogą prowadzić do busy-loop retry. Jeśli rzeczywisty restart nastąpi, stara instancja runtime przestaje istnieć, więc nie wymaga lokalnego clear; nowa instancja zaczyna bez volatile pending.
Dokładny platformowy failure contract pozostaje przyszłą decyzją implementacyjną.

Request zaakceptowany przed terminalnym dispatch wchodzi do tej samej maski, także tuż przed
safe point. Po zamknięciu acceptance w terminalnym dispatch nie przyjmuje się kolejnych
requestów do konsumowanego zbioru; próba nie może zostać pozornie zaakceptowana i zgubiona.
Nie tworzy to drugiego pending rebootu. Jeśli safe point nigdy nie wystąpi, v1 nadal czeka
z widocznym pending. Timeout, forced restart, watchdog escalation i backoff nie należą do
SYS-107 i nie mogą być domyślnym obejściem safety boundary.

Fatal startup sam nie tworzy requestu. Dostępna, bezpieczna recovery capability może jawnie
requestować restart w ERROR, a system/recovery processing wykona go przy swoim safe point.
Brak normalnego domain loop i domain command nie blokuje tej ścieżki. Restart rozpoczyna
pełny BOOT; nie zmienia complete StartupReport, ERROR/FAULT/LOCKED ani handoff semantics.

#### Workflow completion i deterministyczne scenariusze

Factory reset, restore i OTA najpierw pomyślnie kończą operację i wszystkie krytyczne kroki
wymagane przed rebootem, dopiero potem requestują swój completion reason. Nie requestują
completion przy samym rozpoczęciu, abort lub failure. Wcześniejszy request wymaga osobnego,
jawnego przyszłego workflow contract. Niezależnie od jego reason istniejący pending nie daje
permission do restartu podczas nowej critical operation. Merge nie autoryzuje uruchamiania
równoczesnych lub niekompatybilnych workflows.

Config owner rozstrzyga live apply vs restart-required; SYS-107 nie wybiera rodzaju configu.
Po pomyślnym przygotowaniu restart-required config requestuje CONFIG_APPLY. User restart
jest jawną akcją: przyszły Auth/policy workflow akceptuje ją przed zapisaniem intencji.

| Scenariusz | Wynik |
|---|---|
| USER_REQUEST, potem CONFIG_APPLY | Primary USER_REQUEST; maska obu reasons; jeden restart. |
| CONFIG_APPLY, potem USER_REQUEST | Primary CONFIG_APPLY; maska obu reasons; jeden restart. |
| OTA_COMPLETE, potem FACTORY_RESET | Primary OTA_COMPLETE; maska obu reasons, jeśli oba completion są legalnie zaakceptowane; active factory reset blokuje execution do zakończenia. |
| FACTORY_RESET, potem USER_REQUEST | Primary FACTORY_RESET; maska obu reasons. |
| SAME_REASON wielokrotnie | Pending snapshot bez zmian. |
| Jawny request w ERROR shell | Pending czeka na system/recovery safe point; fatal sam niczego nie requestuje. |
| Request podczas critical write | Intencja może być przyjęta, ale executor nie działa do legalnego safe point. |
| Request tuż przed safe point | Jeśli przyjęty przed terminalnym dispatch, jest uwzględniony w konsumowanym zbiorze. |

#### Persistence, observability, testability i scope

Pending state jest volatile dla bieżącego runtime, bez heap, RTC/NTP i persistent replay.
Nowa instancja po dowolnym reboot/power loss zaczyna bez pending requestu. Ewentualny transfer
reason do next boot metadata jest osobnym future/platform contract: może być wiele-do-jednego,
nie musi mapować 1:1 i nie zmienia źródła prawdy hardware reset cause. SYS-107 nie definiuje
current-boot RestartReason catalog, storage, atomicity ani validity tego metadata.

Minimum status/diagnostics to read-only snapshot pending, primary i accepted mask: przy pending=false primary nie jest publikowany jako legalna wartość, a maska jest pusta; przy pending=true snapshot zawiera pending, legalny primary i accepted mask. Additional causes są wyliczane z maski i nie są osobną authoritative kopią.
Nie wymaga loggera, Network ani transportów; przyszłe HTTP/MQTT/Realtime mogą publikować ten
sam snapshot. Pierwszy accepted request i dodanie nowego cause są istotnymi zmianami systemowymi,
logowalnymi i potencjalnie publikowanymi jako event. Duplicate nie wymaga nowego eventu/logu.
Próba wywołania executora i jego failure powinny być logowalne, jeśli możliwe. Logging/publication
failure nie zmienia pending, nie blokuje legalnego restartu i nie uruchamia retry policy; Event API i logger są poza gate.

Przyszłe host tests muszą objąć: stabilne reason→bit, odrzucenie invalid przed shift/index, mask invariants dla pending, empty state, pierwszy request, duplicate bez zmian, drugi
różny reason i union mask, first primary dla obu kolejności, wszystkie powyższe scenariusze,
brak immediate executor, blocked safe point zachowujący pending, execution konsumujące request
przy executorze, executor failure niegubiący intencji, terminal acceptance boundary,
ERROR request i brak requestu od samego fatal startup, invalid reason bez mutacji, spójny
observability snapshot, brak cancellation oraz nową instancję bez odziedziczonego pending.

HIL pozostaje wymagany później dla rzeczywistego ESP restartu, non-return executora po sukcesie, boot reason handoff, power loss
wokół boot metadata, watchdog interactions i real OTA/factory reset/restore workflows.
Host tests policy nie dowodzą fizycznego restartu ani safety urządzenia.

SYS-107 nie zamyka ARCH-101, current-boot RestartReason catalog, boot-loop protection,
watchdog policy, Maintenance, OTA/Restore/Factory Reset implementation, Command API, Auth,
Event API, logging implementation, physical shutdown ani RuntimePlan scheduling.
F1.10 jest docs-only; ApplicationRuntime i wszystkie istniejące C++ kontrakty pozostają bez zmian.

### IDN-001 — rozdzielenie identity
`DeviceIdentity`, `BuildIdentity`, `HardwareIdentity` i `RuntimeIdentity` są osobnymi
pojęciami. Friendly/user-visible name nie jest częścią technical identity.
Kontrakt RuntimeIdentity definiuje SYS-101, a DeviceIdentity v1 — IDN-101.
BuildIdentity i HardwareIdentity zachowują odrębność i dotychczasowe capacities;
rozszerzenia ich schema/grammar pozostają osobnymi otwartymi decyzjami.

### IDN-101 — DeviceIdentity v1

**Status:** ACCEPTED — TARGET contract; F1.12 jest docs-only. CURRENT F1.3
Identity::DeviceIdentity ma tylko deviceType; nie implementuje tego kontraktu.
Legacy AquaCore::DeviceIdentity i wszyscy konsumenci pozostają bez migracji.

#### Wybór modelu device_id

| Model | Ocena dla v1 |
|---|---|
| A. Pełny MAC | Stabilny przy stałym źródle sprzętowym, zero provisioning i brak zapisu flash; wymiana MCU zmienia ID. Wybrany jako MAC48 contract. |
| B. MAC6 | Łatwy UX i zgodny z naming, ale tylko 24 bity; niewystarczający canonical device_id. |
| C. Persistent random ID | Może zachować logiczną identity po wymianie hardware dopiero z kontrolowanym transferem. Wymaga first-boot generation, atomic NVS, polityki reset/backup/restore i ochrony przed sklonowaniem; odrzucony dla v1. |
| D. User-assigned ID | Wymaga provisioning, kontroli kolizji i zmian użytkownika; nie daje zero provisioning ani stabilności machine keys. |
| E. Platform-stable opaque ID | Przydatny dla różnych platform, ale wymaga wyboru width, namespace i cross-platform format; nie uzasadnia teraz frameworka ani arbitralnego bufora. |
| F. device_type + hardware ID jako jeden string | Powiela type, miesza identity z naming i komplikuje equality; zachowujemy dwa osobne pola. |

Wybrany jest DeviceId v1 = pełny stable hardware MAC48, nie dowolny aktywny
adres interfejsu. Typ jest neutralny wobec SDK, ale celowo ma semantykę MAC48:
nie udaje uniwersalnego identyfikatora każdej przyszłej platformy. Persistent random ID
nie jest automatycznie lepszy: bez migration workflow nie przetrwa wymiany MCU, a
przeniesienie backupu na dwa urządzenia może sklonować identity. Tutaj nie wymaga się
takiej ciągłości logicznego assetu. Wszystkie modele są host-testable przez fake input;
hardware source i persistence wymagałyby osobnej walidacji platformowej.

#### Pola, type token i technical identity

Docelowy AquaCore::Identity::DeviceIdentity zawiera dokładnie dwa wymagane pola:
DeviceTypeToken deviceType oraz DeviceId deviceId. Nie zawiera friendly name,
firmwareVersion, coreVersion, hardwareVariant ani RuntimeIdentity. BuildIdentity
opisuje firmware/Core versions, HardwareIdentity wariant/platformę, a SYS-101
jedną instancję runtime; żaden z tych składników nie jest częścią DeviceId.

DeviceTypeToken jest owned fixed string, nie centralnym enum katalogu domen.
Canonical grammar to [a-z][a-z0-9_-]{0,22}: od 1 do 23 ASCII chars, pierwszy
znak lowercase letter, pozostałe lowercase letters, digits, underscore lub hyphen.
Brak whitespace, slash, MQTT +/#, non-ASCII i wersji w znaczeniu tokenu.
Canonical public string jest częścią machine contract: lowercase jest wymagane,
nie następuje case folding, trim ani truncation. Token jest stabilny między
kompatybilnymi wydaniami; nie wolno używać tej samej nazwy dla innego typu produktu.

Istniejące canonical v1 product tokens to luma, doser, hydro, clima, gas i fauna.
panel jest legalnym domain/application-owned tokenem klienta Panel, jeśli ten publikuje
własną identity; nie awansuje go do zależności krytycznej domeny ani nie wymaga
centralnego katalogu Core. Lista przykładów nie jest allowlist walidatora.
Nowy produkt deklaruje własny stały token w composition/build, spełniający grammar;
nie wymaga zmiany Core. Compile-time validation może wspomagać deklarację,
ale jawna runtime validation nadal obowiązuje na granicy danych.

Core może przechowywać, porównywać i serializować type metadata, lecz nie ma
funkcjonalnego switch(device_type), if LUMA/DOSER ani zależności od katalogu domen.
DeviceId identyfikuje hardware-backed instancję v1 niezależnie od typu.
Pełna technical identity jest uporządkowaną parą (device_type, device_id);
DeviceId equality oznacza ten sam pełny 48-bit hardware identifier, niezależnie od `device_type`. DeviceIdentity equality wymaga tego samego DeviceTypeToken oraz tego samego DeviceId: oznacza tę samą techniczną rolę aquaOne na tym samym hardware source. Ten sam DeviceId po zmianie firmware z luma na hydro pozostaje tym samym hardware DeviceId, ale tworzy inną DeviceIdentity pair.
Type nadaje kontekst produktowy, lecz nie naprawia zduplikowanego hardware ID.
Zmiana firmware version lub hardwareVariant nie zmienia DeviceId; flash innego
device_type zachowuje DeviceId, ale zmienia parę technical identity i type-based naming.

Friendly/user name jest osobną konfiguracją/UI metadata, może się zmieniać i nie
służy do unique_id, MQTT root, machine equality ani persistence key.
Nie powstaje dodatkowy canonical composite string ani publiczne IdentitySnapshot API.
Consumers mogą później otrzymać spójny read-only view czterech odrębnych kategorii,
z jawną availability każdej; exact snapshot schema i transport payloads są otwarte.

#### Binary value, canonical text, MAC6 i capacities

Authoritative DeviceId storage to dokładnie 6 octets w canonical MAC byte order,
octet 0 pierwszy w zwyczajowym zapisie MAC. Nie wybieramy host-endian uint64
reinterpretation ani redundantnego authoritative text buffer. UInt64 lower-48-bits
wymaga dodatkowych upper-bit rules; fixed text powiela format i walidację znaków.
Value type jest copyable, zero heap, posiada własne fixed storage i nie zależy
od adresów pointerów. Default jest invalid/unavailable, nigdy legalnym ID.

Canonical textual serialization legalnego ID to dokładnie 12 uppercase ASCII
hex chars, bez :, -, 0x i locale, po dwa znaki na octet, leading zeros zachowane.
Przykład: MAC 24:6F:28:A1:B2:C3 → DeviceId 246F28A1B2C3.
MAC6 to ostatnie 6 chars canonical text, czyli octets 3..5 → A1B2C3.
MAC6 nie jest osobnym authoritative polem DeviceIdentity ani drugim źródłem prawdy.

Publiczne named limits v1, w bajtach ASCII storage:

- DEVICE_TYPE_CAPACITY = 24, w tym NUL; maksymalnie 23 chars.
- DEVICE_ID_BYTE_COUNT = 6; nie ma arbitrary string capacity DeviceId.
- DEVICE_ID_TEXT_LENGTH = 12, MAC6_TEXT_LENGTH = 6.
- C-string formatter storage wymaga odpowiednio 13 i 7 bajtów z NUL;
  nie ustanawia to binary/wire storage ABI.

Zachowujemy type capacity CURRENT 24 zamiast zmniejszenia do 16: daje miejsce na
stabilne nazwy produktów bez arbitralnego wzrostu ani niepotrzebnego zawężenia
istniejącego limitu. Tokeny dzisiejszych produktów są krótkie, ale nie są katalogiem
zamkniętym. Zmiana tego publicznego limitu wymaga jawnej oceny kompatybilności.

BuildIdentity i HardwareIdentity zachowują osobne named capacities CURRENT / existing contract:
FIRMWARE_VERSION_CAPACITY 24, CORE_VERSION_CAPACITY 16 i HARDWARE_VARIANT_CAPACITY 32,
każda z NUL. Nie zwiększamy ich i nie przenosimy pól do DeviceIdentity.
Ich istniejące required/null/empty/byte-length rules bez normalization pozostają podstawą CURRENT; IDN-101 ich nie redefiniuje ani nie zatwierdza jako przyszłego schema. Finalny SemVer, platform/revision schema i dodatkowe pola są osobnymi otwartymi decyzjami, nie warunkiem zaakceptowania DeviceIdentity.
Duplikacja coreVersion limit 16 między modułami jest poza F1.12.
Legacy deviceName capacity 32 nie staje się polem ani limitem technical identity.

Formatter używa caller-provided storage i explicit result; invalid identity lub
zbyt mały buffer daje jawny błąd, bez truncation ani publikacji partial ID.
Jeżeli przy błędzie istnieje writable niepusty C-string buffer, otrzymuje pusty tekst.
Canonical parser, jeśli przyszła implementacja go udostępni, akceptuje dokładnie
12 uppercase hex chars i legalne decoded value; lowercase, separators, prefix,
whitespace i zła długość są odrzucane, nie normalizowane. Backend może odczytać binary
dane lub jawnie skonwertować format SDK na granicy platformy; to nie rozszerza parsera.

#### Source, stability i portability

Composition Root dostarcza stały product-owned type oraz jawnie wstrzyknięty
platform identity source. Source deterministycznie odczytuje stable hardware ID
albo zwraca explicit failure; nie jest RuntimeIdentityGenerator i nie losuje.
Nie używa NVS identity provisioning, czasu, uptime, sieci, friendly name ani wersji.
Dokładny source/read interface, SDK calls i lokalna bounded execution należą do
przyszłego backendu; Core nie importuje ESP32, WiFi ani eFuse API.

ESP32 backend v1 wybiera factory/base hardware MAC, odpowiadający domyślnemu źródłu Wi-Fi STA, bez uzależnienia od runtime interface MAC override, custom/base override ani aktywnego połączenia Wi-Fi. Source selection musi być stałe przez firmware upgrades i nie zależy od STA/AP/BT/Ethernet enablement. Backend odpowiada za właściwy odczyt dla konkretnego ESP-IDF/SoC; Core nie ustanawia konkretnego SDK API.
Odczyt nie wymaga NetworkService, WiFi connection, AP, MQTT ani Internetu.
Platform backend odpowiada za read/preconditions i nie może kolidować z early outputs
ani wcześniejszym RNG window SYS-101. SDK-specific metoda zostaje implementacyjna.
Espressif opisuje factory base MAC, pochodne interfejsów oraz możliwość override:
[ESP-IDF MAC Address documentation](https://docs.espressif.com/projects/esp-idf/en/v4.4.6/esp32/api-reference/system/system.html#mac-address).

Reboot, factory reset, config repair, restore i OTA przy tym samym hardware source
zachowują DeviceId; brak flash identity writes oznacza brak identity-related wear.
Factory reset nie zmienia product-owned type. Backup/restore nie przywraca ID obcego
urządzenia jako local authoritative identity. Replacement MCU lub PCB z innym MCU
oznacza nowy DeviceId v1, nowe naming i nową parę technical identity, bez transferu
starego assetu. Sama wymiana elementów PCB przy tym samym MCU/source nie zmienia ID.
Snapshot w RAM jest niezmienny do końca runtime; kolejna instancja odczytuje to samo
hardware input, podczas gdy RuntimeIdentity niezależnie wykonuje świeżą generację.

Non-MAC platform lub ID szerszy niż 48 bitów nie otrzymuje automatycznej zgodności: nie truncates/hashuje innego ID do MAC48 i nie
udaje factory source. Taka platforma wymaga osobnej decyzji o source/contract extension.
V1 nie opiera canonical ID na rotating/random local MAC ani mutable network config.

#### Validation, ownership, startup i failure

Jawna validating factory tworzy kompletną legalną parę albo explicit field/error,
bez częściowej publikacji, implicit conversion, fallback ID lub setterów dla consumers.
Default/failed value jest invalid i nie udaje legalnej identity. Po utworzeniu legalna
wartość jest immutable w lifetime ownera; zwykłe kopie posiadają własne storage.
DeviceId v1 odrzuca all-zero, all-FF/broadcast oraz multicast (I/G bit octet 0 ustawiony) dla wybranego unicast hardware identity source. Wymóg locally administered nie jest uniwersalną regułą value type: ESP32 v1 factory identity backend MUSI jednak dostarczyć stabilny factory/base hardware MAC zgodny z polityką backendu; universally administered factory source jest oczekiwany, a locally administered, custom i runtime interface override nie są akceptowanym źródłem canonical DeviceId. Type waliduje presence, długość i pełną grammar. Binary input wymaga dokładnie 6 readable bytes; tekst/token
wymaga null check i bounded readable input zgodnie z kontraktem przyszłego API.
Legalny format nie dowodzi autentyczności, stabilności source ani braku klonów:
platform provenance jest odpowiedzialnością backendu. DeviceId/MAC jest publiczną
technical identity: nie jest secret, credential, auth tokenem ani certificate identity
oraz nie może być samodzielną podstawą authorization. Clone/spoofing jest możliwy.

Pasuje model Identity::ValidationResult(error, field). Zachowujemy znaczenia
None, NullInput, EmptyRequiredField, TooLong; przyszła implementacja wymaga także
rozróżnienia invalid format i invalid/reserved DeviceId oraz pola DeviceId.
Nazwy/encoding rozszerzeń C++ pozostają implementacyjne. Platform read failure jest
osobnym wynikiem source, nie fałszywym validation success.
Identity adapter mapuje source unavailable/failure i invalid type/ID na zwykły
StartupStepResult::failed ze stable identity-specific code zgodnym z SYS-104.
Dokładny local code catalog należy do przyszłej implementacji ownera; nie rozszerzamy
RESULT_CONTRACT_FAILURE na invalid backend identity result.

Composition Root posiada source, participant context i jeden runtime-scoped identity
owner/storage. Consumer dostaje read-only view albo value copy; żadnego singletona,
global mutable identity, Registry lookup ani IdentityService frameworka.
Owner/context/source i pożyczone views żyją przez cały okres używania, co najmniej
runtime zgodnie z SYS-102. Nowy owner zaczyna unavailable; po sukcesie publikuje raz
pełną parę. Odczyty nie wykonują hardware I/O ani ponownej derivation.

Normatywna kolejność: early safe outputs → full plan validation → ordinary BOOT →
pierwszy REQUIRED CORE_INIT RuntimeIdentity (SYS-101) → drugi REQUIRED CORE_INIT
DeviceIdentity read/validate/publish → pozostałe CORE_INIT i dalsze fazy.
Root zapewnia ten declaration order SYS-102; nie ma nowego action, priority ani DAG.
Composition przygotowuje type/source/context bez hardware identity read przed start().
BOOT nie wymaga legalnej DeviceIdentity; Hardware read nie jest constructor effect.
Wspólny participant obu identity albo DeviceIdentity przed RuntimeIdentity naruszałby
przyjęty ordering SYS-101, więc nie jest wybrany. Każdy consumer wymagający DeviceIdentity
jest deklarowany po jej inicjalizacji. Backend wymagający NETWORK_INIT jest niezgodny.

Legalna DeviceIdentity jest lokalnym invariantem normalnego RUNNING niezależnie od
opcjonalności MQTT/HA. Missing source/invalid pair oznacza REQUIRED participant failure
w CORE_INIT → ERROR + FAULT + LOCKED według SYS-102/104/105, bez domain processing.
Runtime nie interpretuje identity ani type. Wcześniej wygenerowana RuntimeIdentity
pozostaje legalna i niezmieniona przy DeviceIdentity failure.
Fatal przed publikacją DeviceIdentity pozostawia ją unavailable; bezpieczna lokalna
diagnostyka/ERROR shell może działać bez niej, nie wymyśla ID ani transport namespace.
Fatal później zachowuje opublikowaną parę. Recovery nie ponawia identity read/publish,
drugi start() jest no-op, config repair nie daje ERROR → RUNNING; nowa próba wymaga
nowej instancji startupu. RuntimeStatus, StartupReport i SYS-106 handoff są bez zmian.

#### MQTT/HA naming, collisions, testability i scope

Zachowujemy transport/integration standard: compact base aquaone-<MAC6>, type subtree
aquaone-<MAC6>/<type>, client_id/HA technical name aquaone-<type>-<MAC6> oraz
unique_id/object_id aquaone_<type>_<MAC6>_<entity>. MAC6 jest pochodną tego samego
wybranego full source, nie MAC aktywnego AP czy innego interfejsu.
Nie zmieniamy MQTT implementation ani standardów payload/integration API w F1.12.
NAMING_VERSIONING_STANDARD §96 używa label device_id dla aquaone-luma-A1B2C3: to dotychczasowa presentation/integration name, nie canonical DeviceId vNext i nie może zostać po cichu reinterpretowana jako 12-hex value. Migracja istniejącego naming/API jest osobnym krokiem; przed zmianą MQTT/HA trzeba sprawdzić, z jakiego MAC source pochodzi dotychczasowy MAC6. Jeśli source różni się od canonical factory source, topic roots i unique_id mogą się zmienić. IDN-101 nie migruje teraz NAMING_VERSIONING_STANDARD.

MAC6 nie gwarantuje globalnej unikalności. Local/home naming akceptuje compact suffix
jako integration compromise; dwa różne full MAC mogą mieć identyczny suffix.
Kolizja samego suffix dotyczy base namespace, a dla tego samego type także topic subtree,
client_id i HA IDs. Dodanie type nie chroni dwóch urządzeń tego samego typu.
Full DeviceId rozróżnia takie urządzenia, ale samo jego posiadanie nie usuwa kolizji naming.
Nie używa się MAC6 jako globalnego primary key ani dowodu physical device equality.
W modelu niezależnych równomiernych suffix birthday approximation dla małego ryzyka
to n(n-1)/(2·2^24); rzeczywiste vendor allocation nie musi mieć takiego rozkładu.
Pełny factory MAC korzysta z przydziału producenta, nie losowego collision modelu
RuntimeIdentity; klony, wadliwy przydział i spoofing nadal wykluczają absolutną gwarancję.
V1 nie projektuje auto-renaming/collision resolution ani asset registry; wykryta naming
kolizja wymaga przyszłego integration policy, nie cichego zastąpienia canonical ID.

Przyszłe host tests: legalne i nowe domain-owned type tokens (w tym panel), null/empty,
uppercase/non-ASCII/whitespace/slash/wildcards, first-character grammar, 23-char boundary
i overflow; full binary valid/zero/broadcast/multicast rejection, ESP32 backend local-source rejection, pair equality, same DeviceId + same type equality, same DeviceId + different type inequality, DeviceId equality niezależna od type,
copy independence, invalid factory bez partial publication; canonical 12 hex, leading
zeros, byte order, parser rejection i small-buffer/no-truncation behavior, MAC6 dokładnie
last-six; różne full IDs z tym samym suffix pozostają różne. Fake source sprawdza
required failure/invalid-as-success defensive validation, ordering po early outputs
i RuntimeIdentity, brak zależności od sieci, no-op second start, fatal availability,
immutable values/report i identyczne DeviceId przy tym samym input w nowym runtime.
RuntimeIdentity generuje się niezależnie; test nie wymaga matematycznie różnej wartości.

HIL później: rzeczywisty factory hardware read i preconditions na wspieranych ESP32,
zgodność domyślnego factory/STA naming source, niezależność od runtime/custom MAC override, reboot/power-cycle/upgrade stability,
factory reset/restore bez zmiany ID, MCU replacement, wiele interfejsów i active/custom
MAC overrides bez zmiany canonical source. Host tests wystarczą dla tokenów/formatów,
HIL nie jest wykonywany ani wymagany dla dokumentacji F1.12.

IDN-101 nie zamyka ARCH-101, current-boot RestartReason, non-ESP32 source/extension,
device provisioning/migration, asset management, friendly-name config, MQTT/HA full API,
transport payload formats, naming-collision resolution, security/certificate identities,
BuildIdentity version grammar ani HardwareIdentity platform/revision schema.
SYS-101 i wszystkie istniejące C++ kontrakty pozostają bez zmian.

### CMD-001 — wspólna ścieżka komendy
Każde źródło sterowania korzysta z Source → Command → Validation → Authorization/Policy → Safety/Action Locks → Domain execution → State update → Event/Result.

### EVT-001 — snapshot jest źródłem prawdy
Snapshot jest autorytatywnym stanem. Realtime i Event informują o zmianach, ale nie zastępują snapshotu.

### EVT-002 — resync
W V1 nie ma event replay. Po reconnect, reboot albo wykryciu niespójności klient wykonuje pełny resync.

### CFG-001 — rozdział konfiguracji
CoreConfig, DomainConfig, DomainState, SystemState i RuntimeState są rozdzielone. RuntimeState nie jest persistent.

### CFG-002 — lifecycle konfiguracji
Konfiguracja przechodzi przez load, decode, version, migrate, validate i apply. Walidacja poprzedza zapis i zastosowanie.

### SAF-001 — rozdział Safety/Maintenance/Action Lock
Maintenance, Safety i Action Lock są różnymi mechanizmami. SafetyState jest agregatem, nie drugim systemem blokad.

### ALM-001 — rozdział pojęć alarmowych
Warning, alarm, fault i safety lock są różne. ACK nie oznacza CLEAR, a alarm nie oznacza automatycznie Safety Lock.

### HW-001 — trzy poziomy hardware
Rozdzielamy generic technical abstractions, concrete hardware drivers i domain hardware interfaces. BoardProfile należy do projektu.

### HW-002 — EarlySafeOutputInitializer
`EarlySafeOutputInitializer` wykonuje przed odczytem configu i pełnym hardware init
idempotentną techniczną operację ustawienia konserwatywnego stanu fizycznych wyjść. Nie
interpretuje alarmów ani Safety policy i nie należy do przyszłego Safety framework. Raportuje
jawny sukces albo błąd. Urządzenie bez ryzykownych wyjść może użyć neutralnej implementacji
no-op, bez rozgałęzień po `device_type`.

### WEB-001 — jeden transport
Jedno urządzenie ma jeden fizyczny transport Web. HTTP i Realtime docelowo współdzielą backend i port.

### SEC-001 — Auth i Safety
Auth odpowiada za uprawnienie, Safety za możliwość wykonania akcji w danym stanie. Domain nie zna haseł, sesji ani handshake transportu.

### REG-001 — Feature Registration
Capability/providers są rejestrowane jawnie podczas startup. Registry nie jest service locatorem. Provider nie może utrzymywać konkurencyjnej kopii Domain State.

### SEC-002 — ochrona sekretów
Sekrety nie mogą trafiać do status, diagnostics, logs, Realtime ani MQTT state.
### DIAG-001 — rozdział diagnostyki
CoreDiagnostics i DomainDiagnostics są semantycznie oddzielone i korzystają ze wspólnej infrastruktury.

## DECISION REQUIRED

- ARCH-101 — dokładny API Core ↔ Domain;
- Rozszerzenia identity poza IDN-101 v1 — BuildIdentity version grammar, HardwareIdentity platform/revision schema oraz future non-MAC DeviceId/source;
- CFG-101 — dokładny model pending config i recovery po korupcji;
- CFG-102 — zakres DomainState w backupie;
- CMD-101 — command envelope, CommandResult, error_code, request/correlation ID;
- CMD-102 — idempotency i deduplication;
- EVT-101 — event envelope, priority i sequence format;
- EVT-102 — snapshot schema oraz relacja snapshot/HTTP/realtime;
- ALM-101 — wspólny API alarmów i zakres capability;
- SAF-101 — dokładny model Action Lock i priorytet powodów;
- MNT-101 — kontrakt przygotowania domeny do maintenance;
- DIAG-101 — lista providerów/capability diagnostycznych;
- REG-101 — nazwy providerów, API registry i limity;
- WEB-101 — public/auth policy endpointów;
- WEB-102 — API HTTP i kompatybilność z obecnym Web Core;
- RT-101 — protokół realtime, auth, heartbeat i reconnect;
- MQTT-101 — zakres wspólnej infrastruktury MQTT;
- SEC-101 — auth HTTP/WebSocket oraz model sekretów;
- HW-101 — polityka wspólnych driverów i dokładne kontrakty capability;
- MNT-102 — zakres wspólnego OTA, restartu, backup/restore i factory reset;
- PANEL-101 — protokół i zakres klienta aquaOne Panel;
- SYS-103 — onboarding/recovery mode.

Nie ustalamy tych wartości na podstawie istniejącego Dosera ani innego kodu legacy.

## SPIKE REQUIRED

- WEB/RT: HTTP + WebSocket na jednym porcie i jednym transporcie;
- WEB/RT: HTTP podczas aktywnego WebSocket, reconnect, full resync, slow client i backpressure;
- WEB/RT: kompatybilność ESP32 i ESP32-S3, heap, flash i wpływ na main loop;
- WEB/OTA: multipart OTA, abort, auth handshake oraz reconnect po restart/OTA;
- RT: liczba klientów, heartbeat, kolejki, payload size i timing — wyłącznie z pomiarów;
- CFG: atomic persistence i recovery po utracie zasilania;
- REG: koszt serializacji, provider limits i zachowanie przy pełnym registry;
- TEST: runtime evidence na fizycznym hardware dla wybranych scenariuszy.

Nie wpisujemy do standardu konkretnych limitów ani timeoutów bez pomiarów.

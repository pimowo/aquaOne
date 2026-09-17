# Architecture vNext — decyzje

**Status:** DRAFT ACCEPTED dla FAZY 0, F1.1 CONTRACT GATE, F1.5 SYS-102 DESIGN GATE, F1.6 SYS-104 DESIGN GATE i F1.8 SYS-105 RECOVERY DESIGN GATE
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
SYS-105. SYS-107 pozostaje otwarte.

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

### IDN-001 — rozdzielenie identity
`DeviceIdentity`, `BuildIdentity`, `HardwareIdentity` i `RuntimeIdentity` są osobnymi
pojęciami. Friendly/user-visible name nie jest częścią technical identity. Dokładne pola,
format `device_id`, użycie MAC/MAC6, capacities oraz kontrakt RuntimeIdentity pozostają
DECISION REQUIRED.

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
- SYS-101 — nazwa, algorytm, encoding, generator, RNG source i collision policy RuntimeIdentity;
- SYS-106 — dokładny przyszły writer ownership dla HealthState i SafetyState;
- SYS-107 — dokładny katalog RestartRequestReason i zachowanie wielu pending requestów;
- IDN-101 — pola identity, format device_id, MAC/MAC6, capacities i zasady walidacji;
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

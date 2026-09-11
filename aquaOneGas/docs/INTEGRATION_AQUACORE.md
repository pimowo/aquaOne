# Integracja GasSense z AquaCore

Aktualne AquaCore ma już realne moduły m.in.:
- Config,
- Diagnostics,
- Logging,
- Network,
- System,
- Time,
- Web.

GasSense nie powinien kopiować tych funkcji.

## Reguła integracji

Najpierw sprawdzić publiczne API aktualnej wersji AquaCore, a dopiero potem podpiąć:
- boot,
- storage/config,
- NetworkService,
- RTC/NTP,
- diagnostics,
- logger,
- Web,
- MQTT, gdy warstwa MQTT będzie dostępna/stabilna,
- watchdog/OTA, gdy będą dostępne we wspólnym rdzeniu.

Nie tworzyć lokalnych odpowiedników tylko po to, żeby „na chwilę działało”, jeżeli dana funkcja ma wejść do AquaCore.

## Wersjonowanie

Nie śledzić bezwarunkowo `main` w urządzeniu produkcyjnym.

Po ustabilizowaniu API przypiąć konkretny tag/commit AquaCore.

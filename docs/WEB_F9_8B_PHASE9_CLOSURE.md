# F9.8B — Final Phase 9 closure

**Date:** 2026-10-09
**Baseline:** `22596955346c65ac80cddd2fe6be7fea54d0127d` (`Audit Phase 9 web and realtime closure`)

F9.8A returned **A — PHASE 9 CLOSABLE NOW**. The final evidence and current repository state remain consistent with that decision.

## Closure record

- **F9.1–F9.8: PASS.** F9.1–F9.7 implementation, product migration and HIL records are summarized in the [F9.8A audit](WEB_F9_8A_PHASE9_CLOSURE_AUDIT.md); this document records the final status transition.
- **Core HTTP:** CURRENT native HTTPD transport and published HTTP projections/actions.
- **Core Realtime:** CURRENT optional WS publication and F9.6 coherent resync/recovery capability. The classic ESP32 F9.6 HIL passed; the detailed evidence is in [F9.6C HIL](WEB_REALTIME_F9_6C_HIL.md).
- **Production HTTP:** Luma, Hydro and Doser use native Web. Physical evidence: [Luma F9.7D2 HIL](WEB_F9_7D2_LUMA_HIL.md), [Hydro F9.7E3 gate/HIL record](WEB_F9_7E1_HYDRO_MIGRATION_GATE.md) and [Doser F9.7G1/G5 HIL](WEB_F9_7G1_DOSER_OTA_DESIGN.md). Doser also has product-local native streaming OTA CURRENT.
- **Product Realtime:** Luma, Hydro and Doser have no production WS endpoint, `RealtimeSnapshot` resync cohort or product notifications. This remains nonblocking for Phase 9 under the F9.8A decision. No product Realtime deployment is claimed.
- **Open boundaries:** SEC-101, WEB-101 and remaining RT-101 decisions remain open. SYS-107, MNT-102 and shared OTA remain future/transitional. These do not reopen the Phase-9 exit decision.
- **Phase 10:** REQUIRED NEXT, NOT STARTED. Its scope remains the Home Assistant client/integration: HTTP and future WS clients, Config Flow, Zeroconf/mDNS, entity mapping and client reconnect/resync. Product server-side WS composition and its Auth/wire/cohort decisions must be complete before a product WS client uses it; F9.8B does not implement them.

## Final status

**PHASE 9 CLOSED.** Core HTTP and Realtime foundations, F9.6 physical HIL, and Luma/Hydro/Doser production HTTP migrations meet the accepted Phase-9 scope. Product Realtime is not CURRENT in any of the three products.

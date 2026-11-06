# PlantCare R7.2 log summary

## 12 h continuous run (plan ch.7: >= 137 of 144 valid rows, no abnormal reboot)
- --start 2026-11-04 23:44: earlier records ignored (0 log rows, 0 AI records)
- sample rows: 207, valid: 207, FAST/SIM rows: 0
- time axis: NTP clock, span 17.17 h, expected 207 rows, coverage 100.0 % (valid 100.0 %)
- longest gap between sample rows: 5.0 min
- 12 h window from 11-04 23:44: 144 rows, 144 valid (need >= 137) -> **PASS**
- boots logged: 0 (none), abnormal resets: 0

## State changes (2)
- 11-04 23:45  DRY->WAITING dry_to_wait R=-1.7
- 11-04 23:55  WAITING->OK wait_to_ok R=94.7

## AI explanations
- results: 2 (AI 2, template 0)
- with an HTTP call: 2, AI shown 2 -> success 100.0 %, fallback 0.0 %, retried 0
- latency of the last HTTP attempt: p50 4652 ms, p95 5015 ms
- by event (AI/total): dry_to_wait 1/1, wait_to_ok 1/1

ergleiche mir nochmal kurz unsere Sendeleistung und CR fuer unsere eigenen Aussendungen und fuer die Pakete die wir repeaten                     

⏺ Lass mich kurz die aktuelle Logik in den entsprechenden Stellen prüfen.

  Searched for 3 patterns, read 1 file (ctrl+o to expand)

⏺ Hier die kompakte Übersicht — Tx-Power und CR für alle drei Klassen unserer Aussendungen:

  Eigene Aussendungen

  Rolle: CLIENT
  Pakettyp: Position (Stage 1)
  TX-Power: vanilla
  CR: 5
  Hop: ≤2
  ────────────────────────────────────────    
  Rolle: CLIENT
  Pakettyp: Telemetrie (Stage 2)
  TX-Power: −6 dB (floor 10 dBm)
  CR: 5
  Hop: 0
  ────────────────────────────────────────
  Rolle: CLIENT
  Pakettyp: sonst (Text, NodeInfo, ...)
  TX-Power: vanilla
  CR: vanilla
  Hop: vanilla
  ────────────────────────────────────────
  Rolle: CLIENT_BASE
  Pakettyp: Position
  TX-Power: vanilla
  CR: vanilla
  Hop: vanilla
  ────────────────────────────────────────
  Rolle: CLIENT_BASE
  Pakettyp: Telemetrie
  TX-Power: vanilla
  CR: vanilla
  Hop: 0
  ────────────────────────────────────────
  Rolle: CLIENT_BASE
  Pakettyp: sonst
  TX-Power: vanilla
  CR: vanilla
  Hop: vanilla
  ────────────────────────────────────────
  Rolle: CLIENT_MUTE
  Pakettyp: Position
  TX-Power: vanilla
  CR: vanilla
  Hop: ≤2
  ────────────────────────────────────────
  Rolle: CLIENT_MUTE
  Pakettyp: Telemetrie
  TX-Power: vanilla
  CR: vanilla
  Hop: 0
  ────────────────────────────────────────
  Rolle: CLIENT_MUTE
  Pakettyp: sonst
  TX-Power: vanilla
  CR: vanilla
  Hop: vanilla
  ────────────────────────────────────────
  Rolle: Stage-4-Companion (alle Rollen, nur LF)
  Pakettyp: Position-Klon
  TX-Power: vanilla
  CR: vanilla (LongFast SF11/BW250)
  Hop: wie Original-Position

  Repeats (Pakete fremder Knoten — FloodingRouter::applyClientRepeatPolicy)

  Nur CLIENT und CLIENT_BASE repeaten; CLIENT_MUTE repeatet nie (Standard-Definition). Andere Rollen (REPEATER/ROUTER/...) sind unverändert.

  ┌──────────────────────────────────────────────────────────────────────────────┬─────────────────────────────────────────────────┐
  │                                    Filter                                    │                     Aktion                      │
  ├──────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
  │ Paket undecodierbar                                                          │ drop (B1)                                       │
  ├──────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
  │ Portnum = TELEMETRY_APP                                                      │ drop (B1)                                       │
  ├──────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
  │ Portnum nicht im Core-Whitelist                                              │ drop (B1)                                       │
  ├──────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
  │ Portnum = TRACEROUTE_APP / ROUTING_APP                                       │ vanilla Power, vanilla CR (B3)                  │
  ├──────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
  │ Direct-DM, wir sind preferred next_hop, Ziel war ≤12 h Direkt-Nachbar        │ vanilla Power, vanilla CR (B3)                  │
  ├──────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
  │ First-relay (hop_start - hop_limit == 0, also Originator war Direkt-Nachbar) │ vanilla Power, vanilla CR (B3)                  │
  ├──────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
  │ B2 default — alles andere was repeatet wird                                  │ −6 dB (floor 10 dBm), CR=5                      │
  ├──────────────────────────────────────────────────────────────────────────────┼─────────────────────────────────────────────────┤
  │ Plus für CLIENT_BASE im B2-Pfad                                              │ zusätzlich hop_limit = 0 (Repeat-Kette stoppen) │
  └──────────────────────────────────────────────────────────────────────────────┴─────────────────────────────────────────────────┘

  Quintessenz

  - CR=5 überall wo wir bewusst eingreifen — eigene Telemetrie/Position bei CLIENT, sowie alle B2-Repeats. Begründung: CR=5 ist der Meshtastic-Default
   des Default-LongFast-Profils und damit kompatibel zu allem.
  - −6 dB Power nur bei zwei Klassen: eigene Telemetrie (Stage 2 own-TX, nur CLIENT) und B2-Repeats (CLIENT + CLIENT_BASE). Beides Sachen die "im
  Umfeld bleiben sollen" — kein langes Echo durchs ganze Netz.
  - Eigene Position wird bewusst nicht in der Power gedämpft, weil ein Position-Beacon Reichweite braucht — sonst nützt er nichts. CR wird aber bei
  CLIENT auf 5 gezwungen (Vereinheitlichung). CLIENT_BASE/CLIENT_MUTE Position: komplett vanilla.
  - TRACEROUTE/ROUTING beim Repeat unangetastet, damit Diagnose-Tools verlässlich bleiben.

# Finding and remembering LAN parties

Find LAN in the native multiplayer panel opens a four-row party browser. Select
a row, then Join selected. Previous/Next page through up to 16 recent hosts;
Refresh requests another scan. The list shows each host's name, source IPv4
address, actual TCP port, player count and lobby/running state. A different wire
version or content fingerprint disables Join selected. A running host still
checks the original profile's reservation; discovery cannot replace an owner.
The manual IPv4/port fields remain usable when broadcast discovery is filtered.

The browser sends a query every 1.5 seconds to UDP 35740 on the subnet broadcast
address and loopback. Hosts reply to the querying socket; browser sockets use
an ephemeral local port. The game continues to use its selected TCP port.
Replies expire after six seconds. A matching process-instance/port is merged
across loopback and LAN addresses; non-loopback addresses are preferred. A
selection follows its instance/port, so an expired or restarted host cannot
silently redirect the selected row to a different party.

Discovery is a local convenience, rather than peer authentication. Persistent
player identities never appear in advertisements. Each discovery service uses
fresh OS-random instance/query values. Replies must echo the current or previous
query nonce. Hosts process at most 32 packets per tick and send at most 32 replies
per second; responses are fixed-size. `ARCMP_DISCOVERY_TARGET=127.0.0.1` directs
queries to loopback for isolated multi-process tests.

## Datagram layout

Discovery version 1 is independent of the campaign wire version. Integers are
big-endian. Every packet starts with a 16-byte header: `ACMD` (4), discovery
version (1), kind (1: query, 2: advertisement), game protocol (u16), query nonce
(u64). Queries contain only that header. Advertisements contain 92 bytes:

| Offset | Bytes | Field |
| --- | --- | --- |
| 16 | 2 | Actual game TCP port |
| 18 | 1 | Connected players, 1–4 |
| 19 | 1 | Running/reserved roster, 0 or 1 |
| 20 | 8 | Ephemeral host instance |
| 28 | 32 | Canonical game-content SHA-256 |
| 60 | 32 | Printable, NUL-padded host name |

The reply source supplies the IPv4 address. Addresses inside packets are not
trusted or accepted. Unknown schemas, wrong sizes/nonces, invalid counts/flags
or noncanonical names are rejected. A differing game protocol is retained so
the UI can explain incompatibility before connecting.

## Local preferences

`arcmp-lobby.bin` retains the last valid player name, IPv4 address and port. The
64-byte file contains `ACLP1` plus three zero bytes, name (32), IPv4 text (16),
port (u16), two reserved zero bytes, then CRC-32 over the preceding 60 bytes.
Saving canonicalizes normal C strings, flushes a temporary file and atomically
replaces the old file. Bad/truncated/trailing-byte files are ignored without
partially changing the current settings. This file is separate from player
identity, character drafts and campaign saves.

The `discovery` portable fixture covers real UDP advertisement, a TCP join using
the discovered endpoint, incompatible/full/running updates, nonce refusal,
20,000 canonical mutations and expiry. The native lobby fixture uses actual
Find LAN, row, Join selected, preset and Ready clicks over four engines. See
`STATUS.md` for accepted platform evidence and `WORKFLOW.md` for test cadence.

## Host removal before launch

The host can click a participant row, choose Remove player and confirm within ten
seconds. Up/Down selects a participant while that control has keyboard focus.
The confirmation binds the participant's profile, protecting against slot reuse.
Removal clears readiness, sends a clear reason and disables automatic retry for
that connection. The player can explicitly rejoin. Clients cannot remove players;
launched parties retain their reserved owners. Native UI acceptance is pending;
the session API's authority, identity, launch and rejoin checks pass.

## Chat and content compatibility — delivery 4

The lobby and F12 party panel wrap messages and expose the retained 64-message
history with Page Up/Page Down, the wheel over chat and End for latest. Reading
older messages stays anchored when new messages arrive; sending your own message
returns to the latest text. The native Linux ASan/UBSan and Windows/Wine checks
exercise wrapping, scrolling, arrival while reading and return to latest, with
reviewed rendered frames in `evidence/delivery-4`.

Content fingerprints now validate a local names/size/mtime inventory each launch.
Changed or damaged cache metadata triggers full hashing; old fingerprints migrate
automatically. This local inventory is not the network fingerprint. The latter
remains the canonical content SHA-256, identical across OSes. The same 1,432 real
asset records retain the previous independent reference digest on both targets.

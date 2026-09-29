# DNIIP wire protocol

Defined in `common/protocol.hpp`. A custom, length-prefixed framing over a
persistent TCP connection — deliberately not REST/HTTP between agent and
server (see the project spec's Component 2), and not a delimiter-based
framing (e.g. newline-terminated), since JSON payloads can legally contain
any byte a delimiter might use.

## Frame layout

All multi-byte integers are big-endian (network byte order, via `htonl`/`ntohl`).

```
+-------------+-------------+------------------+------------------+
| magic       | type        | payload_len      | payload          |
| uint32_t    | uint8_t     | uint32_t         | payload_len bytes|
| 4 bytes     | 1 byte      | 4 bytes          | UTF-8 JSON       |
+-------------+-------------+------------------+------------------+
```

- **magic**: `0x444E4949` (ASCII "DNII"). A frame with any other value is
  treated as a protocol violation and the connection is dropped
  (`EpollServer::try_dispatch_frames`).
- **type**: one of `MessageType`:
  | Value | Name | Direction | Status |
  |---|---|---|---|
  | 1 | `kTelemetry` | agent → server | implemented, primary message type |
  | 2 | `kHeartbeat` | agent → server | reserved, not yet sent by the agent |
  | 3 | `kAck` | server → agent | reserved, not yet sent by the server |
  | 4 | `kRegister` | agent → server | reserved, not yet used (nodes currently register implicitly on first `kTelemetry`) |
  | 5 | `kRegisterAck` | server → agent | reserved |
- **payload_len**: length of the JSON payload in bytes. Capped at
  `kMaxPayloadSize` (16 MiB) — an oversized claimed length is treated the
  same as a bad magic (connection dropped), which bounds how much a
  misbehaving/hostile peer can make the server buffer.
- **payload**: a UTF-8 JSON document, `nlohmann::json::dump()`'d on encode.

Framing is stream-oriented: `EpollServer` appends all bytes read from a
socket to a per-connection buffer and repeatedly tries to parse a complete
header + payload out of the front of it (`try_dispatch_frames`), so it
correctly handles a payload split across multiple `recv()` calls, or
multiple frames coalesced into a single `recv()`.

## `kTelemetry` payload schema

Serialized from `dniip::TelemetrySample` (`common/telemetry.hpp`).

```json
{
  "node_id": "node-b",
  "timestamp": "2026-09-27T14:00:00Z",
  "cpu_usage": 78.0,
  "memory_usage": 82.0,
  "packet_loss": 35.0,
  "rtt_ms": 320.0,
  "gateway_reachable": true,
  "dns_resolvable": true,
  "tcp_retransmits": 22,
  "interfaces": [
    {
      "name": "eth0",
      "up": true,
      "rx_bytes": 123456789,
      "tx_bytes": 98765432,
      "rx_errors": 0,
      "tx_errors": 0,
      "rx_dropped": 0,
      "tx_dropped": 0
    }
  ]
}
```

See `docs/samples/telemetry_message.json` for this exact example, captured
from a real seeded run.

| Field | Type | Notes |
|---|---|---|
| `node_id` | string | `gethostname()` by default |
| `timestamp` | string | ISO-8601 UTC, `%Y-%m-%dT%H:%M:%SZ` |
| `cpu_usage` | number | percent, 0–100, from `/proc/stat` delta |
| `memory_usage` | number | percent, 0–100, `(MemTotal - MemAvailable) / MemTotal` from `/proc/meminfo` |
| `packet_loss` | number | percent, 0–100, from a single ICMP echo probe to the default gateway per cycle |
| `rtt_ms` | number | round-trip time of that probe |
| `gateway_reachable` | bool | whether the probe got a reply |
| `dns_resolvable` | bool | whether `getaddrinfo()` resolved a canary hostname |
| `tcp_retransmits` | number | retransmitted TCP segments in this cycle, from the delta of `/proc/net/snmp`'s cumulative `Tcp: RetransSegs` counter (`agent/collectors/tcp_retransmit_collector.hpp`) — not `/proc/net/tcp`, which lists per-connection state but has no retransmit column |
| `interfaces` | array | one entry per non-loopback interface from `/proc/net/dev`, `up` from `/sys/class/net/<if>/operstate` |

Older/partial payloads deserialize gracefully: `from_json` only requires
`node_id`, `timestamp`, `cpu_usage`, `memory_usage`, `packet_loss`, `rtt_ms`,
`gateway_reachable`; `dns_resolvable`, `tcp_retransmits`, and `interfaces`
are read if present and defaulted otherwise (see `common/telemetry.hpp`).

## Connection lifecycle

- The agent opens one persistent TCP connection to the server
  (`agent/networking/tcp_client.hpp`) and reuses it across collection
  cycles; on send failure it disconnects and reconnects on the next cycle.
- The server has no explicit handshake/registration step today — a node is
  implicitly registered (`upsert_node`) on its first `kTelemetry` frame.
- There's no application-level heartbeat yet (`kHeartbeat`/`kAck` are
  defined but unused); liveness is inferred from TCP connection state
  (`dniip_connected_agents` in Prometheus) and `last_seen` in the `nodes`
  table.

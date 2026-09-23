"""
agent/config.py
Agent-level configuration. All tunables live here so the agent
can be reconfigured without touching collector logic.
"""
from __future__ import annotations

import os
import socket
from dataclasses import dataclass, field


@dataclass
class AgentConfig:
    # ---- identity ----
    node_id: str = field(
        default_factory=lambda: os.environ.get("DNIIP_NODE_ID", socket.gethostname())
    )
    node_tags: dict[str, str] = field(
        default_factory=lambda: {
            "env": os.environ.get("DNIIP_ENV", "production"),
            "region": os.environ.get("DNIIP_REGION", "unknown"),
        }
    )

    # ---- server ----
    server_url: str = field(
        default_factory=lambda: os.environ.get(
            "DNIIP_SERVER_URL", "http://localhost:8000"
        )
    )
    api_key: str = field(
        default_factory=lambda: os.environ.get("DNIIP_API_KEY", "")
    )

    # ---- collection intervals (seconds) ----
    metrics_interval: int = int(os.environ.get("DNIIP_METRICS_INTERVAL", "30"))
    logs_interval: int    = int(os.environ.get("DNIIP_LOGS_INTERVAL",   "30"))
    network_interval: int = int(os.environ.get("DNIIP_NETWORK_INTERVAL","30"))
    diag_interval: int    = int(os.environ.get("DNIIP_DIAG_INTERVAL",   "60"))

    # ---- diagnostics targets ----
    ping_targets: list[str] = field(
        default_factory=lambda: os.environ.get(
            "DNIIP_PING_TARGETS", "8.8.8.8,1.1.1.1"
        ).split(",")
    )
    traceroute_targets: list[str] = field(
        default_factory=lambda: os.environ.get(
            "DNIIP_TRACEROUTE_TARGETS", "8.8.8.8"
        ).split(",")
    )

    # ---- limits ----
    dmesg_lines: int    = int(os.environ.get("DNIIP_DMESG_LINES",    "200"))
    journal_lines: int  = int(os.environ.get("DNIIP_JOURNAL_LINES",  "200"))
    send_timeout: float = float(os.environ.get("DNIIP_SEND_TIMEOUT", "10"))
    max_retries: int    = int(os.environ.get("DNIIP_MAX_RETRIES",     "3"))


# Singleton used throughout the agent process
CONFIG = AgentConfig()

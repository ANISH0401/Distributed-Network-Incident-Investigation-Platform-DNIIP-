export interface InterfaceStats {
  name: string;
  up: boolean;
  rx_bytes: number;
  tx_bytes: number;
  rx_errors: number;
  tx_errors: number;
  rx_dropped: number;
  tx_dropped: number;
}

export interface TelemetrySample {
  node_id: string;
  timestamp: string;
  cpu_usage: number;
  memory_usage: number;
  packet_loss: number;
  rtt_ms: number;
  gateway_reachable: boolean;
  dns_resolvable: boolean;
  tcp_retransmits: number;
  interfaces: InterfaceStats[];
}

export interface NodeSummary {
  node_id: string;
  first_seen: string;
  last_seen: string;
  latest_telemetry: TelemetrySample | null;
}

export type Severity = 'LOW' | 'MEDIUM' | 'HIGH' | 'CRITICAL';
export type IncidentStatus = 'ACTIVE' | 'RESOLVED';

export interface Incident {
  incident_id: string;
  severity: Severity;
  category: string;
  root_cause: string;
  affected_nodes: string[];
  symptoms: string[];
  confidence: number;
  created_at: string;
  status: IncidentStatus;
}

export interface IncidentReport extends Incident {
  report_text: string;
  generated_at: string;
}

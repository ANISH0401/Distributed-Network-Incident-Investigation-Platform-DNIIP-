import type { Incident, IncidentReport, IncidentStatus, NodeSummary, TelemetrySample } from './types';

// dniip-api's base URL. Overridable at build time via VITE_API_BASE for
// deployments where the API isn't on localhost:8080 (see docker-compose).
const API_BASE = import.meta.env.VITE_API_BASE ?? 'http://localhost:8080';

async function getJson<T>(path: string): Promise<T> {
  const res = await fetch(`${API_BASE}${path}`);
  if (!res.ok) {
    throw new Error(`GET ${path} failed: ${res.status} ${res.statusText}`);
  }
  return res.json() as Promise<T>;
}

export const api = {
  listNodes: () => getJson<NodeSummary[]>('/api/nodes'),

  nodeTelemetryHistory: (nodeId: string, limit = 50) =>
    getJson<TelemetrySample[]>(`/api/nodes/${encodeURIComponent(nodeId)}/telemetry?limit=${limit}`),

  listIncidents: (status?: IncidentStatus, limit = 100) => {
    const params = new URLSearchParams({ limit: String(limit) });
    if (status) params.set('status', status.toLowerCase());
    return getJson<Incident[]>(`/api/incidents?${params.toString()}`);
  },

  incidentReport: (incidentId: string) =>
    getJson<IncidentReport>(`/api/incidents/${encodeURIComponent(incidentId)}/report`),
};

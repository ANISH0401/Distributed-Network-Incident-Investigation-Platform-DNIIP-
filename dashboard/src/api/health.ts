import type { TelemetrySample } from './types';

// Mirrors server/collector/telemetry_collector.hpp's compute_health_score
// so the number shown in the dashboard always matches what Prometheus/
// Grafana would show for the same sample.
export function computeHealthScore(t: TelemetrySample): number {
  let score = 100;
  score -= t.packet_loss;
  if (!t.gateway_reachable) score -= 30;
  if (!t.dns_resolvable) score -= 15;
  for (const iface of t.interfaces) {
    if (!iface.up) score -= 20;
  }
  return Math.max(0, score);
}

export function healthColor(score: number): 'success' | 'warning' | 'error' {
  if (score >= 80) return 'success';
  if (score >= 50) return 'warning';
  return 'error';
}

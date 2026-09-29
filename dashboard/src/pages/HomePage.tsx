import { Link as RouterLink } from 'react-router-dom';
import Grid from '@mui/material/Grid';
import Card from '@mui/material/Card';
import CardContent from '@mui/material/CardContent';
import Typography from '@mui/material/Typography';
import List from '@mui/material/List';
import ListItemButton from '@mui/material/ListItemButton';
import ListItemText from '@mui/material/ListItemText';
import Chip from '@mui/material/Chip';
import CircularProgress from '@mui/material/CircularProgress';
import Alert from '@mui/material/Alert';
import Paper from '@mui/material/Paper';

import { api } from '../api/client';
import { usePolling } from '../api/usePolling';
import { computeHealthScore, healthColor } from '../api/health';
import SeverityChip from '../components/SeverityChip';

function StatCard({ label, value, color }: { label: string; value: string; color?: string }) {
  return (
    <Card>
      <CardContent>
        <Typography variant="overline" color="text.secondary">
          {label}
        </Typography>
        <Typography variant="h3" sx={{ color }}>
          {value}
        </Typography>
      </CardContent>
    </Card>
  );
}

export default function HomePage() {
  const nodes = usePolling(() => api.listNodes(), 15000);
  const incidents = usePolling(() => api.listIncidents('ACTIVE', 20), 15000);

  if (nodes.error || incidents.error) {
    return <Alert severity="error">{nodes.error ?? incidents.error}</Alert>;
  }
  if (nodes.loading && !nodes.data) {
    return <CircularProgress />;
  }

  const nodeList = nodes.data ?? [];
  const withTelemetry = nodeList.filter((n) => n.latest_telemetry);
  const healthyCount = withTelemetry.filter(
    (n) => computeHealthScore(n.latest_telemetry!) >= 80,
  ).length;
  const avgHealth = withTelemetry.length
    ? Math.round(
        withTelemetry.reduce((sum, n) => sum + computeHealthScore(n.latest_telemetry!), 0) /
          withTelemetry.length,
      )
    : 0;

  const activeIncidents = incidents.data ?? [];

  return (
    <>
      <Typography variant="h4" gutterBottom>
        Overview
      </Typography>

      <Grid container spacing={2} sx={{ mb: 3 }}>
        <Grid size={{ xs: 12, sm: 4 }}>
          <StatCard label="Monitored Nodes" value={String(nodeList.length)} />
        </Grid>
        <Grid size={{ xs: 12, sm: 4 }}>
          <StatCard
            label="Healthy Nodes"
            value={`${healthyCount} / ${withTelemetry.length}`}
            color={healthColor(avgHealth) + '.main'}
          />
        </Grid>
        <Grid size={{ xs: 12, sm: 4 }}>
          <StatCard
            label="Active Incidents"
            value={String(activeIncidents.length)}
            color={activeIncidents.length > 0 ? 'error.main' : 'success.main'}
          />
        </Grid>
      </Grid>

      <Grid container spacing={2}>
        <Grid size={{ xs: 12, md: 6 }}>
          <Paper sx={{ p: 2 }}>
            <Typography variant="h6" gutterBottom>
              Node Health
            </Typography>
            <List dense>
              {nodeList.map((n) => {
                const score = n.latest_telemetry ? computeHealthScore(n.latest_telemetry) : null;
                return (
                  <ListItemButton key={n.node_id} component={RouterLink} to={`/nodes/${n.node_id}`}>
                    <ListItemText
                      primary={n.node_id}
                      secondary={n.latest_telemetry ? `Last seen ${n.last_seen}` : 'No telemetry yet'}
                    />
                    {score !== null && (
                      <Chip
                        label={`${Math.round(score)}`}
                        color={healthColor(score)}
                        size="small"
                      />
                    )}
                  </ListItemButton>
                );
              })}
              {nodeList.length === 0 && (
                <Typography color="text.secondary" sx={{ p: 2 }}>
                  No nodes reporting yet.
                </Typography>
              )}
            </List>
          </Paper>
        </Grid>

        <Grid size={{ xs: 12, md: 6 }}>
          <Paper sx={{ p: 2 }}>
            <Typography variant="h6" gutterBottom>
              Active Incidents
            </Typography>
            <List dense>
              {activeIncidents.map((inc) => (
                <ListItemButton key={inc.incident_id} component={RouterLink} to={`/reports/${inc.incident_id}`}>
                  <ListItemText
                    primary={`${inc.incident_id} — ${inc.root_cause}`}
                    secondary={`Nodes: ${inc.affected_nodes.join(', ')}`}
                  />
                  <SeverityChip severity={inc.severity} />
                </ListItemButton>
              ))}
              {activeIncidents.length === 0 && (
                <Typography color="text.secondary" sx={{ p: 2 }}>
                  No active incidents. All clear.
                </Typography>
              )}
            </List>
          </Paper>
        </Grid>
      </Grid>
    </>
  );
}

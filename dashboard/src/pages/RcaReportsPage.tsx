import { Link as RouterLink } from 'react-router-dom';
import Typography from '@mui/material/Typography';
import List from '@mui/material/List';
import ListItemButton from '@mui/material/ListItemButton';
import ListItemText from '@mui/material/ListItemText';
import Paper from '@mui/material/Paper';
import CircularProgress from '@mui/material/CircularProgress';
import Alert from '@mui/material/Alert';

import { api } from '../api/client';
import { usePolling } from '../api/usePolling';
import SeverityChip from '../components/SeverityChip';

export default function RcaReportsPage() {
  const incidents = usePolling(() => api.listIncidents(undefined, 200), 15000);

  if (incidents.error) return <Alert severity="error">{incidents.error}</Alert>;
  if (incidents.loading && !incidents.data) return <CircularProgress />;

  return (
    <>
      <Typography variant="h4" gutterBottom>
        RCA Reports
      </Typography>
      <Paper>
        <List>
          {(incidents.data ?? []).map((inc) => (
            <ListItemButton key={inc.incident_id} component={RouterLink} to={`/reports/${inc.incident_id}`}>
              <ListItemText
                primary={`${inc.incident_id} — ${inc.root_cause}`}
                secondary={`${inc.status} · Nodes: ${inc.affected_nodes.join(', ')} · ${inc.created_at}`}
              />
              <SeverityChip severity={inc.severity} />
            </ListItemButton>
          ))}
          {(incidents.data ?? []).length === 0 && (
            <Typography color="text.secondary" sx={{ p: 2 }}>
              No incidents recorded yet.
            </Typography>
          )}
        </List>
      </Paper>
    </>
  );
}

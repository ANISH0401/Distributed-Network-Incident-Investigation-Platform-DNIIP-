import { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import Typography from '@mui/material/Typography';
import Tabs from '@mui/material/Tabs';
import Tab from '@mui/material/Tab';
import Table from '@mui/material/Table';
import TableBody from '@mui/material/TableBody';
import TableCell from '@mui/material/TableCell';
import TableContainer from '@mui/material/TableContainer';
import TableHead from '@mui/material/TableHead';
import TableRow from '@mui/material/TableRow';
import Paper from '@mui/material/Paper';
import CircularProgress from '@mui/material/CircularProgress';
import Alert from '@mui/material/Alert';

import { api } from '../api/client';
import { usePolling } from '../api/usePolling';
import SeverityChip from '../components/SeverityChip';
import type { IncidentStatus } from '../api/types';

export default function IncidentsPage() {
  const [tab, setTab] = useState<IncidentStatus>('ACTIVE');
  const navigate = useNavigate();
  const incidents = usePolling(() => api.listIncidents(tab, 200), 15000, [tab]);

  return (
    <>
      <Typography variant="h4" gutterBottom>
        Incidents
      </Typography>

      <Tabs value={tab} onChange={(_, v) => setTab(v)} sx={{ mb: 2 }}>
        <Tab label="Active" value="ACTIVE" />
        <Tab label="Resolved" value="RESOLVED" />
      </Tabs>

      {incidents.error && <Alert severity="error">{incidents.error}</Alert>}
      {incidents.loading && !incidents.data ? (
        <CircularProgress />
      ) : (
        <TableContainer component={Paper}>
          <Table>
            <TableHead>
              <TableRow>
                <TableCell>Incident ID</TableCell>
                <TableCell>Severity</TableCell>
                <TableCell>Root Cause</TableCell>
                <TableCell>Affected Nodes</TableCell>
                <TableCell align="right">Confidence</TableCell>
                <TableCell>Created At</TableCell>
              </TableRow>
            </TableHead>
            <TableBody>
              {(incidents.data ?? []).map((inc) => (
                <TableRow
                  key={inc.incident_id}
                  hover
                  sx={{ cursor: 'pointer' }}
                  onClick={() => navigate(`/reports/${inc.incident_id}`)}
                >
                  <TableCell>{inc.incident_id}</TableCell>
                  <TableCell>
                    <SeverityChip severity={inc.severity} />
                  </TableCell>
                  <TableCell>{inc.root_cause}</TableCell>
                  <TableCell>{inc.affected_nodes.join(', ')}</TableCell>
                  <TableCell align="right">{Math.round(inc.confidence * 100)}%</TableCell>
                  <TableCell>{inc.created_at}</TableCell>
                </TableRow>
              ))}
              {(incidents.data ?? []).length === 0 && (
                <TableRow>
                  <TableCell colSpan={6} align="center">
                    No {tab.toLowerCase()} incidents.
                  </TableCell>
                </TableRow>
              )}
            </TableBody>
          </Table>
        </TableContainer>
      )}
    </>
  );
}

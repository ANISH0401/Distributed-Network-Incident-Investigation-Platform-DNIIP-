import { useParams, Link as RouterLink } from 'react-router-dom';
import Typography from '@mui/material/Typography';
import Breadcrumbs from '@mui/material/Breadcrumbs';
import Link from '@mui/material/Link';
import Grid from '@mui/material/Grid';
import Paper from '@mui/material/Paper';
import Table from '@mui/material/Table';
import TableBody from '@mui/material/TableBody';
import TableCell from '@mui/material/TableCell';
import TableHead from '@mui/material/TableHead';
import TableRow from '@mui/material/TableRow';
import Chip from '@mui/material/Chip';
import CircularProgress from '@mui/material/CircularProgress';
import Alert from '@mui/material/Alert';
import { Line } from 'react-chartjs-2';
import {
  Chart as ChartJS,
  CategoryScale,
  LinearScale,
  PointElement,
  LineElement,
  Tooltip,
  Legend,
} from 'chart.js';

import { api } from '../api/client';
import { usePolling } from '../api/usePolling';

ChartJS.register(CategoryScale, LinearScale, PointElement, LineElement, Tooltip, Legend);

export default function NodeDetailPage() {
  const { nodeId } = useParams<{ nodeId: string }>();
  const history = usePolling(() => api.nodeTelemetryHistory(nodeId!, 50), 15000, [nodeId]);

  if (history.error) return <Alert severity="error">{history.error}</Alert>;
  if (history.loading && !history.data) return <CircularProgress />;

  // API returns newest-first; reverse for a left-to-right time axis.
  const samples = [...(history.data ?? [])].reverse();
  const labels = samples.map((s) => s.timestamp.slice(11, 19));
  const latest = samples[samples.length - 1];

  const chartOptions = {
    responsive: true,
    animation: false as const,
    scales: { y: { beginAtZero: true, max: 100 } },
  };

  return (
    <>
      <Breadcrumbs sx={{ mb: 2 }}>
        <Link component={RouterLink} to="/nodes">
          Nodes
        </Link>
        <Typography color="text.primary">{nodeId}</Typography>
      </Breadcrumbs>

      <Typography variant="h4" gutterBottom>
        {nodeId}
      </Typography>

      <Grid container spacing={2} sx={{ mb: 3 }}>
        <Grid size={{ xs: 12, md: 6 }}>
          <Paper sx={{ p: 2 }}>
            <Typography variant="h6" gutterBottom>
              CPU %
            </Typography>
            <Line
              options={chartOptions}
              data={{
                labels,
                datasets: [
                  { label: 'CPU %', data: samples.map((s) => s.cpu_usage), borderColor: '#1976d2', tension: 0.3 },
                ],
              }}
            />
          </Paper>
        </Grid>
        <Grid size={{ xs: 12, md: 6 }}>
          <Paper sx={{ p: 2 }}>
            <Typography variant="h6" gutterBottom>
              Memory %
            </Typography>
            <Line
              options={chartOptions}
              data={{
                labels,
                datasets: [
                  { label: 'Memory %', data: samples.map((s) => s.memory_usage), borderColor: '#9c27b0', tension: 0.3 },
                ],
              }}
            />
          </Paper>
        </Grid>
      </Grid>

      <Grid container spacing={2}>
        <Grid size={{ xs: 12, md: 6 }}>
          <Paper sx={{ p: 2 }}>
            <Typography variant="h6" gutterBottom>
              Interfaces
            </Typography>
            <Table size="small">
              <TableHead>
                <TableRow>
                  <TableCell>Name</TableCell>
                  <TableCell>Status</TableCell>
                  <TableCell align="right">RX bytes</TableCell>
                  <TableCell align="right">TX bytes</TableCell>
                </TableRow>
              </TableHead>
              <TableBody>
                {(latest?.interfaces ?? []).map((iface) => (
                  <TableRow key={iface.name}>
                    <TableCell>{iface.name}</TableCell>
                    <TableCell>
                      <Chip label={iface.up ? 'up' : 'down'} color={iface.up ? 'success' : 'error'} size="small" />
                    </TableCell>
                    <TableCell align="right">{iface.rx_bytes.toLocaleString()}</TableCell>
                    <TableCell align="right">{iface.tx_bytes.toLocaleString()}</TableCell>
                  </TableRow>
                ))}
                {!latest && (
                  <TableRow>
                    <TableCell colSpan={4}>No telemetry yet</TableCell>
                  </TableRow>
                )}
              </TableBody>
            </Table>
          </Paper>
        </Grid>

        <Grid size={{ xs: 12, md: 6 }}>
          <Paper sx={{ p: 2 }}>
            <Typography variant="h6" gutterBottom>
              Routing / Connectivity
            </Typography>
            {latest ? (
              <Table size="small">
                <TableBody>
                  <TableRow>
                    <TableCell>Gateway reachable</TableCell>
                    <TableCell>
                      <Chip
                        label={latest.gateway_reachable ? 'yes' : 'no'}
                        color={latest.gateway_reachable ? 'success' : 'error'}
                        size="small"
                      />
                    </TableCell>
                  </TableRow>
                  <TableRow>
                    <TableCell>DNS resolvable</TableCell>
                    <TableCell>
                      <Chip
                        label={latest.dns_resolvable ? 'yes' : 'no'}
                        color={latest.dns_resolvable ? 'success' : 'error'}
                        size="small"
                      />
                    </TableCell>
                  </TableRow>
                  <TableRow>
                    <TableCell>RTT (ms)</TableCell>
                    <TableCell>{latest.rtt_ms.toFixed(1)}</TableCell>
                  </TableRow>
                  <TableRow>
                    <TableCell>Packet loss (%)</TableCell>
                    <TableCell>{latest.packet_loss.toFixed(1)}</TableCell>
                  </TableRow>
                  <TableRow>
                    <TableCell>TCP retransmits</TableCell>
                    <TableCell>{latest.tcp_retransmits}</TableCell>
                  </TableRow>
                </TableBody>
              </Table>
            ) : (
              <Typography color="text.secondary">No telemetry yet</Typography>
            )}
          </Paper>
        </Grid>
      </Grid>
    </>
  );
}

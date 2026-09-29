import { useNavigate } from 'react-router-dom';
import Typography from '@mui/material/Typography';
import Table from '@mui/material/Table';
import TableBody from '@mui/material/TableBody';
import TableCell from '@mui/material/TableCell';
import TableContainer from '@mui/material/TableContainer';
import TableHead from '@mui/material/TableHead';
import TableRow from '@mui/material/TableRow';
import Paper from '@mui/material/Paper';
import Chip from '@mui/material/Chip';
import CircularProgress from '@mui/material/CircularProgress';
import Alert from '@mui/material/Alert';

import { api } from '../api/client';
import { usePolling } from '../api/usePolling';
import { computeHealthScore, healthColor } from '../api/health';

export default function NodesPage() {
  const nodes = usePolling(() => api.listNodes(), 15000);
  const navigate = useNavigate();

  if (nodes.error) return <Alert severity="error">{nodes.error}</Alert>;
  if (nodes.loading && !nodes.data) return <CircularProgress />;

  return (
    <>
      <Typography variant="h4" gutterBottom>
        Nodes
      </Typography>
      <TableContainer component={Paper}>
        <Table>
          <TableHead>
            <TableRow>
              <TableCell>Node ID</TableCell>
              <TableCell>Health</TableCell>
              <TableCell align="right">CPU %</TableCell>
              <TableCell align="right">Memory %</TableCell>
              <TableCell align="right">Packet Loss %</TableCell>
              <TableCell>Gateway</TableCell>
              <TableCell>DNS</TableCell>
              <TableCell>Last Seen</TableCell>
            </TableRow>
          </TableHead>
          <TableBody>
            {(nodes.data ?? []).map((n) => {
              const t = n.latest_telemetry;
              const score = t ? computeHealthScore(t) : null;
              return (
                <TableRow
                  key={n.node_id}
                  hover
                  sx={{ cursor: 'pointer' }}
                  onClick={() => navigate(`/nodes/${n.node_id}`)}
                >
                  <TableCell>{n.node_id}</TableCell>
                  <TableCell>
                    {score !== null ? (
                      <Chip label={Math.round(score)} color={healthColor(score)} size="small" />
                    ) : (
                      '—'
                    )}
                  </TableCell>
                  <TableCell align="right">{t ? t.cpu_usage.toFixed(1) : '—'}</TableCell>
                  <TableCell align="right">{t ? t.memory_usage.toFixed(1) : '—'}</TableCell>
                  <TableCell align="right">{t ? t.packet_loss.toFixed(1) : '—'}</TableCell>
                  <TableCell>
                    {t ? (
                      <Chip
                        label={t.gateway_reachable ? 'reachable' : 'unreachable'}
                        color={t.gateway_reachable ? 'success' : 'error'}
                        size="small"
                      />
                    ) : (
                      '—'
                    )}
                  </TableCell>
                  <TableCell>
                    {t ? (
                      <Chip
                        label={t.dns_resolvable ? 'ok' : 'failing'}
                        color={t.dns_resolvable ? 'success' : 'error'}
                        size="small"
                      />
                    ) : (
                      '—'
                    )}
                  </TableCell>
                  <TableCell>{n.last_seen}</TableCell>
                </TableRow>
              );
            })}
          </TableBody>
        </Table>
      </TableContainer>
    </>
  );
}

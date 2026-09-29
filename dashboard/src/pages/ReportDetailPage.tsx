import { useParams, Link as RouterLink } from 'react-router-dom';
import Typography from '@mui/material/Typography';
import Breadcrumbs from '@mui/material/Breadcrumbs';
import Link from '@mui/material/Link';
import Paper from '@mui/material/Paper';
import Box from '@mui/material/Box';
import Stack from '@mui/material/Stack';
import Button from '@mui/material/Button';
import Divider from '@mui/material/Divider';
import CircularProgress from '@mui/material/CircularProgress';
import Alert from '@mui/material/Alert';
import DownloadIcon from '@mui/icons-material/Download';
import PictureAsPdfIcon from '@mui/icons-material/PictureAsPdf';

import { api } from '../api/client';
import { usePolling } from '../api/usePolling';
import SeverityChip from '../components/SeverityChip';

function downloadBlob(filename: string, content: string, type: string) {
  const blob = new Blob([content], { type });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  URL.revokeObjectURL(url);
}

export default function ReportDetailPage() {
  const { incidentId } = useParams<{ incidentId: string }>();
  const report = usePolling(() => api.incidentReport(incidentId!), 30000, [incidentId]);

  if (report.error) return <Alert severity="error">{report.error}</Alert>;
  if (report.loading && !report.data) return <CircularProgress />;
  const r = report.data!;

  const exportJson = () => {
    downloadBlob(`${r.incident_id}.json`, JSON.stringify(r, null, 2), 'application/json');
  };

  // "Download PDF" uses the browser's own print-to-PDF rather than pulling
  // in a PDF-generation library: print.css below hides the nav chrome so
  // "Save as PDF" from the print dialog produces a clean report document.
  const downloadPdf = () => window.print();

  return (
    <>
      <Breadcrumbs sx={{ mb: 2 }} className="no-print">
        <Link component={RouterLink} to="/reports">
          RCA Reports
        </Link>
        <Typography color="text.primary">{r.incident_id}</Typography>
      </Breadcrumbs>

      <Stack direction="row" justifyContent="space-between" alignItems="center" sx={{ mb: 2 }}>
        <Typography variant="h4">{r.incident_id}</Typography>
        <Stack direction="row" spacing={1} className="no-print">
          <Button startIcon={<PictureAsPdfIcon />} variant="outlined" onClick={downloadPdf}>
            Download PDF
          </Button>
          <Button startIcon={<DownloadIcon />} variant="outlined" onClick={exportJson}>
            Export JSON
          </Button>
        </Stack>
      </Stack>

      <Paper sx={{ p: 3 }}>
        <Stack direction="row" spacing={2} alignItems="center" sx={{ mb: 2 }}>
          <SeverityChip severity={r.severity} />
          <Typography color="text.secondary">{r.status}</Typography>
          <Typography color="text.secondary">Confidence: {Math.round(r.confidence * 100)}%</Typography>
        </Stack>

        <Typography variant="subtitle2" color="text.secondary">
          Root Cause
        </Typography>
        <Typography variant="h6" gutterBottom>
          {r.root_cause}
        </Typography>

        <Typography variant="subtitle2" color="text.secondary">
          Affected Nodes
        </Typography>
        <Typography gutterBottom>{r.affected_nodes.join(', ')}</Typography>

        <Divider sx={{ my: 2 }} />

        <Typography variant="subtitle2" color="text.secondary" gutterBottom>
          Full Report
        </Typography>
        <Box
          component="pre"
          sx={{
            fontFamily: 'monospace',
            fontSize: '0.875rem',
            whiteSpace: 'pre-wrap',
            backgroundColor: 'action.hover',
            p: 2,
            borderRadius: 1,
          }}
        >
          {r.report_text}
        </Box>
      </Paper>
    </>
  );
}

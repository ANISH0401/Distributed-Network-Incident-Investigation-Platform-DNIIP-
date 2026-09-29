import { BrowserRouter, Routes, Route } from 'react-router-dom';
import { ThemeProvider, createTheme } from '@mui/material/styles';
import CssBaseline from '@mui/material/CssBaseline';

import Layout from './components/Layout';
import HomePage from './pages/HomePage';
import NodesPage from './pages/NodesPage';
import NodeDetailPage from './pages/NodeDetailPage';
import IncidentsPage from './pages/IncidentsPage';
import RcaReportsPage from './pages/RcaReportsPage';
import ReportDetailPage from './pages/ReportDetailPage';

const theme = createTheme({
  palette: {
    mode: 'light',
    primary: { main: '#1976d2' },
  },
});

export default function App() {
  return (
    <ThemeProvider theme={theme}>
      <CssBaseline />
      <BrowserRouter>
        <Layout>
          <Routes>
            <Route path="/" element={<HomePage />} />
            <Route path="/nodes" element={<NodesPage />} />
            <Route path="/nodes/:nodeId" element={<NodeDetailPage />} />
            <Route path="/incidents" element={<IncidentsPage />} />
            <Route path="/reports" element={<RcaReportsPage />} />
            <Route path="/reports/:incidentId" element={<ReportDetailPage />} />
          </Routes>
        </Layout>
      </BrowserRouter>
    </ThemeProvider>
  );
}

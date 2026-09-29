import Chip from '@mui/material/Chip';
import type { Severity } from '../api/types';

const COLOR_BY_SEVERITY: Record<Severity, 'success' | 'warning' | 'error' | 'default'> = {
  LOW: 'success',
  MEDIUM: 'warning',
  HIGH: 'error',
  CRITICAL: 'error',
};

export default function SeverityChip({ severity }: { severity: Severity }) {
  const isCritical = severity === 'CRITICAL';
  return (
    <Chip
      label={severity}
      color={COLOR_BY_SEVERITY[severity]}
      variant={isCritical ? 'filled' : 'outlined'}
      size="small"
      sx={isCritical ? { fontWeight: 700 } : undefined}
    />
  );
}

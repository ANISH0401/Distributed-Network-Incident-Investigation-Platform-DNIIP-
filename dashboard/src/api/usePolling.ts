import { useEffect, useRef, useState } from 'react';

interface PollingState<T> {
  data: T | null;
  error: string | null;
  loading: boolean;
}

// Polls `fetcher` immediately and then every `intervalMs`. Matches the
// agent's own telemetry cadence (30s) by default so the dashboard never
// looks "more real-time" than the data actually is.
export function usePolling<T>(fetcher: () => Promise<T>, intervalMs = 15000, deps: unknown[] = []): PollingState<T> {
  const [state, setState] = useState<PollingState<T>>({ data: null, error: null, loading: true });
  const fetcherRef = useRef(fetcher);
  fetcherRef.current = fetcher;

  useEffect(() => {
    let cancelled = false;

    async function tick() {
      try {
        const data = await fetcherRef.current();
        if (!cancelled) setState({ data, error: null, loading: false });
      } catch (e) {
        if (!cancelled) setState((prev) => ({ data: prev.data, error: (e as Error).message, loading: false }));
      }
    }

    tick();
    const id = setInterval(tick, intervalMs);
    return () => {
      cancelled = true;
      clearInterval(id);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, deps);

  return state;
}

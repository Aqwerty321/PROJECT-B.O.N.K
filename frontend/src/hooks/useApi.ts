import { useState, useEffect, useRef, useCallback } from 'react';
import type {
  VisualizationSnapshot,
  StatusResponse,
  BurnsResponse,
  BurnCounterfactualResponse,
  ConjunctionsResponse,
  TrajectoryResponse,
} from '../types/api';

const API_BASE = '';  // proxied via vite dev server

export function useSnapshot(intervalMs = 1000) {
  const [snapshot, setSnapshot] = useState<VisualizationSnapshot | null>(null);
  const [error, setError] = useState<string | null>(null);
  const abortRef = useRef<AbortController | null>(null);

  const fetchSnapshot = useCallback(async () => {
    if (abortRef.current) abortRef.current.abort();
    abortRef.current = new AbortController();
    try {
      const res = await fetch(`${API_BASE}/api/visualization/snapshot`, {
        signal: abortRef.current.signal,
      });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data: VisualizationSnapshot = await res.json();
      setSnapshot(data);
      setError(null);
    } catch (e) {
      if ((e as Error).name !== 'AbortError') {
        setError((e as Error).message);
      }
    }
  }, []);

  useEffect(() => {
    fetchSnapshot();
    const id = setInterval(fetchSnapshot, intervalMs);
    return () => {
      clearInterval(id);
      abortRef.current?.abort();
    };
  }, [fetchSnapshot, intervalMs]);

  return { snapshot, error };
}

export function useStatus(intervalMs = 2000) {
  const [status, setStatus] = useState<StatusResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const abortRef = useRef<AbortController | null>(null);

  const fetchStatus = useCallback(async () => {
    if (abortRef.current) abortRef.current.abort();
    abortRef.current = new AbortController();
    try {
      const res = await fetch(`${API_BASE}/api/status?details=1`, {
        signal: abortRef.current.signal,
      });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data: StatusResponse = await res.json();
      setStatus(data);
      setError(null);
    } catch (e) {
      if ((e as Error).name !== 'AbortError') {
        setError((e as Error).message);
      }
    }
  }, []);

  useEffect(() => {
    fetchStatus();
    const id = setInterval(fetchStatus, intervalMs);
    return () => {
      clearInterval(id);
      abortRef.current?.abort();
    };
  }, [fetchStatus, intervalMs]);

  return { status, error };
}

export function useBurns(intervalMs = 2000) {
  const [burns, setBurns] = useState<BurnsResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const abortRef = useRef<AbortController | null>(null);

  const fetchBurns = useCallback(async () => {
    if (abortRef.current) abortRef.current.abort();
    abortRef.current = new AbortController();
    try {
      const res = await fetch(`${API_BASE}/api/debug/burns`, {
        signal: abortRef.current.signal,
      });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data: BurnsResponse = await res.json();
      setBurns(data);
      setError(null);
    } catch (e) {
      if ((e as Error).name !== 'AbortError') {
        setError((e as Error).message);
      }
    }
  }, []);

  useEffect(() => {
    fetchBurns();
    const id = setInterval(fetchBurns, intervalMs);
    return () => {
      clearInterval(id);
      abortRef.current?.abort();
    };
  }, [fetchBurns, intervalMs]);

  return { burns, error };
}

export function useConjunctions(
  intervalMs = 2000,
  satelliteId?: string,
  source?: 'history' | 'predicted' | 'combined',
) {
  const [conjunctions, setConjunctions] = useState<ConjunctionsResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const abortRef = useRef<AbortController | null>(null);

  const fetchConjunctions = useCallback(async () => {
    if (abortRef.current) abortRef.current.abort();
    abortRef.current = new AbortController();
    try {
      const search = new URLSearchParams();
      if (satelliteId) search.set('satellite_id', satelliteId);
      if (source) search.set('source', source);
      const params = search.size > 0 ? `?${search.toString()}` : '';
      const res = await fetch(`${API_BASE}/api/debug/conjunctions${params}`, {
        signal: abortRef.current.signal,
      });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const data: ConjunctionsResponse = await res.json();
      setConjunctions(data);
      setError(null);
    } catch (e) {
      if ((e as Error).name !== 'AbortError') {
        setError((e as Error).message);
      }
    }
  }, [satelliteId, source]);

  useEffect(() => {
    fetchConjunctions();
    const id = setInterval(fetchConjunctions, intervalMs);
    return () => {
      clearInterval(id);
      abortRef.current?.abort();
    };
  }, [fetchConjunctions, intervalMs]);

  return { conjunctions, error };
}

export function useTrajectory(satelliteId: string | null, intervalMs = 2000) {
  const [trajectory, setTrajectory] = useState<TrajectoryResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const abortRef = useRef<AbortController | null>(null);

  useEffect(() => {
    if (!satelliteId) {
      setTrajectory(null);
      return;
    }
    const fetchTrajectory = async () => {
      if (abortRef.current) abortRef.current.abort();
      abortRef.current = new AbortController();
      try {
        const res = await fetch(`${API_BASE}/api/visualization/trajectory?satellite_id=${encodeURIComponent(satelliteId)}`, {
          signal: abortRef.current.signal,
        });
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data: TrajectoryResponse = await res.json();
        setTrajectory(data);
        setError(null);
      } catch (e) {
        if ((e as Error).name !== 'AbortError') {
          setError((e as Error).message);
        }
      }
    };
    fetchTrajectory();
    const id = setInterval(fetchTrajectory, intervalMs);
    return () => {
      clearInterval(id);
      abortRef.current?.abort();
    };
  }, [intervalMs, satelliteId]);

  return { trajectory, error };
}

export function useBurnCounterfactual(burnId: string | null) {
  const [counterfactual, setCounterfactual] = useState<BurnCounterfactualResponse | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const abortRef = useRef<AbortController | null>(null);

  useEffect(() => {
    if (!burnId) {
      setCounterfactual(null);
      setError(null);
      setLoading(false);
      abortRef.current?.abort();
      return;
    }

    const fetchCounterfactual = async () => {
      if (abortRef.current) abortRef.current.abort();
      abortRef.current = new AbortController();
      setLoading(true);
      try {
        const res = await fetch(`${API_BASE}/api/debug/burn-counterfactual?burn_id=${encodeURIComponent(burnId)}`, {
          signal: abortRef.current.signal,
        });
        if (!res.ok) {
          let detail = `HTTP ${res.status}`;
          try {
            const payload = await res.json() as { message?: string };
            if (payload?.message) detail = payload.message;
          } catch {
            // Fall back to the HTTP status text already captured above.
          }
          throw new Error(detail);
        }
        const data: BurnCounterfactualResponse = await res.json();
        setCounterfactual(data);
        setError(null);
      } catch (e) {
        if ((e as Error).name !== 'AbortError') {
          setCounterfactual(null);
          setError((e as Error).message);
        }
      } finally {
        if (!abortRef.current?.signal.aborted) {
          setLoading(false);
        }
      }
    };

    fetchCounterfactual();
    return () => {
      abortRef.current?.abort();
    };
  }, [burnId]);

  return { counterfactual, error, loading };
}

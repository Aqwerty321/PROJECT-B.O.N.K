import { useState, useEffect, useRef, useCallback } from 'react';
import type { VisualizationSnapshot, StatusResponse, BurnsResponse, ConjunctionsResponse, TrajectoryResponse } from '../types/api';

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

  useEffect(() => {
    const fetchStatus = async () => {
      try {
        const res = await fetch(`${API_BASE}/api/status?details=1`);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data: StatusResponse = await res.json();
        setStatus(data);
        setError(null);
      } catch (e) {
        setError((e as Error).message);
      }
    };

    fetchStatus();
    const id = setInterval(fetchStatus, intervalMs);
    return () => clearInterval(id);
  }, [intervalMs]);

  return { status, error };
}

export function useBurns(intervalMs = 2000) {
  const [burns, setBurns] = useState<BurnsResponse | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const fetchBurns = async () => {
      try {
        const res = await fetch(`${API_BASE}/api/debug/burns`);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data: BurnsResponse = await res.json();
        setBurns(data);
        setError(null);
      } catch (e) {
        setError((e as Error).message);
      }
    };
    fetchBurns();
    const id = setInterval(fetchBurns, intervalMs);
    return () => clearInterval(id);
  }, [intervalMs]);

  return { burns, error };
}

export function useConjunctions(intervalMs = 2000, satelliteId?: string) {
  const [conjunctions, setConjunctions] = useState<ConjunctionsResponse | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    const fetchConjunctions = async () => {
      try {
        const params = satelliteId ? `?satellite_id=${encodeURIComponent(satelliteId)}` : '';
        const res = await fetch(`${API_BASE}/api/debug/conjunctions${params}`);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data: ConjunctionsResponse = await res.json();
        setConjunctions(data);
        setError(null);
      } catch (e) {
        setError((e as Error).message);
      }
    };
    fetchConjunctions();
    const id = setInterval(fetchConjunctions, intervalMs);
    return () => clearInterval(id);
  }, [intervalMs, satelliteId]);

  return { conjunctions, error };
}

export function useTrajectory(satelliteId: string | null, intervalMs = 2000) {
  const [trajectory, setTrajectory] = useState<TrajectoryResponse | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!satelliteId) {
      setTrajectory(null);
      return;
    }
    const fetchTrajectory = async () => {
      try {
        const res = await fetch(`${API_BASE}/api/visualization/trajectory?satellite_id=${encodeURIComponent(satelliteId)}`);
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        const data: TrajectoryResponse = await res.json();
        setTrajectory(data);
        setError(null);
      } catch (e) {
        setError((e as Error).message);
      }
    };
    fetchTrajectory();
    const id = setInterval(fetchTrajectory, intervalMs);
    return () => clearInterval(id);
  }, [intervalMs, satelliteId]);

  return { trajectory, error };
}

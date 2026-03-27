import React, { useState, useEffect } from 'react';
import type { StatusResponse } from '../types/api';
import { formatUptime } from '../utils/geo';
import { theme } from '../styles/theme';

interface Props {
  status: StatusResponse | null;
  apiError: string | null;
  snapshotTimestamp?: string;
  debrisCount?: number;
  satCount?: number;
}

interface MetricRowProps {
  label: string;
  value: React.ReactNode;
  highlight?: boolean;
  warn?: boolean;
}

const metricRowStyle: React.CSSProperties = {
  display: 'flex',
  justifyContent: 'space-between',
  alignItems: 'center',
  padding: '6px 0',
  borderBottom: '1px solid rgba(255,255,255,0.04)',
};

const metricLabelStyle: React.CSSProperties = {
  fontSize: '11px',
  color: theme.colors.textDim,
  fontFamily: theme.font.mono,
};

const sectionLabelStyle: React.CSSProperties = {
  fontSize: '11px',
  color: theme.colors.textMuted,
  textTransform: 'uppercase',
  letterSpacing: '0.1em',
  marginBottom: '4px',
  fontFamily: theme.font.mono,
};

function MetricRow({ label, value, highlight, warn }: MetricRowProps) {
  return (
    <div style={metricRowStyle}>
      <span style={metricLabelStyle}>{label}</span>
      <span style={{
        fontSize: '12px',
        fontFamily: theme.font.mono,
        color: warn ? theme.colors.critical : highlight ? theme.colors.accent : theme.colors.text,
        fontWeight: highlight || warn ? 600 : 400,
        transition: 'color 0.3s ease',
      }}>
        {value}
      </span>
    </div>
  );
}

// Animated dots for connecting state
function AnimatedDots() {
  const [dotCount, setDotCount] = useState(1);
  useEffect(() => {
    const interval = setInterval(() => {
      setDotCount(prev => (prev % 3) + 1);
    }, 400);
    return () => clearInterval(interval);
  }, []);
  return <span>{'.'.repeat(dotCount)}</span>;
}

// Pulsing status dot
function StatusDot({ isNominal }: { isNominal: boolean }) {
  return (
    <span style={{
      display: 'inline-block',
      width: '8px',
      height: '8px',
      borderRadius: '50%',
      background: isNominal ? theme.colors.accent : theme.colors.critical,
      boxShadow: isNominal
        ? `0 0 4px ${theme.colors.accent}, 0 0 8px ${theme.colors.accent}`
        : `0 0 4px ${theme.colors.critical}`,
      animation: isNominal ? 'dotPulse 2s ease-in-out infinite' : 'none',
      marginRight: '6px',
      verticalAlign: 'middle',
    }} />
  );
}

export const StatusPanel = React.memo(function StatusPanel({ status, apiError, snapshotTimestamp, debrisCount, satCount }: Props) {
  if (apiError) {
    return (
      <div style={{ padding: '12px' }}>
        <div style={{ color: theme.colors.critical, fontSize: '12px', marginBottom: '6px', fontFamily: theme.font.mono }}>
          Backend Offline
        </div>
        <div style={{ color: theme.colors.textDim, fontSize: '11px', wordBreak: 'break-all', fontFamily: theme.font.mono }}>
          {apiError}
        </div>
        <div style={{ color: theme.colors.textMuted, fontSize: '10px', marginTop: '8px', fontFamily: theme.font.mono }}>
          Ensure backend is running:<br />
          <code style={{
            color: '#94a3b8',
            fontSize: '9px',
            display: 'block',
            marginTop: '4px',
            lineHeight: '1.6',
            wordBreak: 'break-all',
            fontFamily: theme.font.mono,
          }}>
            PROJECTBONK_CORS_ENABLE=true PROJECTBONK_CORS_ALLOW_ORIGIN=http://localhost:5173 ./build/ProjectBONK
          </code>
        </div>
      </div>
    );
  }

  if (!status) {
    return (
      <div style={{
        padding: '18px',
        color: theme.colors.textDim,
        fontSize: '14px',
        fontFamily: theme.font.mono,
        letterSpacing: '0.08em',
      }}>
        ESTABLISHING LINK<AnimatedDots />
      </div>
    );
  }

  const metrics = status.internal_metrics;
  const isNominal = status.status === 'NOMINAL';

  return (
    <div style={{ padding: '14px 16px', display: 'flex', flexDirection: 'column', gap: '2px' }}>
      <div style={{
        display: 'flex',
        justifyContent: 'space-between',
        alignItems: 'center',
        padding: '6px 0',
        borderBottom: '1px solid rgba(255,255,255,0.04)',
      }}>
        <span style={{ fontSize: '12px', color: theme.colors.textDim, fontFamily: theme.font.mono }}>System Status</span>
        <span style={{
          fontSize: '14px',
          fontFamily: theme.font.mono,
          color: isNominal ? theme.colors.accent : theme.colors.critical,
          fontWeight: 600,
          transition: 'color 0.3s ease',
        }}>
          <StatusDot isNominal={isNominal} />
          {status.status}
        </span>
      </div>
      <MetricRow
        label="Uptime"
        value={formatUptime(status.uptime_s)}
      />
      <MetricRow
        label="Sim Ticks"
        value={status.tick_count.toLocaleString()}
        highlight
      />
      <MetricRow
        label="Satellites"
        value={satCount ?? status.internal_metrics?.satellite_count ?? '--'}
      />
      <MetricRow
        label="Debris Objects"
        value={debrisCount !== undefined ? debrisCount.toLocaleString() : (metrics?.debris_count?.toLocaleString() ?? '--')}
      />
      <MetricRow
        label="Total Objects"
        value={status.object_count.toLocaleString()}
      />

      {metrics && (
        <>
          <div style={{ height: '6px' }} />
          <div style={sectionLabelStyle}>
            Engine
          </div>
          <MetricRow
            label="Burn Queue"
            value={metrics.pending_burn_queue}
            warn={metrics.pending_burn_queue > 10}
          />
          <MetricRow
            label="Recovery Reqs"
            value={metrics.pending_recovery_requests}
          />
          <MetricRow
            label="CMD Queue Depth"
            value={`${metrics.command_queue_depth} / ${metrics.command_queue_depth_limit}`}
            warn={metrics.command_queue_depth > metrics.command_queue_depth_limit * 0.8}
          />
          <MetricRow
            label="Failed Objs / Tick"
            value={metrics.failed_objects_last_tick}
            warn={metrics.failed_objects_last_tick > 0}
          />

          {metrics.propagation_last_tick && (
            <>
              <div style={{ height: '6px' }} />
              <div style={sectionLabelStyle}>
                Ops Health
              </div>
              <MetricRow
                label="Outside Slot Box"
                value={metrics.propagation_last_tick.stationkeeping_outside_box}
                warn={metrics.propagation_last_tick.stationkeeping_outside_box > 0}
              />
              <MetricRow
                label="Upload Missed"
                value={metrics.propagation_last_tick.upload_window_missed}
                warn={metrics.propagation_last_tick.upload_window_missed > 0}
              />
              <MetricRow
                label="Worst Slot Drift"
                value={`${metrics.propagation_last_tick.stationkeeping_slot_radius_error_max_km.toFixed(2)} km`}
                warn={metrics.propagation_last_tick.stationkeeping_slot_radius_error_max_km > 10}
              />
              <MetricRow
                label="Recovery Planned"
                value={metrics.propagation_last_tick.recovery_planned}
                highlight={metrics.propagation_last_tick.recovery_planned > 0}
              />
            </>
          )}

          {metrics.command_latency_us && (
            <>
              <div style={{ height: '6px' }} />
              <div style={sectionLabelStyle}>
                Latency (last)
              </div>
              {Object.entries(metrics.command_latency_us).map(([cmd, lat]) => (
                <MetricRow
                  key={cmd}
                  label={cmd}
                  value={`${lat.execution_us_last.toLocaleString()} us`}
                  highlight={lat.execution_us_last < 50000}
                  warn={lat.execution_us_last > 100000}
                />
              ))}
            </>
          )}
        </>
      )}

      {snapshotTimestamp && (
        <>
          <div style={{ height: '6px' }} />
          <MetricRow label="Sim Time" value={snapshotTimestamp.replace('T', ' ').replace('Z', ' UTC')} />
        </>
      )}
    </div>
  );
});

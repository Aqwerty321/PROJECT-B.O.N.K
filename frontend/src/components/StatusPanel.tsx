import type { StatusResponse } from '../types/api';
import { formatUptime } from '../utils/geo';

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

function MetricRow({ label, value, highlight, warn }: MetricRowProps) {
  return (
    <div style={{
      display: 'flex',
      justifyContent: 'space-between',
      alignItems: 'center',
      padding: '3px 0',
      borderBottom: '1px solid rgba(255,255,255,0.04)',
    }}>
      <span style={{ fontSize: '11px', color: '#64748b' }}>{label}</span>
      <span style={{
        fontSize: '12px',
        fontFamily: 'monospace',
        color: warn ? '#ef4444' : highlight ? '#22c55e' : '#e2e8f0',
        fontWeight: highlight || warn ? 600 : 400,
      }}>
        {value}
      </span>
    </div>
  );
}

export function StatusPanel({ status, apiError, snapshotTimestamp, debrisCount, satCount }: Props) {
  if (apiError) {
    return (
      <div style={{ padding: '12px' }}>
        <div style={{ color: '#ef4444', fontSize: '12px', marginBottom: '6px' }}>
          Backend Offline
        </div>
        <div style={{ color: '#64748b', fontSize: '11px', wordBreak: 'break-all' }}>
          {apiError}
        </div>
        <div style={{ color: '#475569', fontSize: '10px', marginTop: '8px' }}>
          Ensure backend is running:<br />
          <code style={{
            color: '#94a3b8',
            fontSize: '9px',
            display: 'block',
            marginTop: '4px',
            lineHeight: '1.6',
            wordBreak: 'break-all',
          }}>
            PROJECTBONK_CORS_ENABLE=true PROJECTBONK_CORS_ALLOW_ORIGIN=http://localhost:5173 ./build/ProjectBONK
          </code>
        </div>
      </div>
    );
  }

  if (!status) {
    return (
      <div style={{ padding: '12px', color: '#64748b', fontSize: '12px' }}>
        Connecting...
      </div>
    );
  }

  const metrics = status.internal_metrics;
  const isNominal = status.status === 'NOMINAL';

  return (
    <div style={{ padding: '8px 12px', display: 'flex', flexDirection: 'column', gap: '1px' }}>
      <MetricRow
        label="System Status"
        value={status.status}
        highlight={isNominal}
        warn={!isNominal}
      />
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
        value={satCount ?? status.internal_metrics?.satellite_count ?? '—'}
      />
      <MetricRow
        label="Debris Objects"
        value={debrisCount !== undefined ? debrisCount.toLocaleString() : (metrics?.debris_count?.toLocaleString() ?? '—')}
      />
      <MetricRow
        label="Total Objects"
        value={status.object_count.toLocaleString()}
      />

      {metrics && (
        <>
          <div style={{ height: '6px' }} />
          <div style={{ fontSize: '10px', color: '#475569', textTransform: 'uppercase', letterSpacing: '0.08em', marginBottom: '2px' }}>
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

          {metrics.command_latency_us && (
            <>
              <div style={{ height: '6px' }} />
              <div style={{ fontSize: '10px', color: '#475569', textTransform: 'uppercase', letterSpacing: '0.08em', marginBottom: '2px' }}>
                Latency (last)
              </div>
              {Object.entries(metrics.command_latency_us).map(([cmd, lat]) => (
                <MetricRow
                  key={cmd}
                  label={cmd}
                  value={`${lat.execution_us_last.toLocaleString()} µs`}
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
}

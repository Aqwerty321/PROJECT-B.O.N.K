export type PageId = 'command' | 'track' | 'threat' | 'burn-ops' | 'evasion' | 'fleet-status';

export type NavLabelMode = 'tactical' | 'simple';

export interface NavItem {
  id: PageId;
  path: string;
  tacticalLabel: string;
  simpleLabel: string;
  blurb: string;
}

export const NAV_LABEL_MODE: NavLabelMode = 'tactical';

export const NAV_ITEMS: NavItem[] = [
  {
    id: 'command',
    path: '/command',
    tacticalLabel: 'Command',
    simpleLabel: 'Overview',
    blurb: 'Mission picture, command state, and quick actions.',
  },
  {
    id: 'track',
    path: '/track',
    tacticalLabel: 'Track',
    simpleLabel: 'Ground Track',
    blurb: 'Ground track, path state, and orbital context.',
  },
  {
    id: 'threat',
    path: '/threat',
    tacticalLabel: 'Threat',
    simpleLabel: 'Conjunctions',
    blurb: 'Bullseye watch, event list, and encounter details.',
  },
  {
    id: 'burn-ops',
    path: '/burn-ops',
    tacticalLabel: 'Burn Ops',
    simpleLabel: 'Maneuvers',
    blurb: 'Burn schedule, timeline, and command friction.',
  },
  {
    id: 'evasion',
    path: '/evasion',
    tacticalLabel: 'Evasion',
    simpleLabel: 'Efficiency',
    blurb: 'Fuel-to-mitigation outcomes and efficiency tracking.',
  },
  {
    id: 'fleet-status',
    path: '/fleet-status',
    tacticalLabel: 'Fleet',
    simpleLabel: 'Status',
    blurb: 'System health, fuel watchlist, and resource posture.',
  },
];

export const DEFAULT_PAGE_ID: PageId = 'command';

export function labelForNav(item: NavItem): string {
  return NAV_LABEL_MODE === 'tactical' ? item.tacticalLabel : item.simpleLabel;
}

export function pageIdFromHash(hash: string): PageId {
  const normalized = hash.replace(/^#/, '').trim();
  const path = normalized.startsWith('/') ? normalized : `/${normalized}`;
  const match = NAV_ITEMS.find(item => item.path === path);
  return match?.id ?? DEFAULT_PAGE_ID;
}

export function hashForPage(id: PageId): string {
  const item = NAV_ITEMS.find(entry => entry.id === id);
  return `#${item?.path ?? '/command'}`;
}

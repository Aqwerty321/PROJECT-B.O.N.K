# CASCADE Frontend Overhaul Plan
# TRON x Star Wars Cockpit Aesthetic
# Created: 2026-03-21

---

## Vision

Full-screen 3D globe fills the entire viewport as backdrop.
All HUD panels float on top as highly transparent glassmorphic overlays
with gaussian blur, arranged in a U-shaped cockpit layout (sides + bottom,
top kept clear for unobstructed Earth view).

Aesthetic: Hybrid TRON (neon glow, geometric edges, dark void) +
Star Wars (tactical HUD, military readouts, holographic feel).

---

## Design Decisions (User Approved)

- Color primary: Electric blue #3A9FE8 (borders, glow, accents)
- Color warnings: Amber #eab308
- Color critical: Red #ef4444
- Background: Cosmic black #050a14
- Panel bg: rgba(5, 10, 20, 0.15-0.20) with backdrop-filter blur(12px)
- All containers + buttons: asymmetric 45deg chamfer (top-right, bottom-left)
- Font: JetBrains Mono everywhere
- Animation: Balanced mix (some CRT flicker, pulsing borders, smooth data transitions)
- Empty states: Animated placeholders ("AWAITING DATA" with pulse)
- Stars: Tiny round dots (circular CanvasTexture), NOT squares
- Globe: Realistic blue Fresnel atmosphere glow
- Bullseye: Animated radar sweep line
- Gantt: Sweep line showing current time, neon glow bars
- Ground track: Persistent mini-map in bottom-left corner
- Scanlines: Keep but reduce opacity for glassmorphism compatibility

---

## U-Shaped Cockpit Layout

```
+--------------------------------------------------------------------+
|                      (TOP CLEAR - globe visible)                    |
|                                                                     |
| +--STATUS------+                                 +--BULLSEYE------+ |
| | System info  |                                 | Radar sweep    | |
| +--------------+          E A R T H              | Threat dots    | |
| +--FUEL--------+         (full screen)           +----------------+ |
| | Sat fuel bars|                                 +--SIM CONTROLS--+ |
| +--------------+                                 | 1H 6H 24H btns| |
| +--MINIMAP-----+                                 +----------------+ |
| | Ground track |                                                    |
| +--------------+                                                    |
| +=========================  MANEUVER TIMELINE  ====================+|
+--------------------------------------------------------------------+
```

Panel sizing (responsive with min/max):
- Left column:  min 240px, max 320px, ~18vw
- Right column: min 260px, max 360px, ~20vw
- Bottom strip: full width minus margins, height ~140-160px
- Margins from viewport edge: 12-16px
- Top clear zone: top ~12-15% of screen

---

## Phase 1: Boot Sequence Fixes

FILE: frontend/src/components/BootSequence.tsx

### Changes:
1. TIMING: Cut post-scroll delay by 4x
   - BOOT_CURSOR_BLINK_MS: 500 -> 125
   - BOOT_FADE_MS: 600 -> 150
   - Keep BOOT_SCROLL_INTERVAL_MS at 55 (scroll speed is fine)

2. BOX: Fixed size from the start, thicker stroke
   - Box appears immediately when CASCADE phase starts (not when logs start)
   - Fixed dimensions: width min(580px, 85vw), height accommodates all 9 log lines
   - SVG strokeWidth: 0.6 -> 1.5 (thicker)
   - Box trace animation: border draws from mid-top clockwise (keep existing logic)
   - Box is FULLY TRACED before any log lines appear

3. SPINNER: Replace 3D Y-axis Rotor with ASCII character cycle
   - Remove: rotorSpin keyframe, 3D transform, perspective
   - Add: cycling through chars ['-', '\\', '|', '/'] via setInterval
   - Cycle speed: ~100ms per character
   - When line completes, spinner resolves to status text (e.g., "OK")

4. LOG ORDERING: Logs appear ONLY after box is fully traced
   - Current: logStart = borderStart + 200 (overlaps border trace)
   - New: logStart = borderStart + borderDuration (logs start after border completes)

5. PHASE TIMING SUMMARY (approximate):
   - Phase A scroll: 36 lines * 55ms = 1980ms
   - Cursor blink: 3 cycles * 2 * 125ms = 750ms (was 3000ms)
   - Fade: 150ms (was 600ms)
   - CASCADE appear: 200ms
   - CASCADE expand: 800ms
   - Border trace: 800ms
   - Log lines: 9 * 200ms = 1800ms
   - SYSTEM READY: 200ms pause + 800ms display
   - TOTAL: ~6680ms (was ~9880ms -- saved ~3.2s)

---

## Phase 2: Theme & Global Styles

FILE: frontend/src/styles/theme.ts
FILE: frontend/src/index.css

### theme.ts changes:
- glassmorphism.background: 'rgba(8, 15, 30, 0.75)' -> 'rgba(5, 10, 20, 0.18)'
- glassmorphism.backdropFilter: keep 'blur(12px) saturate(1.3)'
- glassmorphism.border: keep '1px solid rgba(58, 159, 232, 0.18)'
- Add glassmorphism.boxShadow: '0 0 15px rgba(58, 159, 232, 0.08)'
- scanline opacity: 0.03 -> 0.015 (subtler for transparent panels)
- Add chamfer.buttonClipPath for smaller chamfer on buttons
  e.g., 'polygon(0 0, calc(100% - 8px) 0, 100% 8px, 100% 100%, 8px 100%, 0 calc(100% - 8px))'
- Add animation tokens:
  - borderPulse duration: '2s'
  - radarSweep duration: '4s'
  - hoverGlow color: 'rgba(58, 159, 232, 0.4)'

### index.css changes:
- Confirm body bg is #050a14
- Add keyframes for borderPulse, radarSweep
- Add utility class .chamfer-button with clip-path

---

## Phase 3: Globe Overhaul

FILE: frontend/src/components/EarthGlobe.tsx

### Changes:

1. FULL VIEWPORT RENDERING
   - Container div: position fixed, inset 0, z-index 0
   - Remove borderRadius, overflow hidden (not needed for full screen)
   - Renderer: alpha false, setClearColor(0x050a14, 1) -- opaque cosmic black
   - Camera far plane: 100 -> 200 (for far stars)

2. FIX STARS (round, tiny, realistic)
   - Generate 32x32 circular gradient CanvasTexture:
     ```
     ctx.createRadialGradient(16,16,0, 16,16,16)
     stop(0): rgba(255,255,255,1)
     stop(0.2): rgba(255,255,255,0.8)
     stop(0.5): rgba(255,255,255,0.15)
     stop(1): rgba(255,255,255,0)
     ```
   - Apply as PointsMaterial.map with alphaTest: 0.01
   - Reduce star sizes: 0.3-0.8 range (was 0.8-3.5)
   - Keep STAR_COUNT at 2500
   - Keep vertexColors for subtle tint variation
   - Keep STAR_SPHERE_RADIUS at 50

3. ADD NEBULA BACKGROUND
   - Create 2-3 large translucent sprites/planes far behind Earth
   - Use CanvasTexture with radial gradients:
     - Nebula 1: deep purple/violet tones, positioned upper-right
     - Nebula 2: deep blue tones, positioned lower-left
   - Very low opacity (0.04-0.08)
   - PlaneGeometry(20, 20), always facing camera (billboarded)
   - Place at z=-40 to -45 (behind stars)

4. ADD ATMOSPHERIC GLOW (Fresnel shader)
   - SphereGeometry(1.015, 64, 64) -- slightly larger than Earth
   - Custom ShaderMaterial:
     - Vertex: pass vNormal (normalized normal in view space)
     - Fragment: intensity = pow(0.65 - dot(vNormal, vec3(0,0,1)), 2.0)
       gl_FragColor = vec4(0.3, 0.6, 1.0, 1.0) * intensity
   - AdditiveBlending, side: BackSide, transparent: true
   - Add to earthGroup (rotates with Earth)

5. RESIZE HANDLER
   - Resize to window.innerWidth / window.innerHeight (not container)

---

## Phase 4: Layout Restructure

FILE: frontend/src/App.tsx

### Changes:

1. REMOVE old grid layout entirely

2. NEW STRUCTURE:
   ```
   <div style={root}>
     {/* Full-screen globe backdrop */}
     <EarthGlobe ... style={position:fixed, inset:0, zIndex:0} />

     {/* HUD overlay container */}
     <div style={position:fixed, inset:0, zIndex:10, pointerEvents:none}>

       {/* CRT scanlines - very subtle */}
       <div style={scanlines} />

       {/* Left column */}
       <div style={left column positioning}>
         <GlassPanel title="SYSTEM STATUS">...</GlassPanel>
         <GlassPanel title="FUEL RESERVES">...</GlassPanel>
         <GlassPanel title="GROUND TRACK">
           <GroundTrackMap ... />  {/* persistent mini-map */}
         </GlassPanel>
       </div>

       {/* Right column */}
       <div style={right column positioning}>
         <GlassPanel title="CONJUNCTION BULLSEYE">...</GlassPanel>
         <GlassPanel title="SIM CONTROLS" (or embed in Gantt header)>
           <SimControls />
         </GlassPanel>
       </div>

       {/* Bottom strip */}
       <GlassPanel title="MANEUVER TIMELINE" style={bottom strip}>
         <ManeuverGantt />
       </GlassPanel>
     </div>
   </div>
   ```

3. LEFT COLUMN CSS:
   - position: fixed
   - left: 16px
   - top: 15vh (clear top zone)
   - bottom: 180px (above Gantt)
   - width: clamp(240px, 18vw, 320px)
   - display: flex, flexDirection: column, gap: 8px
   - pointerEvents: auto

4. RIGHT COLUMN CSS:
   - position: fixed
   - right: 16px
   - top: 15vh
   - bottom: 180px
   - width: clamp(260px, 20vw, 360px)
   - display: flex, flexDirection: column, gap: 8px
   - pointerEvents: auto

5. BOTTOM STRIP CSS:
   - position: fixed
   - bottom: 16px
   - left: 16px
   - right: 16px
   - height: clamp(120px, 12vh, 160px)
   - pointerEvents: auto

6. GLOBE receives all pointer events since it's below the HUD
   - Clicking through transparent areas of the HUD hits the globe

7. GroundTrackMap: remove old slide-in overlay logic, integrate as persistent
   mini-map panel in left column. Pass trackHistory state from App.
   Note: trackHistory accumulation logic needs to be added to App.tsx
   (was not present before for the slide-in; use useRef + useEffect on snapshot).

---

## Phase 5: GlassPanel Upgrade

FILE: frontend/src/components/GlassPanel.tsx

### Changes:

1. GLASSMORPHISM:
   - background: theme.glassmorphism.background (now ~18% opacity)
   - backdropFilter: 'blur(12px) saturate(1.3)'
   - boxShadow: '0 0 15px rgba(58, 159, 232, 0.08), inset 0 0 30px rgba(5, 10, 20, 0.3)'
   - border: '1px solid rgba(58, 159, 232, 0.25)'

2. ANIMATED BORDER GLOW:
   - Add CSS animation: border color pulses between 0.18 and 0.35 opacity
   - Duration: 2s, ease-in-out, infinite
   - Implemented via inline style + keyframes in a <style> tag

3. CRT SCANLINES:
   - Reduce opacity from 0.03 to 0.015 (subtler on transparent panels)

4. TITLE BAR:
   - Keep existing dot + text styling
   - Add subtle text-shadow glow on the title text

5. REVEAL ANIMATION:
   - Keep staggered reveal logic
   - Change translateY(8px) to translateY(12px) + slight scale(0.98)
   - Add a brief border-flash on reveal (border goes bright then fades to normal)

---

## Phase 6A: ConjunctionBullseye Radar Sweep

FILE: frontend/src/components/ConjunctionBullseye.tsx

### Changes:

1. RADAR SWEEP LINE:
   - Track sweep angle in component state, increment per frame
   - Full rotation: ~4 seconds (360 / (60fps * 4s) = 1.5 deg/frame)
   - Draw as a line from center to edge with gradient opacity:
     ctx.createLinearGradient from center (alpha 0) to edge (alpha 0.4)
   - Color: electric blue primary
   - Sweep "trail" fading behind the line (~30 deg arc, gradient to 0)

2. PULSING CONCENTRIC RINGS:
   - Time rings pulse in opacity (0.06 to 0.15) with sine wave
   - Inner danger zone fill also pulses slightly

3. THREAT DOT GLOW:
   - Red dots: add shadow/glow effect (draw twice: larger blurred, then sharp)
   - Dots pulse when radar sweep passes over them

4. EMPTY STATE:
   - Still show rings and radar sweep animation
   - Text: "AWAITING CONJUNCTION DATA" with pulsing opacity
   - Remove the old "No conjunctions / detected" two-line text

5. TRANSPARENT BG:
   - Replace ctx.fillRect background with clearRect only
   - Let the GlassPanel background show through

---

## Phase 6B: ManeuverGantt Sweep Line + Neon

FILE: frontend/src/components/ManeuverGantt.tsx

### Changes:

1. SWEEP LINE:
   - Vertical line at current sim time position
   - Color: electric blue, slight glow (draw 3x: wide blur, medium, sharp)
   - Animates smoothly as time updates

2. NEON GLOW ON BURN BARS:
   - Draw each burn bar with 2-pass: outer glow (larger, blurred) + inner fill
   - Active/pending burns pulse slightly

3. EMPTY STATE:
   - Draw the grid background (time ticks, row lines) even with no data
   - Sweep line still moves
   - Center text: "AWAITING BURN DATA" with pulsing opacity

4. TRANSPARENT BG:
   - Replace solid bg fill with clearRect
   - Grid lines still visible against GlassPanel backdrop

---

## Phase 6C: StatusPanel Animated Metrics

FILE: frontend/src/components/StatusPanel.tsx

### Changes:

1. VALUE TRANSITIONS:
   - Subtle CSS transition on color changes
   - Status dot: pulsing green glow when NOMINAL

2. CONNECTING STATE:
   - Replace "Connecting..." with "ESTABLISHING LINK" + animated dots
   - Three dots cycling: .  ..  ...

3. OFFLINE STATE (already updated):
   - Shows full CORS-enabled start command (already done)

---

## Phase 6D: FuelHeatmap Neon Bars

FILE: frontend/src/components/FuelHeatmap.tsx

### Changes:

1. FUEL BAR GLOW:
   - Add box-shadow glow matching the fuel color
   - Intensity proportional to fuel level

2. EMPTY STATE:
   - Replace "No satellite data" with animated placeholder
   - Show 3-4 faint pulsing bar outlines
   - Text: "AWAITING SATELLITE DATA" with pulse

3. STYLING:
   - Use theme.font.mono consistently (not generic 'monospace')
   - Status dot glow intensity increases for DEGRADED/MANEUVERING

---

## Phase 6E: SimControls Chamfered Buttons

FILE: frontend/src/components/SimControls.tsx

### Changes:

1. BUTTON CHAMFER:
   - Apply asymmetric chamfer clip-path (smaller scale: 8px corners)
   - Remove borderRadius (chamfer replaces it)

2. HOVER GLOW:
   - On hover: box-shadow neon blue glow
   - border-color transitions to brighter blue

3. CLICK FEEDBACK:
   - Brief flash: background pulses to rgba(58,159,232,0.3) then fades

4. STYLING:
   - Background: rgba(58,159,232,0.06) default
   - Border: 1px solid rgba(58,159,232,0.3)

---

## Phase 6F: GroundTrackMap Persistent Mini-Map

FILE: frontend/src/components/GroundTrackMap.tsx
FILE: frontend/src/App.tsx (for integration)

### Changes:

1. CONVERT TO MINI-MAP:
   - Remove slide-in overlay logic (if any in App.tsx)
   - Render as a permanent small panel in left column
   - Size: determined by panel container (~240-320px wide, ~140-180px tall)

2. COMPACT RENDERING:
   - Smaller font sizes (8px labels)
   - Tighter margins
   - Reduce ground station label clutter (maybe only show dots, no labels)

3. TRACK HISTORY:
   - App.tsx: accumulate track history via useRef + useEffect on snapshot
   - Pass as prop to GroundTrackMap

---

## Phase 7: Build, Verify, Commit

### Steps:
1. npx tsc --noEmit (zero errors)
2. npx vite build (clean build)
3. ctest --test-dir build (8/8 pass)
4. git add + commit on feature/frontend
5. git checkout main && git merge feature/frontend
6. git push origin main && git push origin feature/frontend

---

## Files Modified (summary)

| File | Phase |
|------|-------|
| BootSequence.tsx | 1 |
| theme.ts | 2 |
| index.css | 2 |
| EarthGlobe.tsx | 3 |
| App.tsx | 4 |
| GlassPanel.tsx | 5 |
| ConjunctionBullseye.tsx | 6A |
| ManeuverGantt.tsx | 6B |
| StatusPanel.tsx | 6C |
| FuelHeatmap.tsx | 6D |
| SimControls.tsx | 6E |
| GroundTrackMap.tsx | 6F |

---

## Key Constraints
- data.txt, tle2025.txt, ps.txt are gitignored
- Default /api/status must remain PS-clean; only ?details=1 exposes internals
- Backend tests: 8/8 must pass (no backend changes in this overhaul)
- No gcc-12 on dev machine
- Branch: feature/frontend -> merge to main

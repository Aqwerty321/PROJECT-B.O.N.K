import { useEffect, useRef, useCallback } from 'react';
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import type { SatelliteSnapshot, DebrisTuple } from '../types/api';
import { statusColor } from '../types/api';
import { theme } from '../styles/theme';

// ---- constants ----

const EARTH_RADIUS = 6371;       // km -- canonical
const SCALE = 1 / EARTH_RADIUS;  // normalize so Earth radius ~ 1 unit
const SAT_ALT_OFFSET = 0.06;     // visual offset above globe surface
const DEBRIS_SIZE = 1.8;         // point size in px
const SAT_SIZE = 0.018;          // satellite mesh radius
const AUTO_ROTATE_SPEED = 0.08;  // degrees per frame
const CAMERA_DISTANCE = 3.2;

// Convert lat/lon/alt to 3D position (Y-up)
function geoTo3D(lat: number, lon: number, altKm: number): THREE.Vector3 {
  const r = (EARTH_RADIUS + altKm) * SCALE;
  const phi = THREE.MathUtils.degToRad(90 - lat);
  const theta = THREE.MathUtils.degToRad(lon + 180);
  return new THREE.Vector3(
    -r * Math.sin(phi) * Math.cos(theta),
    r * Math.cos(phi),
    r * Math.sin(phi) * Math.sin(theta),
  );
}

// ---- props ----

interface EarthGlobeProps {
  satellites: SatelliteSnapshot[];
  debris: DebrisTuple[];
  selectedSatId: string | null;
  onSelectSat: (id: string | null) => void;
  trailPoints?: THREE.Vector3[];     // optional trajectory trail
  predictedPoints?: THREE.Vector3[]; // optional predicted path
}

// ---- component ----

export default function EarthGlobe({
  satellites,
  debris,
  selectedSatId,
  onSelectSat,
  trailPoints,
  predictedPoints,
}: EarthGlobeProps) {
  const mountRef = useRef<HTMLDivElement>(null);
  const sceneRef = useRef<{
    scene: THREE.Scene;
    camera: THREE.PerspectiveCamera;
    renderer: THREE.WebGLRenderer;
    controls: OrbitControls;
    earthGroup: THREE.Group;
    debrisPoints: THREE.Points | null;
    satMeshes: Map<string, THREE.Mesh>;
    selectedRing: THREE.Mesh | null;
    trailLine: THREE.Line | null;
    predictedLine: THREE.Line | null;
    raycaster: THREE.Raycaster;
    mouse: THREE.Vector2;
    animId: number;
    autoRotate: boolean;
  } | null>(null);

  // ---- init scene ----
  useEffect(() => {
    const container = mountRef.current;
    if (!container) return;

    const width = container.clientWidth;
    const height = container.clientHeight;

    // renderer
    const renderer = new THREE.WebGLRenderer({
      antialias: true,
      alpha: true,
      powerPreference: 'high-performance',
    });
    renderer.setSize(width, height);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setClearColor(0x000000, 0);
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.2;
    container.appendChild(renderer.domElement);

    // scene
    const scene = new THREE.Scene();

    // camera
    const camera = new THREE.PerspectiveCamera(45, width / height, 0.01, 100);
    camera.position.set(0, 0.8, CAMERA_DISTANCE);

    // controls
    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;
    controls.minDistance = 1.5;
    controls.maxDistance = 8;
    controls.enablePan = false;

    // lights
    const ambient = new THREE.AmbientLight(0xffffff, 0.5);
    scene.add(ambient);
    const sun = new THREE.DirectionalLight(0xffffff, 1.8);
    sun.position.set(5, 3, 5);
    scene.add(sun);

    // subtle blue rim light from opposite side
    const rim = new THREE.DirectionalLight(0x3a9fe8, 0.3);
    rim.position.set(-3, -1, -5);
    scene.add(rim);

    // earth group (for rotation)
    const earthGroup = new THREE.Group();
    scene.add(earthGroup);

    // load EARTH.glb
    const loader = new GLTFLoader();
    loader.load('/earth_model/EARTH.glb', (gltf) => {
      const model = gltf.scene;
      // normalize scale -- find bounding sphere and scale to radius ~1
      const box = new THREE.Box3().setFromObject(model);
      const sphere = new THREE.Sphere();
      box.getBoundingSphere(sphere);
      const scaleFactor = 1 / sphere.radius;
      model.scale.setScalar(scaleFactor);
      model.position.sub(sphere.center.multiplyScalar(scaleFactor));
      earthGroup.add(model);
    });

    // raycaster for satellite picking
    const raycaster = new THREE.Raycaster();
    raycaster.params.Points = { threshold: 0.05 };
    const mouse = new THREE.Vector2();

    const state = {
      scene,
      camera,
      renderer,
      controls,
      earthGroup,
      debrisPoints: null as THREE.Points | null,
      satMeshes: new Map<string, THREE.Mesh>(),
      selectedRing: null as THREE.Mesh | null,
      trailLine: null as THREE.Line | null,
      predictedLine: null as THREE.Line | null,
      raycaster,
      mouse,
      animId: 0,
      autoRotate: true,
    };
    sceneRef.current = state;

    // animation loop
    function animate() {
      state.animId = requestAnimationFrame(animate);
      if (state.autoRotate) {
        state.earthGroup.rotation.y += THREE.MathUtils.degToRad(AUTO_ROTATE_SPEED);
      }
      state.controls.update();
      state.renderer.render(state.scene, state.camera);
    }
    animate();

    // resize handler
    const onResize = () => {
      if (!container) return;
      const w = container.clientWidth;
      const h = container.clientHeight;
      state.camera.aspect = w / h;
      state.camera.updateProjectionMatrix();
      state.renderer.setSize(w, h);
    };
    window.addEventListener('resize', onResize);

    // click handler for satellite picking
    const onClick = (event: MouseEvent) => {
      const rect = container.getBoundingClientRect();
      state.mouse.x = ((event.clientX - rect.left) / rect.width) * 2 - 1;
      state.mouse.y = -((event.clientY - rect.top) / rect.height) * 2 + 1;
      state.raycaster.setFromCamera(state.mouse, state.camera);

      const satMeshArray = Array.from(state.satMeshes.values());
      const intersects = state.raycaster.intersectObjects(satMeshArray, false);
      if (intersects.length > 0) {
        const mesh = intersects[0].object as THREE.Mesh;
        const satId = mesh.userData.satId as string;
        onSelectSat(satId);
      } else {
        onSelectSat(null);
      }
    };
    renderer.domElement.addEventListener('click', onClick);

    // stop auto-rotate when user interacts
    const onInteract = () => { state.autoRotate = false; };
    renderer.domElement.addEventListener('pointerdown', onInteract);

    return () => {
      window.removeEventListener('resize', onResize);
      renderer.domElement.removeEventListener('click', onClick);
      renderer.domElement.removeEventListener('pointerdown', onInteract);
      cancelAnimationFrame(state.animId);
      renderer.dispose();
      controls.dispose();
      container.removeChild(renderer.domElement);
      sceneRef.current = null;
    };
  // onSelectSat is stable via parent useCallback
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // ---- update debris cloud ----
  const updateDebris = useCallback((debrisData: DebrisTuple[]) => {
    const s = sceneRef.current;
    if (!s) return;

    // remove old
    if (s.debrisPoints) {
      s.earthGroup.remove(s.debrisPoints);
      s.debrisPoints.geometry.dispose();
      (s.debrisPoints.material as THREE.Material).dispose();
      s.debrisPoints = null;
    }

    if (debrisData.length === 0) return;

    const positions = new Float32Array(debrisData.length * 3);
    for (let i = 0; i < debrisData.length; i++) {
      const [, lat, lon, alt] = debrisData[i];
      const p = geoTo3D(lat, lon, alt);
      positions[i * 3] = p.x;
      positions[i * 3 + 1] = p.y;
      positions[i * 3 + 2] = p.z;
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));

    const mat = new THREE.PointsMaterial({
      color: 0xef4444,
      size: DEBRIS_SIZE,
      sizeAttenuation: false,
      transparent: true,
      opacity: 0.55,
      blending: THREE.AdditiveBlending,
      depthWrite: false,
    });

    const points = new THREE.Points(geo, mat);
    s.earthGroup.add(points);
    s.debrisPoints = points;
  }, []);

  // ---- update satellites ----
  const updateSatellites = useCallback((sats: SatelliteSnapshot[], selected: string | null) => {
    const s = sceneRef.current;
    if (!s) return;

    const currentIds = new Set(sats.map(sat => sat.id));

    // remove satellites no longer present
    for (const [id, mesh] of s.satMeshes.entries()) {
      if (!currentIds.has(id)) {
        s.earthGroup.remove(mesh);
        mesh.geometry.dispose();
        (mesh.material as THREE.Material).dispose();
        s.satMeshes.delete(id);
      }
    }

    // add/update satellites
    for (const sat of sats) {
      const pos = geoTo3D(sat.lat, sat.lon, 550); // approximate LEO alt
      pos.multiplyScalar(1 + SAT_ALT_OFFSET);

      let mesh = s.satMeshes.get(sat.id);
      if (!mesh) {
        const geo = new THREE.OctahedronGeometry(SAT_SIZE, 0);
        const mat = new THREE.MeshStandardMaterial({
          color: statusColor(sat.status),
          emissive: statusColor(sat.status),
          emissiveIntensity: 0.5,
          metalness: 0.6,
          roughness: 0.3,
        });
        mesh = new THREE.Mesh(geo, mat);
        mesh.userData.satId = sat.id;
        s.earthGroup.add(mesh);
        s.satMeshes.set(sat.id, mesh);
      }

      mesh.position.copy(pos);

      // update color
      const col = statusColor(sat.status);
      const mat = mesh.material as THREE.MeshStandardMaterial;
      mat.color.setHex(col);
      mat.emissive.setHex(col);

      // highlight selected
      const isSelected = sat.id === selected;
      const scale = isSelected ? 2.0 : 1.0;
      mesh.scale.setScalar(scale);
      mat.emissiveIntensity = isSelected ? 1.0 : 0.5;
    }

    // selection ring
    if (s.selectedRing) {
      s.earthGroup.remove(s.selectedRing);
      s.selectedRing.geometry.dispose();
      (s.selectedRing.material as THREE.Material).dispose();
      s.selectedRing = null;
    }
    if (selected) {
      const selMesh = s.satMeshes.get(selected);
      if (selMesh) {
        const ringGeo = new THREE.RingGeometry(SAT_SIZE * 2.5, SAT_SIZE * 3.2, 32);
        const ringMat = new THREE.MeshBasicMaterial({
          color: 0x3a9fe8,
          transparent: true,
          opacity: 0.7,
          side: THREE.DoubleSide,
          depthWrite: false,
        });
        const ring = new THREE.Mesh(ringGeo, ringMat);
        ring.position.copy(selMesh.position);
        ring.lookAt(0, 0, 0);
        s.earthGroup.add(ring);
        s.selectedRing = ring;
      }
    }
  }, []);

  // ---- update trajectory lines ----
  const updateTrajectory = useCallback((
    trail?: THREE.Vector3[],
    predicted?: THREE.Vector3[],
  ) => {
    const s = sceneRef.current;
    if (!s) return;

    // cleanup old lines
    if (s.trailLine) {
      s.earthGroup.remove(s.trailLine);
      s.trailLine.geometry.dispose();
      (s.trailLine.material as THREE.Material).dispose();
      s.trailLine = null;
    }
    if (s.predictedLine) {
      s.earthGroup.remove(s.predictedLine);
      s.predictedLine.geometry.dispose();
      (s.predictedLine.material as THREE.Material).dispose();
      s.predictedLine = null;
    }

    if (trail && trail.length > 1) {
      const geo = new THREE.BufferGeometry().setFromPoints(trail);
      const mat = new THREE.LineBasicMaterial({
        color: 0x3a9fe8,
        transparent: true,
        opacity: 0.6,
        linewidth: 1,
      });
      s.trailLine = new THREE.Line(geo, mat);
      s.earthGroup.add(s.trailLine);
    }

    if (predicted && predicted.length > 1) {
      const geo = new THREE.BufferGeometry().setFromPoints(predicted);
      const mat = new THREE.LineDashedMaterial({
        color: 0xeab308,
        transparent: true,
        opacity: 0.5,
        dashSize: 0.02,
        gapSize: 0.01,
        linewidth: 1,
      });
      const line = new THREE.Line(geo, mat);
      line.computeLineDistances();
      s.predictedLine = line;
      s.earthGroup.add(line);
    }
  }, []);

  // ---- sync data to scene ----
  useEffect(() => {
    updateDebris(debris);
  }, [debris, updateDebris]);

  useEffect(() => {
    updateSatellites(satellites, selectedSatId);
  }, [satellites, selectedSatId, updateSatellites]);

  useEffect(() => {
    updateTrajectory(trailPoints, predictedPoints);
  }, [trailPoints, predictedPoints, updateTrajectory]);

  return (
    <div
      ref={mountRef}
      style={{
        width: '100%',
        height: '100%',
        position: 'relative',
        borderRadius: '4px',
        overflow: 'hidden',
        background: theme.colors.bg,
      }}
    />
  );
}

// Export the helper so App.tsx can convert trajectory data to Vector3[]
export function trajectoryToVectors(
  points: { lat: number; lon: number; alt_km: number }[],
): THREE.Vector3[] {
  return points.map(p => geoTo3D(p.lat, p.lon, p.alt_km));
}

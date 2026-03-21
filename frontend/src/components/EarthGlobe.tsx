import { useEffect, useRef, useCallback } from 'react';
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import type { SatelliteSnapshot, DebrisTuple } from '../types/api';
import { statusColor } from '../types/api';

// ---- constants ----

const EARTH_RADIUS = 6371;       // km -- canonical
const SCALE = 1 / EARTH_RADIUS;  // normalize so Earth radius ~ 1 unit
const SAT_ALT_OFFSET = 0.06;     // visual offset above globe surface
const DEBRIS_SIZE = 1.8;         // point size in px
const SAT_SIZE = 0.018;          // satellite mesh radius
const AUTO_ROTATE_SPEED = 0.08;  // degrees per frame
const CAMERA_DISTANCE = 2.5;
const STAR_COUNT = 2500;         // background star count
const STAR_SPHERE_RADIUS = 50;   // far-distance sphere for stars

// GLB normalization uses Box3.getBoundingSphere which overestimates by sqrt(3).
// After scaling, the actual Earth mesh radius is 1/sqrt(3), NOT 1.0.
const EARTH_VISUAL_RADIUS = 1 / Math.sqrt(3);  // ≈ 0.577

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

// Create a circular star texture (32x32) to replace square default
function createStarTexture(): THREE.CanvasTexture {
  const size = 32;
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext('2d')!;
  const center = size / 2;
  const grad = ctx.createRadialGradient(center, center, 0, center, center, center);
  grad.addColorStop(0, 'rgba(255,255,255,1)');
  grad.addColorStop(0.2, 'rgba(255,255,255,0.8)');
  grad.addColorStop(0.5, 'rgba(255,255,255,0.15)');
  grad.addColorStop(1, 'rgba(255,255,255,0)');
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, size, size);
  const tex = new THREE.CanvasTexture(canvas);
  tex.needsUpdate = true;
  return tex;
}

// Create nebula sprite texture
function createNebulaTexture(
  r: number, g: number, b: number,
): THREE.CanvasTexture {
  const size = 512;
  const canvas = document.createElement('canvas');
  canvas.width = size;
  canvas.height = size;
  const ctx = canvas.getContext('2d')!;
  const center = size / 2;
  const grad = ctx.createRadialGradient(center, center, 0, center, center, center);
  grad.addColorStop(0, `rgba(${r},${g},${b},0.12)`);
  grad.addColorStop(0.3, `rgba(${r},${g},${b},0.06)`);
  grad.addColorStop(0.7, `rgba(${r},${g},${b},0.02)`);
  grad.addColorStop(1, `rgba(${r},${g},${b},0)`);
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, size, size);
  const tex = new THREE.CanvasTexture(canvas);
  tex.needsUpdate = true;
  return tex;
}

// Fresnel atmosphere shader (BackSide on a sphere slightly larger than Earth).
// BackSide: we see back faces; dot(normal, viewDir) is ~0 at silhouette (outer
// edge of glow) and goes negative toward the back-center (occluded by Earth).
// Using -dot as intensity gives: bright at inner edge (near Earth surface),
// fading to transparent at outer edge -- exactly the radial gradient we want.
// Earth's depth buffer naturally occludes the center.
const atmosphereVertexShader = `
  varying vec3 vNormal;
  varying vec3 vViewDir;
  void main() {
    vNormal = normalize(normalMatrix * normal);
    vec4 mvPos = modelViewMatrix * vec4(position, 1.0);
    vViewDir = normalize(-mvPos.xyz);
    gl_Position = projectionMatrix * mvPos;
  }
`;

const atmosphereFragmentShader = `
  varying vec3 vNormal;
  varying vec3 vViewDir;
  void main() {
    float d = dot(vNormal, vViewDir);          // ~0 at silhouette, negative on back face
    float inner = clamp(-d, 0.0, 1.0);         // 0 at outer edge, grows toward Earth
    float glow = sqrt(inner) * 1.4;            // sqrt for smooth gradient, bright near Earth
    vec3 col = vec3(0.30, 0.65, 1.0);          // bright electric blue
    gl_FragColor = vec4(col * glow, glow);
  }
`;

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

    const width = window.innerWidth;
    const height = window.innerHeight;

    // renderer -- opaque cosmic black, NOT transparent
    const renderer = new THREE.WebGLRenderer({
      antialias: true,
      alpha: false,
      powerPreference: 'high-performance',
    });
    renderer.setSize(width, height);
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setClearColor(0x050a14, 1); // opaque cosmic black
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.2;
    container.appendChild(renderer.domElement);

    // scene
    const scene = new THREE.Scene();

    // camera -- extended far plane for distant stars/nebulae
    const camera = new THREE.PerspectiveCamera(45, width / height, 0.01, 200);
    camera.position.set(0, 0.6, CAMERA_DISTANCE);

    // controls
    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;
    controls.minDistance = 1.3;
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

    // ---- nebula background (behind stars) ----
    {
      // Nebula 1: deep purple/violet -- upper right
      const nebTex1 = createNebulaTexture(80, 40, 120);
      const nebMat1 = new THREE.SpriteMaterial({
        map: nebTex1,
        transparent: true,
        opacity: 0.06,
        blending: THREE.AdditiveBlending,
        depthWrite: false,
      });
      const neb1 = new THREE.Sprite(nebMat1);
      neb1.scale.set(30, 30, 1);
      neb1.position.set(15, 10, -42);
      scene.add(neb1);

      // Nebula 2: deep blue -- lower left
      const nebTex2 = createNebulaTexture(30, 60, 140);
      const nebMat2 = new THREE.SpriteMaterial({
        map: nebTex2,
        transparent: true,
        opacity: 0.05,
        blending: THREE.AdditiveBlending,
        depthWrite: false,
      });
      const neb2 = new THREE.Sprite(nebMat2);
      neb2.scale.set(25, 25, 1);
      neb2.position.set(-12, -8, -40);
      scene.add(neb2);

      // Nebula 3: subtle teal -- center-left
      const nebTex3 = createNebulaTexture(20, 80, 100);
      const nebMat3 = new THREE.SpriteMaterial({
        map: nebTex3,
        transparent: true,
        opacity: 0.04,
        blending: THREE.AdditiveBlending,
        depthWrite: false,
      });
      const neb3 = new THREE.Sprite(nebMat3);
      neb3.scale.set(20, 20, 1);
      neb3.position.set(-20, 5, -45);
      scene.add(neb3);
    }

    // ---- starfield (fixed relative to camera, does NOT rotate with Earth) ----
    {
      const starTexture = createStarTexture();

      const starPositions = new Float32Array(STAR_COUNT * 3);
      const starSizes = new Float32Array(STAR_COUNT);
      const starColors = new Float32Array(STAR_COUNT * 3);

      for (let i = 0; i < STAR_COUNT; i++) {
        // Uniform distribution on sphere surface
        const u = Math.random();
        const v = Math.random();
        const theta = 2 * Math.PI * u;
        const phi = Math.acos(2 * v - 1);
        const r = STAR_SPHERE_RADIUS * (0.9 + 0.1 * Math.random()); // slight depth variation

        starPositions[i * 3] = r * Math.sin(phi) * Math.cos(theta);
        starPositions[i * 3 + 1] = r * Math.sin(phi) * Math.sin(theta);
        starPositions[i * 3 + 2] = r * Math.cos(phi);

        // Reduced sizes: 0.3-0.8 range (was 0.8-3.5)
        const brightness = Math.random();
        const size = brightness < 0.85 ? 0.3 + Math.random() * 0.2
                   : brightness < 0.97 ? 0.5 + Math.random() * 0.2
                   : 0.6 + Math.random() * 0.2;
        starSizes[i] = size;

        // Subtle color variation: mostly white, slight blue or warm tint
        const tint = Math.random();
        if (tint < 0.7) {
          // white
          const b = 0.6 + Math.random() * 0.4;
          starColors[i * 3] = b;
          starColors[i * 3 + 1] = b;
          starColors[i * 3 + 2] = b;
        } else if (tint < 0.85) {
          // blue-white
          const b = 0.5 + Math.random() * 0.3;
          starColors[i * 3] = b * 0.8;
          starColors[i * 3 + 1] = b * 0.9;
          starColors[i * 3 + 2] = b;
        } else {
          // warm
          const b = 0.5 + Math.random() * 0.4;
          starColors[i * 3] = b;
          starColors[i * 3 + 1] = b * 0.85;
          starColors[i * 3 + 2] = b * 0.6;
        }
      }

      const starGeo = new THREE.BufferGeometry();
      starGeo.setAttribute('position', new THREE.BufferAttribute(starPositions, 3));
      starGeo.setAttribute('size', new THREE.BufferAttribute(starSizes, 1));
      starGeo.setAttribute('color', new THREE.BufferAttribute(starColors, 3));

      const starMat = new THREE.PointsMaterial({
        size: 0.6,
        sizeAttenuation: true,
        vertexColors: true,
        transparent: true,
        opacity: 0.9,
        depthWrite: false,
        blending: THREE.AdditiveBlending,
        map: starTexture,
        alphaTest: 0.01,
      });

      const stars = new THREE.Points(starGeo, starMat);
      scene.add(stars); // added to scene, NOT earthGroup -- stays fixed
    }

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

      // Use the Earth's own diffuse texture as emissive map so surface
      // details glow with a subtle blue tint matching the atmosphere halo.
      // We must create a NEW material with emissiveMap specified at construction
      // time -- mutating an existing material's emissiveMap and setting needsUpdate
      // does not reliably trigger shader recompilation with USE_EMISSIVEMAP define.
      model.traverse((child) => {
        if ((child as THREE.Mesh).isMesh) {
          const mesh = child as THREE.Mesh;
          const oldMat = mesh.material as THREE.MeshStandardMaterial;
          if (oldMat && oldMat.isMeshStandardMaterial && oldMat.map) {
            const newMat = new THREE.MeshStandardMaterial({
              map: oldMat.map,
              normalMap: oldMat.normalMap,
              metalnessMap: oldMat.metalnessMap,
              roughnessMap: oldMat.roughnessMap,
              aoMap: oldMat.aoMap,
              metalness: oldMat.metalness,
              roughness: oldMat.roughness,
              emissiveMap: oldMat.map,                         // diffuse texture as emission source
              emissive: new THREE.Color(0.4, 0.55, 0.9),      // blue tint matching halo
              emissiveIntensity: 0.5,
            });
            mesh.material = newMat;
            oldMat.dispose();
          }
        }
      });

      earthGroup.add(model);
    });

    // ---- Fresnel atmospheric glow ----
    {
      // 5% larger than actual Earth visual radius -- tight rim glow
      const atmosGeo = new THREE.SphereGeometry(EARTH_VISUAL_RADIUS * 1.05, 64, 64);
      const atmosMat = new THREE.ShaderMaterial({
        vertexShader: atmosphereVertexShader,
        fragmentShader: atmosphereFragmentShader,
        blending: THREE.AdditiveBlending,
        side: THREE.BackSide,
        transparent: true,
        depthWrite: false,
      });
      const atmosMesh = new THREE.Mesh(atmosGeo, atmosMat);
      earthGroup.add(atmosMesh);
    }

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

    // resize handler -- use window dimensions for full viewport
    const onResize = () => {
      const w = window.innerWidth;
      const h = window.innerHeight;
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
        position: 'fixed',
        inset: 0,
        zIndex: 0,
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

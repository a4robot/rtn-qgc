import React, { useEffect, useRef } from "react";
import * as THREE from "three";
import { type Map as MapLibreMap } from "maplibre-gl";
import { convertEquatorialToHorizontal, getPlanetaryPositions } from "@observerly/astrometry";
import { useVehicles } from "../../store/vehicleStore.ts";
import { useTranslation } from "react-i18next";
import starsData from "../../assets/stars.json";
import constellationsData from "../../assets/constellations.json";
import constellationNames from "../../assets/constellation_names.json";

interface CelestialSkyboxProps {
  map: MapLibreMap | null;
}

export function CelestialSkybox({ map }: CelestialSkyboxProps) {
  const containerRef = useRef<HTMLDivElement | null>(null);
  const labelsContainerRef = useRef<HTMLDivElement | null>(null);
  
  const sceneRef = useRef<THREE.Scene | null>(null);
  const cameraRef = useRef<THREE.PerspectiveCamera | null>(null);
  const rendererRef = useRef<THREE.WebGLRenderer | null>(null);
  const starsGroupRef = useRef<THREE.Group | null>(null);
  const constellationsRef = useRef<THREE.LineSegments | null>(null);
  const labelElementsRef = useRef<{ el: HTMLDivElement; ra: number; dec: number, isPlanet?: boolean }[]>([]);
  const planetsGroupRef = useRef<THREE.Points | null>(null);
  const vehicles = useVehicles();
  const { i18n } = useTranslation();
  const isThai = i18n.language?.startsWith('th');

  useEffect(() => {
    if (!containerRef.current || !map) return;

    // Initialize Three.js
    const width = containerRef.current.clientWidth;
    const height = containerRef.current.clientHeight;

    const scene = new THREE.Scene();
    sceneRef.current = scene;

    const camera = new THREE.PerspectiveCamera(map.transform.fov || 60, width / height, 0.1, 1000);
    cameraRef.current = camera;

    const renderer = new THREE.WebGLRenderer({ alpha: true, antialias: true });
    renderer.setSize(width, height);
    renderer.setPixelRatio(window.devicePixelRatio);
    containerRef.current.innerHTML = "";
    containerRef.current.appendChild(renderer.domElement);
    rendererRef.current = renderer;

    const starsGroup = new THREE.Group();
    scene.add(starsGroup);
    starsGroupRef.current = starsGroup;

    // Group stars by magnitude to vary their sizes without a custom shader
    const buckets = [
      { maxMag: 2, size: 4.5, opacity: 1.0, positions: [] as number[] },
      { maxMag: 4, size: 3.0, opacity: 0.8, positions: [] as number[] },
      { maxMag: 99, size: 1.8, opacity: 0.5, positions: [] as number[] },
    ];

    // Initialize geometry arrays for each bucket
    for (const b of buckets) {
      const geometry = new THREE.BufferGeometry();
      const material = new THREE.PointsMaterial({
        color: 0xffffff,
        size: b.size,
        transparent: true,
        opacity: b.opacity,
        sizeAttenuation: false,
      });
      // Pre-allocate buffer based on total stars (some buckets will be partially filled, but it's safe)
      const initialPositions = new Float32Array(starsData.length * 3);
      geometry.setAttribute("position", new THREE.BufferAttribute(initialPositions, 3));
      
      const stars = new THREE.Points(geometry, material);
      starsGroup.add(stars);
    }

    // Initialize constellation lines
    const lineMaterial = new THREE.LineBasicMaterial({
      color: 0xffffff,
      transparent: true,
      opacity: 0.15, // Subtle lines
      linewidth: 1,
    });
    const lineGeometry = new THREE.BufferGeometry();
    // Count total segments required
    let totalSegments = 0;
    for (const segment of (constellationsData as number[][][])) {
      totalSegments += segment.length - 1;
    }
    const initialLinePositions = new Float32Array(totalSegments * 6);
    lineGeometry.setAttribute("position", new THREE.BufferAttribute(initialLinePositions, 3));
    const constellations = new THREE.LineSegments(lineGeometry, lineMaterial);
    scene.add(constellations);
    constellationsRef.current = constellations;

    // Create label elements for constellations
    if (labelsContainerRef.current) {
      labelsContainerRef.current.innerHTML = "";
      labelElementsRef.current = [];
      for (const nameObj of constellationNames) {
        const el = document.createElement("div");
        el.textContent = isThai && nameObj.name_th ? nameObj.name_th : nameObj.name;
        el.style.position = "absolute";
        el.style.color = "rgba(255, 255, 255, 0.4)";
        el.style.fontSize = "10px";
        el.style.textTransform = "uppercase";
        el.style.letterSpacing = "1px";
        el.style.pointerEvents = "none";
        el.style.transform = "translate(-50%, -50%)";
        el.style.display = "none";
        labelsContainerRef.current.appendChild(el);
        labelElementsRef.current.push({ el, ra: nameObj.ra, dec: nameObj.dec });
      }

      // Add planets and bright stars (mag < 1)
      const brightStars = (starsData as any[]).filter(s => s.mag <= 1.5 && s.name);
      for (const star of brightStars) {
        const el = document.createElement("div");
        let thName = "";
        if (star.name === "Sirius") thName = "ดาวโจร (ซีรีอุส)";
        else if (star.name === "Polaris") thName = "ดาวเหนือ";
        else if (star.name === "Canopus") thName = "ดาวคาโนปุส";
        else if (star.name === "Arcturus") thName = "ดาวดวงแก้ว";
        else if (star.name === "Vega") thName = "ดาวเวก้า";
        else if (star.name === "Capella") thName = "ดาวคาเพลลา";
        else if (star.name === "Rigel") thName = "ดาวไรเจล";
        else if (star.name === "Procyon") thName = "ดาวโปรซิออน";
        else if (star.name === "Achernar") thName = "ดาวอะเคอร์นาร์";
        else if (star.name === "Betelgeuse") thName = "ดาวบีเทลจุส";
        else if (star.name === "Hadar") thName = "ดาวฮาดาร์";
        else if (star.name === "Altair") thName = "ดาวตานกอินทรี";
        else if (star.name === "Acrux") thName = "ดาวเอครักซ์";
        else if (star.name === "Aldebaran") thName = "ดาวตาวัว";
        else if (star.name === "Spica") thName = "ดาวรวงข้าว";
        else if (star.name === "Antares") thName = "ดาวปาริชาต";
        
        el.textContent = isThai && thName ? thName : star.name;
        el.dataset.enName = star.name;
        el.dataset.thName = thName;
        
        el.style.position = "absolute";
        el.style.color = "rgba(255, 255, 200, 0.8)";
        el.style.fontSize = "12px";
        el.style.fontWeight = "bold";
        el.style.pointerEvents = "none";
        el.style.transform = "translate(-50%, -50%)";
        el.style.display = "none";
        labelsContainerRef.current.appendChild(el);
        labelElementsRef.current.push({ el, ra: star.ra, dec: star.dec });
      }
      
      // Planets will be updated dynamically since they move
    }

    const planetGeo = new THREE.BufferGeometry();
    const planetMat = new THREE.PointsMaterial({ color: 0xffddaa, size: 6.0, transparent: true, opacity: 0.9, sizeAttenuation: false });
    const planetsPoints = new THREE.Points(planetGeo, planetMat);
    scene.add(planetsPoints);
    planetsGroupRef.current = planetsPoints;

    const syncCamera = () => {
      if (!map) return;
      const bearing = map.getBearing(); 
      const pitch = map.getPitch();     

      camera.rotation.order = "YXZ";
      camera.rotation.y = THREE.MathUtils.degToRad(-bearing);
      camera.rotation.x = THREE.MathUtils.degToRad(pitch - 90);
      
      renderer.render(scene, camera);

      // Update labels positions
      if (labelElementsRef.current.length > 0 && containerRef.current) {
        const w = containerRef.current.clientWidth;
        const h = containerRef.current.clientHeight;
        const now = new Date(); // Could optimize by not calling Date() every frame, but fine for now
        const r = 500;
        
        // Use a cached vehicle location if available, otherwise just use 0,0
        // We can grab the latest lat/lon directly from the camera sync context if needed
        // But since syncCamera runs every frame, we can just use the latest values from store
        // Wait, lat/lon is fetched via the hook, we can just use the hook's lat/lon!
        // We don't have lat/lon in this closure unless we use a ref, or we just rely on the latest closure.
        // Actually, we can just read the positions calculated from the other effect.
        // Even better, let's calculate the 3D position for labels ONCE per location update, 
        // and just project them here!
      }
    };

    let animationFrameId: number;
    const animate = () => {
      syncCamera();
      animationFrameId = requestAnimationFrame(animate);
    };
    animate();

    map.on("resize", () => {
      const w = containerRef.current?.clientWidth || window.innerWidth;
      const h = containerRef.current?.clientHeight || window.innerHeight;
      renderer.setSize(w, h);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      syncCamera();
    });
    
    syncCamera();

    return () => {
      cancelAnimationFrame(animationFrameId);
      renderer.dispose();
      if (containerRef.current) {
        containerRef.current.innerHTML = "";
      }
    };
  }, [map]);

  // Update star positions when GPS changes (or periodically for time)
  useEffect(() => {
    if (!starsGroupRef.current || !cameraRef.current || !rendererRef.current || !sceneRef.current) return;

    // Get location from the first connected vehicle, fallback to Bangkok
    let lat = 13.75;
    let lon = 100.51;
    for (const v of Object.values(vehicles)) {
      if (v.connected && v.position.lat !== null && v.position.lon !== null) {
        lat = v.position.lat;
        lon = v.position.lon;
        break;
      }
    }

    const now = new Date();
    
    // Prepare arrays for the 3 buckets
    const bucketPositions = [[], [], []] as number[][];
    
    // Calculate star positions
    // starsData has ra (degrees), dec (degrees), mag (number)
    for (const star of (starsData as any[])) {
      const { alt, az } = convertEquatorialToHorizontal(
        now,
        { latitude: lat, longitude: lon },
        { ra: star.ra, dec: star.dec }
      );

      if (alt < 0) continue;
      const altRad = THREE.MathUtils.degToRad(alt);
      const azRad = THREE.MathUtils.degToRad(az);

      const r = 500; // Radius of our sky dome
      
      const y = r * Math.sin(altRad);
      const x = r * Math.cos(altRad) * Math.sin(azRad);
      const z = -r * Math.cos(altRad) * Math.cos(azRad);

      let bIdx = 2; // Default smallest
      if (star.mag < 2) bIdx = 0;
      else if (star.mag < 4) bIdx = 1;

      bucketPositions[bIdx]!.push(x, y, z);
    }

    for (let i = 0; i < 3; i++) {
      const points = starsGroupRef.current.children[i] as THREE.Points;
      // Use exact Float32Array size to avoid rendering 0,0,0
      const posArray = new Float32Array(bucketPositions[i] || []);
      points.geometry.setAttribute("position", new THREE.BufferAttribute(posArray, 3));
      points.geometry.computeBoundingSphere();
    }
    
    // Update constellation lines
    if (constellationsRef.current) {
      const linePositions: number[] = [];
      const r = 500;
      for (const segment of (constellationsData as number[][][])) {
        for (let i = 0; i < segment.length - 1; i++) {
          const pt1 = segment[i];
          const pt2 = segment[i+1];
          
          const hor1 = convertEquatorialToHorizontal(now, { latitude: lat, longitude: lon }, { ra: pt1![0] as number, dec: pt1![1] as number });
          const alt1Rad = THREE.MathUtils.degToRad(hor1.alt);
          const az1Rad = THREE.MathUtils.degToRad(hor1.az);
          const y1 = r * Math.sin(alt1Rad);
          const x1 = r * Math.cos(alt1Rad) * Math.sin(az1Rad);
          const z1 = -r * Math.cos(alt1Rad) * Math.cos(az1Rad);
          
          const hor2 = convertEquatorialToHorizontal(now, { latitude: lat, longitude: lon }, { ra: pt2![0] as number, dec: pt2![1] as number });
          if (hor1.alt < 0 && hor2.alt < 0) continue;
          const alt2Rad = THREE.MathUtils.degToRad(hor2.alt);
          const az2Rad = THREE.MathUtils.degToRad(hor2.az);
          const y2 = r * Math.sin(alt2Rad);
          const x2 = r * Math.cos(alt2Rad) * Math.sin(az2Rad);
          const z2 = -r * Math.cos(alt2Rad) * Math.cos(az2Rad);
          
          linePositions.push(x1, y1, z1, x2, y2, z2);
        }
      }
      const lineGeom = constellationsRef.current.geometry;
      lineGeom.setAttribute("position", new THREE.Float32BufferAttribute(linePositions, 3));
      lineGeom.computeBoundingSphere();
    }

    // Planets
    const planets = getPlanetaryPositions(now, { latitude: lat, longitude: lon });
    const planetPositions = [];
    
    // Ensure we have planet labels
    // Clean up old planet labels first
    if (labelElementsRef.current) {
      const existingPlanets = labelElementsRef.current.filter(l => l.isPlanet);
      existingPlanets.forEach(p => p.el.remove());
      labelElementsRef.current = labelElementsRef.current.filter(l => !l.isPlanet);
    }
    
    const r = 500;
    for (const p of planets) {
      if (p.alt < 0) {
        // Find existing label if we can and hide it, or just skip
        continue;
      }
      const altRad = THREE.MathUtils.degToRad(p.alt);
      const azRad = THREE.MathUtils.degToRad(p.az);
      const y = r * Math.sin(altRad);
      const x = r * Math.cos(altRad) * Math.sin(azRad);
      const z = -r * Math.cos(altRad) * Math.cos(azRad);
      planetPositions.push(x, y, z);
      
      // Add dynamic planet label
      if (labelsContainerRef.current) {
        const el = document.createElement("div");
        const thNames: Record<string, string> = {
          "Mercury": "ดาวพุธ",
          "Venus": "ดาวศุกร์",
          "Mars": "ดาวอังคาร",
          "Jupiter": "ดาวพฤหัสบดี",
          "Saturn": "ดาวเสาร์",
          "Uranus": "ดาวยูเรนัส",
          "Neptune": "ดาวเนปจูน"
        };
        const pName = p.name as string;
        const thName = thNames[pName] || pName;
        
        el.textContent = isThai ? thName : pName;
        el.dataset.enName = pName;
        el.dataset.thName = thName;
        
        el.style.position = "absolute";
        el.style.color = "rgba(255, 100, 100, 1)";
        el.style.fontSize = "14px";
        el.style.fontWeight = "bold";
        el.style.pointerEvents = "none";
        el.style.transform = "translate(-50%, -50%)";
        el.style.display = "none";
        el.dataset.x = x.toString();
        el.dataset.y = y.toString();
        el.dataset.z = z.toString();
        
        labelsContainerRef.current.appendChild(el);
        labelElementsRef.current.push({ el, ra: p.ra, dec: p.dec, isPlanet: true });
      }
    }
    
    if (planetsGroupRef.current) {
      planetsGroupRef.current.geometry.setAttribute("position", new THREE.Float32BufferAttribute(planetPositions, 3));
      planetsGroupRef.current.geometry.computeBoundingSphere();
    }

    // Update label 3D positions (for stars and constellations)
    if (labelElementsRef.current.length > 0) {
      for (const label of labelElementsRef.current) {
        if (label.isPlanet) continue; // Already calculated
        const hor = convertEquatorialToHorizontal(now, { latitude: lat, longitude: lon }, { ra: label.ra, dec: label.dec });
        if (hor.alt < 0) {
            label.el.dataset.hidden = "true";
            continue;
        } else {
            label.el.dataset.hidden = "false";
        }
        const altRad = THREE.MathUtils.degToRad(hor.alt);
        const azRad = THREE.MathUtils.degToRad(hor.az);
        const y = r * Math.sin(altRad);
        const x = r * Math.cos(altRad) * Math.sin(azRad);
        const z = -r * Math.cos(altRad) * Math.cos(azRad);
        
        // Store on dataset for the animate loop
        label.el.dataset.x = x.toString();
        label.el.dataset.y = y.toString();
        label.el.dataset.z = z.toString();
      }
    }

    rendererRef.current.render(sceneRef.current, cameraRef.current);
  }, [vehicles]); // Re-run when vehicles location updates

  // Update label text when language changes
  useEffect(() => {
    if (labelElementsRef.current.length > 0) {
      labelElementsRef.current.forEach((labelObj, i) => {
        if (labelObj.isPlanet) {
           labelObj.el.textContent = isThai ? labelObj.el.dataset.thName || labelObj.el.dataset.enName || "" : labelObj.el.dataset.enName || "";
        } else if (labelObj.el.dataset.enName) {
           labelObj.el.textContent = isThai && labelObj.el.dataset.thName ? labelObj.el.dataset.thName : labelObj.el.dataset.enName;
        } else if (i < constellationNames.length) {
           const nameObj = constellationNames[i] as any;
           labelObj.el.textContent = isThai && nameObj.name_th ? nameObj.name_th : nameObj.name;
        }
      });
    }
  }, [isThai]);

  // Animation loop logic to project labels
  useEffect(() => {
    let animationFrameId: number;
    const animateLabels = () => {
      if (cameraRef.current && containerRef.current && labelElementsRef.current.length > 0) {
        const w = containerRef.current.clientWidth;
        const h = containerRef.current.clientHeight;
        const camera = cameraRef.current;
        const vec = new THREE.Vector3();
        
        // We need 3D coords of the labels.
        // We can retrieve them if we store them in the label objects!
        for (const label of labelElementsRef.current) {
          if (label.el.dataset.x !== undefined) {
            vec.set(parseFloat(label.el.dataset.x!), parseFloat(label.el.dataset.y!), parseFloat(label.el.dataset.z!));
            vec.project(camera);
            
            // If z > 1, it's behind the camera
            if (label.el.dataset.hidden === "true" || vec.z > 1.0 || vec.z < -1.0) {
              label.el.style.display = "none";
            } else {
              const screenX = (vec.x * 0.5 + 0.5) * w;
              const screenY = (vec.y * -0.5 + 0.5) * h;
              
              // Only show if it's on screen
              if (screenX >= 0 && screenX <= w && screenY >= 0 && screenY <= h) {
                label.el.style.display = "block";
                label.el.style.left = `${screenX}px`;
                label.el.style.top = `${screenY}px`;
              } else {
                label.el.style.display = "none";
              }
            }
          }
        }
      }
      animationFrameId = requestAnimationFrame(animateLabels);
    };
    animateLabels();
    return () => cancelAnimationFrame(animationFrameId);
  }, []);

  return (
    <div
      className="celestial-skybox"
      style={{
        position: "absolute",
        top: 0,
        left: 0,
        width: "100%",
        height: "100%",
        zIndex: 0, // Keep it under map context but above body
        backgroundColor: "#030814",
        pointerEvents: "none"
      }}
    >
      <div ref={containerRef} style={{ width: "100%", height: "100%" }} />
      <div ref={labelsContainerRef} style={{ position: "absolute", top: 0, left: 0, width: "100%", height: "100%", overflow: "hidden" }} />
    </div>
  );
}

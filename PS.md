# NATIONAL SPACE HACKATHON 2026

### Orbital Debris Avoidance & Constellation Management System

**Problem Statement**
*Hosted by Indian Institute of Technology, Delhi*

Contents
1 Background
2 Core Objectives
3 Physics, Coordinate Systems, and Orbital Mechanics
3.1 Reference Frames and State Vectors
3.2 Orbital Propagation Models
3.3 Conjunction Thresholds
4 API Specifications and Constraints
4.1 Telemetry Ingestion API
4.2 Maneuver Scheduling API
4.3 Simulation Fast-Forward (Tick) API
5 Detailed Maneuver & Navigation Logic
5.1 Propulsion Constraints and Fuel Mass Depletion
5.2 The Station-Keeping Box
5.3 Maneuver Vectors: The RTN Frame
5.4 Communication Latency and Blackout Zones
5.5 Provided Datasets
5.5.1 Ground Station Network (groundstations.csv)
6 Frontend: The ”Orbital Insight” Visualizer
6.1 Performance Constraints
6.2 Required Visualization Modules
6.3 Visualization API Integration
7 Evaluation Criteria
8 Deployment Requirements
9 Expected Deliverables

## 1. Background

Over the past decade, Low Earth Orbit (LEO) has transformed from a vast frontier into a highly congested orbital highway. The rapid deployment of commercial mega-constellations has exponentially increased the number of active payloads. Alongside these operational satellites, millions of pieces of space debris ranging from defunct rocket bodies and shattered solar panels to stray bolts orbit the Earth at hypervelocity speeds exceeding 27,000 km/h.

This severe congestion brings us perilously close to the Kessler Syndrome, a theoretical scenario proposed by NASA scientist Donald Kessler. In this scenario, the density of objects in LEO becomes high enough that a single collision generates a cloud of shrapnel, triggering a cascading chain reaction of further collisions. Because kinetic energy scales with the square of velocity, even a collision with a centimeter-sized fragment can completely destroy a satellite and instantly generate thousands of new trackable debris pieces.

Currently, satellite collision avoidance is a heavily manual, human-in-the-loop process. Ground-based radar networks, such as the US Space Surveillance Network (SSN), track large debris and issue Conjunction Data Messages (CDMs) when a close approach is predicted. Flight Dynamics Officers (FDOs) on Earth must manually evaluate these warnings, calculate the necessary orbital perturbations, and uplink thruster maneuver commands. However, this legacy approach suffers from critical bottlenecks that make it unsustainable for the future of spaceflight:

* **Scalability Limits**: Manual evaluation cannot scale to handle constellations comprising thousands of satellites, which may collectively face hundreds of conjunction warnings daily.
* **Communication Latency & Blackouts**: Satellites frequently pass through "blackout zones" (such as over deep oceans) where no ground station has line-of-sight. If a conjunction is predicted while a satellite is out of contact, ground control is entirely helpless.
* **Suboptimal Resource Management**: Fuel in space is a finite, non-replenishable resource. Human operators struggle to globally optimize fuel consumption across an entire fleet while simultaneously ensuring satellites return to their assigned orbital slots to maintain mission uptime.

**The Challenge:** The space industry requires a paradigm shift from ground-reliant piloting to onboard autonomy. Your task is to design an Autonomous Constellation Manager (ACM). You must develop a robust, high-performance software suite capable of ingesting high-volume orbital telemetry, predicting conjunctions efficiently without $O(N^{2})$ bottlenecks, and autonomously executing optimal evasion and return maneuvers.

---

## 2. Core Objectives

The primary objective of this hackathon is to architect, develop, and deploy an Autonomous Constellation Manager (ACM). This backend system will act as a centralized, high-performance "brain" for a fleet of over 50 active satellites, navigating a hazardous environment populated by tens of thousands of tracked space debris fragments. Participants must move beyond simple reactive scripting to build a system capable of predictive modeling, spatial optimization, and automated decision-making. Your ACM must successfully handle the following core responsibilities:

* **High-Frequency Telemetry Ingestion**: Your system must establish a robust pipeline to continuously process incoming orbital state vectors specifically, position (r) and velocity (v) in the Earth-Centered Inertial (ECI) coordinate frame. This data stream will represent the real-time kinematic states of both your controlled constellation and the uncontrolled debris field.
* **Predictive Conjunction Assessment (CA)**: The software must forecast potential collisions (Conjunction Data Messages) up to 24 hours in the future. Because checking every satellite against every piece of debris is an $O(N^{2})$ operation, participants must implement highly efficient spatial indexing algorithms to calculate the Time of Closest Approach (TCA) without exceeding computational or time constraints.
* **Autonomous Collision Avoidance (COLA)**: When a critical conjunction (a miss distance of 100 meters) is predicted, the system must autonomously calculate and schedule an evasion maneuver. This involves determining the optimal burn window and the exact change in velocity vector required to push the satellite to a safe standoff distance, factoring in thruster cooldowns and orbital mechanics.
* **Station-Keeping and Orbital Recovery**: A satellite is only useful when it is in its assigned mission slot. Evasion maneuvers will inherently perturb the satellite's orbit, so the ACM must calculate and execute a subsequent "recovery burn" to correct the orbital drift and return the payload to its designated spatial bounding box as quickly as possible.
* **Propellant Budgeting & End-of-Life (EOL) Management**: Spacecraft cannot refuel. Every burn depletes the finite propellant mass, governed by the Tsiolkovsky rocket equation. Your software must track these fuel budgets strictly; if a satellite's fuel reserves drop to a critical threshold (e.g., 5%), the system must preemptively schedule a final maneuver to move it into a safe "graveyard orbit".
* **Global Multi-Objective Optimization**: The ultimate algorithmic challenge is balancing two directly opposing metrics: maximizing Constellation Uptime while minimizing the total Fuel Expenditure across the fleet.

---

## 3. Physics, Coordinate Systems, and Orbital Mechanics

### 3.1 Reference Frames and State Vectors

All kinematic data in this simulation is grounded in the Earth-Centered Inertial (ECI) coordinate system (J2000 epoch). The ECI frame is non-rotating relative to the stars, making it the standard for calculating orbital trajectories without the fictitious forces present in Earth-Centered, Earth-Fixed (ECEF) frames. Every object in the simulation is defined by a 6-dimensional State Vector at a given time *t*:

$$S(t)=\begin{bmatrix}\vec{r}(t)\\\vec{v}(t)\end{bmatrix}=[x,\,y,\,z,\,v_{x},\,v_{y},\,v_{z}]^{T}$$

Position is in kilometers (km) and velocity is in kilometers per second (km/s).

### 3.2 Orbital Propagation Models

Participants cannot assume simple, unperturbed two-body Keplerian orbits. Your propagation engine must, at a minimum, account for the J2 perturbation. The equations of motion governing a satellite are given by the second-order ordinary differential equation:

$$\frac{d^{2}\vec{r}}{dt^{2}}=-\frac{\mu}{|\vec{r}|^{3}}\vec{r}+\vec{a}_{J2}$$

Where the Earth's standard gravitational parameter $\mu$ is 398600.4418, and the J2 acceleration vector is defined as:

$$\vec{a}_{J2}=\frac{3}{2}\frac{J_{2}\,\mu\,R_E^{2}}{|\vec{r}|^{5}}\begin{bmatrix}x\!\left(5\dfrac{z^{2}}{r^{2}}-1\right)\\[6pt]y\!\left(5\dfrac{z^{2}}{r^{2}}-1\right)\\[6pt]z\!\left(5\dfrac{z^{2}}{r^{2}}-3\right)\end{bmatrix}$$

where $r=|\vec{r}|$ and $R_E=6378.137\;\text{km}$.

You are expected to use robust numerical integration methods (e.g., Runge-Kutta 4th Order) to propagate these states forward in time, assuming Earth radius is 6378.137 km and J2 is 1.08263e-3.

### 3.3 Conjunction Thresholds

A collision is defined mathematically when the Euclidean distance between a satellite and any debris object falls below the critical threshold:

$$|\vec{r}_{sat}(t)-\vec{r}_{deb}(t)|<0.100 \;\text{km}$$ (100 meters)

---

## 4. API Specifications and Constraints

### 4.1 Telemetry Ingestion API

**Endpoint:** `POST /api/telemetry`

```json
{
  "timestamp": "2026-03-12T08:00:00.000Z",
  "objects": [
    {
      "id": "DEB-99421",
      "type": "DEBRIS",
      "r": {"x": 4500.2, "y": -2100.5, "z": 4800.1},
      "v": {"x": 1.25, "y": 6.84, "z": 3.12}
    }
  ]
}
```

```json
{
  "status": "ACK",
  "processed_count": 1,
  "active_cdm_warnings": 3
}
```

### 4.2 Maneuver Scheduling API

**Endpoint:** `POST /api/maneuver/schedule`

```json
{
  "satelliteId": "SAT-Alpha-04",
  "maneuver_sequence": [
    {
      "burn_id": "EVASION_BURN_1",
      "burnTime": "2026-03-12T14:15:30.000Z",
      "deltaV_vector": {"x": 0.002, "y": 0.015, "z": -0.001}
    },
    {
      "burn_id": "RECOVERY_BURN_1",
      "burnTime": "2026-03-12T15:45:30.000Z",
      "deltaV_vector": {"x": -0.0019, "y": -0.014, "z": 0.001}
    }
  ]
}
```

```json
{
  "status": "SCHEDULED",
  "validation": {
    "ground_station_los": true,
    "sufficient_fuel": true,
    "projected_mass_remaining_kg": 548.12
  }
}
```

### 4.3 Simulation Fast-Forward (Tick) API

**Endpoint:** `POST /api/simulate/step`

```json
{
  "step_seconds": 3600
}
```

```json
{
  "status": "STEP_COMPLETE",
  "new_timestamp": "2026-03-12T09:00:00.000Z",
  "collisions_detected": 0,
  "maneuvers_executed": 2
}
```

---

## 5. Detailed Maneuver & Navigation Logic

### 5.1 Propulsion Constraints and Fuel Mass Depletion

* **Dry Mass**: 500.0 kg
* **Initial Propellant Mass**: 50.0 kg (Total initial wet mass is 550.0 kg)
* **Specific Impulse**: 300.0 s
* **Maximum Thrust Limit**: 15.0 m/s per individual burn command
* **Thermal Cooldown**: 600 seconds between burns

$$\Delta m=m_{current}(1-e^{-\frac{|\Delta\vec{v}|}{I_{sp}\cdot g_{0}}})$$

Where standard gravity is 9.80665.

### 5.2 The Station-Keeping Box

* **Drift Tolerance**: Satellite must remain within a **10 km radius**
* **Uptime Penalty**: Exponential degradation if outside
* **Recovery Burn Requirement**: Mandatory return maneuver

### 5.3 Maneuver Vectors: The RTN Frame

* **Radial (R)** — From Earth center to satellite
* **Transverse (T)** — Along velocity direction
* **Normal (N)** — Orthogonal to orbital plane

Vectors must be converted from RTN → ECI before API submission.

### 5.4 Communication Latency and Blackout Zones

* **Line-of-Sight Requirement** with ground stations
* **10-second signal delay**
* Predictive maneuvers required during blackout zones

### 5.5 Provided Datasets

```csv
Station_ID, Station_Name, Latitude, Longitude, Elevation_m, Min_Elevation_Angle_deg
GS-001, ISTRAC_Bengaluru, 13.0333,77.5167,820,5.0
GS-002, Svalbard_Sat_Station, 78.2297,15.4077,400,5.0
GS-003, Goldstone_Tracking, 35.4266,-116.8900,1000,10.0
GS-004, Punta_Arenas, -53.1500,-70.9167,30,5.0
GS-005, IIT_Delhi_Ground_Node, 28.5450,77.1926,225,15.0
GS-006, McMurdo_Station, -77.8463,166.6682,10,5.0
```

---

## 6. Frontend: The "Orbital Insight" Visualizer

### Performance Constraints

Must render **50+ satellites and 10,000+ debris objects** at **60 FPS**.

### Required Visualization Modules

* **Ground Track Map (Mercator Projection)**
* **Conjunction Bullseye Plot**
* **Telemetry & Fuel Heatmaps**
* **Maneuver Timeline (Gantt Scheduler)**

### Visualization API

**Endpoint:** `GET /api/visualization/snapshot`

```json
{
  "timestamp": "2026-03-12T08:00:00.000Z",
  "satellites": [
    {
      "id": "SAT-Alpha-04",
      "lat": 28.545,
      "lon": 77.192,
      "fuel_kg": 48.5,
      "status": "NOMINAL"
    }
  ],
  "debris_cloud": [
    ["DEB-99421", 12.42, 45.21, 400.5],
    ["DEB-99422", 12.55, -45.10, 401.2]
  ]
}
```

---

## 7. Evaluation Criteria

| Criteria             | Weightage | Description                  |
| :------------------- | :-------- | :--------------------------- |
| Safety Score         | 25%       | % of collisions avoided      |
| Fuel Efficiency      | 20%       | Total Δv used                |
| Constellation Uptime | 15%       | Time satellites stay in slot |
| Algorithmic Speed    | 15%       | Backend complexity           |
| UI/UX                | 15%       | Visualization clarity        |
| Code Quality         | 10%       | Logging & modularity         |

---

## 8. Deployment Requirements

* **Dockerfile at repo root**
* **Base image:** `ubuntu:22.04`
* **Expose port 8000**

---

## 9. Expected Deliverables

1. Public GitHub repository
2. Dockerized backend
3. Technical report (PDF)
4. Demo video under 5 minutes

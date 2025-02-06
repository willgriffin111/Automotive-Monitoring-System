<script>
  import { onMount, tick } from "svelte";
  import L from "leaflet";
  import "leaflet/dist/leaflet.css";
  import Chart from "chart.js/auto";

  let map;
  let routeData = [];
  let markers = [];
  let showPoints = true;
  let selectedMetric = "default";

  let message = "Not Connected";
  let processingMode = "post";

  let days = [];
  let selectedDay = "";
  let drives = [];
  let selectedDrive = "";

  let chart1, chart2, chart3, chart4, chart5, chart6, chart7, chart8;

  // Variables to hold the live data values
  let liveTime = "";
  let liveSpeed = "";
  let liveRPM = "";
  let liveInstantMPG = "";
  let liveThrottle = "";
  let liveAccelX = "";
  let liveAccelY = "";

  // Stores the interval for live updates
  let liveDataInterval;

  async function checkConnection() {
    try {
      console.log("Fetching connection status...");
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 1000);
      const response = await fetch("http://192.168.4.1/", { signal: controller.signal });
      clearTimeout(timeout);
      if (!response.ok) throw new Error("Network response error");
      const text = await response.text();

      if (text.includes("Connected")) {
        message = "Connected";
        fetchDays();
      } else {
        message = "Not Connected";
      }
    } catch (error) {
      console.error("Fetch error:", error);
      message = "Not Connected";
    }
    await tick();
  }

  async function fetchDays() {
    try {
      console.log("Fetching available days...");
      const response = await fetch("http://192.168.4.1/days");
      if (!response.ok) throw new Error("Failed to fetch days");
      days = await response.json();
      selectedDay = days.length ? days[0] : "";
      fetchDrives();
    } catch (error) {
      console.error("Fetch error:", error);
      days = [];
    }
    await tick();
  }

  async function fetchDrives() {
    if (!selectedDay) return;
    try {
      console.log(`Fetching drives for ${selectedDay}...`);
      const response = await fetch(`http://192.168.4.1/drives?day=${selectedDay}`);
      if (!response.ok) throw new Error("Failed to fetch drives");
      drives = await response.json();
      selectedDrive = drives.length ? drives[0] : "";
    } catch (error) {
      console.error("Fetch error:", error);
      drives = [];
    }
    await tick();
  }

  async function loadDrive() {
    if (!selectedDay || !selectedDrive) return;

    try {
      console.log(`Loading drive ${selectedDrive} for day ${selectedDay}...`);
      const response = await fetch(`http://192.168.4.1/drive?day=${selectedDay}&drive=${selectedDrive}`);
      if (!response.ok) throw new Error("Failed to load drive");

      const text = await response.text();
      let driveData;
      try {
        // STANDARD JSON PARSING
        driveData = JSON.parse(text);
        if (!Array.isArray(driveData)) {
          throw new Error("Parsed JSON is not an array");
        }
      } catch (error) {
        // NDJSON PARSING
        driveData = text
          .split("\n")
          .filter((line) => line.trim() !== "")
          .map((line) => JSON.parse(line));
      }
      
      routeData = driveData;
      await tick();

      if (routeData.length > 0) {
        map.setView([routeData[0].gps.latitude, routeData[0].gps.longitude], 14);
      }

      updateRouteLine();
      updateMarkers();
      updateCharts();
    } catch (error) {
      console.error("Fetch error:", error);
    }
  }
  async function fetchLiveData() {
    try {
        console.log("Fetching live data...");
        const response = await fetch("http://192.168.4.1/live");
        if (!response.ok) throw new Error("Failed to fetch live data");
        const text = await response.text();
        let liveData;
        try {
            liveData = JSON.parse(text);
            if (!Array.isArray(liveData)) {
                liveData = [liveData];
            }
        } catch (error) {
            liveData = text.split("\n")
                .filter((line) => line.trim() !== "")
                .map((line) => JSON.parse(line));
        }

        if (liveData.length > 0) {
            // Append new data if it's actually new
            if (
                routeData.length === 0 ||
                routeData[routeData.length - 1].gps.time !== liveData[0].gps.time
            ) {
                routeData = [...routeData, ...liveData];

                // Update map view to the latest point 
                const latestPoint = liveData[liveData.length - 1].gps;
                map.setView([latestPoint.latitude, latestPoint.longitude], 16, { animate: true });
            } else {
                console.log("Duplicate data; not appending.");
            }
        }

        await tick();

        if (routeData.length > 0) {
            const latest = routeData[routeData.length - 1];
            liveTime = latest.gps.time;
            liveSpeed = latest.obd.speed;
            liveRPM = latest.obd.rpm;
            liveInstantMPG = latest.obd.instant_mpg.toFixed(2);
            liveThrottle = latest.obd.throttle;
            liveAccelX = (latest.imu.accel_x * 0.001 * 9.81).toFixed(2);
            liveAccelY = (latest.imu.accel_y * 0.001 * 9.81).toFixed(2);
        }

        // Call update functions
        updateRouteLine();
        updateMarkers();
        updateCharts();
    } catch (error) {
        console.error("Error fetching live data:", error);
    }
}

  // Initialise the map
  onMount(() => {
    map = L.map("map").setView([0, 0], 2);
    L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
      attribution:
        '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
    }).addTo(map);
  });

  // Reactive block: update map and charts whenever routeData changes.
  $: if (routeData.length) {
    updateRouteLine();
    updateMarkers();
    updateCharts();
  }


  // Initialise the map
  onMount(() => {
    map = L.map("map").setView([0, 0], 2);
    L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
      attribution:
        '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
    }).addTo(map);
  });

  // Reactive block to start/stop live polling depending on processingMode.
  $: {
  if (processingMode === "real-time") {
    // Clear map when switching to real-time mode
    routeData = [];
    updateRouteLine();
    updateMarkers();
    
    if (!liveDataInterval) {
      checkConnection();
      fetchLiveData(); // Initial fetch
      liveDataInterval = setInterval(() => {
        fetchLiveData();
      }, 2000);
    }
  } else {
    if (liveDataInterval) {
      clearInterval(liveDataInterval);
      liveDataInterval = null;
    }
  }
}


  // Chart creation 
  const createChart = (ctx, label, data, color, xLabel, yLabel, labels) => {
    return new Chart(ctx, {
      type: "line",
      data: {
        labels: labels,
        datasets: [{ label, data, borderColor: color, fill: false }],
      },
      options: {
        animation: false,
        responsive: true,
        plugins: { legend: { display: true } },
        scales: {
          x: { title: { display: true, text: xLabel }, ticks: { maxRotation: 45 } },
          y: { title: { display: true, text: yLabel } },
        },
      },
    });
  };

  function updateCharts() {
    const timestamps = routeData.map((entry) => entry.gps.time);
    const speeds = routeData.map((entry) => entry.obd.speed);
    const rpms = routeData.map((entry) => entry.obd.rpm);
    const instantMpg = routeData.map((entry) => entry.obd.instant_mpg);
    const avgMpg = routeData.map((entry) => entry.obd.avg_mpg);
    const throttlePositions = routeData.map((entry) => entry.obd.throttle);
    const mafValues = routeData.map((entry) => entry.obd.maf);
    const accelX = routeData.map((entry) => entry.imu.accel_x * 0.001 * 9.81);
    const accelY = routeData.map((entry) => entry.imu.accel_y * 0.001 * 9.81);

    if (chart1) chart1.destroy();
    if (chart2) chart2.destroy();
    if (chart3) chart3.destroy();
    if (chart4) chart4.destroy();
    if (chart5) chart5.destroy();
    if (chart6) chart6.destroy();
    if (chart7) chart7.destroy();
    if (chart8) chart8.destroy();

    chart1 = createChart(document.getElementById("chart1"), "Speed (kph)", speeds, "orange", "Time", "Speed (kph)", timestamps);
    chart2 = createChart(document.getElementById("chart2"), "RPM", rpms, "red", "Time", "RPM", timestamps);
    chart3 = createChart(document.getElementById("chart3"), "Instant MPG", instantMpg, "green", "Time", "MPG", timestamps);
    chart4 = createChart(document.getElementById("chart4"), "Average MPG", avgMpg, "purple", "Time", "MPG", timestamps);
    chart5 = createChart(document.getElementById("chart5"), "Throttle Position", throttlePositions, "brown", "Time", "Throttle (%)", timestamps);
    chart6 = createChart(document.getElementById("chart6"), "Accel X", accelX, "magenta", "Time", "Acceleration (m/s²)", timestamps);
    chart7 = createChart(document.getElementById("chart7"), "Accel Y", accelY, "darkblue", "Time", "Acceleration (m/s²)", timestamps);
    chart8 = createChart(document.getElementById("chart8"), "MAF", mafValues, "cyan", "Time", "MAF (g/s)", timestamps);
  }

  function updateMarkers() {
  // Remove all existing markers first
  markers.forEach((marker) => marker.remove());
  markers = [];

  if (showPoints) {
    routeData.forEach((point) => {
      const { latitude, longitude, time } = point.gps;
      const { speed, rpm, instant_mpg, throttle } = point.obd;
      
      // Create a unique marker is created each time
      const marker = L.marker([latitude, longitude]).addTo(map)
        .bindPopup(`
          <b>Time:</b> ${time}<br>
          <b>Speed:</b> ${speed} km/h<br>
          <b>RPM:</b> ${rpm}<br>
          <b>Instant MPG:</b> ${instant_mpg.toFixed(2)}<br>
          <b>Throttle:</b> ${throttle}%
        `);

      markers.push(marker);
    });
  }
}


  function updateRouteLine() {
  // Remove old polylines before adding a new one
  map.eachLayer((layer) => {
    if (layer instanceof L.Polyline) {
      map.removeLayer(layer);
    }
  });

  const route = routeData.map((point) => [point.gps.latitude, point.gps.longitude]);

  if (route.length < 2) return; // Skip if no route data

  let metricValues;
  if (selectedMetric === "accel_x" || selectedMetric === "accel_y") {
    metricValues = routeData.map((point) => point.imu[selectedMetric] * 0.001 * 9.81);
  } else if (selectedMetric === "default") {
    metricValues = routeData.map(() => 1);
  } else {
    metricValues = routeData.map((point) => point.obd[selectedMetric]);
  }

  const maxMetric = Math.max(...metricValues);
  const minMetric = Math.min(...metricValues);
  const range = maxMetric - minMetric || 1;

  for (let i = 0; i < route.length - 1; i++) {
    const start = route[i];
    const end = route[i + 1];
    const normalizedValue = (metricValues[i] - minMetric) / range;
    const color = `rgb(${255 * normalizedValue}, 0, ${255 * (1 - normalizedValue)})`;

    // Add new route data to the map
    L.polyline([start, end], { color, weight: 5 }).addTo(map);
  }
}



  function togglePoints() {
    showPoints = !showPoints;
    updateMarkers();
  }

  // Initial connection check
  checkConnection();
</script>


<main>
  <h1>Driving Analysis Interface</h1>
  <div id="map-container">
    <div id="control-panel">
      {#if message != "Connected"}
        <div id="connection-status-container" style="background-color: #f8d7da; color: #721c24;">
          Status: {message}
          <button on:click={checkConnection}>Retry</button>
        </div>
      {:else}
        <div id="connection-status-container" style="background-color: #d4edda; color: #155724;">
          Status: Connected
          <button on:click={checkConnection}>Retry</button>
        </div>
      {/if}
    
      <label for="processing-mode">Processing Mode:</label>
      <select class="dropdown" bind:value={processingMode}>
        <option value="post">Post</option>
        <option value="real-time">Real-time</option>
      </select>
      
      {#if processingMode === "post"}
        <label for="day-select">Select Day:</label>
        <select class="dropdown" bind:value={selectedDay} on:change={fetchDrives}>
          {#each days as day}
            <option value={day}>{day}</option>
          {/each}
        </select>

        <label for="drive-select">Select Drive:</label>
        <select class="dropdown" bind:value={selectedDrive}>
          {#each drives as drive}
            <option value={drive}>{drive}</option>
          {/each}
        </select>

        <button on:click={loadDrive}>Load Drive</button>

        <div class="div-container">
          <label>Route Data Overlay:</label>
          {#each [
            { value: "default", label: "Default" },
            { value: "speed", label: "Speed" },
            { value: "instant_mpg", label: "Instant MPG" },
            { value: "throttle", label: "Throttle Position" },
            { value: "accel_x", label: "Accel X" },
            { value: "accel_y", label: "Accel Y" },
            { value: "rpm", label: "RPM" }
          ] as option}
            <label class="radio-option">
              <input 
                type="radio" 
                name="data-overlay" 
                bind:group={selectedMetric} 
                value={option.value} 
                on:change={updateRouteLine}
              />
              {option.label}
            </label>
          {/each}
        </div>
      {:else if processingMode === "real-time"}
        <label for="data-overlay">Route Data Overlay:</label>
        <select class="dropdown" name="data-overlay" bind:value={selectedMetric} on:change={updateRouteLine}>
          <option value="default">Default</option>
          <option value="speed">Speed</option>
          <option value="instant_mpg">Instant MPG</option>
          <option value="throttle">Throttle Position</option>
          <option value="accel_x">Accel X</option>
          <option value="accel_y">Accel Y</option>
          <option value="rpm">RPM</option>
        </select>

        <div class="div-container" id="live-data-container">
          <p class="live-data">Time: {liveTime}</p>
          <p class="live-data">Speed: {liveSpeed}</p>
          <p class="live-data">RPM: {liveRPM}</p>
          <p class="live-data">Instant MPG: {liveInstantMPG}</p>
          <p class="live-data">Throttle: {liveThrottle}</p>
          <p class="live-data">Accel X: {liveAccelX}</p>
          <p class="live-data">Accel Y: {liveAccelY}</p>
        </div>
      {/if}
      <button on:click={togglePoints}>
        {showPoints ? "Hide Points" : "Show Points"}
      </button>

      <!-- <button on:click={loadDrive}>Load Drive</button> -->
    </div>
    <div id="map"></div>
  </div>

    {#if routeData.length !== 0 && processingMode === "post"}
  <div id="graphs-container">
    <div class="chart"><canvas id="chart1"></canvas></div>
    <div class="chart"><canvas id="chart2"></canvas></div>
    <div class="chart"><canvas id="chart3"></canvas></div>
    <div class="chart"><canvas id="chart4"></canvas></div>
    <div class="chart"><canvas id="chart5"></canvas></div>
    <div class="chart"><canvas id="chart6"></canvas></div>
    <div class="chart"><canvas id="chart7"></canvas></div>
    <div class="chart"><canvas id="chart8"></canvas></div>
  </div>
  {/if}
</main>

<script>
  import { onMount, tick } from "svelte"; // Svelte functions
  import L from "leaflet"; // Leaflet for maps
  import "leaflet/dist/leaflet.css"; // Leaflet CSS
  import Chart from "chart.js/auto"; // Chart.js for graphs
  import { confirm } from '@tauri-apps/plugin-dialog'; // Tauri dialog for confirmation prompts

  //------------------------------------------------------------------------------  
  // Leaflet icon paths when bundling/building the app
  //------------------------------------------------------------------------------
  delete L.Icon.Default.prototype._getIconUrl;
  L.Icon.Default.mergeOptions({
    iconRetinaUrl: "/marker-icon-2x.png",
    iconUrl: "/marker-icon.png",
    shadowUrl: "/marker-shadow.png"
  });

  let map;                   // Leaflet map instance
  let routeData = [];        // Array of GPS/OBD/IMU data points
  let markers = [];          // Array of Leaflet marker instances
  let showPoints = true;     // Toggle whether to show markers
  let selectedMetric = "default";  // Metric used to color the route line

  // Connection & mode
  let message = "Not Connected";  // Connection status message
  let processingMode = "post";    // "post", "real-time", or "Settings"

  // Day/Drive selection
  let days = [];             // List of available recording days
  let selectedDay = "";      // Currently selected day
  let drives = [];           // List of drives for that day
  let selectedDrive = "";    // Currently selected drive file

  // Chart.js instances
  let chart1, chart2, chart3, chart4, chart5, chart6, chart7, chart8;
  let chart1Canvas, chart2Canvas, chart3Canvas, chart4Canvas;
  let chart5Canvas, chart6Canvas, chart7Canvas, chart8Canvas;

  // Live data display variables
  let liveTime = "", liveSpeed = "", liveRPM = "";
  let liveInstantMPG = "", liveAvgMPG = "";
  let liveThrottle = "", liveAccelX = "", liveAccelY = "";
  let speedMetric = "mph";   // Display speed in "mph" or "kph"

  // SD card info (Settings mode)
  let sdStatus = "Loading...";
  let totalSize = "Loading...";
  let usedSize = "Loading...";
  let freeSize = "Loading...";
  let upTime = "Loading...";

  // Interval handle for live polling
  let liveDataInterval;
  let lastLivePoint = null;

  //------------------------------------------------------------------------------  
  // onMount(): initialise Leaflet map and tile layer
  //------------------------------------------------------------------------------
  onMount(() => {
    map = L.map("map").setView([0, 0], 2);
    L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
      attribution:
        '&copy; <a href="https://www.openstreetmap.org/copyright">OSM</a>'
    }).addTo(map);
  });

  //------------------------------------------------------------------------------  
  // checkConnection()
  // - Tests HTTP GET to root endpoint to see if ESP32 server is reachable
  // - Sets `message` to "Connected" or "Not Connected"
  // - On success, triggers `fetchDays()` to populate UI
  //------------------------------------------------------------------------------
  async function checkConnection() {
    try {
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
    await tick();  // wait for UI to update
  }

  //------------------------------------------------------------------------------  
  // handleProcessingModeChange()
  // - Called when processingMode select changes
  // - If user switches to "Settings", fetch SD card info
  //------------------------------------------------------------------------------
  function handleProcessingModeChange() {
    if (processingMode === "Settings") {
      fetchDeviceInfo();
    }
  }

  //------------------------------------------------------------------------------  
  // fetchDeviceInfo()
  // - GET /sdinfo to retrieve SD card status, sizes, and ESP32 uptime
  // - Populates SD info variables or sets to "Fail" on error
  //------------------------------------------------------------------------------
  async function fetchDeviceInfo() {
    try {
      const response = await fetch("http://192.168.4.1/sdinfo");
      if (!response.ok) throw new Error("Failed to fetch SD info");
      const data = await response.json();
      sdStatus = data.sd_status;
      totalSize = data.total_size.toFixed(2) + " MB";
      usedSize = data.used_size.toFixed(2) + " MB";
      freeSize = data.free_size.toFixed(2) + " MB";
      upTime = data.esp32_uptime_sec + " Seconds";
    } catch (error) {
      console.error("Error fetching SD info:", error);
      sdStatus = totalSize = usedSize = freeSize = upTime = "Fail";
    }
    await tick();
  }

  //------------------------------------------------------------------------------  
  // fetchDays()
  // - GET /days to list date directories on SD card
  // - Populates `days` array and sets default selectedDay
  // - Triggers fetchDrives()
  //------------------------------------------------------------------------------
  async function fetchDays() {
    try {
      const response = await fetch("http://192.168.4.1/days");
      if (!response.ok) throw new Error("Failed to fetch days");
      days = await response.json();
      selectedDay = days.length ? days[0] : "";
      fetchDrives();
    } catch (error) {
      console.error("Fetch error:", error);
      days = [];
      selectedDay = "";
    }
    await tick();
  }

  //------------------------------------------------------------------------------  
  // fetchDrives()
  // - GET /drives?day=YYYY-MM-DD to list JSON files for selectedDay
  // - Populates `drives` array and sets default selectedDrive
  //------------------------------------------------------------------------------
  async function fetchDrives() {
    if (!selectedDay) return;
    try {
      const response = await fetch(`http://192.168.4.1/drives?day=${selectedDay}`);
      if (!response.ok) throw new Error("Failed to fetch drives");
      drives = await response.json();
      selectedDrive = drives.length ? drives[0] : "";
    } catch (error) {
      console.error("Fetch error:", error);
      drives = [];
      selectedDrive = "";
    }
    await tick();
  }

  //------------------------------------------------------------------------------  
  // loadDrive()
  // - GET /drive?day=...&drive=... to stream a selected drive JSON file
  // - Parses newline-delimited JSON into `routeData[]`
  // - Recenters map and updates map, markers and charts
  //------------------------------------------------------------------------------
  async function loadDrive() {
    if (!selectedDay || !selectedDrive) return;
    try {
      const response = await fetch(
        `http://192.168.4.1/drive?day=${selectedDay}&drive=${selectedDrive}`
      );
      if (!response.ok) throw new Error("Failed to load drive");
      const text = await response.text();
      routeData = text
        .split("\n")
        .filter(line => line.trim())
        .map(line => JSON.parse(line));
      await tick();
      if (routeData.length) {
        map.setView(
          [routeData[0].gps.latitude, routeData[0].gps.longitude],
          14
        );
      }
      updateRouteLine();
      updateMarkers();
      updateCharts();
    } catch (error) {
      console.error("Fetch error:", error);
    }
  }

  //------------------------------------------------------------------------------  
  // fetchLiveData()
  // - GET /live for the latest drive data chunk
  // - Appends new data points to `routeData`
  // - Updates live display variables, map, and charts
  //------------------------------------------------------------------------------
  async function fetchLiveData() {
    try {
      const response = await fetch("http://192.168.4.1/live");
      if (!response.ok) throw new Error("Failed to fetch live data");
      const text = await response.text();
      const liveData = text
        .split("\n")
        .filter(line => line.trim())
        .map(line => JSON.parse(line));

      if (liveData.length) {
        const newPoints = liveData.filter(pt =>
          !lastLivePoint || pt.gps.time > lastLivePoint.time
        );
        for (const pt of newPoints) {
          if (lastLivePoint) {
            L.polyline(
              [
                [lastLivePoint.latitude, lastLivePoint.longitude],
                [pt.gps.latitude, pt.gps.longitude]
              ],
              { weight: 5 }
            ).addTo(map);
          }
          routeData.push(pt);
          lastLivePoint = pt.gps;
        }
        const last = newPoints[newPoints.length - 1].gps;
        map.setView([last.latitude, last.longitude], 16, { animate: true });
      }

      await tick();  // wait for UI to update

      // Update live UI fields
      if (routeData.length) {
        const latest = routeData[routeData.length - 1];
        liveTime = latest.gps.time;
        liveSpeed =
          speedMetric === "kph"
            ? latest.obd.speed
            : (latest.obd.speed * 0.621371).toFixed(2);
        liveRPM = latest.obd.rpm;
        liveInstantMPG = latest.obd.instant_mpg.toFixed(2);
        liveAvgMPG = latest.obd.avg_mpg.toFixed(2);
        liveThrottle = latest.obd.throttle;
        liveAccelX = (latest.imu.accel_x * 0.001 * 9.81).toFixed(2);
        liveAccelY = (latest.imu.accel_y * 0.001 * 9.81).toFixed(2);
      }

      updateMarkers();
      updateCharts();
    } catch (error) {
      console.error("Error fetching live data:", error);
    }
  }

  //------------------------------------------------------------------------------  
  // Reactive: manage live polling based on processingMode
  //------------------------------------------------------------------------------
  $: {
    if (processingMode === "real-time") {
      routeData = [];
      lastLivePoint = null;
      updateMarkers();
      if (!liveDataInterval) {
        checkConnection();
        fetchLiveData();
        liveDataInterval = setInterval(fetchLiveData, 2000);
      }
    } else {
      if (liveDataInterval) {
        clearInterval(liveDataInterval);
        liveDataInterval = null;
      }
    }
  }

  //------------------------------------------------------------------------------  
  // Reactive: when routeData changes, refresh overlays and charts
  //------------------------------------------------------------------------------
  $: if (routeData.length) {
    updateRouteLine();
    updateMarkers();
    updateCharts();
  }

  //------------------------------------------------------------------------------  
  // deleteDayFolder()
  // - Prompts user for confirmation, then DELETE /delete?path=/YYYY-MM-DD
  // - On success, refreshes days & drives lists
  //------------------------------------------------------------------------------
  async function deleteDayFolder() {
    if (!selectedDay) return;
    const ok = await confirm(
      `Delete all drives for ${selectedDay}?`,
      { title: "Confirm Delete" }
    );
    if (!ok) return;
    try {
      const response = await fetch(
        `http://192.168.4.1/delete?path=/${selectedDay}`,
        { method: "DELETE" }
      );
      if (!response.ok) throw new Error("Delete failed");
      await fetchDays();
      await fetchDrives();
    } catch (error) {
      console.error("Error deleting day folder:", error);
    }
  }

  //------------------------------------------------------------------------------  
  // deleteDriveFile()
  // - Prompts user for confirmation, then DELETE /delete?path=/YYYY-MM-DD/drive.json
  // - On success, refreshes drives list
  //------------------------------------------------------------------------------
  async function deleteDriveFile() {
    if (!selectedDrive) return;
    const ok = await confirm(
      `Delete drive ${selectedDrive}?`,
      { title: "Confirm Delete" }
    );
    if (!ok) return;
    try {
      const path = `/${selectedDay}/${selectedDrive}`;
      const response = await fetch(
        `http://192.168.4.1/delete?path=${encodeURIComponent(path)}`,
        { method: "DELETE" }
      );
      if (!response.ok) throw new Error("Delete failed");
      await fetchDrives();
    } catch (error) {
      console.error("Error deleting drive file:", error);
    }
  }


  //------------------------------------------------------------------------------  
  // createChart()
  // - Helper to instantiate a Chart.js line chart with given params
  //------------------------------------------------------------------------------
  const createChart = (canvas, label, data, color, xLabel, yLabel, labels) => {
    return new Chart(canvas, {
      type: "line",
      data: { labels, datasets: [{ label, data, borderColor: color, fill: false }] },
      options: {
        animation: false,
        responsive: true,
        plugins: { legend: { display: true } },
        scales: {
          x: { title: { display: true, text: xLabel }, ticks: { maxRotation: 45 } },
          y: { title: { display: true, text: yLabel } }
        }
      }
    });
  };

  //------------------------------------------------------------------------------  
  // updateCharts()
  // - Destroys old charts, builds new ones from routeData
  //------------------------------------------------------------------------------
  async function updateCharts() {
    await tick();  // ensure canvases are in DOM

    const timestamps = routeData.map(e => e.gps.time);
    const speeds = routeData.map(e =>
      speedMetric === "kph" ? e.obd.speed : e.obd.speed * 0.621371
    );
    const rpms = routeData.map(e => e.obd.rpm);
    const instMpg = routeData.map(e => e.obd.instant_mpg);
    const avgMpg = routeData.map(e => e.obd.avg_mpg);
    const throttle = routeData.map(e => e.obd.throttle);
    const maf = routeData.map(e => e.obd.maf);
    const accelX = routeData.map(e => e.imu.accel_x * 0.001 * 9.81);
    const accelY = routeData.map(e => e.imu.accel_y * 0.001 * 9.81);

    // Destroy existing charts if they exist
    [chart1, chart2, chart3, chart4, chart5, chart6, chart7, chart8].forEach((chart, index) => {
      if (chart) {
      chart.destroy();
      eval(`chart${index + 1} = null`);
      }
    });

    // Create new charts
    chart1 = createChart(chart1Canvas, `Speed (${speedMetric})`, speeds, "orange", "Time", `Speed (${speedMetric})`, timestamps);
    chart2 = createChart(chart2Canvas, "RPM", rpms, "red", "Time", "RPM", timestamps);
    chart3 = createChart(chart3Canvas, "Instant MPG", instMpg, "green", "Time", "MPG", timestamps);
    chart4 = createChart(chart4Canvas, "Average MPG", avgMpg, "purple", "Time", "MPG", timestamps);
    chart5 = createChart(chart5Canvas, "Throttle Position", throttle, "brown", "Time", "% Throttle", timestamps);
    chart6 = createChart(chart6Canvas, "Accel X (m/s²)", accelX, "magenta", "Time", "Accel X", timestamps);
    chart7 = createChart(chart7Canvas, "Accel Y (m/s²)", accelY, "darkblue", "Time", "Accel Y", timestamps);
    chart8 = createChart(chart8Canvas, "MAF (g/s)", maf, "cyan", "Time", "MAF", timestamps);
  }

  //------------------------------------------------------------------------------  
  // updateMarkers()
  // - Clears old markers, then adds new markers for each point if showPoints
  // - Binds a popup displaying time, speed, RPM, mpg, throttle, accel
  //------------------------------------------------------------------------------
  function updateMarkers() {
    markers.forEach(m => m.remove());
    markers = [];
    if (!showPoints) return;
    for (const point of routeData) {
      const { latitude, longitude, time } = point.gps;
      const { speed, rpm, instant_mpg, avg_mpg, throttle } = point.obd;
      const m = L.marker([latitude, longitude]).addTo(map);
      const spdText = speedMetric === "mph"
        ? `${(speed * 0.621371).toFixed(2)} mph`
        : `${speed} km/h`;
      const popup = `
        <b>Time:</b> ${time}<br>
        <b>Speed:</b> ${spdText}<br>
        <b>RPM:</b> ${rpm}<br>
        <b>Instant MPG:</b> ${instant_mpg.toFixed(2)}<br>
        <b>Average MPG:</b> ${avg_mpg.toFixed(2)}<br>
        <b>Throttle:</b> ${throttle}%<br>
        <b>Accel X:</b> ${(point.imu.accel_x * 0.001 * 9.81).toFixed(2)} m/s²<br>
        <b>Accel Y:</b> ${(point.imu.accel_y * 0.001 * 9.81).toFixed(2)} m/s²
      `;
      m.bindPopup(popup);
      markers.push(m);
    }
  }

  //------------------------------------------------------------------------------  
  // updateRouteLine()
  // - Removes existing polylines then draws new ones colored by selectedMetric
  // - Default = solid line, or color-scale based on metric value
  //------------------------------------------------------------------------------
  function updateRouteLine() {
    // Remove old lines
    map.eachLayer(layer => {
      if (layer instanceof L.Polyline) map.removeLayer(layer);
    });

    const coords = routeData.map(p => [p.gps.latitude, p.gps.longitude]);
    if (coords.length < 2) return;

    let vals = routeData.map((_p, i) => {
      if (selectedMetric === "default") return 1;
      if (["accel_x","accel_y"].includes(selectedMetric)) {
        return routeData[i].imu[selectedMetric] * 0.001 * 9.81;
      }
      return routeData[i].obd[selectedMetric];
    });

    const min = Math.min(...vals), max = Math.max(...vals), range = max - min || 1;

    for (let i = 0; i < coords.length - 1; i++) {
      const t = (vals[i] - min) / range;
      const color = `rgb(${255 * t},0,${255 * (1 - t)})`;
      L.polyline([coords[i], coords[i+1]], { color, weight: 5 }).addTo(map);
    }
  }

  //------------------------------------------------------------------------------  
  // togglePoints()
  // - Toggles marker visibility on map
  //------------------------------------------------------------------------------
  function togglePoints() {
    showPoints = !showPoints;
    updateMarkers();
  }

  // Inital connection check
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

      <label for="processing-mode">Mode:</label>
      <select class="dropdown" bind:value={processingMode} on:change={handleProcessingModeChange}>
        <option value="post">Post</option>
        <option value="real-time">Real-time</option>
        <option value="Settings">Settings</option>
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
          <fieldset>
            <legend>Route Data Overlay:</legend>
            {#each [
              { value: "default", label: "Default" },
              { value: "speed", label: "Speed" },
              { value: "instant_mpg", label: "Instant MPG" },
              { value: "avg_mpg", label: "Average MPG" },
              { value: "throttle", label: "Throttle" },
              { value: "accel_x", label: "Acceleration" },
              { value: "accel_y", label: "Cornerning" },
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
          </fieldset>
        </div>
        <button on:click={togglePoints}>
          {showPoints ? "Hide Points" : "Show Points"}
        </button>
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
          <option value="avg_mpg">Average MPG</option>
        </select>

        <div class="div-container" id="live-data-container">
          <p class="live-data">Time: {liveTime}</p>
          {#if speedMetric === "kph"}
            <p class="live-data">Speed (kph): {liveSpeed}</p>
          {:else}
            <p class="live-data">Speed (mph): {liveSpeed}</p>
          {/if}
          <p class="live-data">RPM: {liveRPM}</p>
          <p class="live-data">Instant MPG: {liveInstantMPG}</p>
          <p class="live-data">Average MPG: {liveAvgMPG}</p>  
          <p class="live-data">Throttle: {liveThrottle}</p>
          <p class="live-data">Accel X: {liveAccelX}</p>
          <p class="live-data">Accel Y: {liveAccelY}</p>
        </div>
        <button on:click={togglePoints}>
          {showPoints ? "Hide Points" : "Show Points"}
        </button>
      {:else if processingMode === "Settings"}
        <label for="speed-metric">Speed metric: </label>
        <select name="speed-metric" class="dropdown" bind:value={speedMetric}>
          <option value="mph">MPH</option>
          <option value="kph">KPH</option>
        </select>
        <label for="sdinfo">SD card details: </label>
        <div class="sd-info-box">
          <p><b>SD Card Status:</b> {sdStatus}</p>
          <p><b>Total Size:</b> {totalSize}</p>
          <p><b>Used Size:</b> {usedSize}</p>
          <p><b>Free Size:</b> {freeSize}</p>
          <p><b>Up-time:</b> {upTime}</p>
        </div>

        <div class="delete-management">
          <div>
            <label for="delete-day-select">Select Day:</label>
            <select id="delete-day-select" class="dropdown" bind:value={selectedDay} on:change={fetchDrives}>
              {#each days as day}
                <option value={day}>{day}</option>
              {/each}
            </select>
          </div>
          <div>
            <label for="delete-drive-select">Select Drive:</label>
            <select id="delete-drive-select" class="dropdown" bind:value={selectedDrive}>
              {#each drives as drive}
                <option value={drive}>{drive}</option>
              {/each}
            </select>
          </div>
          <div id="delete-buttons-container">
            <button on:click={deleteDayFolder} class="delete-buttons">
              Delete All Drives for {selectedDay}
            </button>
            <button on:click={deleteDriveFile} class="delete-buttons">
              Delete Selected Drive on {selectedDrive}
            </button>
          </div>
          <button on:click={loadDrive}>Load Drive</button>
        </div>
      {/if}
    </div>

    <div id="map"></div>
  </div>

  {#if routeData.length !== 0 && processingMode === "post"}
    <div id="graphs-container">
      <div class="chart"><canvas bind:this={chart1Canvas} id="chart1"></canvas></div>
      <div class="chart"><canvas bind:this={chart2Canvas} id="chart2"></canvas></div>
      <div class="chart"><canvas bind:this={chart3Canvas} id="chart3"></canvas></div>
      <div class="chart"><canvas bind:this={chart4Canvas} id="chart4"></canvas></div>
      <div class="chart"><canvas bind:this={chart5Canvas} id="chart5"></canvas></div>
      <div class="chart"><canvas bind:this={chart6Canvas} id="chart6"></canvas></div>
      <div class="chart"><canvas bind:this={chart7Canvas} id="chart7"></canvas></div>
      <div class="chart"><canvas bind:this={chart8Canvas} id="chart8"></canvas></div>
    </div>
  {/if}
</main>

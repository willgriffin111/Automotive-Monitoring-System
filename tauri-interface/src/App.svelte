<script>
  import { onMount } from "svelte";
  import L from "leaflet";
  import "leaflet/dist/leaflet.css";
  import Chart from "chart.js/auto";

  let map;
  let routeData = [];
  let markers = [];
  let showPoints = true;
  let selectedMetric = "default";

  onMount(async () => {
    const response = await fetch("/data.json");
    const data = await response.json();
    routeData = data;

    map = L.map("map").setView([data[0].gps.latitude, data[0].gps.longitude], 14);

    L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
      attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
    }).addTo(map);

    updateRouteLine();
    updateMarkers();

    setTimeout(() => {
      map.invalidateSize();
    }, 100);

    const timestamps = data.map((entry) => entry.gps.time);
    const speeds = data.map((entry) => entry.obd.speed);
    const rpms = data.map((entry) => entry.obd.rpm);
    const instantMpg = data.map((entry) => entry.obd.instant_mpg);
    const avgMpg = data.map((entry) => entry.obd.avg_mpg);
    const throttlePositions = data.map((entry) => entry.obd.throttle);
    const mafValues = data.map((entry) => entry.obd.maf);
    const accelX = data.map((entry) => entry.imu.accel_x * 0.001 * 9.81);
    const accelY = data.map((entry) => entry.imu.accel_y * 0.001 * 9.81);

    const createChart = (ctx, label, data, color, xLabel, yLabel) => {
      new Chart(ctx, {
        type: "line",
        data: {
          labels: timestamps,
          datasets: [{ label, data, borderColor: color, fill: false }],
        },
        options: {
          responsive: true,
          plugins: { legend: { display: true } },
          scales: {
            x: { title: { display: true, text: xLabel }, ticks: { maxRotation: 45 } },
            y: { title: { display: true, text: yLabel } },
          },
        }
      });
    };

    createChart(document.getElementById("chart1"), "Speed (kph)", speeds, "orange", "Time", "Speed (kph)");
    createChart(document.getElementById("chart2"), "RPM", rpms, "red", "Time", "RPM");
    createChart(document.getElementById("chart3"), "Instant MPG", instantMpg, "green", "Time", "MPG");
    createChart(document.getElementById("chart4"), "Average MPG", avgMpg, "purple", "Time", "MPG");
    createChart(document.getElementById("chart5"), "Throttle Position", throttlePositions, "brown", "Time", "Throttle (%)");
    createChart(document.getElementById("chart6"), "Accel X", accelX, "magenta", "Time", "Acceleration (m/s²)");
    createChart(document.getElementById("chart7"), "Accel Y", accelY, "darkblue", "Time", "Acceleration (m/s²)");
    createChart(document.getElementById("chart8"), "MAF", mafValues, "cyan", "Time", "MAF (g/s)");
  });

  function updateMarkers() {
    markers.forEach((marker) => map.removeLayer(marker));
    markers = [];

    if (showPoints) {
      routeData.forEach((point) => {
        const { latitude, longitude, time } = point.gps;
        const { speed, rpm, instant_mpg, throttle } = point.obd;

        const marker = L.marker([latitude, longitude])
          .addTo(map)
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
    map.eachLayer((layer) => {
      if (layer instanceof L.Polyline) {
        map.removeLayer(layer);
      }
    });

    const route = routeData.map((point) => [point.gps.latitude, point.gps.longitude]);
    let metricValues;

    if (selectedMetric === "accel_x" || selectedMetric === "accel_y") {
      metricValues = routeData.map((point) => point.imu[selectedMetric] * 0.001 * 9.81);
    } else if (selectedMetric === "default") {
      metricValues = routeData.map((point) => 1);
    }
    else {
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
      const segment = L.polyline([start, end], { color, weight: 5 });
      segment.addTo(map);
    }
  }

  function togglePoints() {
    showPoints = !showPoints;
    updateMarkers();
  }
</script>

<main>
  <h1>Driving Analysis Interface</h1>
  <div id="map-container">
    <div id="control-panel">
      <h2>Map Control</h2>
      <label for="metric-select">Select Metric:</label>
      <select id="metric-select" bind:value={selectedMetric} on:change={updateRouteLine}>
        <option value="default">Default</option>
        <option value="speed">Speed</option>
        <option value="instant_mpg">Instant MPG</option>
        <option value="throttle">Throttle Position</option>
        <option value="accel_x">Accel X</option>
        <option value="accel_y">Accel Y</option>
        <option value="rpm">RPM</option>
      </select>
      <button on:click={togglePoints}>
        {showPoints ? "Hide Points" : "Show Points"}
      </button>
    </div>
    <div id="map"></div>
  </div>
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
</main>

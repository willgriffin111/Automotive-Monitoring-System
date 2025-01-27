<script>
  import { onMount } from "svelte";
  import L from "leaflet";
  import "leaflet/dist/leaflet.css";
  import Chart from "chart.js/auto"

  let map;
  let routeData = []; // Holds the loaded JSON data
  let rpmChart;

  onMount(async () => {
  const response = await fetch("/data.json");
  const data = await response.json();
  routeData = data;

  map = L.map("map").setView([data[0].gps.latitude, data[0].gps.longitude], 14);

  L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
    attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors',
  }).addTo(map);

  const route = data.map((point) => [point.gps.latitude, point.gps.longitude]);

  L.polyline(route, { color: "blue" }).addTo(map);

  data.forEach((point) => {
    const { latitude, longitude, time } = point.gps;
    const { speed, rpm, instant_mpg, throttle } = point.obd;

    L.marker([latitude, longitude])
      .addTo(map)
      .bindPopup(`
        <b>Time:</b> ${time}<br>
        <b>Speed:</b> ${speed} km/h<br>
        <b>RPM:</b> ${rpm}<br>
        <b>Instant MPG:</b> ${instant_mpg.toFixed(2)}<br>
        <b>Throttle:</b> ${throttle}%
      `);
  });

  // Force map to refresh its layout
  setTimeout(() => {
    map.invalidateSize();
  }, 100);

// Data Extraction
const timestamps = data.map((entry) => entry.gps.time);
    const latitudes = data.map((entry) => entry.gps.latitude);
    const longitudes = data.map((entry) => entry.gps.longitude);
    const speeds = data.map((entry) => entry.obd.speed);
    const rpms = data.map((entry) => entry.obd.rpm);
    const instantMpg = data.map((entry) => entry.obd.instant_mpg);
    const avgMpg = data.map((entry) => entry.obd.avg_mpg);
    const throttlePositions = data.map((entry) => entry.obd.throttle);
    const mafValues = data.map((entry) => entry.obd.maf);
    const accelX = data.map((entry) => entry.imu.accel_x * 0.001 * 9.81);
    const accelY = data.map((entry) => entry.imu.accel_y * 0.001 * 9.81);

    // Helper Function to Create Charts
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
        },
      });
    };

    // Initialise All 9 Graphs
    createChart(document.getElementById("chart1"), "Speed (kph)", speeds, "orange", "Time", "Speed (kph)");
    createChart(document.getElementById("chart2"), "RPM", rpms, "red", "Time", "RPM");
    createChart(document.getElementById("chart3"), "Instant MPG", instantMpg, "green", "Time", "MPG");
    createChart(document.getElementById("chart4"), "Average MPG", avgMpg, "purple", "Time", "MPG");
    createChart(document.getElementById("chart5"), "Throttle Position", throttlePositions, "brown", "Time", "Throttle (%)");
    createChart(document.getElementById("chart6"), "Accel X", accelX, "magenta", "Time", "Acceleration (m/s²)");
    createChart(document.getElementById("chart7"), "Accel Y", accelY, "darkblue", "Time", "Acceleration (m/s²)");
    createChart(document.getElementById("chart8"), "MAF", mafValues, "cyan", "Time", "MAF (g/s)");
  });
</script>

<main>
  <h1>Driving Analysis Interface</h1>
  <div id="map"></div>

  <!-- Graphs Section -->
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

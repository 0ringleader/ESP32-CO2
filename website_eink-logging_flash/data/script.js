let co2Chart, tempChart, humidityChart;
let allData = [];
let currentPage = 0;
const PAGE_SIZE = 144; // 24 hours at 10-min intervals
let isLoadingMore = false;
let hasMoreData = true;

// Zoom plugin options (shared across charts)
const zoomOptions = {
  pan: {
    enabled: true,
    mode: 'x',
    modifierKey: null,
  },
  zoom: {
    wheel: {
      enabled: true,
    },
    pinch: {
      enabled: true,
    },
    drag: {
      enabled: false,
    },
    mode: 'x',
  },
  limits: {
    x: { min: 'original', max: 'original' },
  },
};

function createChartConfig(label, data, labels, color, bgColor, yAxisLabel) {
  return {
    type: 'line',
    data: {
      labels: labels,
      datasets: [{
        label: label,
        data: data,
        borderColor: color,
        backgroundColor: bgColor,
        tension: 0.2,
        pointRadius: 2,
        pointHoverRadius: 5,
        fill: true,
      }]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      interaction: {
        mode: 'index',
        intersect: false,
      },
      scales: {
        y: {
          beginAtZero: false,
          title: { display: true, text: yAxisLabel }
        },
        x: {
          title: { display: true, text: 'Time' },
          ticks: {
            maxRotation: 45,
            minRotation: 0,
            autoSkip: true,
            maxTicksLimit: 20,
          }
        }
      },
      plugins: {
        legend: { display: false },
        zoom: zoomOptions,
        tooltip: {
          callbacks: {
            title: function(context) {
              const idx = context[0].dataIndex;
              if (allData[idx]) {
                const date = new Date(allData[idx].timestamp * 1000);
                return date.toLocaleString();
              }
              return context[0].label;
            }
          }
        }
      }
    }
  };
}

function formatTimeLabel(timestamp) {
  const date = new Date(timestamp * 1000);
  const now = new Date();
  const diffDays = Math.floor((now - date) / (1000 * 60 * 60 * 24));
  
  if (diffDays === 0) {
    return date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
  } else if (diffDays === 1) {
    return 'Yesterday ' + date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
  } else if (diffDays < 7) {
    return date.toLocaleDateString('en-US', { weekday: 'short' }) + ' ' + 
           date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
  } else {
    return date.toLocaleDateString('en-US', { month: 'short', day: 'numeric' }) + ' ' +
           date.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit' });
  }
}

function updateCharts() {
  const labels = allData.map(item => formatTimeLabel(item.timestamp));
  const co2Data = allData.map(item => item.co2);
  const tempData = allData.map(item => item.temperature);
  const humidityData = allData.map(item => item.humidity);

  const ctxCO2 = document.getElementById('co2Chart').getContext('2d');
  const ctxTemp = document.getElementById('tempChart').getContext('2d');
  const ctxHumidity = document.getElementById('humidityChart').getContext('2d');

  if (co2Chart) co2Chart.destroy();
  if (tempChart) tempChart.destroy();
  if (humidityChart) humidityChart.destroy();

  co2Chart = new Chart(ctxCO2, createChartConfig(
    'CO2 (ppm)', co2Data, labels, '#007BFF', 'rgba(0, 123, 255, 0.1)', 'CO2 (ppm)'
  ));

  tempChart = new Chart(ctxTemp, createChartConfig(
    'Temperature (°C)', tempData, labels, '#FF6384', 'rgba(255, 99, 132, 0.1)', 'Temperature (°C)'
  ));

  humidityChart = new Chart(ctxHumidity, createChartConfig(
    'Humidity (%)', humidityData, labels, '#36A2EB', 'rgba(54, 162, 235, 0.1)', 'Humidity (%)'
  ));

  updateDataRangeInfo();
}

function updateDataRangeInfo() {
  if (allData.length === 0) return;
  
  const oldest = new Date(allData[0].timestamp * 1000);
  const newest = new Date(allData[allData.length - 1].timestamp * 1000);
  
  const rangeText = `Showing ${allData.length} samples from ${oldest.toLocaleString()} to ${newest.toLocaleString()}`;
  
  let rangeEl = document.getElementById('data-range-info');
  if (!rangeEl) {
    rangeEl = document.createElement('div');
    rangeEl.id = 'data-range-info';
    rangeEl.className = 'data-range';
    document.querySelector('.chart-section').insertBefore(rangeEl, document.querySelector('.chart-header').nextSibling);
  }
  rangeEl.textContent = rangeText;
}

function resetZoom(chartType) {
  switch(chartType) {
    case 'co2': if (co2Chart) co2Chart.resetZoom(); break;
    case 'temp': if (tempChart) tempChart.resetZoom(); break;
    case 'humidity': if (humidityChart) humidityChart.resetZoom(); break;
  }
}

function setStatusColor(co2) {
  let color, label;
  if (co2 < 800) { color = '#4CAF50'; label = 'Good'; }
  else if (co2 < 1200) { color = '#FFC107'; label = 'Moderate'; }
  else if (co2 < 1500) { color = '#FF9800'; label = 'High'; }
  else { color = '#F44336'; label = 'Very High'; }

  document.getElementById('co2-status').style.color = color;
  document.getElementById('co2-health-label').innerHTML = label;
}

function fetchStatus() {
  fetch('/api/status')
    .then(response => response.json())
    .then(data => {
      document.getElementById('co2-status').innerHTML = data.co2 + ' ppm';
      document.getElementById('temp-value').innerHTML = data.temp + '°C';
      document.getElementById('humidity-value').innerHTML = data.humidity + '%';
      setStatusColor(data.co2);
      document.getElementById('alert-toggle').checked = data.alert_enabled;
    })
    .catch(error => console.error('Error fetching status:', error));
}

function fetchHistory(page = 0, append = false) {
  const loadBtn = document.getElementById('load-more-btn');
  const loadingIndicator = document.getElementById('loading-indicator');
  
  if (isLoadingMore) return;
  isLoadingMore = true;
  
  if (loadBtn) loadBtn.disabled = true;
  if (loadingIndicator) loadingIndicator.style.display = 'block';

  fetch(`/api/history?page=${page}&size=${PAGE_SIZE}`)
    .then(response => response.json())
    .then(data => {
      if (data.history && data.history.length > 0) {
        if (append) {
          // Prepend older data
          allData = [...data.history, ...allData];
        } else {
          allData = data.history;
        }
        updateCharts();
        
        // Check if there's more data
        hasMoreData = data.hasMore !== false && data.history.length >= PAGE_SIZE;
      } else {
        hasMoreData = false;
      }
      
      if (loadBtn) {
        loadBtn.disabled = !hasMoreData;
        loadBtn.textContent = hasMoreData ? 'Load Older Data' : 'No More Data';
      }
    })
    .catch(error => {
      console.error('Error fetching history:', error);
    })
    .finally(() => {
      isLoadingMore = false;
      if (loadingIndicator) loadingIndicator.style.display = 'none';
    });
}

function loadMoreData() {
  if (!hasMoreData || isLoadingMore) return;
  currentPage++;
  fetchHistory(currentPage, true);
}

function toggleAlert() {
  const isChecked = document.getElementById('alert-toggle').checked;
  fetch('/set-alert', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'enabled=' + (isChecked ? 'true' : 'false')
  })
  .then(response => {
    if (!response.ok) throw new Error('Network response was not ok');
  })
  .catch(error => {
    console.error('Error toggling alert:', error);
    document.getElementById('alert-toggle').checked = !isChecked;
  });
}

function measureNow() {
  const btn = document.getElementById('measure-button');
  btn.innerHTML = 'Measuring...';
  btn.disabled = true;

  fetch('/measure-now', { method: 'POST' })
    .then(response => {
      if (!response.ok) throw new Error('Network response was not ok');
      fetchStatus();
      // Refresh history to include new measurement
      currentPage = 0;
      fetchHistory(0, false);
    })
    .catch(error => console.error('Error forcing measurement:', error))
    .finally(() => {
      setTimeout(() => {
        btn.innerHTML = 'Force Measurement Now';
        btn.disabled = false;
      }, 3000);
    });
}

function clearLog() {
  if (!confirm('Are you sure you want to clear all logged data?')) return;
  
  fetch('/clear-log', { method: 'POST' })
    .then(response => {
      if (!response.ok) throw new Error('Network response was not ok');
      alert('Log cleared successfully');
      allData = [];
      currentPage = 0;
      hasMoreData = true;
      fetchHistory(0, false);
    })
    .catch(error => console.error('Error clearing log:', error));
}

// Initialize on page load
document.addEventListener('DOMContentLoaded', () => {
  fetchStatus();
  fetchHistory(0, false);
  
  // Periodic refresh of current status
  setInterval(fetchStatus, 5000);
  
  // Periodic refresh of latest data (but don't reset all data)
  setInterval(() => {
    // Only refresh latest page to catch new measurements
    fetch(`/api/history?page=0&size=${PAGE_SIZE}`)
      .then(response => response.json())
      .then(data => {
        if (data.history && data.history.length > 0) {
          // Find where old data ends and merge
          const latestStoredTimestamp = allData.length > 0 ? allData[allData.length - 1].timestamp : 0;
          const newEntries = data.history.filter(item => item.timestamp > latestStoredTimestamp);
          
          if (newEntries.length > 0) {
            allData = [...allData, ...newEntries];
            updateCharts();
          }
        }
      })
      .catch(error => console.error('Error refreshing history:', error));
  }, 600000); // Every 10 minutes
});
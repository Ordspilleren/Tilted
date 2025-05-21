<script lang="ts">
    import { onMount } from 'svelte';
    import { selectedSensorId, sensorData, loading, error, selectedDateRange } from './lib/stores';
    import { fetchSensorData } from './lib/api';
    import Header from './components/Header.svelte';
    import SensorSelector from './components/SensorSelector.svelte';
    import GravityChart from './components/GravityChart.svelte';
    import SensorDataDetail from './components/SensorData.svelte'; // Renamed import variable for clarity
      
    // Load data when sensor ID or date range changes
    $: if ($selectedSensorId && $selectedDateRange.startTime && $selectedDateRange.endTime) {
      loadSensorData($selectedSensorId, $selectedDateRange.startTime, $selectedDateRange.endTime);
    } else if (!$selectedSensorId) {
      // If sensor becomes unselected, clear data
      sensorData.set(null);
      // Optionally clear error if it's not a sensor ID loading error
      // if ($error && !($error.includes("sensor IDs") || $error.includes("No sensors found"))) {
      //   error.set(null);
      // }
    }
    
    async function loadSensorData(sensorId: string, startTime: number, endTime: number) {
      if (!sensorId || !startTime || !endTime) return;
      
      loading.set(true);
      error.set(null); // Clear previous data-fetching errors
      // sensorData.set(null); // Clear previous data to show loading indicator correctly
      
      try {
        const data = await fetchSensorData(sensorId, startTime, endTime);
        sensorData.set(data); // data can be null if fetchSensorData returns null on error
                              // or SensorData object if successful (even with empty dataPoints)
        if (!data) {
            // This case might not be hit if fetchSensorData throws errors as per its new implementation
            error.set('No data returned from server. Check console.');
        } else if (data.dataPoints.length === 0) {
            // This is not an error, just an empty set for the range. UI will handle the message.
        }
      } catch (err: any) {
        console.error(`Error loading sensor data:`, err);
        sensorData.set(null); // Ensure data is cleared on error
        error.set(err.message || 'Failed to load sensor data. Unknown error.');
      } finally {
        loading.set(false);
      }
    }
</script>
  
<div class="min-h-screen bg-gray-100">
  <Header />
  
  <main class="container mx-auto px-4 py-6">
    <div class="space-y-6">
      <SensorSelector />
      
      {#if $error}
        <div class="bg-red-100 border-l-4 border-red-500 text-red-700 p-4 mb-4">
          <p>{$error}</p>
        </div>
      {/if}

      {#if !$selectedSensorId && !$error} 
        <!-- Show only if no sensor selected AND no overriding error (like "failed to load sensor IDs") -->
        <div class="bg-yellow-100 border-l-4 border-yellow-500 text-yellow-700 p-4">
          <p>Please select a sensor to view data.</p>
        </div>
      {:else if $selectedSensorId}
        {#if $loading}
          <div class="flex justify-center items-center h-64">
            <div class="text-blue-600">Loading sensor data...</div>
          </div>
        {:else if $sensorData && $sensorData.dataPoints.length > 0 && !$error}
          <div class="grid grid-cols-1 md:grid-cols-3 gap-6">
            <div class="md:col-span-2">
              <GravityChart />
            </div>
            <div>
              <SensorDataDetail />
            </div>
          </div>
        {:else if $sensorData && $sensorData.dataPoints.length === 0 && !$error}
          <div class="bg-yellow-100 border-l-4 border-yellow-500 text-yellow-700 p-4">
            <p>No data available for the selected sensor in this time range.</p>
          </div>
        {:else if !$error} 
          <!-- Fallback for when a sensor is selected, not loading, no data, and no specific "no data points" message.
               This could happen if $sensorData is null after an attempted load without an error being set,
               or before the first load for a selected sensor. -->
          <div class="bg-yellow-100 border-l-4 border-yellow-500 text-yellow-700 p-4">
            <p>Select a time range or check if data exists for the current selection.</p>
          </div>
        {/if}
      {/if}
    </div>
  </main>
  
  <footer class="bg-white py-4 border-t">
    <div class="container mx-auto px-4 text-center text-gray-500 text-sm">
      Tilted © {new Date().getFullYear()}
    </div>
  </footer>
</div>
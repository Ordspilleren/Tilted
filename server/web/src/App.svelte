<script lang="ts">
    import { onMount } from 'svelte';
    import { selectedSensorId, sensorData, loading, error, timeRange } from './lib/stores';
    import { fetchSensorData } from './lib/api';
    import Header from './components/Header.svelte';
    import SensorSelector from './components/SensorSelector.svelte';
    import GravityChart from './components/GravityChart.svelte';
    import SensorData from './components/SensorData.svelte';
      
    // Load data when sensor ID or time range changes
    $: if ($selectedSensorId) {
      loadSensorData($selectedSensorId, $timeRange);
    }
    
    async function loadSensorData(sensorId: string, hours: number) {
      if (!sensorId) return;
      
      try {
        loading.set(true);
        error.set(null);
        const data = await fetchSensorData(sensorId, hours);
        if (data) {
          sensorData.set(data);
        } else {
          error.set('No data available for this sensor');
        }
      } catch (err) {
        console.error(`Error loading sensor data: ${err}`);
        error.set('Failed to load sensor data');
      } finally {
        loading.set(false);
      }
    }
  </script>
  
  <div class="min-h-screen bg-gray-100">
    <Header />
    
    <main class="container mx-auto px-4 py-6">
      {#if $loading && !$sensorData}
        <div class="flex justify-center items-center h-64">
          <div class="text-blue-600">Loading sensor data...</div>
        </div>
      {:else if $error}
        <div class="bg-red-100 border-l-4 border-red-500 text-red-700 p-4 mb-4">
          <p>{$error}</p>
        </div>
      {:else}
        <div class="space-y-6">
          <SensorSelector />
          
          <div class="grid grid-cols-1 md:grid-cols-3 gap-6">
            <div class="md:col-span-2">
              <GravityChart />
            </div>
            <div>
              <SensorData />
            </div>
          </div>
        </div>
      {/if}
    </main>
    
    <footer class="bg-white py-4 border-t">
      <div class="container mx-auto px-4 text-center text-gray-500 text-sm">
        Tilted &copy; {new Date().getFullYear()}
      </div>
    </footer>
  </div>
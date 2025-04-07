<script lang="ts">
    import { onMount } from 'svelte';
    import { fetchSensorIds } from '../lib/api';
    import { selectedSensorId, timeRange, loading, error } from '../lib/stores';
  
    let sensorIds: string[] = [];
    let timeRanges = [
      { value: 6, label: 'Last 6 hours' },
      { value: 12, label: 'Last 12 hours' },
      { value: 24, label: 'Last 24 hours' },
      { value: 48, label: 'Last 2 days' },
      { value: 168, label: 'Last week' }
    ];
  
    onMount(async () => {
      try {
        loading.set(true);
        sensorIds = await fetchSensorIds();
        if (sensorIds.length > 0) {
          selectedSensorId.set(sensorIds[0]);
        }
      } catch (err) {
        error.set('Failed to load sensor IDs');
        console.error(err);
      } finally {
        loading.set(false);
      }
    });
  
    function handleSensorChange(event: Event) {
      const target = event.target as HTMLSelectElement;
      selectedSensorId.set(target.value);
    }
  
    function handleTimeRangeChange(event: Event) {
      const target = event.target as HTMLSelectElement;
      timeRange.set(parseInt(target.value, 10));
    }
  </script>
  
  <div class="bg-white p-4 rounded-lg shadow-md">
    <div class="flex flex-col md:flex-row md:justify-between md:items-center gap-4">
      <div class="w-full md:w-1/2">
        <label for="sensor-select" class="block text-sm font-medium text-gray-700 mb-1">
          Select Sensor
        </label>
        <select 
          id="sensor-select"
          class="w-full p-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
          on:change={handleSensorChange}
          disabled={sensorIds.length === 0}
        >
          {#if sensorIds.length === 0}
            <option value="">No sensors available</option>
          {:else}
            {#each sensorIds as sensorId}
              <option value={sensorId}>{sensorId}</option>
            {/each}
          {/if}
        </select>
      </div>
      
      <div class="w-full md:w-1/2">
        <label for="time-range" class="block text-sm font-medium text-gray-700 mb-1">
          Time Range
        </label>
        <select 
          id="time-range"
          class="w-full p-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
          on:change={handleTimeRangeChange}
          bind:value={$timeRange}
        >
          {#each timeRanges as range}
            <option value={range.value}>{range.label}</option>
          {/each}
        </select>
      </div>
    </div>
  </div>
<script lang="ts">
  import { onMount } from 'svelte';
  import { fetchSensorIds } from '../lib/api';
  import { selectedSensorId, selectedDateRange, error as globalErrorStore } from '../lib/stores'; // Renamed error to globalErrorStore to avoid conflict

  let localSensorIds: string[] = []; // Use a local reactive variable for the list
  let isLoadingSensorIds = false;

  // Local state for input fields, bound to datetime-local string format
  let startTimeStr: string;
  let endTimeStr: string;

  // Helper to convert Unix MS to 'YYYY-MM-DDTHH:mm' for datetime-local input
  function toDateTimeLocalString(timestamp: number): string {
    const date = new Date(timestamp);
    // Create a new Date object that represents the local time components
    // by adjusting for the timezone offset. This makes toISOString().slice(0,16) work as expected.
    const localDate = new Date(date.getTime() - date.getTimezoneOffset() * 60000);
    return localDate.toISOString().slice(0, 16);
  }

  // Helper to convert 'YYYY-MM-DDTHH:mm' string from datetime-local to Unix MS (UTC)
  function fromDateTimeLocalString(dateTimeLocalStr: string): number {
    return new Date(dateTimeLocalStr).getTime();
  }
  
  // Sync local input strings with the store values
  $: {
    if ($selectedDateRange) {
      startTimeStr = toDateTimeLocalString($selectedDateRange.startTime);
      endTimeStr = toDateTimeLocalString($selectedDateRange.endTime);
    }
  }

  onMount(async () => {
    isLoadingSensorIds = true;
    globalErrorStore.set(null); // Clear previous global errors from this component's scope
    try {
      const fetchedSensorIds = await fetchSensorIds();
      localSensorIds = fetchedSensorIds; // Update local reactive variable

      if (localSensorIds.length > 0) {
        // If no sensor is currently selected, or if the selected one is not in the new list, select the first one.
        if (!$selectedSensorId || !localSensorIds.includes($selectedSensorId)) { 
          selectedSensorId.set(localSensorIds[0]);
        }
      } else {
        selectedSensorId.set(null); // No sensors available
        // Optional: set a message if no sensors are found, but not necessarily an "error"
        // globalErrorStore.set('No sensors found. Please add a sensor.'); 
      }
    } catch (err) {
      globalErrorStore.set('Failed to load sensor IDs. Please refresh.');
      console.error(err);
      localSensorIds = []; // Ensure sensorIds is empty on error
      selectedSensorId.set(null);
    } finally {
      isLoadingSensorIds = false;
    }
  });

  function handleSensorChange(event: Event) {
    const target = event.target as HTMLSelectElement;
    selectedSensorId.set(target.value || null); // Ensure null if empty value
  }

  function handleStartTimeChange(event: Event) {
    const target = event.target as HTMLInputElement;
    const newStartTime = fromDateTimeLocalString(target.value);
    if ($selectedDateRange && newStartTime < $selectedDateRange.endTime) {
      selectedDateRange.update(current => ({ ...current, startTime: newStartTime }));
    } else {
      alert("Start time cannot be after end time. Reverting.");
      // Revert input value to current store value
      if ($selectedDateRange) startTimeStr = toDateTimeLocalString($selectedDateRange.startTime);
    }
  }

  function handleEndTimeChange(event: Event) {
    const target = event.target as HTMLInputElement;
    const newEndTime = fromDateTimeLocalString(target.value);
     if ($selectedDateRange && newEndTime > $selectedDateRange.startTime) {
      selectedDateRange.update(current => ({ ...current, endTime: newEndTime }));
    } else {
      alert("End time cannot be before start time. Reverting.");
      // Revert input value to current store value
      if ($selectedDateRange) endTimeStr = toDateTimeLocalString($selectedDateRange.endTime);
    }
  }
</script>

<div class="bg-white p-4 rounded-lg shadow-md">
  <div class="flex flex-col md:flex-row md:justify-between md:items-end gap-4">
    <div class="w-full md:w-1/3">
      <label for="sensor-select" class="block text-sm font-medium text-gray-700 mb-1">
        Select Sensor
      </label>
      <select 
        id="sensor-select"
        class="w-full p-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
        on:change={handleSensorChange}
        disabled={isLoadingSensorIds || localSensorIds.length === 0}
        bind:value={$selectedSensorId} 
      >
        {#if isLoadingSensorIds}
          <option value="">Loading sensors...</option>
        {:else if localSensorIds.length === 0}
          <option value="">No sensors available</option>
        {:else}
          {#each localSensorIds as id (id)}
            <option value={id}>{id}</option>
          {/each}
        {/if}
      </select>
    </div>
    
    <div class="w-full md:w-1/3">
      <label for="start-time" class="block text-sm font-medium text-gray-700 mb-1">
        Start Time
      </label>
      <input 
        type="datetime-local"
        id="start-time"
        class="w-full p-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
        bind:value={startTimeStr}
        on:input={handleStartTimeChange}
        disabled={!$selectedSensorId}
      />
    </div>
    
    <div class="w-full md:w-1/3">
      <label for="end-time" class="block text-sm font-medium text-gray-700 mb-1">
        End Time
      </label>
      <input 
        type="datetime-local"
        id="end-time"
        class="w-full p-2 border border-gray-300 rounded-md focus:ring-blue-500 focus:border-blue-500"
        bind:value={endTimeStr}
        on:input={handleEndTimeChange}
        disabled={!$selectedSensorId}
      />
    </div>
  </div>
</div>
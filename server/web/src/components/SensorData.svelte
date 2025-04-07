<script lang="ts">
    import { sensorData } from '../lib/stores';
    
    // Helper function to get the latest data point
    $: latestData = $sensorData && $sensorData.dataPoints.length > 0 
      ? $sensorData.dataPoints[$sensorData.dataPoints.length - 1] 
      : null;
  </script>
  
  <div class="bg-white p-4 rounded-lg shadow-md">
    <h2 class="text-lg font-semibold mb-4">Current Sensor Readings</h2>
    
    {#if $sensorData && latestData}
      <div class="grid grid-cols-2 md:grid-cols-3 gap-4">
        <!-- Gravity Card -->
        <div class="p-3 bg-blue-50 rounded-lg border border-blue-100">
          <div class="text-sm text-blue-700 font-medium">Gravity</div>
          <div class="text-2xl font-bold">{latestData.gravity.toFixed(3)}</div>
        </div>
        
        <!-- Temperature Card -->
        <div class="p-3 bg-red-50 rounded-lg border border-red-100">
          <div class="text-sm text-red-700 font-medium">Temperature</div>
          <div class="text-2xl font-bold">{latestData.temp.toFixed(1)}°</div>
        </div>
        
        <!-- Tilt Card -->
        <div class="p-3 bg-green-50 rounded-lg border border-green-100">
          <div class="text-sm text-green-700 font-medium">Tilt</div>
          <div class="text-2xl font-bold">{latestData.tilt.toFixed(1)}°</div>
        </div>
        
        <!-- Voltage Card -->
        <div class="p-3 bg-yellow-50 rounded-lg border border-yellow-100">
          <div class="text-sm text-yellow-700 font-medium">Battery</div>
          <div class="text-2xl font-bold">{latestData.volt.toFixed(2)}V</div>
        </div>
        
        <!-- Interval Card -->
        <div class="p-3 bg-purple-50 rounded-lg border border-purple-100">
          <div class="text-sm text-purple-700 font-medium">Interval</div>
          <div class="text-2xl font-bold">{latestData.interval}s</div>
        </div>
        
        <!-- Last Update Card -->
        <div class="p-3 bg-gray-50 rounded-lg border border-gray-100">
          <div class="text-sm text-gray-700 font-medium">Last Update</div>
          <div class="text-sm font-medium">
            {new Date(latestData.timestamp).toLocaleString()}
          </div>
        </div>
      </div>
      
      <div class="mt-4 text-sm text-gray-500">
        <div>Sensor ID: <span class="font-medium">{$sensorData.sensorId}</span></div>
        <div>Gateway: <span class="font-medium">{$sensorData.gatewayName} ({$sensorData.gatewayId})</span></div>
      </div>
    {:else}
      <div class="text-gray-400 text-center py-4">
        No sensor data available
      </div>
    {/if}
  </div>
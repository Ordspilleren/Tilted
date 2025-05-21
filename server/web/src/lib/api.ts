import type { SensorData } from './types';

const API_BASE = '/api';

export async function fetchSensorIds(): Promise<string[]> {
  try {
    const response = await fetch(`${API_BASE}/sensors`);
    if (!response.ok) {
      throw new Error(`Error fetching sensor IDs: ${response.statusText} (${response.status})`);
    }
    return await response.json();
  } catch (error) {
    console.error('Failed to fetch sensor IDs:', error);
    throw error; // Re-throw to be caught by caller
  }
}

export async function fetchSensorData(
  sensorId: string, 
  startTime: number, // Unix ms
  endTime: number    // Unix ms
): Promise<SensorData | null> {
  try {
    const response = await fetch(
      `${API_BASE}/readings/${sensorId}?startTime=${startTime}&endTime=${endTime}`
    );
    if (!response.ok) {
      throw new Error(`Error fetching sensor data: ${response.statusText} (${response.status})`);
    }
    const data = await response.json();
    // Ensure dataPoints is always an array, even if null from server
    if (data && data.dataPoints === null) {
        data.dataPoints = [];
    }
    return data;
  } catch (error) {
    console.error(`Failed to fetch data for sensor ${sensorId}:`, error);
    // Return null or throw, depending on how you want to handle errors in App.svelte
    // Throwing makes error handling more consistent in App.svelte's loadSensorData
    throw error;
  }
}
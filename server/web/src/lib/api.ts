import type { SensorData } from './types';

const API_BASE = '/api';

export async function fetchSensorIds(): Promise<string[]> {
  try {
    const response = await fetch(`${API_BASE}/sensors`);
    if (!response.ok) {
      throw new Error(`Error fetching sensor IDs: ${response.statusText}`);
    }
    return await response.json();
  } catch (error) {
    console.error('Failed to fetch sensor IDs:', error);
    return [];
  }
}

export async function fetchSensorData(sensorId: string, timeRange: number = 24): Promise<SensorData | null> {
  try {
    // timeRange in hours
    const response = await fetch(`${API_BASE}/readings/${sensorId}?hours=${timeRange}`);
    if (!response.ok) {
      throw new Error(`Error fetching sensor data: ${response.statusText}`);
    }
    return await response.json();
  } catch (error) {
    console.error(`Failed to fetch data for sensor ${sensorId}:`, error);
    return null;
  }
}
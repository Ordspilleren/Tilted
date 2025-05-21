import { writable } from 'svelte/store';
import type { SensorData } from './types';

export const selectedSensorId = writable<string | null>(null);
export const sensorData = writable<SensorData | null>(null);
export const loading = writable<boolean>(false);
export const error = writable<string | null>(null);

// NEW: Store for selected date range (Unix timestamps in milliseconds)
export const selectedDateRange = writable<{ startTime: number; endTime: number }>({
  startTime: new Date(Date.now() - 24 * 60 * 60 * 1000).getTime(), // Default: 24 hours ago
  endTime: new Date().getTime(), // Default: Now
});
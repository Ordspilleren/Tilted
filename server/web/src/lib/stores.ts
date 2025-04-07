import { writable } from 'svelte/store';
import type { SensorData } from './types';

export const selectedSensorId = writable<string | null>(null);
export const sensorData = writable<SensorData | null>(null);
export const loading = writable<boolean>(false);
export const error = writable<string | null>(null);
export const timeRange = writable<number>(24); // Default to 24 hours
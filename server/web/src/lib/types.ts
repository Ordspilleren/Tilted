export interface Reading {
    sensorId: string;
    gravity: number;
    tilt: number;
    temp: number;
    volt: number;
    interval: number;
  }
  
  export interface SensorReading {
    reading: Reading;
    gatewayId: string;
    gatewayName: string;
  }
  
  export interface DataPoint {
    timestamp: number;
    gravity: number;
    tilt: number;
    temp: number;
    volt: number;
    interval: number;
  }
  
  export interface SensorData {
    sensorId: string;
    gatewayId: string;
    gatewayName: string;
    dataPoints: DataPoint[];
  }
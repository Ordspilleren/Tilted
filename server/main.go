package main

import (
	"bytes"
	"fmt"
	"log"
	"net/http"
	"net/url"
	"time"

	"github.com/labstack/echo/v4"
	"github.com/labstack/echo/v4/middleware"
)

// Configuration constants
const (
	// VictoriaMetrics connection details
	VictoriaMetricsURL = "http://victoriametrics:8428" // Change to your VictoriaMetrics address
	ImportAPI          = "/api/v1/import/prometheus"
)

// SensorReading represents the data structure sent by the ESP32
type SensorReading struct {
	Reading     Reading `json:"reading"`
	GatewayID   string  `json:"gatewayId"`
	GatewayName string  `json:"gatewayName"`
}

// Reading contains the actual sensor data
type Reading struct {
	SensorID string  `json:"sensorId"`
	Gravity  float64 `json:"gravity"`
	Tilt     float64 `json:"tilt"`
	Temp     float64 `json:"temp"`
	Volt     float64 `json:"volt"`
	Interval int     `json:"interval"`
}

func main() {
	// Create a new Echo instance
	e := echo.New()

	// Add middleware
	e.Use(middleware.Logger())
	e.Use(middleware.Recover())
	e.Use(middleware.CORS())

	// Routes
	e.POST("/api/readings", handleSensorData)
	e.GET("/health", healthCheck)

	// Start server
	port := ":8080"
	log.Printf("Starting server on port %s", port)
	if err := e.Start(port); err != http.ErrServerClosed {
		log.Fatal(err)
	}
}

// handleSensorData processes the incoming sensor readings from ESP32
func handleSensorData(c echo.Context) error {
	sensorData := new(SensorReading)
	if err := c.Bind(sensorData); err != nil {
		return c.JSON(http.StatusBadRequest, map[string]string{
			"error": "Invalid request format",
		})
	}

	log.Printf("Received data from gateway: %s (%s)", sensorData.GatewayName, sensorData.GatewayID)
	log.Printf("Sensor: %s, Gravity: %.3f, Tilt: %.2f, Temp: %.2f",
		sensorData.Reading.SensorID,
		sensorData.Reading.Gravity,
		sensorData.Reading.Tilt,
		sensorData.Reading.Temp)

	// Push metrics to VictoriaMetrics
	if err := pushToVictoriaMetrics(sensorData); err != nil {
		log.Printf("Error pushing metrics: %v", err)
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"status": "error",
			"error":  "Failed to store metrics",
		})
	}

	return c.JSON(http.StatusOK, map[string]string{
		"status": "success",
	})
}

// pushToVictoriaMetrics sends the metrics directly to VictoriaMetrics using the /api/v1/import/prometheus endpoint
func pushToVictoriaMetrics(data *SensorReading) error {
	// Current timestamp in milliseconds
	timestamp := time.Now().UnixMilli()

	// Common labels for all metrics
	labels := fmt.Sprintf(`{sensor_id="%s",gateway_id="%s",gateway_name="%s"}`,
		data.Reading.SensorID,
		data.GatewayID,
		data.GatewayName)

	// Build the Prometheus-formatted metrics
	var metricsData bytes.Buffer

	// Write each metric with appropriate labels and timestamp
	metricsData.WriteString(fmt.Sprintf("tilted_gravity%s %.3f %d\n", labels, data.Reading.Gravity, timestamp))
	metricsData.WriteString(fmt.Sprintf("tilted_tilt%s %.2f %d\n", labels, data.Reading.Tilt, timestamp))
	metricsData.WriteString(fmt.Sprintf("tilted_temp%s %.2f %d\n", labels, data.Reading.Temp, timestamp))
	metricsData.WriteString(fmt.Sprintf("tilted_volt%s %.2f %d\n", labels, data.Reading.Volt, timestamp))
	metricsData.WriteString(fmt.Sprintf("tilted_interval%s %d %d\n", labels, data.Reading.Interval, timestamp))

	// Construct the URL with any necessary parameters
	u, err := url.Parse(VictoriaMetricsURL + ImportAPI)
	if err != nil {
		return fmt.Errorf("error parsing URL: %v", err)
	}

	// Create HTTP request to push metrics
	req, err := http.NewRequest(http.MethodPost, u.String(), &metricsData)
	if err != nil {
		return fmt.Errorf("error creating request: %v", err)
	}
	req.Header.Set("Content-Type", "text/plain")

	// Send the request
	client := &http.Client{Timeout: 5 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return fmt.Errorf("error sending metrics: %v", err)
	}
	defer resp.Body.Close()

	// Check response
	if resp.StatusCode != http.StatusNoContent {
		return fmt.Errorf("received non-OK response from VictoriaMetrics: %d", resp.StatusCode)
	}

	log.Printf("Successfully pushed metrics to VictoriaMetrics")
	return nil
}

// healthCheck provides a simple health check endpoint
func healthCheck(c echo.Context) error {
	return c.JSON(http.StatusOK, map[string]string{
		"status": "ok",
		"time":   time.Now().Format(time.RFC3339),
	})
}

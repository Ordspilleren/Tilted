package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"net/url"
	"strconv"
	"time"

	"github.com/Ordspilleren/Tilted/server/web"
	"github.com/labstack/echo/v4"
	"github.com/labstack/echo/v4/middleware"
)

// Configuration constants
const (
	// VictoriaMetrics connection details
	VictoriaMetricsURL = "http://victoriametrics:8428" // Change to your VictoriaMetrics address
	ImportAPI          = "/api/v1/import/prometheus"
	QueryAPI           = "/api/v1/query"
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

// DataPoint represents a point of data for frontend visualization
type DataPoint struct {
	Timestamp int64   `json:"timestamp"`
	Gravity   float64 `json:"gravity"`
	Tilt      float64 `json:"tilt"`
	Temp      float64 `json:"temp"`
	Volt      float64 `json:"volt"`
	Interval  int     `json:"interval"`
}

// SensorData represents the complete data for a sensor
type SensorData struct {
	SensorID    string      `json:"sensorId"`
	GatewayID   string      `json:"gatewayId"`
	GatewayName string      `json:"gatewayName"`
	DataPoints  []DataPoint `json:"dataPoints"`
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
	e.GET("/api/sensors", getSensorIDs)
	e.GET("/api/readings/:sensorId", getSensorData)
	e.GET("/health", healthCheck)

	// Serve Svelte frontend for all other routes
	e.GET("/*", echo.WrapHandler(web.FrontEndHandler))

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

// getSensorIDs retrieves all unique sensor IDs from VictoriaMetrics
func getSensorIDs(c echo.Context) error {
	// Construct the URL for VictoriaMetrics query
	u, err := url.Parse(VictoriaMetricsURL + "/api/v1/label/sensor_id/values")
	if err != nil {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Error constructing URL: %v", err),
		})
	}

	// Create HTTP request to get sensor IDs
	req, err := http.NewRequest(http.MethodGet, u.String(), nil)
	if err != nil {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Error creating request: %v", err),
		})
	}

	// Send the request
	client := &http.Client{Timeout: 5 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Error querying VictoriaMetrics: %v", err),
		})
	}
	defer resp.Body.Close()

	// Check response
	if resp.StatusCode != http.StatusOK {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Received non-OK response from VictoriaMetrics: %d", resp.StatusCode),
		})
	}

	// Parse the response
	var response struct {
		Status string   `json:"status"`
		Data   []string `json:"data"`
	}

	if err := json.NewDecoder(resp.Body).Decode(&response); err != nil {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Error parsing response: %v", err),
		})
	}

	return c.JSON(http.StatusOK, response.Data)
}

// getSensorData retrieves data for a specific sensor from VictoriaMetrics
func getSensorData(c echo.Context) error {
	sensorID := c.Param("sensorId")
	if sensorID == "" {
		return c.JSON(http.StatusBadRequest, map[string]string{
			"error": "Sensor ID is required",
		})
	}

	// Get time range parameter (default to 24 hours)
	hoursStr := c.QueryParam("hours")
	hours := 24 // Default to 24 hours
	if hoursStr != "" {
		var err error
		hours, err = strconv.Atoi(hoursStr)
		if err != nil {
			return c.JSON(http.StatusBadRequest, map[string]string{
				"error": "Invalid hours parameter",
			})
		}
	}

	// Query metrics for this sensor
	metrics := []string{
		"tilted_gravity",
		"tilted_tilt",
		"tilted_temp",
		"tilted_volt",
		"tilted_interval",
	}

	// Prepare data structure for response
	sensorData := SensorData{
		SensorID:   sensorID,
		DataPoints: []DataPoint{},
	}

	// Query each metric and populate datapoints
	for _, metric := range metrics {
		dataPoints, gatewayInfo, err := queryMetric(metric, sensorID, hours)
		if err != nil {
			return c.JSON(http.StatusInternalServerError, map[string]string{
				"error": fmt.Sprintf("Error querying %s: %v", metric, err),
			})
		}

		// Set gateway info if not already set
		if sensorData.GatewayID == "" && gatewayInfo != nil {
			sensorData.GatewayID = gatewayInfo["gateway_id"]
			sensorData.GatewayName = gatewayInfo["gateway_name"]
		}

		// Merge data points
		mergeDataPoints(&sensorData.DataPoints, dataPoints, metric)
	}

	return c.JSON(http.StatusOK, sensorData)
}

// queryMetric queries a specific metric from VictoriaMetrics for a given sensor and time range
func queryMetric(metric, sensorID string, hours int) (map[int64]float64, map[string]string, error) {
	// Format the query
	query := fmt.Sprintf(`%s{sensor_id="%s"}[%dh]`, metric, sensorID, hours)

	// Construct the URL with query parameters
	u, err := url.Parse(VictoriaMetricsURL + QueryAPI)
	if err != nil {
		return nil, nil, fmt.Errorf("error parsing URL: %v", err)
	}

	// Add query parameters
	q := u.Query()
	q.Set("query", query)
	u.RawQuery = q.Encode()

	// Create HTTP request
	req, err := http.NewRequest(http.MethodGet, u.String(), nil)
	if err != nil {
		return nil, nil, fmt.Errorf("error creating request: %v", err)
	}

	// Send the request
	client := &http.Client{Timeout: 10 * time.Second}
	resp, err := client.Do(req)
	if err != nil {
		return nil, nil, fmt.Errorf("error querying VictoriaMetrics: %v", err)
	}
	defer resp.Body.Close()

	// Check response
	if resp.StatusCode != http.StatusOK {
		return nil, nil, fmt.Errorf("received non-OK response from VictoriaMetrics: %d", resp.StatusCode)
	}

	// Parse the response
	var response struct {
		Status string `json:"status"`
		Data   struct {
			ResultType string `json:"resultType"`
			Result     []struct {
				Metric map[string]string `json:"metric"`
				Values [][]interface{}   `json:"values"`
			} `json:"result"`
		} `json:"data"`
	}

	if err := json.NewDecoder(resp.Body).Decode(&response); err != nil {
		return nil, nil, fmt.Errorf("error parsing response: %v", err)
	}

	// Extract data points
	dataPoints := make(map[int64]float64)
	var gatewayInfo map[string]string

	// Process results
	if len(response.Data.Result) > 0 {
		result := response.Data.Result[0]

		// Store gateway info for reference
		gatewayInfo = map[string]string{
			"gateway_id":   result.Metric["gateway_id"],
			"gateway_name": result.Metric["gateway_name"],
		}

		// Process values
		for _, val := range result.Values {
			if len(val) >= 2 {
				timestamp, ok := val[0].(float64)
				if !ok {
					continue
				}

				valueStr, ok := val[1].(string)
				if !ok {
					continue
				}

				value, err := strconv.ParseFloat(valueStr, 64)
				if err != nil {
					continue
				}

				// Store timestamp in milliseconds for JavaScript
				dataPoints[int64(timestamp*1000)] = value
			}
		}
	}

	return dataPoints, gatewayInfo, nil
}

// mergeDataPoints merges metric data points into the combined data points array
func mergeDataPoints(combined *[]DataPoint, newPoints map[int64]float64, metricName string) {
	// Convert existing data points to map for easier lookup
	dataMap := make(map[int64]*DataPoint)
	for i := range *combined {
		dp := &(*combined)[i]
		dataMap[dp.Timestamp] = dp
	}

	// Merge new points
	for timestamp, value := range newPoints {
		dp, exists := dataMap[timestamp]
		if !exists {
			// Create new data point
			newDP := DataPoint{
				Timestamp: timestamp,
				// Default values for all fields
				Gravity:  0,
				Tilt:     0,
				Temp:     0,
				Volt:     0,
				Interval: 0,
			}

			// Set the value based on metric name
			switch metricName {
			case "tilted_gravity":
				newDP.Gravity = value
			case "tilted_tilt":
				newDP.Tilt = value
			case "tilted_temp":
				newDP.Temp = value
			case "tilted_volt":
				newDP.Volt = value
			case "tilted_interval":
				newDP.Interval = int(value)
			}

			*combined = append(*combined, newDP)
			dataMap[timestamp] = &(*combined)[len(*combined)-1]
		} else {
			// Update existing data point
			switch metricName {
			case "tilted_gravity":
				dp.Gravity = value
			case "tilted_tilt":
				dp.Tilt = value
			case "tilted_temp":
				dp.Temp = value
			case "tilted_volt":
				dp.Volt = value
			case "tilted_interval":
				dp.Interval = int(value)
			}
		}
	}

	// Sort data points by timestamp
	if len(*combined) > 1 {
		sortDataPoints(combined)
	}
}

// sortDataPoints sorts data points by timestamp
func sortDataPoints(dataPoints *[]DataPoint) {
	// We could implement a sort but for simplicity, let's just create a new slice
	timeMap := make(map[int64]DataPoint)
	var timestamps []int64

	for _, dp := range *dataPoints {
		timeMap[dp.Timestamp] = dp
		timestamps = append(timestamps, dp.Timestamp)
	}

	// Simple bubble sort for timestamps
	for i := range timestamps {
		for j := range len(timestamps) - i - 1 {
			if timestamps[j] > timestamps[j+1] {
				timestamps[j], timestamps[j+1] = timestamps[j+1], timestamps[j]
			}
		}
	}

	// Recreate data points array in sorted order
	sorted := make([]DataPoint, len(timestamps))
	for i, ts := range timestamps {
		sorted[i] = timeMap[ts]
	}

	*dataPoints = sorted
}

package main

import (
	"database/sql"
	"flag"
	"fmt"
	"log"
	"net/http"
	"strconv"
	"time"

	"github.com/Ordspilleren/Tilted/server/web"
	"github.com/labstack/echo/v4"
	"github.com/labstack/echo/v4/middleware"
	_ "github.com/mattn/go-sqlite3"
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

// Global database connection
var db *sql.DB

func main() {
	// Initialize SQLite database
	var err error
	db, err = initDB()
	if err != nil {
		log.Fatalf("Failed to initialize database: %v", err)
	}
	defer db.Close()

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

// initDB initializes the SQLite database and creates necessary tables
func initDB() (*sql.DB, error) {
	databaseLocation := flag.String("database", "tilted.db", "")
	flag.Parse()
	db, err := sql.Open("sqlite3", fmt.Sprintf("%s?_journal_mode=WAL&_synchronous=NORMAL&_cache_size=5000", *databaseLocation))
	if err != nil {
		return nil, fmt.Errorf("failed to open database: %v", err)
	}

	// Create tables with a normalized schema
	createTablesSQL := `
    -- Sensors table to store sensor information
    CREATE TABLE IF NOT EXISTS sensors (
        id INTEGER PRIMARY KEY,
        sensor_id TEXT UNIQUE NOT NULL
    );
    
    -- Gateways table to store gateway information
    CREATE TABLE IF NOT EXISTS gateways (
        id INTEGER PRIMARY KEY,
        gateway_id TEXT NOT NULL,
        gateway_name TEXT NOT NULL,
        UNIQUE(gateway_id, gateway_name)
    );
    
    -- Readings table optimized for time-series data
    CREATE TABLE IF NOT EXISTS readings (
        timestamp INTEGER PRIMARY KEY, 
        sensor_id INTEGER NOT NULL,
        gateway_id INTEGER NOT NULL,
        gravity REAL NOT NULL,
        tilt REAL NOT NULL,
        temp REAL NOT NULL,
        volt REAL NOT NULL,
        interval INTEGER NOT NULL,
        FOREIGN KEY (sensor_id) REFERENCES sensors(id),
        FOREIGN KEY (gateway_id) REFERENCES gateways(id)
    ) WITHOUT ROWID;
    
    -- Create index for querying by sensor_id
    CREATE INDEX IF NOT EXISTS idx_readings_sensor_id ON readings(sensor_id);
    `

	_, err = db.Exec(createTablesSQL)
	if err != nil {
		db.Close()
		return nil, fmt.Errorf("failed to create tables: %v", err)
	}

	// Enable foreign keys
	_, err = db.Exec("PRAGMA foreign_keys = ON")
	if err != nil {
		db.Close()
		return nil, fmt.Errorf("failed to enable foreign keys: %v", err)
	}

	return db, nil
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

	// Save data to SQLite
	if err := saveToDatabase(sensorData); err != nil {
		log.Printf("Error saving to database: %v", err)
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"status": "error",
			"error":  "Failed to store metrics",
		})
	}

	return c.JSON(http.StatusOK, map[string]string{
		"status": "success",
	})
}

// saveToDatabase stores the sensor readings in SQLite with normalized schema
func saveToDatabase(data *SensorReading) error {
	tx, err := db.Begin()
	if err != nil {
		return fmt.Errorf("failed to begin transaction: %v", err)
	}
	defer func() {
		if err != nil {
			tx.Rollback()
		}
	}()

	// 1. Get or create sensor ID
	var sensorInternalID int64
	err = tx.QueryRow("SELECT id FROM sensors WHERE sensor_id = ?", data.Reading.SensorID).Scan(&sensorInternalID)
	if err == sql.ErrNoRows {
		// Insert new sensor
		result, err := tx.Exec("INSERT INTO sensors (sensor_id) VALUES (?)", data.Reading.SensorID)
		if err != nil {
			return fmt.Errorf("failed to insert sensor: %v", err)
		}
		sensorInternalID, err = result.LastInsertId()
		if err != nil {
			return fmt.Errorf("failed to get sensor ID: %v", err)
		}
	} else if err != nil {
		return fmt.Errorf("error checking for sensor: %v", err)
	}

	// 2. Get or create gateway ID
	var gatewayInternalID int64
	err = tx.QueryRow("SELECT id FROM gateways WHERE gateway_id = ? AND gateway_name = ?",
		data.GatewayID, data.GatewayName).Scan(&gatewayInternalID)
	if err == sql.ErrNoRows {
		// Insert new gateway
		result, err := tx.Exec("INSERT INTO gateways (gateway_id, gateway_name) VALUES (?, ?)",
			data.GatewayID, data.GatewayName)
		if err != nil {
			return fmt.Errorf("failed to insert gateway: %v", err)
		}
		gatewayInternalID, err = result.LastInsertId()
		if err != nil {
			return fmt.Errorf("failed to get gateway ID: %v", err)
		}
	} else if err != nil {
		return fmt.Errorf("error checking for gateway: %v", err)
	}

	// 3. Insert reading with the current timestamp
	timestamp := time.Now().UnixMilli()
	_, err = tx.Exec(`
		INSERT INTO readings (
			timestamp, sensor_id, gateway_id, gravity, tilt, temp, volt, interval
		) VALUES (?, ?, ?, ?, ?, ?, ?, ?)`,
		timestamp, sensorInternalID, gatewayInternalID,
		data.Reading.Gravity, data.Reading.Tilt, data.Reading.Temp,
		data.Reading.Volt, data.Reading.Interval)
	if err != nil {
		return fmt.Errorf("failed to insert reading: %v", err)
	}

	// Commit the transaction
	if err = tx.Commit(); err != nil {
		return fmt.Errorf("failed to commit transaction: %v", err)
	}

	log.Printf("Successfully saved metrics to SQLite database")
	return nil
}

// healthCheck provides a simple health check endpoint
func healthCheck(c echo.Context) error {
	// Ping database to ensure connection is still alive
	err := db.Ping()
	if err != nil {
		return c.JSON(http.StatusServiceUnavailable, map[string]string{
			"status": "error",
			"error":  "Database connection failed",
			"time":   time.Now().Format(time.RFC3339),
		})
	}

	return c.JSON(http.StatusOK, map[string]string{
		"status": "ok",
		"time":   time.Now().Format(time.RFC3339),
	})
}

// getSensorIDs retrieves all unique sensor IDs from the database
func getSensorIDs(c echo.Context) error {
	query := `SELECT sensor_id FROM sensors ORDER BY sensor_id`

	rows, err := db.Query(query)
	if err != nil {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Error querying database: %v", err),
		})
	}
	defer rows.Close()

	var sensorIDs []string
	for rows.Next() {
		var id string
		if err := rows.Scan(&id); err != nil {
			return c.JSON(http.StatusInternalServerError, map[string]string{
				"error": fmt.Sprintf("Error scanning results: %v", err),
			})
		}
		sensorIDs = append(sensorIDs, id)
	}

	if err := rows.Err(); err != nil {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Error iterating results: %v", err),
		})
	}

	return c.JSON(http.StatusOK, sensorIDs)
}

// getSensorData retrieves data for a specific sensor from the database
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

	// Calculate the timestamp for hours ago
	hoursAgo := time.Now().Add(-time.Duration(hours) * time.Hour).UnixMilli()

	// Query the database for sensor data with JOINs to get the necessary information
	query := `
    SELECT 
        r.timestamp, s.sensor_id, g.gateway_id, g.gateway_name,
        r.gravity, r.tilt, r.temp, r.volt, r.interval
    FROM 
        readings r
    JOIN 
        sensors s ON r.sensor_id = s.id
    JOIN 
        gateways g ON r.gateway_id = g.id
    WHERE 
        s.sensor_id = ? AND r.timestamp >= ?
    ORDER BY 
        r.timestamp ASC
    `

	rows, err := db.Query(query, sensorID, hoursAgo)
	if err != nil {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Error querying database: %v", err),
		})
	}
	defer rows.Close()

	// Prepare data structure for response
	sensorData := SensorData{
		SensorID:   sensorID,
		DataPoints: []DataPoint{},
	}

	// Process results
	for rows.Next() {
		var (
			timestamp   int64
			sensorIDStr string
			gatewayID   string
			gatewayName string
			gravity     float64
			tilt        float64
			temp        float64
			volt        float64
			interval    int
		)

		if err := rows.Scan(&timestamp, &sensorIDStr, &gatewayID, &gatewayName,
			&gravity, &tilt, &temp, &volt, &interval); err != nil {
			return c.JSON(http.StatusInternalServerError, map[string]string{
				"error": fmt.Sprintf("Error scanning row: %v", err),
			})
		}

		// Set gateway info if not already set
		if sensorData.GatewayID == "" {
			sensorData.GatewayID = gatewayID
			sensorData.GatewayName = gatewayName
		}

		// Add data point
		dataPoint := DataPoint{
			Timestamp: timestamp,
			Gravity:   gravity,
			Tilt:      tilt,
			Temp:      temp,
			Volt:      volt,
			Interval:  interval,
		}

		sensorData.DataPoints = append(sensorData.DataPoints, dataPoint)
	}

	if err := rows.Err(); err != nil {
		return c.JSON(http.StatusInternalServerError, map[string]string{
			"error": fmt.Sprintf("Error iterating results: %v", err),
		})
	}

	return c.JSON(http.StatusOK, sensorData)
}

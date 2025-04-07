# Build the web ui from source
FROM docker.io/node:23 AS build-node
WORKDIR /app/web
ADD web ./
RUN npm run build

# Start from a Go base image
FROM golang:1.24-alpine AS builder

# Set working directory
WORKDIR /app

# Copy go.mod and go.sum files
COPY go.mod go.sum ./

# Download dependencies
RUN go mod download

# Copy the source code
COPY . ./
COPY --link --from=build-node /app/web/build /app/web/build

# Build the application
RUN CGO_ENABLED=0 GOOS=linux go build -o tilted -a -ldflags '-extldflags "-static"' .

# Use a minimal alpine image for the final image
FROM alpine:3.17

# Add ca-certificates for HTTPS requests to VictoriaMetrics
RUN apk --no-cache add ca-certificates tzdata

# Set working directory
WORKDIR /app

# Copy the binary from the builder stage
COPY --from=builder /app/tilted .

# Create a non-root user to run the application
RUN adduser -D -g '' appuser
USER appuser

# Expose the application port
EXPOSE 8080

# Run the application
CMD ["./tilted"]
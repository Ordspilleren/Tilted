package web

import (
	"embed"
	"io/fs"
	"net/http"
)

//go:embed build/*
var webUI embed.FS

// webFS is a custom file system that serves index.html for non-existent paths
// This enables SPA (Single Page Application) routing
type webFS struct {
	Fs http.FileSystem
}

// Open implements the http.FileSystem interface.
// If the requested file doesn't exist, it falls back to serving index.html
func (fs *webFS) Open(name string) (http.File, error) {
	f, err := fs.Fs.Open(name)
	if err != nil {
		return fs.Fs.Open("index.html")
	}
	return f, err
}

// FrontEndHandler provides an HTTP handler to serve the frontend
var FrontEndHandler http.Handler

func init() {
	contentStatic, _ := fs.Sub(fs.FS(webUI), "build")
	FrontEndHandler = http.FileServer(&webFS{Fs: http.FS(contentStatic)})
}
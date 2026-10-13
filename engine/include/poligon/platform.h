#pragma once

namespace poligon {

// Handles pending operating system events (window messages).
// Returns false once the user has asked to close the program: window close button or Escape.
// Call once per frame. Safe to call in headless mode, where it always returns true.
bool ProcessPlatformEvents();

} // namespace poligon

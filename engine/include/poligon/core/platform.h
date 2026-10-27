#pragma once

#include <cstdint>

namespace poligon {

// Handles pending operating system events (window messages).
// Returns false once the user has asked to close the program with the window's close
// button, and with `escape_quits` also when Escape is pressed. A game run by GameLoop leaves
// Escape to the loop (GameLoopConfig::escape_quits) or to its own action (Key::Escape); a
// program with a loop of its own passes true to keep Escape as the way out.
// Call once per frame. Safe to call in headless mode, where it always returns true.
bool ProcessPlatformEvents(bool escape_quits = false);

// Whether Escape went down during the last ProcessPlatformEvents call (GameLoop reads it).
bool WasEscapePressed();

// Shows a message in a small window with an OK button and waits until it is closed.
// Only for programs a person starts by hand (a viewer started with a double click): a
// program that may run headless or in a test must never call this, because nobody would
// be there to click.
void ShowMessageBox(const char* title, const char* message);

// Memory the program has asked the system for and holds (Windows: the private bytes of the
// process, from GetProcessMemoryInfo), in bytes. 0 where it cannot be found out.
std::uint64_t GetProcessMemoryBytes();

} // namespace poligon

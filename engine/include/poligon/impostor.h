#pragma once

#include <string>

#include "poligon/renderer.h"

namespace poligon {

// Impostor files. An impostor (see Impostor in poligon/renderer.h) is kept as three files:
//   NAME.impostor        a few lines of text: format version, frames per side, center, radius
//   NAME_color.png       the color pictures
//   NAME_normal.png      the normal pictures
// `path` is the .impostor file; the pictures are found next to it.
//
// The text file looks like this:
//   poligon-impostor 1
//   frames_per_side 8
//   center 0.125 9.5 -0.25
//   radius 11.2
// Numbers are written with 9 significant digits, so a loaded impostor matches the saved one.

// Returns false if a file cannot be written.
bool SaveImpostor(const char* path, const Impostor& impostor);
// Returns false and fills `error` if a file is missing or does not hold a valid impostor.
bool LoadImpostor(const char* path, Impostor& impostor, std::string& error);

} // namespace poligon

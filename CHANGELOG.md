# v0.5.1-beta

* Improved keyboard navigation
* Added image scaling to images, so that images always fit the window

## Technical

* Got rid of ImGui's built in keyboard navigation, implemented tiny custom context based keybinds instead

## Planned For Next Release

* Add clipboard integration, copy paste into the search bar straight from WhatsApp
* Add helper to show what bhawan codes are available
* Add export button
* Add simple image editor
* Make app usable

# v0.5-beta

* Added zoom to image viewer
* Added stable macOS builds
* Added image carousel to preview images
* Added keyboard integrations, you no longer have to use the mouse for everything
* Added support for system file manager for everything
* Removed debug logger
* Removed auto face-counting (will be added back in a later update)
* Removed graph based image editor
* Removed genie
* Removed audio player
* Removed inbuilt file manager
* Removed nickname management

## Technical 
* Rewrote program in C++
* Smaller binaries Windows executable is now at ~5MB in size, (down from ~150MB)
* Image caching has been removed as it is no longer needed - RAM usage at around ~70-100MB on Windows (down from 5.8GB)
* Better utilisation of the GPU

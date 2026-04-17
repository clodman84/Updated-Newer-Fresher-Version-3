# What's Changed 

* Added Face Counting
* Images always open the right way up now
* Added simple image editor
* Added multiselect and mass "Same As"
* Improved keyboard navigation
* Lowered JPEG loading times to ~100ms

## Planned For Next Release

* Add clipboard integration, copy paste into the search bar straight from WhatsApp
* Add helper to show what bhawan codes are available
* Add nickname feature
* Add bookmarks for unrolled frames
* Add lua based scripting engine for control desk
* Add documentation
* Add tutorials
* Add wrong messlist detection
* Add DOOM
* Double-Click to centre editor settings
* Add a little dot for what effects are enabled
* Add NEF, CR3, CR2, RAW, Tagging Feature
* Add metadata tagging
* Aspect ratio crop
* Auto Angle
* Add help hover to the thing
* Preset making
* Add bookmark feature in the carousel to show what images have been billed and what haven't
* Test the whole application in valgrind
* Move all Data folder access behind SDL_GetBasePath()

# v0.5.1-beta

* Improved keyboard navigation
* Added image scaling to images, so that images always fit the window
* Added search bar to look people up
* Added autosave
* Added export button
* Added same as button
* Made app usable

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

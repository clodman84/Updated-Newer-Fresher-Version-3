# What's Changed 

* Added Face Counting
* Added bookmarks for unrolled frames
* Images always open the right way up now
* Added simple image editor
* Added multi-select and mass "Same As"
* Improved keyboard navigation
* Lowered JPEG loading times to <= 100ms (24MP JPEG) on my computer
* Greatly reduced memory consumption by loading thumbnails only when visible through background threads
* Added icons
* Added Google Drive connection to the app

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

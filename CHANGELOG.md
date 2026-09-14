# Changelog

# v0.5.0

### Image Carousel & Browsing
- Added an image carousel to preview *every* image in a roll instead of just the next and previous image
- Added bookmarks in the carousel to mark which images have been billed
- Images now always open right-side up
- Added zoom to the image viewer
- Images now scale to always fit the window
- Multi-select for mass "Same As" tagging

### Tagging & Billing Workflow
- Added Google Drive connection to download rolls directly from the internet instead of waiting for the physical hard drive to reach you
- 200% faster image exports including the addition of room number and bhawan watermarks
- 10x faster automatic face counting
- The app no longer puts things inside the Data folder by default, you get to choose which folder you want to use

### Image Editor
- Removed the old graph-based image editor
- Added a simpler image editor that edits images in real-time, (it is faster than GIMP or Photoshop)
- Refreshed the image editor UI to be easier to use, (this is a work in progress)

### Navigation & Interface
- Cleaner tabs based UI designed for billing multiple rolls at the same time, no more floating windows
- Keyboard navigation, the mouse is no longer required for anything (hopefully)
- Added support for the system file manager throughout the app
- Added icons
- Removed the inbuilt file manager, audio player, genie, debug logger, and nickname management (some of these will come back)

### Performance
- JPEG loading time is sub 100ms for a 24MP image
- RAM usage dropped by 98%
- The app rendering is GPU accelerated

### Platform & Build
- Rewrote the program in C++
- Switched to a VCPKG-based build system
- New NSIS-based installer for Windows
- Stable macOS builds

---

## Planned for Next Release

- Clipboard integration to paste straight from WhatsApp into the search bar
- Helper to show available bhawan codes
- Nickname feature
- Python scripting built into the app for the control desk
- Documentation and tutorials
- Wrong messlist detection
- DOOM
- Support for NEF, CR3, CR2, RAW formats
- Metadata tagging
- Aspect ratio crop
- Auto angle correction
- Contextual help hover
- Preset making
- Full Valgrind pass over the application

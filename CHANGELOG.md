# Changelog

# v0.5.0

### Image Carousel & Browsing
<img width="1260" height="268" alt="image" src="https://github.com/user-attachments/assets/e59d6fa0-92e2-485c-adcb-4474fca18065" />

- Added an image carousel to preview *every* image in a roll instead of just the next and previous image
- Added bookmarks in the carousel to mark which images have been billed
- Images now always open right-side up
- Added zoom to the image viewer
- Images now scale to always fit the window
- Multi-select for mass "Same As" tagging
  <img width="1223" height="261" alt="image" src="https://github.com/user-attachments/assets/f35918fc-0431-4b50-a64e-7ed1cd240599" />

### Tagging & Billing Workflow
- Added Google Drive connection to download rolls directly from the internet instead of waiting for the physical hard drive
  <img width="669" height="577" alt="image" src="https://github.com/user-attachments/assets/37092e9c-80ab-4caa-81c6-abeef57d2115" />
  
- 200% faster image exports including the addition of room number and bhawan watermarks
  <img width="535" height="430" alt="image" src="https://github.com/user-attachments/assets/3b35c142-68e9-468c-9361-214647e8d0c3" />
- 10x faster automatic face counting
  <img width="1547" height="537" alt="image" src="https://github.com/user-attachments/assets/6c20c291-7879-4d98-b27c-00bc7dce2206" />
- The app no longer puts things inside the Data folder by default, you get to choose which folder you want to use.

### Image Editor
<img width="1540" height="717" alt="image" src="https://github.com/user-attachments/assets/f01789ea-8370-447b-8104-04cbd36c9eed" />

- Removed the old graph-based image editor
- Added a simpler image editor that edits images in real-time, (it is faster than GIMP or Photoshop)
- Refreshed the image editor UI to be easier to use, (this is a work in progress)

### Navigation & Interface
<img width="1920" height="1047" alt="image" src="https://github.com/user-attachments/assets/ab772fc0-f651-4420-bf2b-887b2394b2c9" />

- Cleaner tabs based UI designed for billing multiple rolls at the same time, no more floating windows
- Keyboard navigation, the mouse is no longer required for anything (hopefully)
- Added support for the system file manager throughout the app
- Added icons
- Removed the inbuilt file manager, audio player, genie, debug logger, and nickname management (some of these will come back)

### Performance
- JPEG loading time is sub 100ms for a 24MP image
- RAM usage dropped by 98%
- GPU accelerated rendering

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

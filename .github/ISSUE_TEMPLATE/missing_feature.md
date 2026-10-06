---
name: Missing format or feature
about: A DRM format, modifier vendor, kernel interface or Vulkan path brodmabuf does not handle
labels: enhancement
---

**What is missing:** the format (FourCC) or modifier, the ioctl or kernel
interface, or the Vulkan extension and what it would be used for.

**The driver or application that needs it:** what happens without it. A real
driver, application and version is the most useful answer; it is what decides
which gaps get closed first.

**How others do it:** a library or compositor that already handles it
(Mesa, wlroots, KWin, mutter, GStreamer, ...), if you know.

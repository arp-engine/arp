#pragma once

// WindowsApplicationIcon/1: applications opt in by embedding an RT_GROUP_ICON
// at this reserved ID in their executable. WindowSystem binds that group to
// both class icons (title bar and taskbar/Alt-Tab), using OS-requested sizes.
// No group means the application explicitly uses the platform default; a
// present group that cannot be loaded makes window initialization fail.
// LR_SHARED icons are module-owned and must not be destroyed by a window.
// This header is also consumed by windres; keep it free of C++ declarations.
#define ARP_APPLICATION_ICON_RESOURCE_ID 101

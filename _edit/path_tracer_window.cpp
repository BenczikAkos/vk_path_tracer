#include "path_tracer_window.hpp"

void PathTracerWindow::contextInit() {
    m_contextInfo.apiMajor = 1;
    m_contextInfo.apiMinor = 4;
    m_contextInfo.addDeviceExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    m_contextInfo.addDeviceExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, false);
    m_contextInfo.addDeviceExtension(VK_KHR_RAY_QUERY_EXTENSION_NAME, false);
    AppWindowProfilerVK::contextInit();
}

bool PathTracerWindow::begin() { return true; };

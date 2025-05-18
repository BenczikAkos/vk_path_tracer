#pragma once

#include <nvvk/appwindowprofiler_vk.hpp>


class PathTracerWindow : public nvvk::AppWindowProfilerVK
{
public:
  PathTracerWindow()
    : nvvk::AppWindowProfilerVK(true)
  {
  };

  int run(const std::string& name, int argc, const char** argv, int width, int height)
  {
    return AppWindowProfilerVK::run(name, argc, argv, width, height);
  }

  void contextInit() override;
  bool begin() override;

private:
  const uint32_t render_width = 800;
  const uint32_t render_height = 600;
  const uint32_t workgroup_width  = 16;
  const uint32_t workgroup_height = 8;

}; 
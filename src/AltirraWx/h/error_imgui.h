// wxWidgets shim — provides the same API surface as the ImGui version
// so stubs_linux.cpp compiles without ImGui.

#ifndef AT_ERROR_IMGUI_H
#define AT_ERROR_IMGUI_H

#include <string>
#include <utility>
#include <vector>

std::vector<std::pair<std::string,std::string>> ATImGuiPopPendingErrors();

#endif

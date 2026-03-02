// wxWidgets shim — provides the same API surface as the ImGui version
// so console_linux.cpp compiles without ImGui.

#ifndef AT_IMGUI_MANAGER_H
#define AT_IMGUI_MANAGER_H

#include <string>

// Minimal stub class — console_linux.cpp references g_pImGui
class ATImGuiManager {
public:
	bool IsVisible() const { return false; }
	void SetVisible(bool) {}
	void ToggleVisible() {}
};

#endif

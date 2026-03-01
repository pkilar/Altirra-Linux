#ifndef AT_IMGUI_MANAGER_H
#define AT_IMGUI_MANAGER_H

#include <string>

struct SDL_Window;
union SDL_Event;
struct SDL_GLContextState;
typedef struct SDL_GLContextState *SDL_GLContext;

class ATImGuiManager {
public:
	ATImGuiManager();
	~ATImGuiManager();

	bool Init(SDL_Window *window, SDL_GLContext glContext, const char *configDir);
	void Shutdown();

	void NewFrame();
	void Render();
	void ProcessEvent(SDL_Event &event);

	bool WantCaptureMouse() const;
	bool WantCaptureKeyboard() const;

	bool IsVisible() const { return mbVisible; }
	void SetVisible(bool visible) { mbVisible = visible; }
	void ToggleVisible() { mbVisible = !mbVisible; }

private:
	bool mbInitialized = false;
	bool mbVisible = false;
	std::string mIniPath;
};

#endif

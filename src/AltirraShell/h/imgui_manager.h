#ifndef AT_IMGUI_MANAGER_H
#define AT_IMGUI_MANAGER_H

struct SDL_Window;
union SDL_Event;
typedef void *SDL_GLContext;

class ATImGuiManager {
public:
	ATImGuiManager();
	~ATImGuiManager();

	bool Init(SDL_Window *window, SDL_GLContext glContext);
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
};

#endif

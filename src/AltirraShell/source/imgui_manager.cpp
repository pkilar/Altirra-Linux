#include <imgui_manager.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>
#include <SDL3/SDL.h>

ATImGuiManager::ATImGuiManager() {
}

ATImGuiManager::~ATImGuiManager() {
	Shutdown();
}

bool ATImGuiManager::Init(SDL_Window *window, SDL_GLContext glContext, const char *configDir) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	// Store imgui.ini next to Altirra.ini in the config directory
	if (configDir) {
		mIniPath = configDir;
		if (!mIniPath.empty() && mIniPath.back() != '/')
			mIniPath += '/';
		mIniPath += "imgui.ini";
		io.IniFilename = mIniPath.c_str();
	}

	ImGui::StyleColorsDark();

	// Slightly transparent background for overlay feel
	ImGuiStyle& style = ImGui::GetStyle();
	style.Alpha = 0.95f;

	if (!ImGui_ImplSDL3_InitForOpenGL(window, glContext))
		return false;

	if (!ImGui_ImplOpenGL3_Init("#version 120"))
		return false;

	mbInitialized = true;
	return true;
}

void ATImGuiManager::Shutdown() {
	if (!mbInitialized)
		return;

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
	mbInitialized = false;
}

void ATImGuiManager::NewFrame() {
	if (!mbInitialized)
		return;

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();
}

void ATImGuiManager::Render() {
	if (!mbInitialized)
		return;

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ATImGuiManager::ProcessEvent(SDL_Event &event) {
	if (!mbInitialized)
		return;

	ImGui_ImplSDL3_ProcessEvent(&event);
}

bool ATImGuiManager::WantCaptureMouse() const {
	if (!mbInitialized || !mbVisible)
		return false;

	return ImGui::GetIO().WantCaptureMouse;
}

bool ATImGuiManager::WantCaptureKeyboard() const {
	if (!mbInitialized || !mbVisible)
		return false;

	return ImGui::GetIO().WantCaptureKeyboard;
}

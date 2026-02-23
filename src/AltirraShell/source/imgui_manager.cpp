#include <imgui_manager.h>

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl2.h>
#include <SDL.h>

ATImGuiManager::ATImGuiManager() {
}

ATImGuiManager::~ATImGuiManager() {
	Shutdown();
}

bool ATImGuiManager::Init(SDL_Window *window, SDL_GLContext glContext) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();

	// Slightly transparent background for overlay feel
	ImGuiStyle& style = ImGui::GetStyle();
	style.Alpha = 0.95f;

	if (!ImGui_ImplSDL2_InitForOpenGL(window, glContext))
		return false;

	if (!ImGui_ImplOpenGL2_Init())
		return false;

	mbInitialized = true;
	return true;
}

void ATImGuiManager::Shutdown() {
	if (!mbInitialized)
		return;

	ImGui_ImplOpenGL2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	mbInitialized = false;
}

void ATImGuiManager::NewFrame() {
	if (!mbInitialized)
		return;

	ImGui_ImplOpenGL2_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
}

void ATImGuiManager::Render() {
	if (!mbInitialized)
		return;

	ImGui::Render();
	ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
}

void ATImGuiManager::ProcessEvent(SDL_Event &event) {
	if (!mbInitialized)
		return;

	ImGui_ImplSDL2_ProcessEvent(&event);
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

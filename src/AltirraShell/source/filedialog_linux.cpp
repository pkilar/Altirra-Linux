//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - native file dialog helper
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <filedialog_linux.h>

#include <imgui.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>

namespace {

enum class DialogBackend {
	kNone,
	kZenity,
	kKdialog,
	kDetected
};

DialogBackend s_detectedBackend = DialogBackend::kNone;
bool s_backendDetected = false;

// ImGui fallback state
bool s_fallbackOpen = false;
char s_fallbackPath[1024] = {};
char s_fallbackTitle[128] = {};

DialogBackend DetectBackend() {
	if (s_backendDetected)
		return s_detectedBackend;

	s_backendDetected = true;

	// Try zenity first
	if (system("which zenity > /dev/null 2>&1") == 0) {
		s_detectedBackend = DialogBackend::kZenity;
		return s_detectedBackend;
	}

	// Try kdialog
	if (system("which kdialog > /dev/null 2>&1") == 0) {
		s_detectedBackend = DialogBackend::kKdialog;
		return s_detectedBackend;
	}

	s_detectedBackend = DialogBackend::kNone;
	return s_detectedBackend;
}

// Parse filter string "Label|*.ext1;*.ext2|Label2|*.ext3" into
// zenity-compatible --file-filter arguments.
std::vector<std::string> BuildZenityFilters(const char *filters) {
	std::vector<std::string> args;
	if (!filters || !*filters)
		return args;

	const char *p = filters;
	while (*p) {
		// Read label
		const char *labelStart = p;
		while (*p && *p != '|') ++p;
		std::string label(labelStart, p);
		if (*p == '|') ++p;

		// Read pattern
		const char *patStart = p;
		while (*p && *p != '|') ++p;
		std::string pattern(patStart, p);
		if (*p == '|') ++p;

		// Convert semicolons to spaces for zenity
		for (char& c : pattern) {
			if (c == ';') c = ' ';
		}

		args.push_back("--file-filter=" + label + " | " + pattern);
	}

	return args;
}

// Parse filter string into kdialog-compatible filter expression.
// kdialog format: "*.ext1 *.ext2|Label\n*.ext3|Label2"
std::string BuildKdialogFilter(const char *filters) {
	std::string result;
	if (!filters || !*filters)
		return result;

	const char *p = filters;
	bool first = true;
	while (*p) {
		const char *labelStart = p;
		while (*p && *p != '|') ++p;
		std::string label(labelStart, p);
		if (*p == '|') ++p;

		const char *patStart = p;
		while (*p && *p != '|') ++p;
		std::string pattern(patStart, p);
		if (*p == '|') ++p;

		// Convert semicolons to spaces
		for (char& c : pattern) {
			if (c == ';') c = ' ';
		}

		if (!first) result += '\n';
		first = false;
		result += pattern + "|" + label;
	}

	return result;
}

// Fork+exec a command, capture stdout, return the trimmed output.
std::string RunCommand(const std::vector<std::string>& argv) {
	int pipefd[2];
	if (pipe(pipefd) != 0)
		return {};

	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return {};
	}

	if (pid == 0) {
		// Child
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);

		// Build argv for execvp
		std::vector<const char*> cargv;
		for (const auto& s : argv)
			cargv.push_back(s.c_str());
		cargv.push_back(nullptr);

		execvp(cargv[0], const_cast<char* const*>(cargv.data()));
		_exit(127);
	}

	// Parent
	close(pipefd[1]);

	std::string output;
	char buf[4096];
	ssize_t n;
	while ((n = read(pipefd[0], buf, sizeof(buf))) > 0)
		output.append(buf, n);
	close(pipefd[0]);

	int status = 0;
	waitpid(pid, &status, 0);

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return {};

	// Trim trailing newline
	while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
		output.pop_back();

	return output;
}

// Extract the first extension from a filter string (e.g. "*.atcheats" → ".atcheats").
// Returns empty string if no extension found.
std::string ExtractDefaultExtension(const char *filters) {
	if (!filters || !*filters)
		return {};

	// Skip first label
	const char *p = filters;
	while (*p && *p != '|') ++p;
	if (*p == '|') ++p;

	// Now at pattern part, e.g. "*.atcheats;*.bin"
	// Find first "*."
	while (*p && *p != '|') {
		if (*p == '*' && *(p + 1) == '.') {
			const char *extStart = p + 1; // points at '.'
			const char *extEnd = extStart + 1;
			while (*extEnd && *extEnd != ';' && *extEnd != '|' && *extEnd != ' ')
				++extEnd;
			if (extEnd > extStart + 1) // not just ".*"
				return std::string(extStart, extEnd);
		}
		++p;
	}

	return {};
}

// Append default extension if the path has none.
std::string EnsureExtension(const std::string& path, const char *filters) {
	if (path.empty())
		return path;

	// Check if filename already has an extension
	size_t lastSlash = path.rfind('/');
	size_t lastDot = path.rfind('.');
	if (lastDot != std::string::npos && (lastSlash == std::string::npos || lastDot > lastSlash))
		return path; // already has extension

	std::string ext = ExtractDefaultExtension(filters);
	if (!ext.empty())
		return path + ext;

	return path;
}

} // anonymous namespace

VDStringW ATLinuxOpenFileDialog(const char *title, const char *filters) {
	DialogBackend backend = DetectBackend();

	if (backend == DialogBackend::kZenity) {
		std::vector<std::string> argv;
		argv.push_back("zenity");
		argv.push_back("--file-selection");
		argv.push_back(std::string("--title=") + (title ? title : "Open File"));

		auto zenityFilters = BuildZenityFilters(filters);
		for (const auto& f : zenityFilters)
			argv.push_back(f);

		std::string result = RunCommand(argv);
		if (!result.empty())
			return VDTextU8ToW(VDStringA(result.c_str(), (int)result.size()));
	} else if (backend == DialogBackend::kKdialog) {
		std::vector<std::string> argv;
		argv.push_back("kdialog");
		argv.push_back("--getopenfilename");
		argv.push_back("~");

		std::string filter = BuildKdialogFilter(filters);
		if (!filter.empty())
			argv.push_back(filter);

		std::string result = RunCommand(argv);
		if (!result.empty())
			return VDTextU8ToW(VDStringA(result.c_str(), (int)result.size()));
	}

	// Fallback: open ImGui popup (caller must poll ATLinuxFileDialogDrawFallback)
	ATLinuxFileDialogOpenFallback(title);
	return VDStringW();
}

std::vector<VDStringW> ATLinuxOpenMultiFileDialog(const char *title, const char *filters) {
	DialogBackend backend = DetectBackend();

	if (backend == DialogBackend::kZenity) {
		std::vector<std::string> argv;
		argv.push_back("zenity");
		argv.push_back("--file-selection");
		argv.push_back("--multiple");
		argv.push_back("--separator=|");
		argv.push_back(std::string("--title=") + (title ? title : "Open Files"));

		auto zenityFilters = BuildZenityFilters(filters);
		for (const auto& f : zenityFilters)
			argv.push_back(f);

		std::string result = RunCommand(argv);
		if (!result.empty()) {
			std::vector<VDStringW> paths;
			size_t start = 0;
			while (start < result.size()) {
				size_t sep = result.find('|', start);
				if (sep == std::string::npos)
					sep = result.size();
				if (sep > start)
					paths.push_back(VDTextU8ToW(VDStringA(result.c_str() + start, (int)(sep - start))));
				start = sep + 1;
			}
			return paths;
		}
	} else if (backend == DialogBackend::kKdialog) {
		std::vector<std::string> argv;
		argv.push_back("kdialog");
		argv.push_back("--getopenfilename");
		argv.push_back("--multiple");
		argv.push_back("~");

		std::string filter = BuildKdialogFilter(filters);
		if (!filter.empty())
			argv.push_back(filter);

		std::string result = RunCommand(argv);
		if (!result.empty()) {
			std::vector<VDStringW> paths;
			size_t start = 0;
			while (start < result.size()) {
				size_t sep = result.find('\n', start);
				if (sep == std::string::npos)
					sep = result.size();
				if (sep > start)
					paths.push_back(VDTextU8ToW(VDStringA(result.c_str() + start, (int)(sep - start))));
				start = sep + 1;
			}
			return paths;
		}
	}

	// Fallback: single-file dialog wrapped in vector
	ATLinuxFileDialogOpenFallback(title);
	return {};
}

VDStringW ATLinuxSaveFileDialog(const char *title, const char *filters) {
	DialogBackend backend = DetectBackend();

	if (backend == DialogBackend::kZenity) {
		std::vector<std::string> argv;
		argv.push_back("zenity");
		argv.push_back("--file-selection");
		argv.push_back("--save");
		argv.push_back("--confirm-overwrite");
		argv.push_back(std::string("--title=") + (title ? title : "Save File"));

		auto zenityFilters = BuildZenityFilters(filters);
		for (const auto& f : zenityFilters)
			argv.push_back(f);

		std::string result = RunCommand(argv);
		if (!result.empty()) {
			result = EnsureExtension(result, filters);
			return VDTextU8ToW(VDStringA(result.c_str(), (int)result.size()));
		}
	} else if (backend == DialogBackend::kKdialog) {
		std::vector<std::string> argv;
		argv.push_back("kdialog");
		argv.push_back("--getsavefilename");
		argv.push_back("~");

		std::string filter = BuildKdialogFilter(filters);
		if (!filter.empty())
			argv.push_back(filter);

		std::string result = RunCommand(argv);
		if (!result.empty()) {
			result = EnsureExtension(result, filters);
			return VDTextU8ToW(VDStringA(result.c_str(), (int)result.size()));
		}
	}

	// Fallback: open ImGui popup
	ATLinuxFileDialogOpenFallback(title ? title : "Save File");
	return VDStringW();
}

void ATLinuxFileDialogOpenFallback(const char *title) {
	s_fallbackOpen = true;
	s_fallbackPath[0] = 0;
	snprintf(s_fallbackTitle, sizeof(s_fallbackTitle), "%s", title ? title : "Open File");
	ImGui::OpenPopup(s_fallbackTitle);
}

bool ATLinuxFileDialogIsFallbackOpen() {
	return s_fallbackOpen;
}

bool ATLinuxFileDialogDrawFallback(VDStringW& result) {
	if (!s_fallbackOpen)
		return false;

	bool confirmed = false;

	ImGui::SetNextWindowSize(ImVec2(500, 120), ImGuiCond_FirstUseEver);
	if (ImGui::BeginPopupModal(s_fallbackTitle, &s_fallbackOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
		ImGui::Text("Enter file path:");
		ImGui::SetNextItemWidth(460);

		bool enter = ImGui::InputText("##filepath", s_fallbackPath, sizeof(s_fallbackPath),
			ImGuiInputTextFlags_EnterReturnsTrue);

		if (enter || ImGui::Button("OK", ImVec2(120, 0))) {
			if (s_fallbackPath[0]) {
				result = VDTextU8ToW(VDStringA(s_fallbackPath));
				confirmed = true;
				s_fallbackOpen = false;
				ImGui::CloseCurrentPopup();
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120, 0))) {
			s_fallbackOpen = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	return confirmed;
}

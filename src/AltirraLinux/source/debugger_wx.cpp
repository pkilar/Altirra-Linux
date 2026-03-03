//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
//	Linux port contributions
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <stdafx.h>
#include <debugger_wx.h>

#include <fstream>
#include <map>
#include <mutex>

#include <wx/aui/aui.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/combobox.h>
#include <wx/dcbuffer.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/rawbmp.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/toolbar.h>

#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/time.h>
#include <vd2/system/vdtypes.h>
#include <at/atcpu/execstate.h>
#include <at/atcore/profile.h>
#include <at/atdebugger/target.h>

#include <wx/filedlg.h>

#include "debugger.h"
#include "debugdisplay.h"
#include "disasm.h"
#include "printeroutput.h"

// profiler.h uses ATCPUTimestampDecoder by reference in internal methods
struct ATCPUTimestampDecoder;
struct ATCPUHistoryEntry;
#include "profiler.h"

#include "simulator.h"
#include "trace.h"

extern ATSimulator g_sim;

///////////////////////////////////////////////////////////////////////////
// Debugger client — receives state updates from the emulator debugger
///////////////////////////////////////////////////////////////////////////

namespace {

class ATWxDebuggerClient : public IATDebuggerClient {
public:
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override {
		std::lock_guard<std::mutex> lock(mMutex);
		if (mbStateValid && mState.mbRunning && !state.mbRunning)
			mbJustBroke = true;
		mState = state;
		mbStateValid = true;
	}

	void OnDebuggerEvent(ATDebugEvent eventId) override {
		std::lock_guard<std::mutex> lock(mMutex);
		switch (eventId) {
			case kATDebugEvent_BreakpointsChanged:
				mbBreakpointsChanged = true;
				break;
			case kATDebugEvent_SymbolsChanged:
				mbSymbolsChanged = true;
				break;
			default:
				break;
		}
	}

	ATDebuggerSystemState GetState() {
		std::lock_guard<std::mutex> lock(mMutex);
		return mState;
	}

	bool IsStateValid() {
		std::lock_guard<std::mutex> lock(mMutex);
		return mbStateValid;
	}

	bool ConsumeBreak() {
		std::lock_guard<std::mutex> lock(mMutex);
		bool v = mbJustBroke;
		mbJustBroke = false;
		return v;
	}

	bool ConsumeBreakpointsChanged() {
		std::lock_guard<std::mutex> lock(mMutex);
		bool v = mbBreakpointsChanged;
		mbBreakpointsChanged = false;
		return v;
	}

	bool ConsumeSymbolsChanged() {
		std::lock_guard<std::mutex> lock(mMutex);
		bool v = mbSymbolsChanged;
		mbSymbolsChanged = false;
		return v;
	}

private:
	std::mutex mMutex;
	ATDebuggerSystemState mState {};
	bool mbStateValid = false;
	bool mbJustBroke = false;
	bool mbBreakpointsChanged = false;
	bool mbSymbolsChanged = false;
};

} // anon

///////////////////////////////////////////////////////////////////////////
// Console output buffer (thread-safe)
///////////////////////////////////////////////////////////////////////////

static std::mutex s_consoleMutex;
static VDStringA s_consolePending;

///////////////////////////////////////////////////////////////////////////
// Forward declarations
///////////////////////////////////////////////////////////////////////////

class ATWxDebuggerFrame;
static ATWxDebuggerFrame *s_pDebugFrame = nullptr;
static ATWxDebuggerClient *s_pClient = nullptr;

///////////////////////////////////////////////////////////////////////////
// Registers panel
///////////////////////////////////////////////////////////////////////////

class ATWxRegistersPanel : public wxPanel {
public:
	ATWxRegistersPanel(wxWindow *parent);
	void UpdateFromState(const ATDebuggerSystemState& state);

private:
	void OnEditRegister(wxCommandEvent& event);

	wxStaticText *mpPC = nullptr;
	wxStaticText *mpA = nullptr;
	wxStaticText *mpX = nullptr;
	wxStaticText *mpY = nullptr;
	wxStaticText *mpS = nullptr;
	wxStaticText *mpP = nullptr;
	wxStaticText *mpFlags = nullptr;
	wxStaticText *mpCycle = nullptr;

	ATCPUExecState mPrevState {};
	bool mbPrevValid = false;
};

ATWxRegistersPanel::ATWxRegistersPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxFont mono(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);

	wxFlexGridSizer *grid = new wxFlexGridSizer(2, 4, 4);
	grid->AddGrowableCol(1);

	auto AddReg = [&](const char *label, wxStaticText *&text) {
		grid->Add(new wxStaticText(this, wxID_ANY, label), 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL);
		text = new wxStaticText(this, wxID_ANY, "----");
		text->SetFont(mono);
		grid->Add(text, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
	};

	AddReg("PC:", mpPC);
	AddReg("A:", mpA);
	AddReg("X:", mpX);
	AddReg("Y:", mpY);
	AddReg("S:", mpS);
	AddReg("P:", mpP);
	AddReg("Flags:", mpFlags);
	AddReg("Cycle:", mpCycle);

	SetSizer(grid);
}

void ATWxRegistersPanel::UpdateFromState(const ATDebuggerSystemState& state) {
	if (!state.mpDebugTarget)
		return;

	const ATCPUExecState6502& r = state.mExecState.m6502;

	char buf[64];
	snprintf(buf, sizeof(buf), "%04X", r.mPC);
	mpPC->SetLabel(buf);

	snprintf(buf, sizeof(buf), "%02X", r.mA);
	mpA->SetLabel(buf);

	snprintf(buf, sizeof(buf), "%02X", r.mX);
	mpX->SetLabel(buf);

	snprintf(buf, sizeof(buf), "%02X", r.mY);
	mpY->SetLabel(buf);

	snprintf(buf, sizeof(buf), "%02X", r.mS);
	mpS->SetLabel(buf);

	snprintf(buf, sizeof(buf), "%02X", r.mP);
	mpP->SetLabel(buf);

	// Decode P register flags: NV-BDIZC
	snprintf(buf, sizeof(buf), "%c%c-%c%c%c%c%c",
		(r.mP & 0x80) ? 'N' : 'n',
		(r.mP & 0x40) ? 'V' : 'v',
		(r.mP & 0x08) ? 'D' : 'd',
		(r.mP & 0x04) ? 'I' : 'i',
		(r.mP & 0x02) ? 'Z' : 'z',
		(r.mP & 0x01) ? 'C' : 'c',
		(r.mP & 0x10) ? 'B' : 'b');
	mpFlags->SetLabel(buf);

	snprintf(buf, sizeof(buf), "%u", state.mCycle);
	mpCycle->SetLabel(buf);

	mbPrevValid = true;
	mPrevState = state.mExecState;
}

void ATWxRegistersPanel::OnEditRegister(wxCommandEvent&) {
}

///////////////////////////////////////////////////////////////////////////
// Disassembly panel
///////////////////////////////////////////////////////////////////////////

class ATWxDisassemblyPanel : public wxPanel {
public:
	ATWxDisassemblyPanel(wxWindow *parent);
	void UpdateFromState(const ATDebuggerSystemState& state);
	void NavigateTo(uint32 addr);

private:
	void OnGo(wxCommandEvent& event);
	void OnGoPC(wxCommandEvent& event);
	void Repopulate();

	wxTextCtrl *mpAddrInput = nullptr;
	wxListCtrl *mpList = nullptr;
	wxCheckBox *mpFollowPC = nullptr;

	uint16 mBaseAddr = 0;
	uint16 mCurrentPC = 0;
	bool mbFollowPC = true;
	static const int kLineCount = 40;

	struct DisasmLine {
		uint16 addr;
		VDStringA text;
	};
	std::vector<DisasmLine> mLines;

	enum { ID_GO = 4100, ID_GO_PC, ID_FOLLOW_PC };
};

ATWxDisassemblyPanel::ATWxDisassemblyPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

	// Navigation bar
	wxBoxSizer *nav = new wxBoxSizer(wxHORIZONTAL);
	mpAddrInput = new wxTextCtrl(this, wxID_ANY, "0000", wxDefaultPosition,
		wxSize(80, -1), wxTE_PROCESS_ENTER);
	nav->Add(mpAddrInput, 0, wxRIGHT, 2);
	nav->Add(new wxButton(this, ID_GO, "Go", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 2);
	nav->Add(new wxButton(this, ID_GO_PC, "PC", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 4);
	mpFollowPC = new wxCheckBox(this, ID_FOLLOW_PC, "Follow PC");
	mpFollowPC->SetValue(true);
	nav->Add(mpFollowPC, 0, wxALIGN_CENTER_VERTICAL);
	top->Add(nav, 0, wxEXPAND | wxALL, 2);

	// Disassembly list
	mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
	mpList->AppendColumn("Disassembly", wxLIST_FORMAT_LEFT, 600);

	wxFont mono(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	mpList->SetFont(mono);

	top->Add(mpList, 1, wxEXPAND);
	SetSizer(top);

	Bind(wxEVT_BUTTON, &ATWxDisassemblyPanel::OnGo, this, ID_GO);
	Bind(wxEVT_BUTTON, &ATWxDisassemblyPanel::OnGoPC, this, ID_GO_PC);
	mpAddrInput->Bind(wxEVT_TEXT_ENTER, &ATWxDisassemblyPanel::OnGo, this);
}

void ATWxDisassemblyPanel::UpdateFromState(const ATDebuggerSystemState& state) {
	mCurrentPC = state.mPC;

	if (mbFollowPC || mpFollowPC->GetValue()) {
		mbFollowPC = mpFollowPC->GetValue();
		if (mbFollowPC) {
			mBaseAddr = state.mPC;
			char buf[8];
			snprintf(buf, sizeof(buf), "%04X", mBaseAddr);
			mpAddrInput->ChangeValue(buf);
		}
	}

	Repopulate();
}

void ATWxDisassemblyPanel::NavigateTo(uint32 addr) {
	mBaseAddr = (uint16)addr;
	char buf[8];
	snprintf(buf, sizeof(buf), "%04X", mBaseAddr);
	mpAddrInput->ChangeValue(buf);
	Repopulate();
}

void ATWxDisassemblyPanel::OnGo(wxCommandEvent&) {
	VDStringA addrStr(mpAddrInput->GetValue().utf8_str());
	unsigned int addr;
	if (sscanf(addrStr.c_str(), "%x", &addr) == 1) {
		mBaseAddr = addr & 0xFFFF;
	} else {
		IATDebugger *dbg = ATGetDebugger();
		if (dbg) {
			sint32 sym = dbg->ResolveSymbol(addrStr.c_str(), true, true, false);
			if (sym >= 0)
				mBaseAddr = (uint16)sym;
		}
	}
	Repopulate();
}

void ATWxDisassemblyPanel::OnGoPC(wxCommandEvent&) {
	IATDebugger *dbg = ATGetDebugger();
	if (dbg) {
		mBaseAddr = dbg->GetPC();
		char buf[8];
		snprintf(buf, sizeof(buf), "%04X", mBaseAddr);
		mpAddrInput->ChangeValue(buf);
		Repopulate();
	}
}

void ATWxDisassemblyPanel::Repopulate() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	IATDebugTarget *target = dbg->GetTarget();
	if (!target)
		return;

	mpList->DeleteAllItems();
	mLines.clear();

	ATDebugDisasmMode mode = target->GetDisasmMode();
	uint16 anchor = ATDisassembleGetFirstAnchor(target, mBaseAddr > 32 ? mBaseAddr - 32 : 0, mBaseAddr, 0);
	uint16 addr = anchor;

	for (int i = 0; i < kLineCount; ++i) {
		ATCPUHistoryEntry hent {};
		ATDisassembleCaptureInsnContext(target, addr, 0, hent);

		VDStringA line;
		ATDisasmResult result = ATDisassembleInsn(line, target, mode, hent,
			true, false, true, true, true, false, false, true, true, false);

		DisasmLine dl;
		dl.addr = addr;
		dl.text = line;
		mLines.push_back(dl);

		long idx = mpList->InsertItem(i, line.c_str());

		// Highlight current PC
		if (addr == mCurrentPC) {
			mpList->SetItemBackgroundColour(idx, wxColour(255, 255, 200));
		}

		// Check for breakpoint
		if (dbg->IsBreakpointAtPC(addr)) {
			mpList->SetItemTextColour(idx, wxColour(200, 0, 0));
		}

		addr = result.mNextPC;
	}
}

///////////////////////////////////////////////////////////////////////////
// Memory panel
///////////////////////////////////////////////////////////////////////////

class ATWxMemoryPanel : public wxPanel {
public:
	ATWxMemoryPanel(wxWindow *parent);
	void UpdateFromState(const ATDebuggerSystemState& state);

private:
	void OnGo(wxCommandEvent& event);
	void OnQuickNav(wxCommandEvent& event);
	void Repopulate();

	wxTextCtrl *mpAddrInput = nullptr;
	wxListCtrl *mpList = nullptr;
	uint16 mBaseAddr = 0;

	enum { ID_GO = 4200, ID_NAV_ZP, ID_NAV_STK, ID_NAV_HW };
};

ATWxMemoryPanel::ATWxMemoryPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer *nav = new wxBoxSizer(wxHORIZONTAL);
	mpAddrInput = new wxTextCtrl(this, wxID_ANY, "0000", wxDefaultPosition,
		wxSize(80, -1), wxTE_PROCESS_ENTER);
	nav->Add(mpAddrInput, 0, wxRIGHT, 2);
	nav->Add(new wxButton(this, ID_GO, "Go", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 4);
	nav->Add(new wxButton(this, ID_NAV_ZP, "ZP", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 2);
	nav->Add(new wxButton(this, ID_NAV_STK, "Stk", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 2);
	nav->Add(new wxButton(this, ID_NAV_HW, "HW", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
	top->Add(nav, 0, wxEXPAND | wxALL, 2);

	mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
	mpList->AppendColumn("Memory", wxLIST_FORMAT_LEFT, 600);

	wxFont mono(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	mpList->SetFont(mono);

	top->Add(mpList, 1, wxEXPAND);
	SetSizer(top);

	Bind(wxEVT_BUTTON, &ATWxMemoryPanel::OnGo, this, ID_GO);
	Bind(wxEVT_BUTTON, &ATWxMemoryPanel::OnQuickNav, this, ID_NAV_ZP, ID_NAV_HW);
	mpAddrInput->Bind(wxEVT_TEXT_ENTER, &ATWxMemoryPanel::OnGo, this);
}

void ATWxMemoryPanel::UpdateFromState(const ATDebuggerSystemState&) {
	Repopulate();
}

void ATWxMemoryPanel::OnGo(wxCommandEvent&) {
	VDStringA addrStr(mpAddrInput->GetValue().utf8_str());
	unsigned int addr;
	if (sscanf(addrStr.c_str(), "%x", &addr) == 1) {
		mBaseAddr = addr & 0xFFF0;
	} else {
		IATDebugger *dbg = ATGetDebugger();
		if (dbg) {
			sint32 sym = dbg->ResolveSymbol(addrStr.c_str(), true, true, false);
			if (sym >= 0)
				mBaseAddr = (uint16)(sym & 0xFFF0);
		}
	}
	Repopulate();
}

void ATWxMemoryPanel::OnQuickNav(wxCommandEvent& event) {
	switch (event.GetId()) {
		case ID_NAV_ZP:  mBaseAddr = 0x0000; break;
		case ID_NAV_STK: mBaseAddr = 0x0100; break;
		case ID_NAV_HW:  mBaseAddr = 0xD000; break;
	}
	char buf[8];
	snprintf(buf, sizeof(buf), "%04X", mBaseAddr);
	mpAddrInput->ChangeValue(buf);
	Repopulate();
}

void ATWxMemoryPanel::Repopulate() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	IATDebugTarget *target = dbg->GetTarget();
	if (!target)
		return;

	mpList->DeleteAllItems();

	// Show 16 rows x 16 bytes = 256 bytes
	for (int row = 0; row < 16; ++row) {
		uint16 rowAddr = mBaseAddr + row * 16;
		uint8 bytes[16];
		target->DebugReadMemory(rowAddr, bytes, 16);

		char line[128];
		int off = snprintf(line, sizeof(line), "%04X: ", rowAddr);

		// Hex bytes
		for (int i = 0; i < 16; ++i) {
			off += snprintf(line + off, sizeof(line) - off, "%02X ", bytes[i]);
			if (i == 7)
				off += snprintf(line + off, sizeof(line) - off, " ");
		}

		// ASCII
		off += snprintf(line + off, sizeof(line) - off, " |");
		for (int i = 0; i < 16; ++i) {
			char c = (bytes[i] >= 0x20 && bytes[i] < 0x7F) ? (char)bytes[i] : '.';
			off += snprintf(line + off, sizeof(line) - off, "%c", c);
		}
		off += snprintf(line + off, sizeof(line) - off, "|");

		mpList->InsertItem(row, line);
	}
}

///////////////////////////////////////////////////////////////////////////
// Console panel
///////////////////////////////////////////////////////////////////////////

class ATWxConsolePanel : public wxPanel {
public:
	ATWxConsolePanel(wxWindow *parent);
	void AppendText(const char *s);
	void FlushPending();

private:
	void OnCommand(wxCommandEvent& event);
	void OnClear(wxCommandEvent& event);

	wxTextCtrl *mpOutput = nullptr;
	wxTextCtrl *mpInput = nullptr;

	std::vector<VDStringA> mHistory;
	int mHistoryPos = -1;

	enum { ID_INPUT = 4300, ID_CLEAR };
};

ATWxConsolePanel::ATWxConsolePanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

	// Output area
	mpOutput = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
		wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	wxFont mono(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	mpOutput->SetFont(mono);
	top->Add(mpOutput, 1, wxEXPAND);

	// Input row
	wxBoxSizer *inputRow = new wxBoxSizer(wxHORIZONTAL);
	mpInput = new wxTextCtrl(this, ID_INPUT, "", wxDefaultPosition,
		wxDefaultSize, wxTE_PROCESS_ENTER);
	mpInput->SetFont(mono);
	inputRow->Add(mpInput, 1, wxEXPAND | wxRIGHT, 2);
	inputRow->Add(new wxButton(this, ID_CLEAR, "Clear", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
	top->Add(inputRow, 0, wxEXPAND | wxALL, 2);

	SetSizer(top);

	Bind(wxEVT_TEXT_ENTER, &ATWxConsolePanel::OnCommand, this, ID_INPUT);
	Bind(wxEVT_BUTTON, &ATWxConsolePanel::OnClear, this, ID_CLEAR);
}

void ATWxConsolePanel::AppendText(const char *s) {
	mpOutput->AppendText(s);
}

void ATWxConsolePanel::FlushPending() {
	VDStringA pending;
	{
		std::lock_guard<std::mutex> lock(s_consoleMutex);
		if (s_consolePending.empty())
			return;
		pending = std::move(s_consolePending);
		s_consolePending.clear();
	}
	mpOutput->AppendText(pending.c_str());
}

void ATWxConsolePanel::OnCommand(wxCommandEvent&) {
	VDStringA cmd(mpInput->GetValue().utf8_str());
	if (cmd.empty())
		return;

	mHistory.push_back(cmd);
	mHistoryPos = -1;

	IATDebugger *dbg = ATGetDebugger();
	if (dbg)
		dbg->QueueCommand(cmd.c_str(), true);

	mpInput->Clear();
}

void ATWxConsolePanel::OnClear(wxCommandEvent&) {
	mpOutput->Clear();
}

///////////////////////////////////////////////////////////////////////////
// Breakpoints panel
///////////////////////////////////////////////////////////////////////////

class ATWxBreakpointsPanel : public wxPanel {
public:
	ATWxBreakpointsPanel(wxWindow *parent);
	void UpdateBreakpoints();

private:
	void OnAdd(wxCommandEvent& event);
	void OnClearAll(wxCommandEvent& event);
	void OnRemove(wxCommandEvent& event);

	wxListCtrl *mpList = nullptr;
	wxTextCtrl *mpAddrInput = nullptr;
	wxChoice *mpTypeChoice = nullptr;

	struct BPEntry {
		uint32 userIdx;
	};
	vdfastvector<BPEntry> mEntries;

	enum { ID_ADD = 4400, ID_CLEAR_ALL, ID_REMOVE };
};

ATWxBreakpointsPanel::ATWxBreakpointsPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

	// Add row
	wxBoxSizer *addRow = new wxBoxSizer(wxHORIZONTAL);
	mpAddrInput = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition,
		wxSize(80, -1), wxTE_PROCESS_ENTER);
	addRow->Add(mpAddrInput, 0, wxRIGHT, 2);

	mpTypeChoice = new wxChoice(this, wxID_ANY);
	mpTypeChoice->Append("PC");
	mpTypeChoice->Append("Read");
	mpTypeChoice->Append("Write");
	mpTypeChoice->SetSelection(0);
	addRow->Add(mpTypeChoice, 0, wxRIGHT, 2);

	addRow->Add(new wxButton(this, ID_ADD, "Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 4);
	addRow->Add(new wxButton(this, ID_REMOVE, "Remove", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 2);
	addRow->Add(new wxButton(this, ID_CLEAR_ALL, "Clear All", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
	top->Add(addRow, 0, wxEXPAND | wxALL, 2);

	mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	mpList->AppendColumn("#", wxLIST_FORMAT_LEFT, 40);
	mpList->AppendColumn("Type", wxLIST_FORMAT_LEFT, 50);
	mpList->AppendColumn("Address", wxLIST_FORMAT_LEFT, 80);
	mpList->AppendColumn("Symbol", wxLIST_FORMAT_LEFT, 150);
	top->Add(mpList, 1, wxEXPAND);

	SetSizer(top);

	Bind(wxEVT_BUTTON, &ATWxBreakpointsPanel::OnAdd, this, ID_ADD);
	Bind(wxEVT_BUTTON, &ATWxBreakpointsPanel::OnClearAll, this, ID_CLEAR_ALL);
	Bind(wxEVT_BUTTON, &ATWxBreakpointsPanel::OnRemove, this, ID_REMOVE);
	mpAddrInput->Bind(wxEVT_TEXT_ENTER, &ATWxBreakpointsPanel::OnAdd, this);
}

void ATWxBreakpointsPanel::UpdateBreakpoints() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	mpList->DeleteAllItems();
	mEntries.clear();

	vdfastvector<uint32> bpList;
	dbg->GetBreakpointList(bpList);

	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();

	int row = 0;
	for (uint32 idx : bpList) {
		ATDebuggerBreakpointInfo info;
		if (!dbg->GetBreakpointInfo(idx, info))
			continue;

		BPEntry entry;
		entry.userIdx = idx;
		mEntries.push_back(entry);

		char numBuf[16];
		snprintf(numBuf, sizeof(numBuf), "%u", idx);
		long item = mpList->InsertItem(row, numBuf);

		const char *type = "?";
		if (info.mbBreakOnPC) type = "PC";
		else if (info.mbBreakOnRead) type = "RD";
		else if (info.mbBreakOnWrite) type = "WR";
		else if (info.mbBreakOnInsn) type = "IN";
		mpList->SetItem(item, 1, type);

		char addrBuf[16];
		snprintf(addrBuf, sizeof(addrBuf), "$%04X", info.mAddress);
		mpList->SetItem(item, 2, addrBuf);

		if (dbs) {
			ATSymbol sym;
			if (dbs->LookupSymbol(info.mAddress, 0, sym))
				mpList->SetItem(item, 3, sym.mpName);
		}

		++row;
	}
}

void ATWxBreakpointsPanel::OnAdd(wxCommandEvent&) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	VDStringA addrStr(mpAddrInput->GetValue().utf8_str());
	if (addrStr.empty())
		return;

	unsigned int addr;
	if (sscanf(addrStr.c_str(), "%x", &addr) != 1) {
		sint32 sym = dbg->ResolveSymbol(addrStr.c_str(), true, true, false);
		if (sym < 0) {
			wxMessageBox("Invalid address or symbol.", "Error", wxOK | wxICON_ERROR, this);
			return;
		}
		addr = (uint32)sym;
	}

	int typeIdx = mpTypeChoice->GetSelection();
	if (typeIdx == 0) {
		dbg->ToggleBreakpoint(addr);
	} else {
		dbg->ToggleAccessBreakpoint(addr, typeIdx == 2);
	}

	mpAddrInput->Clear();
	UpdateBreakpoints();
}

void ATWxBreakpointsPanel::OnClearAll(wxCommandEvent&) {
	IATDebugger *dbg = ATGetDebugger();
	if (dbg)
		dbg->ClearAllBreakpoints();
	UpdateBreakpoints();
}

void ATWxBreakpointsPanel::OnRemove(wxCommandEvent&) {
	long sel = mpList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel < 0 || sel >= (long)mEntries.size())
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (dbg)
		dbg->ClearUserBreakpoint(mEntries[sel].userIdx, true);
	UpdateBreakpoints();
}

///////////////////////////////////////////////////////////////////////////
// Call Stack panel
///////////////////////////////////////////////////////////////////////////

class ATWxCallStackPanel : public wxPanel {
public:
	ATWxCallStackPanel(wxWindow *parent);
	void UpdateFromState(const ATDebuggerSystemState& state);

private:
	wxListCtrl *mpList = nullptr;
};

ATWxCallStackPanel::ATWxCallStackPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);
	mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	mpList->AppendColumn("SP", wxLIST_FORMAT_LEFT, 50);
	mpList->AppendColumn("PC", wxLIST_FORMAT_LEFT, 60);
	mpList->AppendColumn("Symbol", wxLIST_FORMAT_LEFT, 200);
	top->Add(mpList, 1, wxEXPAND);
	SetSizer(top);
}

void ATWxCallStackPanel::UpdateFromState(const ATDebuggerSystemState& state) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	mpList->DeleteAllItems();

	ATCallStackFrame frames[16];
	uint32 count = dbg->GetCallStack(frames, 16);

	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();

	for (uint32 i = 0; i < count; ++i) {
		char spBuf[8], pcBuf[8];
		snprintf(spBuf, sizeof(spBuf), "%04X", frames[i].mSP);
		snprintf(pcBuf, sizeof(pcBuf), "%04X", frames[i].mPC);

		long idx = mpList->InsertItem(i, spBuf);
		mpList->SetItem(idx, 1, pcBuf);

		if (dbs) {
			ATSymbol sym;
			if (dbs->LookupSymbol(frames[i].mPC, 0, sym))
				mpList->SetItem(idx, 2, sym.mpName);
		}
	}
}

///////////////////////////////////////////////////////////////////////////
// Watch panel
///////////////////////////////////////////////////////////////////////////

class ATWxWatchPanel : public wxPanel {
public:
	ATWxWatchPanel(wxWindow *parent);
	void UpdateFromState(const ATDebuggerSystemState& state);

private:
	void OnAdd(wxCommandEvent& event);
	void OnClearAll(wxCommandEvent& event);

	wxListCtrl *mpList = nullptr;
	wxTextCtrl *mpExprInput = nullptr;

	enum { ID_ADD = 4500, ID_CLEAR_ALL };
};

ATWxWatchPanel::ATWxWatchPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer *addRow = new wxBoxSizer(wxHORIZONTAL);
	mpExprInput = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition,
		wxSize(120, -1), wxTE_PROCESS_ENTER);
	addRow->Add(mpExprInput, 1, wxRIGHT, 2);
	addRow->Add(new wxButton(this, ID_ADD, "Add", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 2);
	addRow->Add(new wxButton(this, ID_CLEAR_ALL, "Clear", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
	top->Add(addRow, 0, wxEXPAND | wxALL, 2);

	mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	mpList->AppendColumn("Expression", wxLIST_FORMAT_LEFT, 120);
	mpList->AppendColumn("Value", wxLIST_FORMAT_LEFT, 100);
	top->Add(mpList, 1, wxEXPAND);

	SetSizer(top);

	Bind(wxEVT_BUTTON, &ATWxWatchPanel::OnAdd, this, ID_ADD);
	Bind(wxEVT_BUTTON, &ATWxWatchPanel::OnClearAll, this, ID_CLEAR_ALL);
	mpExprInput->Bind(wxEVT_TEXT_ENTER, &ATWxWatchPanel::OnAdd, this);
}

void ATWxWatchPanel::UpdateFromState(const ATDebuggerSystemState& state) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	// Update existing watch values
	for (long i = 0; i < mpList->GetItemCount(); ++i) {
		ATDebuggerWatchInfo info;
		if (dbg->GetWatchInfo(i, info)) {
			char buf[32];
			switch (info.mMode) {
				case ATDebuggerWatchMode::ByteAtAddress: {
					IATDebugTarget *t = dbg->GetTarget();
					if (t) {
						uint8 v = t->DebugReadByte(info.mAddress);
						snprintf(buf, sizeof(buf), "$%02X (%u)", v, v);
						mpList->SetItem(i, 1, buf);
					}
					break;
				}
				case ATDebuggerWatchMode::WordAtAddress: {
					IATDebugTarget *t = dbg->GetTarget();
					if (t) {
						uint8 lo = t->DebugReadByte(info.mAddress);
						uint8 hi = t->DebugReadByte(info.mAddress + 1);
						uint16 v = lo | ((uint16)hi << 8);
						snprintf(buf, sizeof(buf), "$%04X (%u)", v, v);
						mpList->SetItem(i, 1, buf);
					}
					break;
				}
				default:
					break;
			}
		}
	}
}

void ATWxWatchPanel::OnAdd(wxCommandEvent&) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	VDStringA expr(mpExprInput->GetValue().utf8_str());
	if (expr.empty())
		return;

	// Try as hex address first for byte watch
	unsigned int addr;
	if (sscanf(expr.c_str(), "%x", &addr) == 1) {
		dbg->AddWatch(addr & 0xFFFF, 1);
		long idx = mpList->InsertItem(mpList->GetItemCount(), expr.c_str());
		mpList->SetItem(idx, 1, "---");
	}

	mpExprInput->Clear();
}

void ATWxWatchPanel::OnClearAll(wxCommandEvent&) {
	IATDebugger *dbg = ATGetDebugger();
	if (dbg)
		dbg->ClearAllWatches();
	mpList->DeleteAllItems();
}

///////////////////////////////////////////////////////////////////////////
// History panel
///////////////////////////////////////////////////////////////////////////

class ATWxHistoryPanel : public wxPanel {
public:
	ATWxHistoryPanel(wxWindow *parent);
	void UpdateFromState(const ATDebuggerSystemState& state);

private:
	void OnToggleRecording(wxCommandEvent& event);
	void OnViewModeChanged(wxCommandEvent& event);

	wxListCtrl *mpList = nullptr;
	wxCheckBox *mpRecordCB = nullptr;
	wxChoice *mpViewMode = nullptr;
	bool mDetailedView = false;

	enum { ID_RECORD = 4600, ID_VIEW_MODE };
};

ATWxHistoryPanel::ATWxHistoryPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer *toolbar = new wxBoxSizer(wxHORIZONTAL);
	mpRecordCB = new wxCheckBox(this, ID_RECORD, "Record History");
	toolbar->Add(mpRecordCB, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

	toolbar->Add(new wxStaticText(this, wxID_ANY, "View:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	mpViewMode = new wxChoice(this, ID_VIEW_MODE);
	mpViewMode->Append("Disassembly");
	mpViewMode->Append("Registers");
	mpViewMode->SetSelection(0);
	mpViewMode->Bind(wxEVT_CHOICE, &ATWxHistoryPanel::OnViewModeChanged, this);
	toolbar->Add(mpViewMode, 0, wxALIGN_CENTER_VERTICAL);
	top->Add(toolbar, 0, wxALL, 4);

	mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	mpList->AppendColumn("History", wxLIST_FORMAT_LEFT, 500);

	wxFont mono(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	mpList->SetFont(mono);

	top->Add(mpList, 1, wxEXPAND);
	SetSizer(top);

	Bind(wxEVT_CHECKBOX, &ATWxHistoryPanel::OnToggleRecording, this, ID_RECORD);
}

void ATWxHistoryPanel::UpdateFromState(const ATDebuggerSystemState& state) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	IATDebugTarget *target = dbg->GetTarget();
	if (!target)
		return;

	IATDebugTargetHistory *hist = (IATDebugTargetHistory *)target->AsInterface(IATDebugTargetHistory::kTypeID);
	if (!hist)
		return;

	mpRecordCB->SetValue(hist->GetHistoryEnabled());

	if (!hist->GetHistoryEnabled())
		return;

	mpList->DeleteAllItems();

	auto range = hist->GetHistoryRange();
	uint32 start = range.first;
	uint32 end = range.second;
	uint32 count = end - start;
	if (count > 256)
		start = end - 256;

	const int maxLines = 256;
	const ATCPUHistoryEntry *hparray[maxLines];
	uint32 fetched = hist->ExtractHistory(hparray, start, std::min(count, (uint32)maxLines));

	if (mDetailedView) {
		// Detailed register view: Cycle | PC | Instruction | A | X | Y | S | P
		for (uint32 i = 0; i < fetched; ++i) {
			const ATCPUHistoryEntry& h = *hparray[i];

			// Build instruction mnemonic via disassembler
			VDStringA insn;
			ATDebugDisasmMode mode = target->GetDisasmMode();
			ATDisassembleInsn(insn, target, mode, h,
				false, false, false, false, false, false, false, false, true, false);

			char line[256];
			snprintf(line, sizeof(line),
				"%8u  $%04X  %-20s  A=%02X X=%02X Y=%02X S=%02X P=%02X%s%s%s%s%s%s%s%s",
				h.mCycle, h.mPC, insn.c_str(),
				h.mA, h.mX, h.mY, h.mS, h.mP,
				(h.mP & 0x80) ? " N" : "",
				(h.mP & 0x40) ? " V" : "",
				(h.mP & 0x08) ? " D" : "",
				(h.mP & 0x04) ? " I" : "",
				(h.mP & 0x02) ? " Z" : "",
				(h.mP & 0x01) ? " C" : "",
				h.mbIRQ ? " [IRQ]" : "",
				h.mbNMI ? " [NMI]" : "");

			mpList->InsertItem(i, line);
		}
	} else {
		// Original disassembly view
		ATDebugDisasmMode mode = target->GetDisasmMode();
		for (uint32 i = 0; i < fetched; ++i) {
			VDStringA line;
			ATDisassembleInsn(line, target, mode, *hparray[i],
				true, true, true, true, true, false, false, true, true, false);

			mpList->InsertItem(i, line.c_str());
		}
	}
}

void ATWxHistoryPanel::OnToggleRecording(wxCommandEvent&) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) return;
	IATDebugTarget *target = dbg->GetTarget();
	if (!target) return;
	IATDebugTargetHistory *hist = (IATDebugTargetHistory *)target->AsInterface(IATDebugTargetHistory::kTypeID);
	if (!hist) return;

	hist->SetHistoryEnabled(!hist->GetHistoryEnabled());
}

void ATWxHistoryPanel::OnViewModeChanged(wxCommandEvent&) {
	mDetailedView = (mpViewMode->GetSelection() == 1);

	// Adjust column width for the view mode
	if (mDetailedView) {
		mpList->SetColumnWidth(0, 800);
	} else {
		mpList->SetColumnWidth(0, 500);
	}
}

///////////////////////////////////////////////////////////////////////////
// Printer Output panel
///////////////////////////////////////////////////////////////////////////

class ATWxPrinterPanel : public wxPanel {
public:
	ATWxPrinterPanel(wxWindow *parent);
	void Refresh();

private:
	void OnClear(wxCommandEvent& event);
	void OnOutputSelect(wxCommandEvent& event);

	wxComboBox *mpOutputSelect = nullptr;
	wxTextCtrl *mpOutput = nullptr;
	int mSelectedOutput = 0;
	size_t mLastOffset = 0;

	enum { ID_CLEAR = 4700, ID_OUTPUT_SELECT };
};

ATWxPrinterPanel::ATWxPrinterPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer *toolbar = new wxBoxSizer(wxHORIZONTAL);
	mpOutputSelect = new wxComboBox(this, ID_OUTPUT_SELECT, "", wxDefaultPosition,
		wxSize(200, -1), 0, nullptr, wxCB_READONLY);
	toolbar->Add(mpOutputSelect, 0, wxRIGHT, 4);
	toolbar->Add(new wxButton(this, ID_CLEAR, "Clear", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
	top->Add(toolbar, 0, wxEXPAND | wxALL, 2);

	mpOutput = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
		wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
	wxFont mono(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	mpOutput->SetFont(mono);
	top->Add(mpOutput, 1, wxEXPAND);

	SetSizer(top);

	Bind(wxEVT_BUTTON, &ATWxPrinterPanel::OnClear, this, ID_CLEAR);
	Bind(wxEVT_COMBOBOX, &ATWxPrinterPanel::OnOutputSelect, this, ID_OUTPUT_SELECT);
}

void ATWxPrinterPanel::Refresh() {
	ATPrinterOutputManager *mgr = static_cast<ATPrinterOutputManager *>(
		&g_sim.GetPrinterOutputManager());

	uint32 count = mgr->GetOutputCount();

	// Update combo box if output count changed
	if ((int)mpOutputSelect->GetCount() != (int)count) {
		mpOutputSelect->Clear();
		for (uint32 i = 0; i < count; i++) {
			ATPrinterOutput& out = mgr->GetOutput(i);
			VDStringA name = VDTextWToU8(VDStringW(out.GetName()));
			mpOutputSelect->Append(name.c_str());
		}
		if (count > 0 && mpOutputSelect->GetSelection() < 0)
			mpOutputSelect->SetSelection(0);
	}

	if (count == 0) return;
	if (mSelectedOutput >= (int)count) mSelectedOutput = 0;

	ATPrinterOutput& out = mgr->GetOutput(mSelectedOutput);
	size_t currentLen = out.GetLength();

	if (currentLen > mLastOffset) {
		const wchar_t *ptr = out.GetTextPointer(mLastOffset);
		size_t newChars = currentLen - mLastOffset;
		VDStringW wstr(ptr, newChars);
		VDStringA u8 = VDTextWToU8(wstr);
		mpOutput->AppendText(u8.c_str());
		mLastOffset = currentLen;
		out.Revalidate();
	}
}

void ATWxPrinterPanel::OnClear(wxCommandEvent&) {
	ATPrinterOutputManager *mgr = static_cast<ATPrinterOutputManager *>(
		&g_sim.GetPrinterOutputManager());
	if (mSelectedOutput < (int)mgr->GetOutputCount()) {
		mgr->GetOutput(mSelectedOutput).Clear();
		mpOutput->Clear();
		mLastOffset = 0;
	}
}

void ATWxPrinterPanel::OnOutputSelect(wxCommandEvent& event) {
	mSelectedOutput = event.GetSelection();
	mLastOffset = 0;
	mpOutput->Clear();
}

///////////////////////////////////////////////////////////////////////////
// Source Code panel
///////////////////////////////////////////////////////////////////////////

class ATWxSourcePanel : public wxPanel {
public:
	ATWxSourcePanel(wxWindow *parent);
	void UpdateFromState(const ATDebuggerSystemState& state);
	bool NavigateToAddress(uint32 addr);

private:
	void OnFileSelect(wxCommandEvent& event);
	void RefreshFileList();
	void LoadFile(int fileIdx);
	void Repopulate();

	wxChoice *mpFileChoice = nullptr;
	wxListCtrl *mpList = nullptr;

	struct SourceFile {
		VDStringW mPath;
		uint32 mNumLines;
	};
	std::vector<SourceFile> mFiles;
	std::vector<std::string> mSourceLines;
	std::map<int, uint32> mLineToAddr;
	std::map<uint32, int> mAddrToLine;
	uint32 mModuleId = 0;
	uint16 mFileId = 0;
	int mSelectedFile = -1;
	int mPCLine = -1;
	bool mNeedsFileList = true;

	enum { ID_FILE_SELECT = 4800 };
};

ATWxSourcePanel::ATWxSourcePanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

	mpFileChoice = new wxChoice(this, ID_FILE_SELECT);
	top->Add(mpFileChoice, 0, wxEXPAND | wxALL, 2);

	mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
	mpList->AppendColumn("Source", wxLIST_FORMAT_LEFT, 800);

	wxFont mono(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	mpList->SetFont(mono);
	top->Add(mpList, 1, wxEXPAND);

	SetSizer(top);

	Bind(wxEVT_CHOICE, &ATWxSourcePanel::OnFileSelect, this, ID_FILE_SELECT);
}

void ATWxSourcePanel::RefreshFileList() {
	mFiles.clear();
	mpFileChoice->Clear();

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) return;

	dbg->EnumSourceFiles(
		[](const wchar_t *path, uint32 numLines) {
			// Can't capture 'this' in a C callback-style lambda easily,
			// so we use a global workaround
		}
	);

	// Use a different approach: iterate symbol stores
	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();
	if (!dbs) return;

	// EnumSourceFiles takes a vdfunction, use it directly
	auto *filesPtr = &mFiles;
	dbg->EnumSourceFiles(
		[filesPtr](const wchar_t *path, uint32 numLines) {
			if (numLines > 0)
				filesPtr->push_back({VDStringW(path), numLines});
		}
	);

	for (size_t i = 0; i < mFiles.size(); i++) {
		const wchar_t *name = VDFileSplitPath(mFiles[i].mPath.c_str());
		VDStringA u8 = VDTextWToU8(VDStringW(name));
		char label[256];
		snprintf(label, sizeof(label), "%s (%u lines)", u8.c_str(), mFiles[i].mNumLines);
		mpFileChoice->Append(label);
	}

	mNeedsFileList = false;
}

void ATWxSourcePanel::LoadFile(int fileIdx) {
	mSourceLines.clear();
	mLineToAddr.clear();
	mAddrToLine.clear();
	mPCLine = -1;
	mModuleId = 0;
	mFileId = 0;

	if (fileIdx < 0 || fileIdx >= (int)mFiles.size()) return;

	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();
	if (!dbs) return;

	const VDStringW& path = mFiles[fileIdx].mPath;
	uint32 moduleId;
	uint16 fileId;
	if (!dbs->LookupFile(path.c_str(), moduleId, fileId)) return;

	mModuleId = moduleId;
	mFileId = fileId;

	vdfastvector<ATSourceLineInfo> lines;
	dbs->GetLinesForFile(moduleId, fileId, lines);

	for (const auto& li : lines) {
		int lineIdx = (int)li.mLine - 1;
		if (lineIdx >= 0) {
			if (mLineToAddr.find(lineIdx) == mLineToAddr.end())
				mLineToAddr[lineIdx] = li.mOffset;
			if (mAddrToLine.find(li.mOffset) == mAddrToLine.end())
				mAddrToLine[li.mOffset] = lineIdx;
		}
	}

	// Try to load source file from disk
	ATDebuggerSourceFileInfo sourceFileInfo;
	if (!dbs->GetSourceFilePath(moduleId, fileId, sourceFileInfo)) return;

	const wchar_t *tryPaths[] = { sourceFileInfo.mSourcePath.c_str(), sourceFileInfo.mModulePath.c_str() };
	bool loaded = false;

	for (const wchar_t *tryPath : tryPaths) {
		if (!tryPath || !tryPath[0]) continue;
		VDStringA narrowPath = VDTextWToU8(VDStringW(tryPath));
		std::ifstream ifs(narrowPath.c_str());
		if (!ifs.is_open()) continue;

		std::string line;
		while (std::getline(ifs, line))
			mSourceLines.push_back(std::move(line));
		loaded = true;
		break;
	}

	if (!loaded) {
		mSourceLines.push_back("(Source file not found on disk)");
		if (!mLineToAddr.empty()) {
			int maxLine = mLineToAddr.rbegin()->first;
			mSourceLines.resize(maxLine + 1);
		}
	}

	Repopulate();
}

void ATWxSourcePanel::Repopulate() {
	IATDebugger *dbg = ATGetDebugger();
	mpList->DeleteAllItems();

	for (int i = 0; i < (int)mSourceLines.size(); i++) {
		auto addrIt = mLineToAddr.find(i);
		bool hasMappedAddr = (addrIt != mLineToAddr.end());

		char prefix[32];
		if (hasMappedAddr)
			snprintf(prefix, sizeof(prefix), "%5d %04X  ", i + 1, addrIt->second);
		else
			snprintf(prefix, sizeof(prefix), "%5d       ", i + 1);

		std::string display = std::string(prefix) + mSourceLines[i];
		long idx = mpList->InsertItem(i, display.c_str());

		if (i == mPCLine) {
			mpList->SetItemBackgroundColour(idx, wxColour(100, 100, 0));
			mpList->SetItemTextColour(idx, wxColour(255, 255, 77));
		} else if (hasMappedAddr) {
			mpList->SetItemTextColour(idx, wxColour(220, 220, 220));
		} else {
			mpList->SetItemTextColour(idx, wxColour(128, 128, 128));
		}

		if (hasMappedAddr && dbg && dbg->IsBreakpointAtPC(addrIt->second))
			mpList->SetItemTextColour(idx, wxColour(220, 40, 40));
	}

	// Scroll to PC line
	if (mPCLine >= 0 && mPCLine < (int)mSourceLines.size())
		mpList->EnsureVisible(mPCLine);
}

void ATWxSourcePanel::UpdateFromState(const ATDebuggerSystemState& state) {
	if (mNeedsFileList) RefreshFileList();

	mPCLine = -1;
	if (!state.mbRunning && state.mPCModuleId == mModuleId && state.mPCFileId == mFileId && state.mPCLine > 0) {
		mPCLine = (int)state.mPCLine - 1;
	} else if (!state.mbRunning) {
		auto it = mAddrToLine.find(state.mPC);
		if (it != mAddrToLine.end())
			mPCLine = it->second;
	}

	if (!mSourceLines.empty())
		Repopulate();
}

void ATWxSourcePanel::OnFileSelect(wxCommandEvent& event) {
	mSelectedFile = event.GetSelection();
	LoadFile(mSelectedFile);
}

bool ATWxSourcePanel::NavigateToAddress(uint32 addr) {
	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();
	if (!dbs) return false;

	uint32 moduleId;
	ATSourceLineInfo lineInfo;
	if (!dbs->LookupLine(addr, false, moduleId, lineInfo))
		return false;

	ATDebuggerSourceFileInfo sourceFileInfo;
	if (!dbs->GetSourceFilePath(moduleId, lineInfo.mFileId, sourceFileInfo))
		return false;

	// Find matching file in our list, refreshing if needed
	if (mNeedsFileList) RefreshFileList();

	int targetIdx = -1;
	for (int i = 0; i < (int)mFiles.size(); i++) {
		if (VDFileIsPathEqual(mFiles[i].mPath.c_str(), sourceFileInfo.mSourcePath.c_str())) {
			targetIdx = i;
			break;
		}
		// Try filename-only match
		if (VDFileIsPathEqual(VDFileSplitPath(mFiles[i].mPath.c_str()),
				VDFileSplitPath(sourceFileInfo.mSourcePath.c_str()))) {
			targetIdx = i;
			break;
		}
	}

	if (targetIdx < 0) return false;

	// Load the file if not already loaded
	if (targetIdx != mSelectedFile) {
		mSelectedFile = targetIdx;
		mpFileChoice->SetSelection(targetIdx);
		LoadFile(targetIdx);
	}

	// Scroll to the target line
	int targetLine = (int)lineInfo.mLine - 1;
	if (targetLine >= 0 && targetLine < (int)mSourceLines.size())
		mpList->EnsureVisible(targetLine);

	return true;
}

///////////////////////////////////////////////////////////////////////////
// Performance Overlay panel
///////////////////////////////////////////////////////////////////////////

class ATWxPerformanceProfiler : public IATProfiler {
public:
	static constexpr int kWidth = 256;
	static constexpr int kHeight = 200;

	void OnEvent(ATProfileEvent event) override;
	void OnEventWithArg(ATProfileEvent event, uintptr arg) override {}
	void BeginRegion(ATProfileRegion region) override;
	void EndRegion(ATProfileRegion region) override;

	struct Column {
		int regionPixels[kATProfileRegionCount] {};
	};

	Column mColumns[kWidth] {};
	int mX = 0;
	int mRegionStackHt = 0;
	ATProfileRegion mRegionStack[64] {};
	uint64 mFrameStartTime = 0;
	uint64 mRegionStartTime = 0;
	double mTicksToPixels = 0;
};

void ATWxPerformanceProfiler::OnEvent(ATProfileEvent event) {
	if (event != kATProfileEvent_BeginFrame) return;
	mRegionStackHt = 0;
	mX = (mX + 1) & (kWidth - 1);
	mFrameStartTime = VDGetPreciseTick();
	mRegionStartTime = mFrameStartTime;
	mTicksToPixels = VDGetPreciseSecondsPerTick() * (double)kHeight * 30.0;

	Column& col = mColumns[mX];
	for (int i = 0; i < kATProfileRegionCount; i++)
		col.regionPixels[i] = 0;
}

void ATWxPerformanceProfiler::BeginRegion(ATProfileRegion region) {
	if (mRegionStackHt < 64) {
		if (mRegionStackHt > 0) {
			uint64 now = VDGetPreciseTick();
			int pixels = (int)((double)(now - mRegionStartTime) * mTicksToPixels);
			if (pixels > 0)
				mColumns[mX].regionPixels[mRegionStack[mRegionStackHt - 1]] += pixels;
			mRegionStartTime = now;
		}
		mRegionStack[mRegionStackHt++] = region;
		mRegionStartTime = VDGetPreciseTick();
	}
}

void ATWxPerformanceProfiler::EndRegion(ATProfileRegion region) {
	if (mRegionStackHt > 0) {
		uint64 now = VDGetPreciseTick();
		int pixels = (int)((double)(now - mRegionStartTime) * mTicksToPixels);
		if (pixels > 0)
			mColumns[mX].regionPixels[mRegionStack[mRegionStackHt - 1]] += pixels;
		--mRegionStackHt;
		mRegionStartTime = now;
	}
}

static ATWxPerformanceProfiler *s_pPerfProfiler = nullptr;

class ATWxPerformancePanel : public wxPanel {
public:
	ATWxPerformancePanel(wxWindow *parent);
	~ATWxPerformancePanel();

private:
	void OnPaint(wxPaintEvent& event);

	wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(ATWxPerformancePanel, wxPanel)
	EVT_PAINT(ATWxPerformancePanel::OnPaint)
wxEND_EVENT_TABLE()

ATWxPerformancePanel::ATWxPerformancePanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxFULL_REPAINT_ON_RESIZE | wxBORDER_NONE)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetMinSize(wxSize(ATWxPerformanceProfiler::kWidth + 16, ATWxPerformanceProfiler::kHeight + 120));

	if (!s_pPerfProfiler) {
		s_pPerfProfiler = new ATWxPerformanceProfiler;
		s_pPerfProfiler->mFrameStartTime = VDGetPreciseTick();
		s_pPerfProfiler->mTicksToPixels = VDGetPreciseSecondsPerTick() * (double)ATWxPerformanceProfiler::kHeight * 30.0;
		g_pATProfiler = s_pPerfProfiler;
	}
}

ATWxPerformancePanel::~ATWxPerformancePanel() {
	if (s_pPerfProfiler) {
		if (g_pATProfiler == s_pPerfProfiler)
			g_pATProfiler = nullptr;
		delete s_pPerfProfiler;
		s_pPerfProfiler = nullptr;
	}
}

void ATWxPerformancePanel::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(this);
	wxSize sz = GetSize();

	dc.SetBackground(wxBrush(wxColour(30, 30, 30)));
	dc.Clear();

	if (!s_pPerfProfiler) return;

	static const wxColour kRegionColors[kATProfileRegionCount] = {
		wxColour(128, 128, 128),  // Idle
		wxColour(255, 255, 255),  // IdleFrameDelay
		wxColour(64, 96, 224),    // Simulation
		wxColour(224, 32, 16),    // NativeEvents
		wxColour(0, 0, 0),        // NativeMessage (hidden)
		wxColour(0, 0, 0),        // DisplayPost (hidden)
		wxColour(32, 224, 16),    // DisplayTick
		wxColour(255, 224, 16),   // DisplayPresent
	};

	static const char *kRegionNames[kATProfileRegionCount] = {
		"Idle", "Idle (delay)", "Simulation", "Native events",
		nullptr, nullptr, "Display tick", "Display present",
	};

	constexpr int kW = ATWxPerformanceProfiler::kWidth;
	constexpr int kH = ATWxPerformanceProfiler::kHeight;

	int xOff = 8;
	int yOff = 4;

	// Background rectangle
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(wxColour(0, 0, 0)));
	dc.DrawRectangle(xOff, yOff, kW, kH);

	// Draw stacked columns
	int curX = s_pPerfProfiler->mX;
	for (int col = 0; col < kW; col++) {
		int idx = (curX + 1 + col) & (kW - 1);
		const auto& c = s_pPerfProfiler->mColumns[idx];

		int y = yOff + kH;

		for (int r = 0; r < kATProfileRegionCount; r++) {
			if (c.regionPixels[r] <= 0) continue;
			if (r == kATProfileRegion_NativeMessage || r == kATProfileRegion_DisplayPost) continue;

			int h = c.regionPixels[r];
			if (h > kH) h = kH;

			dc.SetBrush(wxBrush(kRegionColors[r]));
			dc.DrawRectangle(xOff + col, y - h, 1, h);
			y -= h;
		}
	}

	// Border
	dc.SetPen(wxPen(wxColour(255, 255, 255, 80)));
	dc.SetBrush(*wxTRANSPARENT_BRUSH);
	dc.DrawRectangle(xOff, yOff, kW, kH);

	// Legend
	dc.SetFont(GetFont().IsOk() ? GetFont() : wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	int legendY = yOff + kH + 8;
	for (int i = 0; i < kATProfileRegionCount; i++) {
		if (!kRegionNames[i]) continue;
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(kRegionColors[i]));
		dc.DrawRectangle(xOff, legendY, 12, 12);
		dc.SetTextForeground(wxColour(200, 200, 200));
		dc.DrawText(kRegionNames[i], xOff + 16, legendY);
		legendY += 16;
	}
}

///////////////////////////////////////////////////////////////////////////
// Trace Viewer panel
///////////////////////////////////////////////////////////////////////////

class ATWxTracePanel : public wxPanel {
public:
	ATWxTracePanel(wxWindow *parent);

private:
	void OnStartStop(wxCommandEvent& event);
	void OnClear(wxCommandEvent& event);
	void OnZoomFit(wxCommandEvent& event);
	void OnPaint(wxPaintEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	void OnMouseMiddleDrag(wxMouseEvent& event);

	wxCheckBox *mpCpuCB = nullptr;
	wxCheckBox *mpVideoCB = nullptr;
	wxCheckBox *mpBasicCB = nullptr;
	wxButton *mpStartStopBtn = nullptr;
	wxPanel *mpCanvas = nullptr;

	bool mRecording = false;
	bool mHasData = false;
	vdrefptr<ATTraceCollection> mpCollection;
	double mTotalDuration = 0;
	double mViewStart = 0;
	double mViewEnd = 1.0;
	int mLastMouseX = 0;

	enum { ID_START_STOP = 4900, ID_CLEAR, ID_ZOOM_FIT };

	wxDECLARE_EVENT_TABLE();
};

wxBEGIN_EVENT_TABLE(ATWxTracePanel, wxPanel)
wxEND_EVENT_TABLE()

ATWxTracePanel::ATWxTracePanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer *toolbar = new wxBoxSizer(wxHORIZONTAL);
	mpStartStopBtn = new wxButton(this, ID_START_STOP, "Start Recording", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	toolbar->Add(mpStartStopBtn, 0, wxRIGHT, 4);
	mpCpuCB = new wxCheckBox(this, wxID_ANY, "CPU"); mpCpuCB->SetValue(true);
	toolbar->Add(mpCpuCB, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 4);
	mpVideoCB = new wxCheckBox(this, wxID_ANY, "Video");
	toolbar->Add(mpVideoCB, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 4);
	mpBasicCB = new wxCheckBox(this, wxID_ANY, "BASIC");
	toolbar->Add(mpBasicCB, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, 4);
	toolbar->Add(new wxButton(this, ID_CLEAR, "Clear", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0, wxRIGHT, 2);
	toolbar->Add(new wxButton(this, ID_ZOOM_FIT, "Zoom Fit", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
	top->Add(toolbar, 0, wxEXPAND | wxALL, 2);

	mpCanvas = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxFULL_REPAINT_ON_RESIZE | wxBORDER_NONE);
	mpCanvas->SetBackgroundStyle(wxBG_STYLE_PAINT);
	top->Add(mpCanvas, 1, wxEXPAND);

	SetSizer(top);

	Bind(wxEVT_BUTTON, &ATWxTracePanel::OnStartStop, this, ID_START_STOP);
	Bind(wxEVT_BUTTON, &ATWxTracePanel::OnClear, this, ID_CLEAR);
	Bind(wxEVT_BUTTON, &ATWxTracePanel::OnZoomFit, this, ID_ZOOM_FIT);
	mpCanvas->Bind(wxEVT_PAINT, &ATWxTracePanel::OnPaint, this);
	mpCanvas->Bind(wxEVT_MOUSEWHEEL, &ATWxTracePanel::OnMouseWheel, this);
	mpCanvas->Bind(wxEVT_MIDDLE_DOWN, &ATWxTracePanel::OnMouseMiddleDrag, this);
	mpCanvas->Bind(wxEVT_MOTION, &ATWxTracePanel::OnMouseMiddleDrag, this);
}

void ATWxTracePanel::OnStartStop(wxCommandEvent&) {
	if (!mRecording) {
		ATTraceSettings settings {};
		settings.mbTraceCpuInsns = mpCpuCB->GetValue();
		settings.mbTraceVideo = mpVideoCB->GetValue();
		settings.mbTraceBasic = mpBasicCB->GetValue();
		g_sim.StartTracing(settings);
		mRecording = true;
		mHasData = false;
		mpStartStopBtn->SetLabel("Stop Recording");
		mpCpuCB->Disable();
		mpVideoCB->Disable();
		mpBasicCB->Disable();
	} else {
		mpCollection = g_sim.GetTraceCollection();
		g_sim.StopTracing();
		mRecording = false;
		mpStartStopBtn->SetLabel("Start Recording");
		mpCpuCB->Enable();
		mpVideoCB->Enable();
		mpBasicCB->Enable();

		if (mpCollection && mpCollection->GetGroupCount() > 0) {
			mHasData = true;
			mTotalDuration = 0;
			for (size_t gi = 0; gi < mpCollection->GetGroupCount(); ++gi) {
				double d = mpCollection->GetGroup(gi)->GetDuration();
				if (d > mTotalDuration) mTotalDuration = d;
			}
			mViewStart = 0;
			mViewEnd = mTotalDuration > 0 ? mTotalDuration : 1.0;
		}
		mpCanvas->Refresh(false);
	}
}

void ATWxTracePanel::OnClear(wxCommandEvent&) {
	mHasData = false;
	mTotalDuration = 0;
	mpCollection.clear();
	mpCanvas->Refresh(false);
}

void ATWxTracePanel::OnZoomFit(wxCommandEvent&) {
	if (mHasData) {
		mViewStart = 0;
		mViewEnd = mTotalDuration > 0 ? mTotalDuration : 1.0;
		mpCanvas->Refresh(false);
	}
}

void ATWxTracePanel::OnPaint(wxPaintEvent&) {
	wxAutoBufferedPaintDC dc(mpCanvas);
	wxSize sz = mpCanvas->GetSize();

	dc.SetBackground(wxBrush(wxColour(30, 30, 30)));
	dc.Clear();

	if (!mHasData || !mpCollection) {
		dc.SetTextForeground(wxColour(128, 128, 128));
		dc.DrawText(mRecording ? "Recording..." : "No trace data", 8, 8);
		return;
	}

	ATTraceCollection *tc = mpCollection;
	double viewDuration = mViewEnd - mViewStart;
	if (viewDuration <= 0) viewDuration = 1.0;
	double pixelsPerSec = sz.GetWidth() / viewDuration;

	// Ruler (24px)
	const int rulerH = 24;
	dc.SetPen(*wxTRANSPARENT_PEN);
	dc.SetBrush(wxBrush(wxColour(32, 32, 32)));
	dc.DrawRectangle(0, 0, sz.GetWidth(), rulerH);

	// Tick marks
	dc.SetFont(wxFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	double tickSpacing = 1.0;
	const char *tickFmt = "%.0fs";
	double pixPerTick = tickSpacing * pixelsPerSec;
	if (pixPerTick < 60) { tickSpacing = 0.1; tickFmt = "%.1fs"; pixPerTick = tickSpacing * pixelsPerSec; }
	if (pixPerTick < 60) { tickSpacing = 0.01; tickFmt = "%.2fs"; pixPerTick = tickSpacing * pixelsPerSec; }
	if (pixPerTick < 60) { tickSpacing = 0.001; tickFmt = "%.3fs"; pixPerTick = tickSpacing * pixelsPerSec; }
	if (pixPerTick > 300) { tickSpacing *= 5; }

	dc.SetPen(wxPen(wxColour(180, 180, 180)));
	dc.SetTextForeground(wxColour(200, 200, 200));
	double firstTick = std::ceil(mViewStart / tickSpacing) * tickSpacing;
	for (double t = firstTick; t <= mViewEnd; t += tickSpacing) {
		int x = (int)((t - mViewStart) * pixelsPerSec);
		dc.DrawLine(x, rulerH - 6, x, rulerH);
		char buf[32];
		snprintf(buf, sizeof(buf), tickFmt, t);
		dc.DrawText(buf, x + 2, 2);
	}

	// Channel rows
	const int groupH = 22;
	const int chanH = 20;
	int y = rulerH;
	int channelIdx = 0;

	dc.SetFont(wxFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

	for (size_t gi = 0; gi < tc->GetGroupCount(); ++gi) {
		ATTraceGroup *grp = tc->GetGroup(gi);
		VDStringA groupName = VDTextWToU8(VDStringW(grp->GetName()));

		// Group header
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.SetBrush(wxBrush(wxColour(50, 50, 60)));
		dc.DrawRectangle(0, y, sz.GetWidth(), groupH);
		dc.SetTextForeground(wxColour(220, 220, 255));
		dc.DrawText(groupName.c_str(), 4, y + 3);
		y += groupH;

		for (size_t ci = 0; ci < grp->GetChannelCount(); ++ci) {
			IATTraceChannel *ch = grp->GetChannel(ci);
			VDStringA chName = VDTextWToU8(VDStringW(ch->GetName()));

			// Channel background
			wxColour bgCol = (channelIdx & 1) ? wxColour(34, 36, 36) : wxColour(38, 40, 40);
			dc.SetBrush(wxBrush(bgCol));
			dc.DrawRectangle(0, y, sz.GetWidth(), chanH);

			// Channel name
			dc.SetTextForeground(wxColour(160, 160, 160));
			dc.DrawText(chName.c_str(), 2, y + 2);

			// Draw events
			double threshold = viewDuration / sz.GetWidth();
			ch->StartIteration(mViewStart, mViewEnd, threshold);

			ATTraceEvent ev;
			while (ch->GetNextEvent(ev)) {
				int x0 = (int)((ev.mEventStart - mViewStart) * pixelsPerSec);
				int x1 = (int)((ev.mEventStop - mViewStart) * pixelsPerSec);
				if (x1 < 0 || x0 > sz.GetWidth()) continue;
				if (x0 < 0) x0 = 0;
				if (x1 > sz.GetWidth()) x1 = sz.GetWidth();
				if (x1 - x0 < 1) x1 = x0 + 1;

				uint32 bg = ev.mBgColor;
				dc.SetBrush(wxBrush(wxColour((bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF)));
				dc.DrawRectangle(x0, y + 1, x1 - x0, chanH - 2);

				// Label if wide enough
				if (x1 - x0 > 40 && ev.mpName) {
					VDStringA label = VDTextWToU8(VDStringW(ev.mpName));
					uint32 fg = ev.mFgColor;
					dc.SetTextForeground(wxColour((fg >> 16) & 0xFF, (fg >> 8) & 0xFF, fg & 0xFF));
					dc.SetClippingRegion(x0, y, x1 - x0, chanH);
					dc.DrawText(label.c_str(), x0 + 2, y + 2);
					dc.DestroyClippingRegion();
				}
			}

			y += chanH;
			++channelIdx;
		}
	}
}

void ATWxTracePanel::OnMouseWheel(wxMouseEvent& event) {
	if (!mHasData) return;

	double viewDuration = mViewEnd - mViewStart;
	double pixelsPerSec = mpCanvas->GetSize().GetWidth() / viewDuration;
	double mouseTime = mViewStart + event.GetX() / pixelsPerSec;

	if (event.ControlDown()) {
		// Ctrl+wheel: zoom
		double zoomFactor = 1.3;
		double ratio = (mouseTime - mViewStart) / viewDuration;
		if (event.GetWheelRotation() > 0) {
			double newDuration = viewDuration / zoomFactor;
			mViewStart = mouseTime - ratio * newDuration;
			mViewEnd = mViewStart + newDuration;
		} else {
			double newDuration = viewDuration * zoomFactor;
			mViewStart = mouseTime - ratio * newDuration;
			mViewEnd = mViewStart + newDuration;
		}
	} else {
		// Wheel: horizontal scroll
		double scrollAmt = viewDuration * 0.1 * (event.GetWheelRotation() > 0 ? -1.0 : 1.0);
		mViewStart += scrollAmt;
		mViewEnd += scrollAmt;
	}

	// Clamp
	if (mViewStart < 0) { mViewEnd -= mViewStart; mViewStart = 0; }
	if (mViewEnd > mTotalDuration) { mViewStart -= (mViewEnd - mTotalDuration); mViewEnd = mTotalDuration; if (mViewStart < 0) mViewStart = 0; }

	mpCanvas->Refresh(false);
}

void ATWxTracePanel::OnMouseMiddleDrag(wxMouseEvent& event) {
	if (event.MiddleDown()) {
		mLastMouseX = event.GetX();
		return;
	}
	if (event.Dragging() && event.MiddleIsDown() && mHasData) {
		int dx = event.GetX() - mLastMouseX;
		mLastMouseX = event.GetX();
		double viewDuration = mViewEnd - mViewStart;
		double pixelsPerSec = mpCanvas->GetSize().GetWidth() / viewDuration;
		double timeDelta = dx / pixelsPerSec;
		mViewStart -= timeDelta;
		mViewEnd -= timeDelta;
		mpCanvas->Refresh(false);
	}
}

///////////////////////////////////////////////////////////////////////////
// Debug Display (ANTIC visualization) panel
///////////////////////////////////////////////////////////////////////////

class ATWxDebugDisplayPanel : public wxPanel {
public:
	ATWxDebugDisplayPanel(wxWindow *parent);
	~ATWxDebugDisplayPanel();
	void RefreshDisplay();

private:
	void OnPaint(wxPaintEvent& event);
	void OnModeChange(wxCommandEvent& event);

	wxChoice *mpModeChoice = nullptr;
	wxChoice *mpPaletteChoice = nullptr;
	wxPanel *mpCanvas = nullptr;

	ATDebugDisplay *mpDebugDisplay = nullptr;
	wxBitmap mBitmap;

	enum { ID_MODE = 5300, ID_PALETTE };
};

ATWxDebugDisplayPanel::ATWxDebugDisplayPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer *toolbar = new wxBoxSizer(wxHORIZONTAL);
	toolbar->Add(new wxStaticText(this, wxID_ANY, "Mode:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	mpModeChoice = new wxChoice(this, ID_MODE);
	mpModeChoice->Append("ANTIC History");
	mpModeChoice->Append("ANTIC History Start");
	mpModeChoice->SetSelection(0);
	toolbar->Add(mpModeChoice, 0, wxRIGHT, 8);

	toolbar->Add(new wxStaticText(this, wxID_ANY, "Palette:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	mpPaletteChoice = new wxChoice(this, ID_PALETTE);
	mpPaletteChoice->Append("Registers");
	mpPaletteChoice->Append("Analysis");
	mpPaletteChoice->SetSelection(0);
	toolbar->Add(mpPaletteChoice, 0);
	top->Add(toolbar, 0, wxEXPAND | wxALL, 2);

	mpCanvas = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(376, 240),
		wxFULL_REPAINT_ON_RESIZE | wxBORDER_SIMPLE);
	mpCanvas->SetBackgroundStyle(wxBG_STYLE_PAINT);
	top->Add(mpCanvas, 0, wxALL, 4);

	SetSizer(top);

	mpDebugDisplay = new ATDebugDisplay;
	mpDebugDisplay->Init(g_sim.GetMemoryManager(), &g_sim.GetAntic(), &g_sim.GetGTIA(), nullptr);

	mpCanvas->Bind(wxEVT_PAINT, &ATWxDebugDisplayPanel::OnPaint, this);
	Bind(wxEVT_CHOICE, &ATWxDebugDisplayPanel::OnModeChange, this, ID_MODE);
	Bind(wxEVT_CHOICE, &ATWxDebugDisplayPanel::OnModeChange, this, ID_PALETTE);
}

ATWxDebugDisplayPanel::~ATWxDebugDisplayPanel() {
	if (mpDebugDisplay) {
		mpDebugDisplay->Shutdown();
		delete mpDebugDisplay;
	}
}

void ATWxDebugDisplayPanel::RefreshDisplay() {
	if (!mpDebugDisplay) return;

	mpDebugDisplay->SetMode((ATDebugDisplay::Mode)mpModeChoice->GetSelection());
	mpDebugDisplay->SetPaletteMode((ATDebugDisplay::PaletteMode)mpPaletteChoice->GetSelection());
	mpDebugDisplay->Update();

	const VDPixmapBuffer& buf = mpDebugDisplay->GetFrameBuffer();
	if (!buf.data || !buf.palette) return;

	// Create or recreate bitmap
	if (!mBitmap.IsOk() || mBitmap.GetWidth() != 376 || mBitmap.GetHeight() != 240)
		mBitmap.Create(376, 240, 24);

	wxNativePixelData data(mBitmap);
	if (!data) return;

	wxNativePixelData::Iterator p(data);
	for (int row = 0; row < 240; ++row) {
		p.MoveTo(data, 0, row);
		const uint8 *src = (const uint8 *)buf.data + buf.pitch * row;
		for (int x = 0; x < 376; ++x, ++p) {
			uint32 pal = buf.palette[src[x]];
			// Palette is 0x00BBGGRR format
			p.Red()   = (pal >> 16) & 0xFF;
			p.Green() = (pal >> 8)  & 0xFF;
			p.Blue()  = pal & 0xFF;
		}
	}

	mpCanvas->Refresh(false);
}

void ATWxDebugDisplayPanel::OnPaint(wxPaintEvent&) {
	wxPaintDC dc(mpCanvas);
	if (mBitmap.IsOk())
		dc.DrawBitmap(mBitmap, 0, 0, false);
}

void ATWxDebugDisplayPanel::OnModeChange(wxCommandEvent&) {
	RefreshDisplay();
}

///////////////////////////////////////////////////////////////////////////
// CPU Profiler panel
///////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////
// Profile timeline bar chart canvas

class ATWxProfileTimeline : public wxPanel {
public:
	ATWxProfileTimeline(wxWindow *parent)
		: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, 120))
	{
		SetBackgroundStyle(wxBG_STYLE_PAINT);
		Bind(wxEVT_PAINT, &ATWxProfileTimeline::OnPaint, this);
	}

	void SetData(const std::vector<std::pair<VDStringA, uint32>>& bars, uint32 totalCycles) {
		mBars = bars;
		mTotalCycles = totalCycles;
		Refresh();
	}

	void Clear() { mBars.clear(); mTotalCycles = 0; Refresh(); }

private:
	void OnPaint(wxPaintEvent&) {
		wxAutoBufferedPaintDC dc(this);
		wxSize sz = GetClientSize();

		dc.SetBackground(wxBrush(wxColour(30, 30, 30)));
		dc.Clear();

		if (mBars.empty() || mTotalCycles == 0) {
			dc.SetTextForeground(wxColour(128, 128, 128));
			dc.DrawText("No profile data", 4, 4);
			return;
		}

		dc.SetFont(wxFont(8, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));

		int barH = std::max(8, (sz.GetHeight() - 4) / (int)mBars.size() - 2);
		int labelW = 100;
		int barAreaW = sz.GetWidth() - labelW - 60;
		if (barAreaW < 50) barAreaW = 50;

		// Color palette for bars
		static const wxColour colors[] = {
			wxColour(0x48, 0x9E, 0xFF), wxColour(0xFF, 0x6B, 0x6B),
			wxColour(0x6B, 0xFF, 0x6B), wxColour(0xFF, 0xD9, 0x3D),
			wxColour(0xBF, 0x6B, 0xFF), wxColour(0xFF, 0xA5, 0x48),
			wxColour(0x48, 0xD1, 0xCC), wxColour(0xFF, 0x80, 0xBF),
		};

		int y = 2;
		for (size_t i = 0; i < mBars.size() && y + barH <= sz.GetHeight(); i++) {
			const auto& [name, cycles] = mBars[i];
			float pct = (float)cycles / (float)mTotalCycles;
			int barW = (int)(pct * barAreaW);
			if (barW < 1) barW = 1;

			// Label
			dc.SetTextForeground(wxColour(180, 180, 180));
			dc.SetClippingRegion(0, y, labelW - 4, barH);
			dc.DrawText(name.c_str(), 2, y);
			dc.DestroyClippingRegion();

			// Bar
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.SetBrush(wxBrush(colors[i % std::size(colors)]));
			dc.DrawRectangle(labelW, y, barW, barH);

			// Percentage
			char buf[16];
			snprintf(buf, sizeof(buf), "%.1f%%", pct * 100.0f);
			dc.SetTextForeground(wxColour(200, 200, 200));
			dc.DrawText(buf, labelW + barW + 4, y);

			y += barH + 2;
		}
	}

	std::vector<std::pair<VDStringA, uint32>> mBars;
	uint32 mTotalCycles = 0;
};

///////////////////////////////////////////////////////////////////////////
// CPU Profiler panel with timeline

class ATWxProfilerPanel : public wxPanel {
public:
	ATWxProfilerPanel(wxWindow *parent);

	void UpdateFromState(const ATDebuggerSystemState& state);

private:
	void OnStartStop(wxCommandEvent& event);
	void OnExport(wxCommandEvent& event);
	void Repopulate();

	wxChoice *mpModeChoice = nullptr;
	wxButton *mpStartStopBtn = nullptr;
	wxListCtrl *mpList = nullptr;
	wxStaticText *mpStatusText = nullptr;
	ATWxProfileTimeline *mpTimeline = nullptr;

	bool mProfiling = false;
	vdrefptr<ATProfileMergedFrame> mpMerged;
	ATProfileSession mSession;
	bool mHasSession = false;

	enum { ID_START_STOP = 5400, ID_EXPORT, ID_MODE };
};

ATWxProfilerPanel::ATWxProfilerPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

	wxBoxSizer *toolbar = new wxBoxSizer(wxHORIZONTAL);
	mpStartStopBtn = new wxButton(this, ID_START_STOP, "Start Profiling", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
	toolbar->Add(mpStartStopBtn, 0, wxRIGHT, 4);
	toolbar->Add(new wxStaticText(this, wxID_ANY, "Mode:"), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
	mpModeChoice = new wxChoice(this, ID_MODE);
	mpModeChoice->Append("Instructions");
	mpModeChoice->Append("Functions");
	mpModeChoice->Append("Call Graph");
	mpModeChoice->Append("Basic Block");
	mpModeChoice->Append("Basic Lines");
	mpModeChoice->SetSelection(1);
	toolbar->Add(mpModeChoice, 0, wxRIGHT, 4);
	toolbar->Add(new wxButton(this, ID_EXPORT, "Export CSV", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT), 0);
	top->Add(toolbar, 0, wxEXPAND | wxALL, 2);

	mpStatusText = new wxStaticText(this, wxID_ANY, "Profiler idle");
	top->Add(mpStatusText, 0, wxLEFT | wxBOTTOM, 4);

	mpTimeline = new ATWxProfileTimeline(this);
	top->Add(mpTimeline, 0, wxEXPAND | wxLEFT | wxRIGHT, 2);

	mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL);
	mpList->AppendColumn("Address", wxLIST_FORMAT_LEFT, 70);
	mpList->AppendColumn("Symbol", wxLIST_FORMAT_LEFT, 150);
	mpList->AppendColumn("Calls", wxLIST_FORMAT_RIGHT, 70);
	mpList->AppendColumn("Insns", wxLIST_FORMAT_RIGHT, 80);
	mpList->AppendColumn("Cycles", wxLIST_FORMAT_RIGHT, 80);
	mpList->AppendColumn("Cycles%", wxLIST_FORMAT_RIGHT, 70);
	mpList->AppendColumn("CPI", wxLIST_FORMAT_RIGHT, 50);

	wxFont mono(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
	mpList->SetFont(mono);
	top->Add(mpList, 1, wxEXPAND);

	SetSizer(top);

	Bind(wxEVT_BUTTON, &ATWxProfilerPanel::OnStartStop, this, ID_START_STOP);
	Bind(wxEVT_BUTTON, &ATWxProfilerPanel::OnExport, this, ID_EXPORT);
}

void ATWxProfilerPanel::OnStartStop(wxCommandEvent&) {
	ATCPUProfiler *profiler = g_sim.GetProfiler();
	if (!profiler) return;

	if (!mProfiling) {
		// Map mode choice index to ATProfileMode enum
		int modeIdx = mpModeChoice->GetSelection();
		ATProfileMode mode = (ATProfileMode)modeIdx;

		g_sim.SetProfilingEnabled(true);
		profiler->Start(mode, kATProfileCounterMode_None, kATProfileCounterMode_None);
		mProfiling = true;
		mpStartStopBtn->SetLabel("Stop Profiling");
		mpStatusText->SetLabel("Profiling...");
	} else {
		// Stop profiling and retrieve session data
		if (profiler->IsRunning()) {
			profiler->End();
			profiler->GetSession(mSession);
			mHasSession = true;

			// Merge all frames
			uint32 frameCount = (uint32)mSession.mpFrames.size();
			if (frameCount > 0) {
				ATProfileMergedFrame *merged = nullptr;
				ATProfileMergeFrames(mSession, 0, frameCount, &merged);
				mpMerged.clear();
				mpMerged.set(merged);
				Repopulate();
			}
		}
		g_sim.SetProfilingEnabled(false);

		mProfiling = false;
		mpStartStopBtn->SetLabel("Start Profiling");

		if (!mpMerged) {
			mpStatusText->SetLabel("No profile data collected");
		}
	}
}

void ATWxProfilerPanel::OnExport(wxCommandEvent&) {
	if (!mpMerged) return;

	wxFileDialog dlg(this, "Export Profiler CSV", "", "profile.csv",
		"CSV files (*.csv)|*.csv", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
	if (dlg.ShowModal() != wxID_OK) return;

	std::ofstream ofs(dlg.GetPath().utf8_str().data());
	if (!ofs.is_open()) return;

	ofs << "Address,Symbol,Calls,Instructions,Cycles,UnhaltedCycles\n";
	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();

	for (const ATProfileRecord& rec : mpMerged->mRecords) {
		const char *symName = "";
		ATSymbol sym;
		if (dbs && dbs->LookupSymbol(rec.mAddress, kATSymbol_Execute, sym))
			symName = sym.mpName;
		ofs << "$" << std::hex << rec.mAddress << std::dec << ","
			<< symName << ","
			<< rec.mCalls << "," << rec.mInsns << ","
			<< rec.mCycles << "," << rec.mUnhaltedCycles << "\n";
	}
}

void ATWxProfilerPanel::Repopulate() {
	mpList->DeleteAllItems();
	if (!mpMerged) { mpTimeline->Clear(); return; }

	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();

	uint32 totalCycles = mpMerged->mTotalCycles;
	if (totalCycles == 0) totalCycles = 1;

	// Sort records by cycles (descending)
	std::vector<const ATProfileRecord *> sorted;
	sorted.reserve(mpMerged->mRecords.size());
	for (const auto& r : mpMerged->mRecords)
		sorted.push_back(&r);
	std::sort(sorted.begin(), sorted.end(),
		[](const ATProfileRecord *a, const ATProfileRecord *b) { return a->mCycles > b->mCycles; });

	// Build timeline bar data (top 15 hotspots)
	std::vector<std::pair<VDStringA, uint32>> bars;
	for (size_t i = 0; i < sorted.size() && i < 15; i++) {
		const ATProfileRecord *rec = sorted[i];
		VDStringA name;
		if (dbs) {
			ATSymbol sym;
			if (dbs->LookupSymbol(rec->mAddress, kATSymbol_Execute, sym)) {
				name = sym.mpName;
			}
		}
		if (name.empty()) {
			char buf[16];
			snprintf(buf, sizeof(buf), "$%04X", rec->mAddress);
			name = VDStringA(buf);
		}
		bars.push_back({name, rec->mCycles});
	}
	mpTimeline->SetData(bars, totalCycles);

	int row = 0;
	for (const ATProfileRecord *rec : sorted) {
		if (row >= 500) break;

		char addrBuf[16];
		snprintf(addrBuf, sizeof(addrBuf), "$%04X", rec->mAddress);
		long idx = mpList->InsertItem(row, addrBuf);

		if (dbs) {
			ATSymbol sym;
			if (dbs->LookupSymbol(rec->mAddress, kATSymbol_Execute, sym))
				mpList->SetItem(idx, 1, sym.mpName);
		}

		char buf[32];
		snprintf(buf, sizeof(buf), "%u", rec->mCalls);
		mpList->SetItem(idx, 2, buf);

		snprintf(buf, sizeof(buf), "%u", rec->mInsns);
		mpList->SetItem(idx, 3, buf);

		snprintf(buf, sizeof(buf), "%u", rec->mCycles);
		mpList->SetItem(idx, 4, buf);

		float pct = (float)rec->mCycles * 100.0f / (float)totalCycles;
		snprintf(buf, sizeof(buf), "%.1f%%", pct);
		mpList->SetItem(idx, 5, buf);

		float cpi = rec->mInsns > 0 ? (float)rec->mCycles / (float)rec->mInsns : 0;
		snprintf(buf, sizeof(buf), "%.1f", cpi);
		mpList->SetItem(idx, 6, buf);

		++row;
	}

	char statusBuf[128];
	snprintf(statusBuf, sizeof(statusBuf), "%u records, %u total cycles, %u frames",
		(uint32)mpMerged->mRecords.size(), mpMerged->mTotalCycles,
		(uint32)mSession.mpFrames.size());
	mpStatusText->SetLabel(statusBuf);
}

void ATWxProfilerPanel::UpdateFromState(const ATDebuggerSystemState&) {
	// Could refresh profiler status here if needed
}

///////////////////////////////////////////////////////////////////////////
// Debugger frame (top-level window with AUI manager)
///////////////////////////////////////////////////////////////////////////

class ATWxDebuggerFrame : public wxFrame {
public:
	ATWxDebuggerFrame(wxWindow *parent);
	~ATWxDebuggerFrame();

	void AppendConsoleText(const char *s);
	bool NavigateSourceToAddress(uint32 addr);

private:
	void OnClose(wxCloseEvent& event);
	void OnTimer(wxTimerEvent& event);
	void OnRunStop(wxCommandEvent& event);
	void OnStepInto(wxCommandEvent& event);
	void OnStepOver(wxCommandEvent& event);
	void OnStepOut(wxCommandEvent& event);
	void OnViewPane(wxCommandEvent& event);

	void UpdateAllPanes();

	wxAuiManager mAuiMgr;
	wxTimer mRefreshTimer;

	ATWxRegistersPanel *mpRegisters = nullptr;
	ATWxDisassemblyPanel *mpDisassembly = nullptr;
	ATWxMemoryPanel *mpMemory = nullptr;
	ATWxConsolePanel *mpConsole = nullptr;
	ATWxBreakpointsPanel *mpBreakpoints = nullptr;
	ATWxCallStackPanel *mpCallStack = nullptr;
	ATWxWatchPanel *mpWatch = nullptr;
	ATWxHistoryPanel *mpHistory = nullptr;
	ATWxSourcePanel *mpSource = nullptr;
	ATWxPrinterPanel *mpPrinter = nullptr;
	ATWxPerformancePanel *mpPerformance = nullptr;
	ATWxTracePanel *mpTrace = nullptr;
	ATWxDebugDisplayPanel *mpDebugDisplay = nullptr;
	ATWxProfilerPanel *mpProfiler = nullptr;

	bool mbJustBroke = false;

	enum {
		ID_REFRESH_TIMER = 5000,
		ID_RUN_STOP = 5100,
		ID_STEP_INTO,
		ID_STEP_OVER,
		ID_STEP_OUT,
		ID_VIEW_REGISTERS = 5200,
		ID_VIEW_DISASSEMBLY,
		ID_VIEW_MEMORY,
		ID_VIEW_CONSOLE,
		ID_VIEW_BREAKPOINTS,
		ID_VIEW_CALLSTACK,
		ID_VIEW_WATCH,
		ID_VIEW_HISTORY,
		ID_VIEW_SOURCE,
		ID_VIEW_PRINTER,
		ID_VIEW_PERFORMANCE,
		ID_VIEW_TRACE,
		ID_VIEW_DEBUG_DISPLAY,
		ID_VIEW_PROFILER
	};
};

ATWxDebuggerFrame::ATWxDebuggerFrame(wxWindow *parent)
	: wxFrame(parent, wxID_ANY, "Altirra Debugger", wxDefaultPosition,
		wxSize(1100, 700), wxDEFAULT_FRAME_STYLE)
	, mRefreshTimer(this, ID_REFRESH_TIMER)
{
	mAuiMgr.SetManagedWindow(this);

	// Build menu bar
	wxMenuBar *mb = new wxMenuBar;

	wxMenu *debugMenu = new wxMenu;
	debugMenu->Append(ID_RUN_STOP, "Run/Break\tF5");
	debugMenu->Append(ID_STEP_INTO, "Step Into\tF11");
	debugMenu->Append(ID_STEP_OVER, "Step Over\tF10");
	debugMenu->Append(ID_STEP_OUT, "Step Out\tShift+F11");
	mb->Append(debugMenu, "&Debug");

	wxMenu *viewMenu = new wxMenu;
	viewMenu->AppendCheckItem(ID_VIEW_REGISTERS, "Registers");
	viewMenu->AppendCheckItem(ID_VIEW_DISASSEMBLY, "Disassembly");
	viewMenu->AppendCheckItem(ID_VIEW_MEMORY, "Memory");
	viewMenu->AppendCheckItem(ID_VIEW_CONSOLE, "Console");
	viewMenu->AppendCheckItem(ID_VIEW_BREAKPOINTS, "Breakpoints");
	viewMenu->AppendCheckItem(ID_VIEW_CALLSTACK, "Call Stack");
	viewMenu->AppendCheckItem(ID_VIEW_WATCH, "Watch");
	viewMenu->AppendCheckItem(ID_VIEW_HISTORY, "History");
	viewMenu->AppendSeparator();
	viewMenu->AppendCheckItem(ID_VIEW_SOURCE, "Source Code");
	viewMenu->AppendCheckItem(ID_VIEW_PRINTER, "Printer Output");
	viewMenu->AppendCheckItem(ID_VIEW_PERFORMANCE, "Performance");
	viewMenu->AppendCheckItem(ID_VIEW_TRACE, "Trace Viewer");
	viewMenu->AppendCheckItem(ID_VIEW_DEBUG_DISPLAY, "Debug Display");
	viewMenu->AppendCheckItem(ID_VIEW_PROFILER, "CPU Profiler");
	mb->Append(viewMenu, "&View");

	SetMenuBar(mb);

	// Create toolbar
	wxToolBar *tb = CreateToolBar(wxTB_HORIZONTAL | wxTB_TEXT | wxTB_NOICONS);
	tb->AddTool(ID_RUN_STOP, "Run/Break", wxNullBitmap, "Run or break execution (F5)");
	tb->AddSeparator();
	tb->AddTool(ID_STEP_INTO, "Step Into", wxNullBitmap, "Step into (F11)");
	tb->AddTool(ID_STEP_OVER, "Step Over", wxNullBitmap, "Step over (F10)");
	tb->AddTool(ID_STEP_OUT, "Step Out", wxNullBitmap, "Step out (Shift+F11)");
	tb->Realize();

	// Create panels
	mpRegisters = new ATWxRegistersPanel(this);
	mpDisassembly = new ATWxDisassemblyPanel(this);
	mpMemory = new ATWxMemoryPanel(this);
	mpConsole = new ATWxConsolePanel(this);
	mpBreakpoints = new ATWxBreakpointsPanel(this);
	mpCallStack = new ATWxCallStackPanel(this);
	mpWatch = new ATWxWatchPanel(this);
	mpHistory = new ATWxHistoryPanel(this);
	mpSource = new ATWxSourcePanel(this);
	mpPrinter = new ATWxPrinterPanel(this);
	mpPerformance = new ATWxPerformancePanel(this);
	mpTrace = new ATWxTracePanel(this);
	mpDebugDisplay = new ATWxDebugDisplayPanel(this);
	mpProfiler = new ATWxProfilerPanel(this);

	// Add panes with AUI layout
	mAuiMgr.AddPane(mpDisassembly, wxAuiPaneInfo().Name("disassembly")
		.Caption("Disassembly").Center().CloseButton(true).MaximizeButton(true));

	mAuiMgr.AddPane(mpRegisters, wxAuiPaneInfo().Name("registers")
		.Caption("Registers").Right().Position(0).CloseButton(true)
		.BestSize(220, 200).MinSize(180, 150));

	mAuiMgr.AddPane(mpCallStack, wxAuiPaneInfo().Name("callstack")
		.Caption("Call Stack").Right().Position(1).CloseButton(true)
		.BestSize(220, 200).MinSize(180, 100));

	mAuiMgr.AddPane(mpWatch, wxAuiPaneInfo().Name("watch")
		.Caption("Watch").Right().Position(2).CloseButton(true)
		.BestSize(220, 150).MinSize(180, 100));

	mAuiMgr.AddPane(mpMemory, wxAuiPaneInfo().Name("memory")
		.Caption("Memory").Bottom().Position(0).CloseButton(true)
		.BestSize(600, 250).MinSize(300, 150));

	mAuiMgr.AddPane(mpConsole, wxAuiPaneInfo().Name("console")
		.Caption("Console").Bottom().Position(1).CloseButton(true)
		.BestSize(400, 250).MinSize(200, 100));

	mAuiMgr.AddPane(mpBreakpoints, wxAuiPaneInfo().Name("breakpoints")
		.Caption("Breakpoints").Left().Position(0).CloseButton(true)
		.BestSize(250, 250).MinSize(200, 100));

	mAuiMgr.AddPane(mpHistory, wxAuiPaneInfo().Name("history")
		.Caption("History").Left().Position(1).CloseButton(true)
		.BestSize(250, 250).MinSize(200, 100));

	mAuiMgr.AddPane(mpSource, wxAuiPaneInfo().Name("source")
		.Caption("Source Code").Center().Position(1).CloseButton(true).Hide()
		.BestSize(600, 400).MinSize(300, 200));

	mAuiMgr.AddPane(mpPrinter, wxAuiPaneInfo().Name("printer")
		.Caption("Printer Output").Bottom().Position(2).CloseButton(true).Hide()
		.BestSize(400, 200).MinSize(200, 100));

	mAuiMgr.AddPane(mpPerformance, wxAuiPaneInfo().Name("performance")
		.Caption("Performance").Right().Position(3).CloseButton(true).Hide()
		.BestSize(280, 340).MinSize(270, 280));

	mAuiMgr.AddPane(mpTrace, wxAuiPaneInfo().Name("trace")
		.Caption("Trace Viewer").Bottom().Position(3).CloseButton(true).Hide()
		.BestSize(800, 300).MinSize(400, 150));

	mAuiMgr.AddPane(mpDebugDisplay, wxAuiPaneInfo().Name("debugdisplay")
		.Caption("Debug Display").Right().Position(4).CloseButton(true).Hide()
		.BestSize(400, 310).MinSize(390, 280));

	mAuiMgr.AddPane(mpProfiler, wxAuiPaneInfo().Name("profiler")
		.Caption("CPU Profiler").Center().Position(2).CloseButton(true).Hide()
		.BestSize(600, 400).MinSize(400, 200));

	mAuiMgr.Update();

	// Bind events
	Bind(wxEVT_CLOSE_WINDOW, &ATWxDebuggerFrame::OnClose, this);
	Bind(wxEVT_TIMER, &ATWxDebuggerFrame::OnTimer, this, ID_REFRESH_TIMER);
	Bind(wxEVT_MENU, &ATWxDebuggerFrame::OnRunStop, this, ID_RUN_STOP);
	Bind(wxEVT_MENU, &ATWxDebuggerFrame::OnStepInto, this, ID_STEP_INTO);
	Bind(wxEVT_MENU, &ATWxDebuggerFrame::OnStepOver, this, ID_STEP_OVER);
	Bind(wxEVT_MENU, &ATWxDebuggerFrame::OnStepOut, this, ID_STEP_OUT);
	Bind(wxEVT_MENU, &ATWxDebuggerFrame::OnViewPane, this, ID_VIEW_REGISTERS, ID_VIEW_PROFILER);

	// Enable the debugger if not already
	IATDebugger *dbg = ATGetDebugger();
	if (dbg && !dbg->IsEnabled())
		dbg->SetEnabled(true);

	// Start refresh timer (100ms = 10Hz)
	mRefreshTimer.Start(100);

	// Initial update
	if (s_pClient && s_pClient->IsStateValid())
		UpdateAllPanes();

	// Update breakpoints
	mpBreakpoints->UpdateBreakpoints();
}

ATWxDebuggerFrame::~ATWxDebuggerFrame() {
	mRefreshTimer.Stop();
	mAuiMgr.UnInit();
	s_pDebugFrame = nullptr;
}

void ATWxDebuggerFrame::AppendConsoleText(const char *s) {
	if (mpConsole)
		mpConsole->AppendText(s);
}

bool ATWxDebuggerFrame::NavigateSourceToAddress(uint32 addr) {
	if (!mpSource) return false;

	// Make the source pane visible
	wxAuiPaneInfo& pane = mAuiMgr.GetPane(mpSource);
	if (!pane.IsShown()) {
		pane.Show();
		mAuiMgr.Update();
	}

	return mpSource->NavigateToAddress(addr);
}

void ATWxDebuggerFrame::OnClose(wxCloseEvent&) {
	Destroy();
}

void ATWxDebuggerFrame::OnTimer(wxTimerEvent&) {
	if (!s_pClient)
		return;

	// Flush console output
	if (mpConsole)
		mpConsole->FlushPending();

	// Check for state changes
	bool broke = s_pClient->ConsumeBreak();
	if (broke) {
		mbJustBroke = true;
		Raise(); // Bring debugger to front on break
	}

	bool bpChanged = s_pClient->ConsumeBreakpointsChanged();
	if (bpChanged)
		mpBreakpoints->UpdateBreakpoints();

	// Always update panes when stopped
	if (s_pClient->IsStateValid()) {
		ATDebuggerSystemState state = s_pClient->GetState();
		if (!state.mbRunning || broke)
			UpdateAllPanes();
	}

	// Update View menu check state
	wxMenuBar *mb = GetMenuBar();
	if (mb) {
		wxMenu *viewMenu = mb->GetMenu(1);
		if (viewMenu) {
			viewMenu->Check(ID_VIEW_REGISTERS, mAuiMgr.GetPane("registers").IsShown());
			viewMenu->Check(ID_VIEW_DISASSEMBLY, mAuiMgr.GetPane("disassembly").IsShown());
			viewMenu->Check(ID_VIEW_MEMORY, mAuiMgr.GetPane("memory").IsShown());
			viewMenu->Check(ID_VIEW_CONSOLE, mAuiMgr.GetPane("console").IsShown());
			viewMenu->Check(ID_VIEW_BREAKPOINTS, mAuiMgr.GetPane("breakpoints").IsShown());
			viewMenu->Check(ID_VIEW_CALLSTACK, mAuiMgr.GetPane("callstack").IsShown());
			viewMenu->Check(ID_VIEW_WATCH, mAuiMgr.GetPane("watch").IsShown());
			viewMenu->Check(ID_VIEW_HISTORY, mAuiMgr.GetPane("history").IsShown());
			viewMenu->Check(ID_VIEW_SOURCE, mAuiMgr.GetPane("source").IsShown());
			viewMenu->Check(ID_VIEW_PRINTER, mAuiMgr.GetPane("printer").IsShown());
			viewMenu->Check(ID_VIEW_PERFORMANCE, mAuiMgr.GetPane("performance").IsShown());
			viewMenu->Check(ID_VIEW_TRACE, mAuiMgr.GetPane("trace").IsShown());
			viewMenu->Check(ID_VIEW_DEBUG_DISPLAY, mAuiMgr.GetPane("debugdisplay").IsShown());
			viewMenu->Check(ID_VIEW_PROFILER, mAuiMgr.GetPane("profiler").IsShown());
		}
	}
}

void ATWxDebuggerFrame::UpdateAllPanes() {
	ATDebuggerSystemState state = s_pClient->GetState();

	if (mpRegisters && mAuiMgr.GetPane("registers").IsShown())
		mpRegisters->UpdateFromState(state);

	if (mpDisassembly && mAuiMgr.GetPane("disassembly").IsShown())
		mpDisassembly->UpdateFromState(state);

	if (mpMemory && mAuiMgr.GetPane("memory").IsShown())
		mpMemory->UpdateFromState(state);

	if (mpCallStack && mAuiMgr.GetPane("callstack").IsShown())
		mpCallStack->UpdateFromState(state);

	if (mpWatch && mAuiMgr.GetPane("watch").IsShown())
		mpWatch->UpdateFromState(state);

	if (mpHistory && mAuiMgr.GetPane("history").IsShown())
		mpHistory->UpdateFromState(state);

	if (mpSource && mAuiMgr.GetPane("source").IsShown())
		mpSource->UpdateFromState(state);

	if (mpPrinter && mAuiMgr.GetPane("printer").IsShown())
		mpPrinter->Refresh();

	if (mpPerformance && mAuiMgr.GetPane("performance").IsShown())
		mpPerformance->Refresh();

	if (mpDebugDisplay && mAuiMgr.GetPane("debugdisplay").IsShown())
		mpDebugDisplay->RefreshDisplay();

	if (mpProfiler && mAuiMgr.GetPane("profiler").IsShown())
		mpProfiler->UpdateFromState(state);
}

void ATWxDebuggerFrame::OnRunStop(wxCommandEvent&) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	if (dbg->IsRunning())
		dbg->Break();
	else
		dbg->Run(kATDebugSrcMode_Disasm);
}

void ATWxDebuggerFrame::OnStepInto(wxCommandEvent&) {
	IATDebugger *dbg = ATGetDebugger();
	if (dbg && !dbg->IsRunning())
		dbg->StepInto(kATDebugSrcMode_Disasm);
}

void ATWxDebuggerFrame::OnStepOver(wxCommandEvent&) {
	IATDebugger *dbg = ATGetDebugger();
	if (dbg && !dbg->IsRunning())
		dbg->StepOver(kATDebugSrcMode_Disasm);
}

void ATWxDebuggerFrame::OnStepOut(wxCommandEvent&) {
	IATDebugger *dbg = ATGetDebugger();
	if (dbg && !dbg->IsRunning())
		dbg->StepOut(kATDebugSrcMode_Disasm);
}

void ATWxDebuggerFrame::OnViewPane(wxCommandEvent& event) {
	const char *paneName = nullptr;
	switch (event.GetId()) {
		case ID_VIEW_REGISTERS:     paneName = "registers"; break;
		case ID_VIEW_DISASSEMBLY:   paneName = "disassembly"; break;
		case ID_VIEW_MEMORY:        paneName = "memory"; break;
		case ID_VIEW_CONSOLE:       paneName = "console"; break;
		case ID_VIEW_BREAKPOINTS:   paneName = "breakpoints"; break;
		case ID_VIEW_CALLSTACK:     paneName = "callstack"; break;
		case ID_VIEW_WATCH:         paneName = "watch"; break;
		case ID_VIEW_HISTORY:       paneName = "history"; break;
		case ID_VIEW_SOURCE:        paneName = "source"; break;
		case ID_VIEW_PRINTER:       paneName = "printer"; break;
		case ID_VIEW_PERFORMANCE:   paneName = "performance"; break;
		case ID_VIEW_TRACE:         paneName = "trace"; break;
		case ID_VIEW_DEBUG_DISPLAY: paneName = "debugdisplay"; break;
		case ID_VIEW_PROFILER:      paneName = "profiler"; break;
		default: return;
	}

	wxAuiPaneInfo& pane = mAuiMgr.GetPane(paneName);
	pane.Show(!pane.IsShown());
	mAuiMgr.Update();
}

///////////////////////////////////////////////////////////////////////////
// Public API
///////////////////////////////////////////////////////////////////////////

void ATWxDebuggerOpen(wxWindow *parent) {
	if (s_pDebugFrame) {
		s_pDebugFrame->Raise();
		return;
	}

	s_pDebugFrame = new ATWxDebuggerFrame(parent);
	s_pDebugFrame->Show(true);
}

void ATWxDebuggerClose() {
	if (s_pDebugFrame) {
		s_pDebugFrame->Destroy();
		s_pDebugFrame = nullptr;
	}
}

bool ATWxDebuggerIsOpen() {
	return s_pDebugFrame != nullptr;
}

void ATWxDebuggerAppendConsole(const char *s) {
	// Thread-safe: buffer text for later flush
	std::lock_guard<std::mutex> lock(s_consoleMutex);
	s_consolePending += s;
}

bool ATWxDebuggerNavigateSource(uint32 addr) {
	if (s_pDebugFrame)
		return s_pDebugFrame->NavigateSourceToAddress(addr);
	return false;
}

bool ATWxDebuggerDidBreak() {
	if (s_pClient)
		return s_pClient->ConsumeBreak();
	return false;
}

void ATWxDebuggerInit() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	if (!s_pClient) {
		s_pClient = new ATWxDebuggerClient;
		dbg->AddClient(s_pClient, true);
	}
}

void ATWxDebuggerShutdown() {
	ATWxDebuggerClose();

	if (s_pClient) {
		IATDebugger *dbg = ATGetDebugger();
		if (dbg)
			dbg->RemoveClient(s_pClient);
		delete s_pClient;
		s_pClient = nullptr;
	}
}

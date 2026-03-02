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

#include <mutex>

#include <wx/aui/aui.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/listctrl.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/toolbar.h>

#include <vd2/system/text.h>
#include <vd2/system/vdtypes.h>
#include <at/atcpu/execstate.h>
#include <at/atdebugger/target.h>

#include "debugger.h"
#include "disasm.h"
#include "simulator.h"

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
	nav->Add(new wxButton(this, ID_GO, "Go", wxDefaultPosition, wxSize(40, -1)), 0, wxRIGHT, 2);
	nav->Add(new wxButton(this, ID_GO_PC, "PC", wxDefaultPosition, wxSize(40, -1)), 0, wxRIGHT, 4);
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
	nav->Add(new wxButton(this, ID_GO, "Go", wxDefaultPosition, wxSize(40, -1)), 0, wxRIGHT, 4);
	nav->Add(new wxButton(this, ID_NAV_ZP, "ZP", wxDefaultPosition, wxSize(35, -1)), 0, wxRIGHT, 2);
	nav->Add(new wxButton(this, ID_NAV_STK, "Stk", wxDefaultPosition, wxSize(35, -1)), 0, wxRIGHT, 2);
	nav->Add(new wxButton(this, ID_NAV_HW, "HW", wxDefaultPosition, wxSize(35, -1)), 0);
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
	inputRow->Add(new wxButton(this, ID_CLEAR, "Clear", wxDefaultPosition, wxSize(50, -1)), 0);
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

	addRow->Add(new wxButton(this, ID_ADD, "Add", wxDefaultPosition, wxSize(50, -1)), 0, wxRIGHT, 4);
	addRow->Add(new wxButton(this, ID_REMOVE, "Remove", wxDefaultPosition, wxSize(60, -1)), 0, wxRIGHT, 2);
	addRow->Add(new wxButton(this, ID_CLEAR_ALL, "Clear All", wxDefaultPosition, wxSize(70, -1)), 0);
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
	addRow->Add(new wxButton(this, ID_ADD, "Add", wxDefaultPosition, wxSize(50, -1)), 0, wxRIGHT, 2);
	addRow->Add(new wxButton(this, ID_CLEAR_ALL, "Clear", wxDefaultPosition, wxSize(50, -1)), 0);
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

	wxListCtrl *mpList = nullptr;
	wxCheckBox *mpRecordCB = nullptr;

	enum { ID_RECORD = 4600 };
};

ATWxHistoryPanel::ATWxHistoryPanel(wxWindow *parent)
	: wxPanel(parent, wxID_ANY)
{
	wxBoxSizer *top = new wxBoxSizer(wxVERTICAL);

	mpRecordCB = new wxCheckBox(this, ID_RECORD, "Record History");
	top->Add(mpRecordCB, 0, wxALL, 4);

	mpList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_NO_HEADER);
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

	ATDebugDisasmMode mode = target->GetDisasmMode();
	for (uint32 i = 0; i < fetched; ++i) {
		VDStringA line;
		ATDisassembleInsn(line, target, mode, *hparray[i],
			true, true, true, true, true, false, false, true, true, false);

		mpList->InsertItem(i, line.c_str());
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

///////////////////////////////////////////////////////////////////////////
// Debugger frame (top-level window with AUI manager)
///////////////////////////////////////////////////////////////////////////

class ATWxDebuggerFrame : public wxFrame {
public:
	ATWxDebuggerFrame(wxWindow *parent);
	~ATWxDebuggerFrame();

	void AppendConsoleText(const char *s);

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
		ID_VIEW_HISTORY
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
	mb->Append(viewMenu, "&View");

	SetMenuBar(mb);

	// Create panels
	mpRegisters = new ATWxRegistersPanel(this);
	mpDisassembly = new ATWxDisassemblyPanel(this);
	mpMemory = new ATWxMemoryPanel(this);
	mpConsole = new ATWxConsolePanel(this);
	mpBreakpoints = new ATWxBreakpointsPanel(this);
	mpCallStack = new ATWxCallStackPanel(this);
	mpWatch = new ATWxWatchPanel(this);
	mpHistory = new ATWxHistoryPanel(this);

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

	mAuiMgr.Update();

	// Bind events
	Bind(wxEVT_CLOSE_WINDOW, &ATWxDebuggerFrame::OnClose, this);
	Bind(wxEVT_TIMER, &ATWxDebuggerFrame::OnTimer, this, ID_REFRESH_TIMER);
	Bind(wxEVT_MENU, &ATWxDebuggerFrame::OnRunStop, this, ID_RUN_STOP);
	Bind(wxEVT_MENU, &ATWxDebuggerFrame::OnStepInto, this, ID_STEP_INTO);
	Bind(wxEVT_MENU, &ATWxDebuggerFrame::OnStepOver, this, ID_STEP_OVER);
	Bind(wxEVT_MENU, &ATWxDebuggerFrame::OnStepOut, this, ID_STEP_OUT);
	Bind(wxEVT_MENU, &ATWxDebuggerFrame::OnViewPane, this, ID_VIEW_REGISTERS, ID_VIEW_HISTORY);

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
		case ID_VIEW_REGISTERS:   paneName = "registers"; break;
		case ID_VIEW_DISASSEMBLY: paneName = "disassembly"; break;
		case ID_VIEW_MEMORY:      paneName = "memory"; break;
		case ID_VIEW_CONSOLE:     paneName = "console"; break;
		case ID_VIEW_BREAKPOINTS: paneName = "breakpoints"; break;
		case ID_VIEW_CALLSTACK:   paneName = "callstack"; break;
		case ID_VIEW_WATCH:       paneName = "watch"; break;
		case ID_VIEW_HISTORY:     paneName = "history"; break;
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

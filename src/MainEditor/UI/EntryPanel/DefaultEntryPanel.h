#pragma once

#include "EntryPanel.h"

class DefaultEntryPanel : public EntryPanel
{
public:
	DefaultEntryPanel(wxWindow* parent);
	~DefaultEntryPanel() = default;

	bool loadEntry(ArchiveEntry* entry) override;
	bool loadEntries(vector<ArchiveEntry*>& entries);
	bool saveEntry() override;

private:
	vector<ArchiveEntry*> entries_;

	wxStaticText* label_type_  = nullptr;
	wxStaticText* label_size_  = nullptr;
	wxStaticText* label_index_ = nullptr;

	// Actions
	wxStaticBox* frame_actions_          = nullptr;
	wxButton*    btn_gfx_convert_        = nullptr;
	wxButton*    btn_gfx_modify_offsets_ = nullptr;
	wxButton*    btn_texture_edit_       = nullptr;

	// Events
	void onBtnGfxModifyOffsets(wxCommandEvent& e);
};


// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2017 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    EntryType.cpp
// Description: Entry Type detection system
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "EntryType.h"
#include "App.h"
#include "Archive/ArchiveManager.h"
#include "Archive/Formats/ZipArchive.h"
#include "General/Console/Console.h"
#include "MainEditor/MainEditor.h"
#include "Utility/Parser.h"


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
namespace
{
vector<EntryType*> entry_types;      // The big list of all entry types
vector<string>     entry_categories; // All entry type categories

// Special entry types
EntryType etype_unknown; // The default, 'unknown' entry type
EntryType etype_folder;  // Folder entry type
EntryType etype_marker;  // Marker entry type
EntryType etype_map;     // Map marker type
} // namespace


// -----------------------------------------------------------------------------
//
// EntryType Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// EntryType class constructor
// -----------------------------------------------------------------------------
EntryType::EntryType(const string& id) : id_{ id }, format_{ EntryDataFormat::anyFormat() } {}

// -----------------------------------------------------------------------------
// Adds the type to the list of entry types
// -----------------------------------------------------------------------------
void EntryType::addToList()
{
	entry_types.push_back(this);
	index_ = entry_types.size() - 1;
}

// -----------------------------------------------------------------------------
// Dumps entry type info to the log
// -----------------------------------------------------------------------------
void EntryType::dump()
{
	Log::info(S_FMT("Type %s \"%s\", format %s, extension %s", id_, name_, format_->id(), extension_));
	Log::info(S_FMT("Size limit: %d-%d", size_limit_[0], size_limit_[1]));

	for (const auto& a : match_archive_)
		Log::info(S_FMT("Match Archive: \"%s\"", a));

	for (const auto& a : match_extension_)
		Log::info(S_FMT("Match Extension: \"%s\"", a));

	for (const auto& a : match_name_)
		Log::info(S_FMT("Match Name: \"%s\"", a));

	for (int a : match_size_)
		Log::info(S_FMT("Match Size: %d", a));

	for (int a : size_multiple_)
		Log::info(S_FMT("Size Multiple: %d", a));

	Log::info("---");
}

// -----------------------------------------------------------------------------
// Copies this entry type's info/properties to [target]
// -----------------------------------------------------------------------------
void EntryType::copyToType(EntryType* target)
{
	// Copy type attributes
	target->editor_      = editor_;
	target->extension_   = extension_;
	target->icon_        = icon_;
	target->name_        = name_;
	target->reliability_ = reliability_;
	target->category_    = category_;
	target->colour_      = colour_;

	// Copy type match criteria
	target->format_          = format_;
	target->size_limit_[0]   = size_limit_[0];
	target->size_limit_[1]   = size_limit_[1];
	target->section_         = section_;
	target->match_extension_ = match_extension_;
	target->match_name_      = match_name_;
	target->match_size_      = match_size_;
	target->match_extension_ = match_extension_;
	target->match_archive_   = match_archive_;

	// Copy extra properties
	extra_.copyTo(target->extra_);
}

// -----------------------------------------------------------------------------
// Returns a file filter string for this type:
// "<type name> files (*.<type extension)|*.<type extension>"
// -----------------------------------------------------------------------------
string EntryType::fileFilterString() const
{
	string ret = name_ + " files (*.";
	ret += extension_;
	ret += ")|*.";
	ret += extension_;

	return ret;
}

// -----------------------------------------------------------------------------
// Returns true if [entry] matches the EntryType's criteria, false otherwise
// -----------------------------------------------------------------------------
int EntryType::isThisType(ArchiveEntry* entry)
{
	// Check entry was given
	if (!entry)
		return EntryDataFormat::MATCH_FALSE;

	// Check type is detectable
	if (!detectable_)
		return EntryDataFormat::MATCH_FALSE;

	// Check min size
	if (size_limit_[0] >= 0 && entry->size() < (unsigned)size_limit_[0])
		return EntryDataFormat::MATCH_FALSE;

	// Check max size
	if (size_limit_[1] >= 0 && entry->size() > (unsigned)size_limit_[1])
		return EntryDataFormat::MATCH_FALSE;

	// Check for archive match if needed
	if (!match_archive_.empty())
	{
		bool match = false;
		for (const auto& a : match_archive_)
		{
			if (entry->parent() && entry->parent()->formatId() == a)
			{
				match = true;
				break;
			}
		}
		if (!match)
			return EntryDataFormat::MATCH_FALSE;
	}

	// Check for size match if needed
	if (!match_size_.empty())
	{
		bool match = false;
		for (unsigned a : match_size_)
		{
			if (entry->size() == a)
			{
				match = true;
				break;
			}
		}

		if (!match)
			return EntryDataFormat::MATCH_FALSE;
	}

	// Check for data format match if needed
	int r = EntryDataFormat::MATCH_TRUE;
	if (format_ == EntryDataFormat::textFormat())
	{
		// Hack for identifying ACS script sources despite DB2 apparently appending
		// two null bytes to them, which make the memchr test fail.
		size_t end = entry->size() - 1;
		if (end > 3)
			end -= 2;
		// Text is a special case, as other data formats can sometimes be detected as 'text',
		// we'll only check for it if text data is specified in the entry type
		if (entry->size() > 0 && memchr(entry->rawData(), 0, end) != nullptr)
			return EntryDataFormat::MATCH_FALSE;
	}
	else if (format_ != EntryDataFormat::anyFormat() && entry->size() > 0)
	{
		r = format_->isThisFormat(entry->data());
		if (r == EntryDataFormat::MATCH_FALSE)
			return EntryDataFormat::MATCH_FALSE;
	}

	// Check for size multiple match if needed
	if (!size_multiple_.empty())
	{
		bool   match              = false;
		size_t size_multiple_size = size_multiple_.size();
		for (size_t a = 0; a < size_multiple_size; a++)
		{
			if (entry->size() % size_multiple_[a] == 0)
			{
				match = true;
				break;
			}
		}

		if (!match)
			return EntryDataFormat::MATCH_FALSE;
	}

	// If both names and extensions are defined, and the type only needs one
	// of the two, not both, take it into account.
	bool extorname   = false;
	bool matchedname = false;
	if (match_ext_or_name_ && !match_name_.empty() && !match_extension_.empty())
		extorname = true;

	// Entry name related stuff
	if (!match_name_.empty() || !match_extension_.empty())
	{
		// Get entry name (lowercase), find extension separator
		string fn      = entry->upperName();
		size_t ext_sep = fn.find_first_of('.', 0);

		// Check for name match if needed
		if (!match_name_.empty())
		{
			string name = fn;
			if (ext_sep != wxString::npos)
				name = fn.Left(ext_sep);

			bool   match           = false;
			size_t match_name_size = match_name_.size();
			for (size_t a = 0; a < match_name_size; a++)
			{
				if (name.Matches(match_name_[a]))
				{
					match = true;
					break;
				}
			}

			if (!match && !extorname)
				return EntryDataFormat::MATCH_FALSE;
			else
				matchedname = match;
		}

		// Check for extension match if needed
		if (!match_extension_.empty())
		{
			bool match = false;
			if (ext_sep != wxString::npos)
			{
				string ext                  = fn.Mid(ext_sep + 1);
				size_t match_extension_size = match_extension_.size();
				for (size_t a = 0; a < match_extension_size; a++)
				{
					if (ext == match_extension_[a])
					{
						match = true;
						break;
					}
				}
			}

			if (!match && !(extorname && matchedname))
				return EntryDataFormat::MATCH_FALSE;
		}
	}

	// Check for entry section match if needed
	if (!section_.empty())
	{
		// Check entry is part of an archive (if not it can't be in a section)
		if (!entry->parent())
			return EntryDataFormat::MATCH_FALSE;

		string e_section = entry->parent()->detectNamespace(entry);

		r = EntryDataFormat::MATCH_FALSE;
		for (const auto& ns : section_)
			if (S_CMPNOCASE(ns, e_section))
				r = EntryDataFormat::MATCH_TRUE;
	}

	// Passed all checks, so we have a match
	return r;
}

// -----------------------------------------------------------------------------
// Reads in a block of entry type definitions. Returns false if there was a
// parsing error, true otherwise
// -----------------------------------------------------------------------------
bool EntryType::readEntryTypeDefinition(MemChunk& mc, const string& source)
{
	// Parse the definition
	Parser p;
	p.parseText(mc, source);

	// Get entry_types tree
	auto pt_etypes = p.parseTreeRoot()->childPTN("entry_types");

	// Check it exists
	if (!pt_etypes)
		return false;

	// Go through all parsed types
	for (unsigned a = 0; a < pt_etypes->nChildren(); a++)
	{
		// Get child as ParseTreeNode
		auto typenode = pt_etypes->childPTN(a);

		// Create new entry type
		auto ntype = new EntryType{ typenode->name().Lower() };

		// Copy from existing type if inherited
		if (!typenode->inherit().IsEmpty())
		{
			auto parent_type = fromId(typenode->inherit().Lower());

			if (parent_type != unknownType())
				parent_type->copyToType(ntype);
			else
				Log::info(
					S_FMT("Warning: Entry type %s inherits from unknown type %s", ntype->id(), typenode->inherit()));
		}

		// Go through all parsed fields
		for (unsigned b = 0; b < typenode->nChildren(); b++)
		{
			// Get child as ParseTreeNode
			auto fieldnode = typenode->childPTN(b);

			// Process it
			if (S_CMPNOCASE(fieldnode->name(), "name")) // Name field
			{
				ntype->name_ = fieldnode->stringValue();
			}
			else if (S_CMPNOCASE(fieldnode->name(), "detectable")) // Detectable field
			{
				ntype->detectable_ = fieldnode->boolValue();
			}
			else if (S_CMPNOCASE(fieldnode->name(), "export_ext")) // Export Extension field
			{
				ntype->extension_ = fieldnode->stringValue();
			}
			else if (S_CMPNOCASE(fieldnode->name(), "format")) // Format field
			{
				string format_string = fieldnode->stringValue();
				ntype->format_       = EntryDataFormat::format(format_string);

				// Warn if undefined format
				if (ntype->format_ == EntryDataFormat::anyFormat())
					Log::warning(S_FMT("Entry type %s requires undefined format %s", ntype->id(), format_string));
			}
			else if (S_CMPNOCASE(fieldnode->name(), "icon")) // Icon field
			{
				ntype->icon_ = fieldnode->stringValue();
				if (ntype->icon_.StartsWith("e_"))
					ntype->icon_ = ntype->icon_.Mid(2);
			}
			else if (S_CMPNOCASE(fieldnode->name(), "editor")) // Editor field (to be removed)
			{
				ntype->editor_ = fieldnode->stringValue();
			}
			else if (S_CMPNOCASE(fieldnode->name(), "section")) // Section field
			{
				for (unsigned v = 0; v < fieldnode->nValues(); v++)
					ntype->section_.push_back(fieldnode->stringValue(v).Lower());
			}
			else if (S_CMPNOCASE(fieldnode->name(), "match_ext")) // Match Extension field
			{
				for (unsigned v = 0; v < fieldnode->nValues(); v++)
					ntype->match_extension_.push_back(fieldnode->stringValue(v).Upper());
			}
			else if (S_CMPNOCASE(fieldnode->name(), "match_name")) // Match Name field
			{
				for (unsigned v = 0; v < fieldnode->nValues(); v++)
					ntype->match_name_.push_back(fieldnode->stringValue(v).Upper());
			}
			else if (S_CMPNOCASE(fieldnode->name(), "match_extorname")) // Match name or extension
			{
				ntype->match_ext_or_name_ = fieldnode->boolValue();
			}
			else if (S_CMPNOCASE(fieldnode->name(), "size")) // Size field
			{
				for (unsigned v = 0; v < fieldnode->nValues(); v++)
					ntype->match_size_.push_back(fieldnode->intValue(v));
			}
			else if (S_CMPNOCASE(fieldnode->name(), "min_size")) // Min Size field
			{
				ntype->size_limit_[0] = fieldnode->intValue();
			}
			else if (S_CMPNOCASE(fieldnode->name(), "max_size")) // Max Size field
			{
				ntype->size_limit_[1] = fieldnode->intValue();
			}
			else if (S_CMPNOCASE(fieldnode->name(), "size_multiple")) // Size Multiple field
			{
				for (unsigned v = 0; v < fieldnode->nValues(); v++)
					ntype->size_multiple_.push_back(fieldnode->intValue(v));
			}
			else if (S_CMPNOCASE(fieldnode->name(), "reliability")) // Reliability field
			{
				ntype->reliability_ = fieldnode->intValue();
			}
			else if (S_CMPNOCASE(fieldnode->name(), "match_archive")) // Archive field
			{
				for (unsigned v = 0; v < fieldnode->nValues(); v++)
					ntype->match_archive_.push_back(fieldnode->stringValue(v).Lower());
			}
			else if (S_CMPNOCASE(fieldnode->name(), "extra")) // Extra properties
			{
				for (unsigned v = 0; v < fieldnode->nValues(); v++)
					ntype->extra_.addFlag(fieldnode->stringValue(v));
			}
			else if (S_CMPNOCASE(fieldnode->name(), "category")) // Type category
			{
				ntype->category_ = fieldnode->stringValue();

				// Add to category list if needed
				bool exists = false;
				for (auto& category : entry_categories)
				{
					if (S_CMPNOCASE(category, ntype->category_))
					{
						exists = true;
						break;
					}
				}
				if (!exists)
					entry_categories.push_back(ntype->category_);
			}
			else if (S_CMPNOCASE(fieldnode->name(), "image_format")) // Image format hint
				ntype->extra_["image_format"] = fieldnode->stringValue(0);
			else if (S_CMPNOCASE(fieldnode->name(), "colour")) // Colour
			{
				if (fieldnode->nValues() >= 3)
					ntype->colour_ = ColRGBA(fieldnode->intValue(0), fieldnode->intValue(1), fieldnode->intValue(2));
				else
					Log::warning(S_FMT("Not enough colour components defined for entry type %s", ntype->id()));
			}
			else
			{
				// Unhandled properties can go into 'extra', only their first value is kept
				ntype->extra_[fieldnode->name()] = fieldnode->stringValue();
			}
		}

		// ntype->dump();
		ntype->addToList();
	}

	return true;
}

// -----------------------------------------------------------------------------
// Loads all built-in and custom user entry types
// -----------------------------------------------------------------------------
bool EntryType::loadEntryTypes()
{
	auto fmt_any = EntryDataFormat::anyFormat();

	// Setup unknown type
	etype_unknown.format_      = fmt_any;
	etype_unknown.icon_        = "unknown";
	etype_unknown.detectable_  = false;
	etype_unknown.reliability_ = 0;
	etype_unknown.addToList();

	// Setup folder type
	etype_folder.format_     = fmt_any;
	etype_folder.icon_       = "folder";
	etype_folder.name_       = "Folder";
	etype_folder.detectable_ = false;
	etype_folder.addToList();

	// Setup marker type
	etype_marker.format_     = fmt_any;
	etype_marker.icon_       = "marker";
	etype_marker.name_       = "Marker";
	etype_marker.detectable_ = false;
	etype_marker.category_   = ""; // No category, markers only appear when 'All' categories shown
	etype_marker.addToList();

	// Setup map marker type
	etype_map.format_     = fmt_any;
	etype_map.icon_       = "map";
	etype_map.name_       = "Map Marker";
	etype_map.category_   = "Maps"; // Should appear with maps
	etype_map.detectable_ = false;
	etype_map.colour_     = ColRGBA(0, 255, 0);
	etype_map.addToList();

	// -------- READ BUILT-IN TYPES ---------

	// Get builtin entry types from resource archive
	auto res_archive = App::archiveManager().programResourceArchive();

	// Check resource archive exists
	if (!res_archive)
	{
		Log::error("No resource archive open!");
		return false;
	}

	// Get entry types directory
	auto et_dir = res_archive->dir("config/entry_types/");

	// Check it exists
	if (!et_dir)
	{
		Log::error("config/entry_types does not exist in slade.pk3");
		return false;
	}

	// Read in each file in the directory
	bool         etypes_read       = false;
	unsigned int et_dir_numEntries = et_dir->numEntries();
	for (unsigned a = 0; a < et_dir_numEntries; a++)
	{
		if (readEntryTypeDefinition(et_dir->entryAt(a)->data(), et_dir->entryAt(a)->name()))
			etypes_read = true;
	}

	// Warn if no types were read (this shouldn't happen unless the resource archive is corrupted)
	if (!etypes_read)
		Log::warning("No built-in entry types could be loaded from slade.pk3");

	// -------- READ CUSTOM TYPES ---------

	// If the directory doesn't exist create it
	if (!wxDirExists(App::path("entry_types", App::Dir::User)))
		wxMkdir(App::path("entry_types", App::Dir::User));

	// Open the custom palettes directory
	wxDir res_dir;
	res_dir.Open(App::path("entry_types", App::Dir::User));

	// Go through each file in the directory
	string filename = wxEmptyString;
	bool   files    = res_dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES);
	while (files)
	{
		// Load file data
		MemChunk mc;
		mc.importFile(res_dir.GetName() + "/" + filename);

		// Parse file
		readEntryTypeDefinition(mc, filename);

		// Next file
		files = res_dir.GetNext(&filename);
	}

	return true;
}

// -----------------------------------------------------------------------------
// Attempts to detect the given entry's type
// -----------------------------------------------------------------------------
bool EntryType::detectEntryType(ArchiveEntry* entry)
{
	// Do nothing if the entry is a folder or a map marker
	if (!entry || entry->type() == &etype_folder || entry->type() == &etype_map)
		return false;

	// If the entry's size is zero, set it to marker type
	if (entry->size() == 0)
	{
		entry->setType(&etype_marker);
		return true;
	}

	// Reset entry type
	entry->setType(&etype_unknown);

	// Go through all registered types
	size_t entry_types_size = entry_types.size();
	for (size_t a = 0; a < entry_types_size; a++)
	{
		// If the current type is more 'reliable' than this one, skip it
		if (entry->typeReliability() >= entry_types[a]->reliability())
			continue;

		// Check for possible type match
		int r = entry_types[a]->isThisType(entry);
		if (r > 0)
		{
			// Type matches, set it
			entry->setType(entry_types[a], r);

			// No need to continue if the identification is 100% reliable
			if (entry->typeReliability() >= 255)
				return true;
		}
	}

	// Return t/f depending on if a matching type was found
	if (entry->type() == &etype_unknown)
		return false;
	else
		return true;
}

// -----------------------------------------------------------------------------
// Returns the entry type with the given id, or etype_unknown if no id match is
// found
// -----------------------------------------------------------------------------
EntryType* EntryType::fromId(const string& id)
{
	for (auto type : entry_types)
		if (type->id_ == id)
			return type;

	return &etype_unknown;
}

// -----------------------------------------------------------------------------
// Returns the global 'unknown' entry type
// -----------------------------------------------------------------------------
EntryType* EntryType::unknownType()
{
	return &etype_unknown;
}

// -----------------------------------------------------------------------------
// Returns the global 'folder' entry type
// -----------------------------------------------------------------------------
EntryType* EntryType::folderType()
{
	return &etype_folder;
}

// -----------------------------------------------------------------------------
// Returns the global 'map marker' entry type
// -----------------------------------------------------------------------------
EntryType* EntryType::mapMarkerType()
{
	return &etype_map;
}

// -----------------------------------------------------------------------------
// Returns a list of icons for all entry types, organised by type index
// -----------------------------------------------------------------------------
wxArrayString EntryType::iconList()
{
	wxArrayString list;

	for (auto& entry_type : entry_types)
		list.Add(entry_type->icon());

	return list;
}

// -----------------------------------------------------------------------------
// Clears all defined entry types
// -----------------------------------------------------------------------------
void EntryType::cleanupEntryTypes()
{
	// This is only called on exit so no real point to doing it yet,
	// all it seems to do is cause crashes on exit

	/*for (size_t a = 4; a < entry_types.size(); a++)
	{
		auto e = entry_types[a];
		if (e != &etype_unknown && e != &etype_folder && e != &etype_marker && e != &etype_map)
			delete entry_types[a];
	}*/
}

// -----------------------------------------------------------------------------
// Returns a list of all entry types
// -----------------------------------------------------------------------------
vector<EntryType*> EntryType::allTypes()
{
	return entry_types;
}

// -----------------------------------------------------------------------------
// Returns a list of all entry type categories
// -----------------------------------------------------------------------------
vector<string> EntryType::allCategories()
{
	return entry_categories;
}


// -----------------------------------------------------------------------------
//
// Console Commands
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Command to attempt to detect the currently selected entries as the given
// type id. Lists all type ids if no parameters given
// -----------------------------------------------------------------------------
CONSOLE_COMMAND(type, 0, true)
{
	auto all_types = EntryType::allTypes();
	if (args.empty())
	{
		// List existing types and their IDs
		string listing   = "List of entry types:\n\t";
		string separator = "]\n\t";
		string colon     = ": ";
		string paren     = " [";
		for (size_t a = 3; a < all_types.size(); a++)
		{
			listing += all_types[a]->name();
			listing += paren;
			listing += all_types[a]->id();
			listing += colon;
			listing += all_types[a]->formatId();
			listing += separator;
		}
		Log::info(listing);
	}
	else
	{
		// Find type by id or first matching format
		auto desttype = EntryType::unknownType();
		bool match    = false;

		// Use true unknown type rather than map marker...
		if (!args[0].CmpNoCase("unknown") || !args[0].CmpNoCase("none") || !args[0].CmpNoCase("any"))
			match = true;

		// Find actual format
		else
			for (size_t a = 3; a < all_types.size(); a++)
			{
				if (!args[0].CmpNoCase(all_types[a]->formatId()) || !args[0].CmpNoCase(all_types[a]->id()))
				{
					desttype = all_types[a];
					match    = true;
					break;
				}
			}
		if (!match)
		{
			Log::info(S_FMT("Type %s does not exist (use \"type\" without parameter for a list)", args[0].mb_str()));
			return;
		}

		// Allow to force type change even if format checks fails (use at own risk!)
		int  force = !(args.size() < 2 || args[1].CmpNoCase("force"));
		auto meep  = MainEditor::currentEntrySelection();
		if (meep.empty())
		{
			Log::info("No entry selected");
			return;
		}

		EntryDataFormat* foo = nullptr;
		if (desttype != EntryType::unknownType())
		{
			// Check if format corresponds to entry
			foo = EntryDataFormat::format(desttype->formatId());
			if (foo)
				Log::info(S_FMT("Identifying as %s", desttype->name().mb_str()));
			else
				Log::info("No data format for this type!");
		}
		else
			force = true; // Always force the unknown type

		for (auto& b : meep)
		{
			int okay = false;
			if (foo)
			{
				okay = foo->isThisFormat(b->data());
				if (okay)
					Log::info(S_FMT("%s: Identification successful (%i/255)", b->name().mb_str(), okay));
				else
					Log::info(S_FMT("%s: Identification failed", b->name().mb_str()));
			}

			// Change type
			if (force || okay)
			{
				b->setType(desttype, okay);
				Log::info(S_FMT("%s: Type changed.", b->name().mb_str()));
			}
		}
	}
}

CONSOLE_COMMAND(size, 0, true)
{
	auto meep = MainEditor::currentEntry();
	if (!meep)
	{
		Log::info("No entry selected");
		return;
	}
	Log::info(S_FMT("%s: %i bytes", meep->name().mb_str(), meep->size()));
}

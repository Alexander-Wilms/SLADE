#pragma once

class SImage;
class Archive;
class ArchiveEntry;
class Palette;
class Tokenizer;

namespace Misc
{
bool loadImageFromEntry(SImage* image, ArchiveEntry* entry, int index = 0);

// Palette detection
namespace PaletteHack
{
	static const int NONE      = 0;
	static const int ALPHA     = 1;
	static const int HERETIC   = 2;
	static const int SHADOW    = 3;
	static const int ROTT_N    = 4;
	static const int ROTT_D    = 5;
	static const int ROTT_F    = 6;
	static const int ROTT_A    = 7;
	static const int SOD_ID    = 8;
	static const int SOD_TITLE = 9;
	static const int SOD_END   = 10;
}; // namespace PaletteHack
int      detectPaletteHack(ArchiveEntry* entry);
bool     loadPaletteFromArchive(Palette* pal, Archive* archive, int lump = PaletteHack::NONE);

string   sizeAsString(uint32_t size);
string   lumpNameToFileName(string lump);
string   fileNameToLumpName(string file);
uint32_t crc(const uint8_t* buf, uint32_t len);
ColHSL   rgbToHsl(double r, double g, double b);
ColRGBA  hslToRgb(double h, double s, double t);
ColLAB   rgbToLab(double r, double g, double b);
ColHSL   rgbToHsl(ColRGBA rgba);
ColRGBA  hslToRgb(ColHSL hsl);
ColLAB   rgbToLab(ColRGBA);
Vec2i    findJaguarTextureDimensions(ArchiveEntry* entry, const string& name);

// Mass Rename
string massRenameFilter(wxArrayString& names);
void   doMassRename(wxArrayString& names, string name_filter);

// Dialog/Window sizes
struct WindowInfo
{
	string id;
	int    width, height, left, top;
	WindowInfo(const string& id, int w, int h, int l, int t) : id{ id }, width{ w }, height{ h }, left{ l }, top{ t } {}
};
WindowInfo getWindowInfo(const string& id);
void       setWindowInfo(const string& id, int width, int height, int left, int top);
void       readWindowInfo(Tokenizer& tz);
void       writeWindowInfo(wxFile& file);
} // namespace Misc

// ============================================================
//   Matt Phillips (c) 2016 BIG EVIL CORPORATION
// ============================================================
//   http://www.bigevilcorporation.co.uk
// ============================================================
//   snasm68k_bin2cpp - Converts SNASM68K COFF files to
//   C++ binary block, and extracts known memory addresses
// ============================================================

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <iomanip>

#include "../../snasm68k_coffdump/SN68kCoffDump/FileCOFF.h"

#define HEX2(val) std::hex << std::setfill('0') << std::setw(2) << std::uppercase << (int)val
#define HEX4(val) std::hex << std::setfill('0') << std::setw(4) << std::uppercase << (int)val

static const int MAX_BYTES_PER_LINE = 128;

enum class CmdArgs : int
{
	Executable,
	Operation,
	InputFile,
	OutputROM,
	OutputVars,
	FirstSymbol,

	Count
};

enum class SymbolSize : int
{
	byte,
	word,
	longword
};

struct Symbol
{
	std::string name;
	unsigned int value;
};

bool stricmp(const std::string& a, const std::string& b)
{
	return std::equal(a.begin(), a.end(),
		b.begin(), b.end(),
		[](char a, char b) {
		return tolower(a) == tolower(b);
	});
}

unsigned int FindSymbolValue(const FileCOFF& coffFile, const std::string& symbolName)
{
	unsigned int value = 0xFFFFFFFF;

	for (int i = 0; i < coffFile.m_sortedSymbols.size() && value == 0xFFFFFFFF; i++)
	{
		if (stricmp(coffFile.m_sortedSymbols[i].name, symbolName))
		{
			value = coffFile.m_sortedSymbols[i].value;
		}
	}

	return value;
}

void WriteHeader(std::stringstream& stream)
{
	stream << "//------------------------------------------------------------------" << std::endl;
	stream << "// bin2cpp" << std::endl;
	stream << "// BIG EVIL CORPORATION LTD" << std::endl;
	stream << "// SNASM68K BIN/COFF to C++ tool" << std::endl;
	stream << "// Matt Phillips 2018" << std::endl;
	stream << "//------------------------------------------------------------------" << std::endl;
	stream << std::endl;
}

void PrintUsage()
{
	std::cout << "Usage:" << std::endl
		<< "  snasm68k_bin2cpp.exe -b [binary.bin] [output.h]" << std::endl
		<< "  snasm68k_bin2cpp.exe -c [symbols.coff] [output.h] [symbols.h] <symbolname> <symbolname>..." << std::endl;
}

int main(int argc, char* argv[])
{
	if (argc < (int)CmdArgs::InputFile)
	{
		PrintUsage();
		return 0;
	}

	if (stricmp(std::string(argv[(int)CmdArgs::Operation]), "-b"))
	{
		//Binary file
		std::fstream binaryFile;
		std::fstream outputFileROM;

		binaryFile.open(argv[(int)CmdArgs::InputFile], std::ios::in | std::ios::binary);
		if (binaryFile.is_open())
		{
			//Get binary filesize
			binaryFile.seekg(0, std::ios::end);
			std::streampos binarySize = binaryFile.tellg();
			binaryFile.seekg(0, std::ios::beg);

			//Read binary
			char* binaryData = new char[binarySize];
			binaryFile.read(binaryData, binarySize);
			binaryFile.close();

			//Open output file
			outputFileROM.open(argv[(int)CmdArgs::OutputROM], std::ios::out | std::ios::binary);
			if (outputFileROM.is_open())
			{
				//Write header
				std::stringstream outStreamROM;
				WriteHeader(outStreamROM);

				outStreamROM << "// Binary data" << std::endl;
				outStreamROM << "static const int snasm68k_binary_size = 0x" << HEX4(binarySize) << ";" << std::endl;
				outStreamROM << "static const unsigned char snasm68k_binary[] = " << std::endl;
				outStreamROM << "{" << std::endl;

				for (int i = 0; i < binarySize; i++)
				{
					if ((i % MAX_BYTES_PER_LINE) == 0)
						outStreamROM << std::endl;

					outStreamROM << "0x" << HEX2(binaryData[i]) << ",";
				}

				outStreamROM << std::endl;
				outStreamROM << "};" << std::endl;
				outStreamROM << std::endl;

				//Write file
				outputFileROM.write(outStreamROM.str().data(), outStreamROM.str().size());
				outputFileROM.close();

				std::cout << "Wrote ROM file (" << binarySize << " bytes) to " << argv[(int)CmdArgs::OutputROM] << std::endl;
			}

			//Free buffer
			delete binaryData;
		}
	}
	else if (stricmp(std::string(argv[(int)CmdArgs::Operation]), "-c"))
	{
		//COFF file
		std::fstream coffFile;
		std::fstream outputFileROM;
		std::fstream outputFileVars;

		coffFile.open(argv[(int)CmdArgs::InputFile], std::ios::in | std::ios::binary);
		if (coffFile.is_open())
		{
			//Get COFF filesize
			coffFile.seekg(0, std::ios::end);
			std::streampos coffSize = coffFile.tellg();
			coffFile.seekg(0, std::ios::beg);

			//Read binary
			char* symbolData = new char[coffSize];
			coffFile.read(symbolData, coffSize);
			coffFile.close();

			//Serialise COFF file
			FileCOFF coffFile;
			Stream stream((char*)&symbolData[0]);
			stream.Serialise(coffFile);

			//Free buffer
			delete symbolData;

			//Sanity checks
			if (coffFile.m_fileHeader.machineType != COFF_MACHINE_68000)
			{
				//Unsupported machine/processor type
				std::cout << "Unknown COFF machine/processor type, not a SNASM68K COFF" << std::endl;
				return 0;
			}
			else if (coffFile.m_sectionHeaders.size() != COFF_SECTION_COUNT)
			{
				//SNASM2 COFF has a fixed number of sections
				std::cout << "Unsupported section count, not a SNASM68K COFF" << std::endl;
				return 0;
			}
			else
			{
				//Extract ROM
				u8* romData = coffFile.m_sectionHeaders[COFF_SECTION_ROM_DATA].data;
				u32 romSize = coffFile.m_sectionHeaders[COFF_SECTION_ROM_DATA].size;

				//Open output files
				outputFileROM.open(argv[(int)CmdArgs::OutputROM], std::ios::out | std::ios::binary);
				if (outputFileROM.is_open())
				{
					outputFileVars.open(argv[(int)CmdArgs::OutputVars], std::ios::out | std::ios::binary);
					if (outputFileVars.is_open())
					{
						//Find symbols
						std::vector<Symbol> symbols;

						for (int i = (int)CmdArgs::FirstSymbol; i < argc; i++)
						{
							Symbol symbol;
							symbol.name = argv[i];
							symbol.value = FindSymbolValue(coffFile, symbol.name);
							symbols.push_back(symbol);
							std::cout << "Found symbol '" << symbol.name << "' : value = 0x" << HEX4(symbol.value) << std::endl;
						}

						//Write headers
						std::stringstream outStreamROM;
						std::stringstream outStreamVars;

						WriteHeader(outStreamROM);
						WriteHeader(outStreamVars);

						outStreamVars << "// Symbol addresses" << std::endl;

						for (int i = 0; i < symbols.size(); i++)
						{
							outStreamVars << "static const unsigned int snasm68k_symbol_" << symbols[i].name << "_val = 0x" << HEX4(symbols[i].value) << ";" << std::endl;
						}

						outStreamROM << "// Binary data" << std::endl;
						outStreamROM << "static const int snasm68k_binary_size = 0x" << HEX4(romSize) << ";" << std::endl;
						outStreamROM << "static const unsigned char snasm68k_binary[] = " << std::endl;
						outStreamROM << "{" << std::endl;

						for (int i = 0; i < romSize; i++)
						{
							if ((i % MAX_BYTES_PER_LINE) == 0)
								outStreamROM << std::endl;

							outStreamROM << "0x" << HEX2(romData[i]) << ",";
						}

						outStreamROM << std::endl;
						outStreamROM << "};" << std::endl;
						outStreamROM << std::endl;

						//Write files
						outputFileROM.write(outStreamROM.str().data(), outStreamROM.str().size());
						outputFileVars.write(outStreamVars.str().data(), outStreamVars.str().size());
						outputFileROM.close();
						outputFileVars.close();

						std::cout << "Wrote ROM file (" << romSize << " bytes) to " << argv[(int)CmdArgs::OutputROM] << std::endl;
						std::cout << "Wrote vars file to " << argv[(int)CmdArgs::OutputVars] << std::endl;
					}
				}
			}
		}
	}
	else
	{
		PrintUsage();
		return 0;
	}

	return 0;
}

/* Copyright (c) 2009, Avian Contributors

   Permission to use, copy, modify, and/or distribute this software
   for any purpose with or without fee is hereby granted, provided
   that the above copyright notice and this permission notice appear
   in all copies.

   There is NO WARRANTY for this software.  See license.txt for
   details. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tools.h"

namespace {

#define IMAGE_SIZEOF_SHORT_NAME 8

#define IMAGE_FILE_RELOCS_STRIPPED 1
#define IMAGE_FILE_LINE_NUMS_STRIPPED 4
#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_FILE_MACHINE_I386 0x014c
#define IMAGE_FILE_32BIT_MACHINE 256

#define IMAGE_SCN_ALIGN_1BYTES 0x100000
#define IMAGE_SCN_ALIGN_2BYTES 0x200000
#define IMAGE_SCN_ALIGN_4BYTES 0x300000
#define IMAGE_SCN_ALIGN_8BYTES 0x400000
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ 0x40000000
#define IMAGE_SCN_MEM_WRITE 0x80000000
#define IMAGE_SCN_CNT_CODE 32

struct IMAGE_FILE_HEADER {
  uint16_t Machine;
  uint16_t NumberOfSections;
  uint32_t TimeDateStamp;
  uint32_t PointerToSymbolTable;
  uint32_t NumberOfSymbols;
  uint16_t SizeOfOptionalHeader;
  uint16_t Characteristics;
} __attribute__((packed));

struct IMAGE_SECTION_HEADER {
  uint8_t Name[IMAGE_SIZEOF_SHORT_NAME];
  union {
    uint32_t PhysicalAddress;
    uint32_t VirtualSize;
  } Misc;
  uint32_t VirtualAddress;
  uint32_t SizeOfRawData;
  uint32_t PointerToRawData;
  uint32_t PointerToRelocations;
  uint32_t PointerToLinenumbers;
  uint16_t NumberOfRelocations;
  uint16_t NumberOfLinenumbers;
  uint32_t Characteristics;
} __attribute__((packed));

struct IMAGE_SYMBOL {
  union {
    struct {
      uint32_t Short;
      uint32_t Long;
    } Name;
  } N;
  uint32_t Value;
  int16_t SectionNumber;
  uint16_t Type;
  uint8_t StorageClass;
  uint8_t NumberOfAuxSymbols;
} __attribute__((packed));

inline unsigned
pad(unsigned n)
{
  return (n + (4 - 1)) & ~(4 - 1);
}

using namespace avian::tools;

template<unsigned BytesPerWord>
class WindowsPlatform : public Platform {
public:


  class FileWriter {
  public:
    unsigned sectionCount;
    unsigned symbolCount;
    unsigned dataStart;
    unsigned dataOffset;

    IMAGE_FILE_HEADER header;

    StringTable strings;
    Buffer symbols;

    FileWriter(unsigned machine, unsigned machineMask, unsigned symbolCount):
      sectionCount(0),
      symbolCount(symbolCount),
      dataStart(sizeof(IMAGE_FILE_HEADER)),
      dataOffset(0)
    {
      header.Machine = machine;
      // header.NumberOfSections = sectionCount;
      header.TimeDateStamp = 0;
      // header.PointerToSymbolTable = sizeof(IMAGE_FILE_HEADER)
      //   + sizeof(IMAGE_SECTION_HEADER)
      //   + pad(size);
      // header.NumberOfSymbols = symbolCount;
      header.SizeOfOptionalHeader = 0;
      header.Characteristics = IMAGE_FILE_RELOCS_STRIPPED
        | IMAGE_FILE_LINE_NUMS_STRIPPED
        | machineMask;
    }

    void writeHeader(OutputStream* out) {
      header.NumberOfSections = sectionCount;
      header.PointerToSymbolTable = dataStart + dataOffset;
      printf("symbol table start: 0x%x\n", header.PointerToSymbolTable);
      dataOffset = pad(dataOffset + symbolCount * sizeof(IMAGE_SYMBOL));
      printf("string table start: 0x%x\n", dataStart + dataOffset);
      header.NumberOfSymbols = symbolCount;
      out->writeChunk(&header, sizeof(IMAGE_FILE_HEADER));
    }

    void addSymbol(String name, unsigned addr, unsigned sectionNumber, unsigned type, unsigned storageClass) {
      unsigned nameOffset = strings.add(name);
      IMAGE_SYMBOL symbol = {
        { 0 }, // Name
        addr, // Value
        sectionNumber, // SectionNumber
        type, // Type
        storageClass, // StorageClass
        0, // NumberOfAuxSymbols
      };
      symbol.N.Name.Long = nameOffset+4;
      symbols.write(&symbol, sizeof(IMAGE_SYMBOL));
    }

    void writeData(OutputStream* out) {
      out->writeChunk(symbols.data, symbols.length);
      uint32_t size = strings.length + 4;
      out->writeChunk(&size, 4);
      out->writeChunk(strings.data, strings.length);
    }
  };

  class SectionWriter {
  public:
    FileWriter& file;
    IMAGE_SECTION_HEADER header;
    size_t dataSize;
    size_t finalSize;
    const uint8_t* data;
    unsigned dataOffset;

    SectionWriter(
        FileWriter& file,
        const char* name,
        unsigned sectionMask,
        const uint8_t* data,
        size_t dataSize):

      file(file),
      data(data),
      dataSize(dataSize),
      finalSize(pad(dataSize))
    {
      file.sectionCount++;
      file.dataStart += sizeof(IMAGE_SECTION_HEADER);
      strcpy(reinterpret_cast<char*>(header.Name), name);
      header.Misc.VirtualSize = 0;
      header.SizeOfRawData = finalSize;
      // header.PointerToRawData = file.dataOffset;
      dataOffset = file.dataOffset;
      file.dataOffset += finalSize;
      header.PointerToRelocations = 0;
      header.PointerToLinenumbers = 0;
      header.NumberOfRelocations = 0;
      header.NumberOfLinenumbers = 0;
      header.Characteristics = sectionMask;
    }

    void writeHeader(OutputStream* out) {
      header.PointerToRawData = dataOffset + file.dataStart;
      printf("section %s: data at 0x%x, ending at 0x%x\n", header.Name, header.PointerToRawData, header.PointerToRawData + header.SizeOfRawData);
      out->writeChunk(&header, sizeof(IMAGE_SECTION_HEADER));
    }

    void writeData(OutputStream* out) {
      out->writeChunk(data, dataSize);
      out->writeRepeat(0, finalSize - dataSize);
    }


  };

  virtual bool writeObject(OutputStream* out, Slice<SymbolInfo> symbols, Slice<const uint8_t> data, unsigned accessFlags, unsigned alignment) {

    int machine;
    int machineMask;

    if (BytesPerWord == 8) {
      machine = IMAGE_FILE_MACHINE_AMD64;
      machineMask = 0;
    } else { // if (BytesPerWord == 8)
      machine = IMAGE_FILE_MACHINE_I386;
      machineMask = IMAGE_FILE_32BIT_MACHINE;
    }

    int sectionMask;
    switch (alignment) {
    case 0:
    case 1:
      sectionMask = IMAGE_SCN_ALIGN_1BYTES;
      break;
    case 2:
      sectionMask = IMAGE_SCN_ALIGN_2BYTES;
      break;
    case 4:
      sectionMask = IMAGE_SCN_ALIGN_4BYTES;
      break;
    case 8:
      sectionMask = IMAGE_SCN_ALIGN_8BYTES;
      break;
    default:
      fprintf(stderr, "unsupported alignment: %d\n", alignment);
      return false;
    }

    sectionMask |= IMAGE_SCN_MEM_READ;

    const char* sectionName;
    if (accessFlags & Platform::Writable) {
      if (accessFlags & Platform::Executable) {
        sectionName = ".rwx";
        sectionMask |= IMAGE_SCN_MEM_WRITE
          | IMAGE_SCN_MEM_EXECUTE
          | IMAGE_SCN_CNT_CODE;
      } else {
        sectionName = ".data";
        sectionMask |= IMAGE_SCN_MEM_WRITE;
      }
    } else {
      sectionName = ".text";
      sectionMask |= IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_CODE;
    }

    FileWriter file(machine, machineMask, symbols.count);

    SectionWriter section(file, sectionName, sectionMask, data.items, data.count);

    file.writeHeader(out);

    for(SymbolInfo* sym = symbols.begin(); sym != symbols.end(); sym++) {
      file.addSymbol(sym->name, sym->addr, 1, 0, 2);
    }

    section.writeHeader(out);

    section.writeData(out);

    file.writeData(out);  

    return true;

  }

  WindowsPlatform():
    Platform(PlatformInfo(PlatformInfo::Windows, BytesPerWord == 4 ? PlatformInfo::x86 : PlatformInfo::x86_64)) {}
};

WindowsPlatform<4> windows32Platform;
WindowsPlatform<8> windows64Platform;

} // namespace

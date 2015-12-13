#include <algorithm>
#include <cassert>
#include <share.h>

#include "PortableExecutable.h"
#include "Handle.h"
#include "Log.h"

DWORD PortableExecutable::VirtualToRaw(DWORD dwAddress) const //address without the image base!
{
	for(const auto& uSection : vecPESections)
	{
		DWORD dwDifference = dwAddress - uSection.VirtualAddress;

		if(dwDifference < uSection.SizeOfRawData) {
			return uSection.PointerToRawData + dwDifference;
		}
	}

	return 0;
}

bool PortableExecutable::ReadFile()
{
	if(auto const pFile = Handle.get())
	{
		//DOS Header
		fseek(pFile, 0, SEEK_SET);
		fread(&uDOSHeader, sizeof(IMAGE_DOS_HEADER), 1, pFile);
		if(uDOSHeader.e_magic == IMAGE_DOS_SIGNATURE)
		{
			//PE Header
			fseek(pFile, uDOSHeader.e_lfanew, SEEK_SET);
			fread(&uPEHeader, sizeof(IMAGE_NT_HEADERS), 1, pFile);
			if(uPEHeader.Signature == IMAGE_NT_SIGNATURE)
			{
				//Sections
				vecPESections.resize(uPEHeader.FileHeader.NumberOfSections);
				fread(&vecPESections[0], sizeof(IMAGE_SECTION_HEADER), vecPESections.size(), pFile);

				//Imports
				auto& Imports = uPEHeader.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
				size_t import_desc_count = Imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
				if(import_desc_count > 1)
				{
					// minus one for end of array
					std::vector<IMAGE_IMPORT_DESCRIPTOR> import_desc(import_desc_count - 1);
					fseek(pFile, static_cast<long>(VirtualToRaw(Imports.VirtualAddress)), SEEK_SET);
					fread(&import_desc[0], sizeof(IMAGE_IMPORT_DESCRIPTOR), import_desc.size(), pFile);

					for(const auto& desc : import_desc)
					{
						// insert one, then fill in-place
						vecImports.emplace_back();
						auto& current_import = vecImports.back();

						current_import.uDesc = desc;
						if(!current_import.uDesc.Name) {
							break;
						}

						char name_buf[0x100];
						fseek(pFile, static_cast<long>(VirtualToRaw(current_import.uDesc.Name)), SEEK_SET);
						fgets(name_buf, 0x100, pFile);

						current_import.Name = name_buf;

						//Thunks
						PEThunkData current_thunk;

						fseek(pFile, static_cast<long>(VirtualToRaw(current_import.uDesc.FirstThunk)), SEEK_SET);

						for(fread(&current_thunk.uThunkData.u1, sizeof(IMAGE_THUNK_DATA), 1, pFile);
							current_thunk.uThunkData.u1.AddressOfData;
							fread(&current_thunk.uThunkData.u1, sizeof(IMAGE_THUNK_DATA), 1, pFile))
						{
							current_import.vecThunkData.push_back(current_thunk);
						}

						auto thunk_addr = reinterpret_cast<IMAGE_THUNK_DATA*>(current_import.uDesc.FirstThunk);
						for(auto& thunk : current_import.vecThunkData)
						{
							thunk.Address = reinterpret_cast<DWORD>(thunk_addr++);

							if(thunk.uThunkData.u1.AddressOfData & 0x80000000)
							{
								thunk.bIsOrdinal = true;
								thunk.Ordinal = static_cast<int>(thunk.uThunkData.u1.AddressOfData & 0x7FFFFFFFu);
							}
							else
							{
								thunk.bIsOrdinal = false;

								fseek(pFile, static_cast<long>(VirtualToRaw(thunk.uThunkData.u1.AddressOfData & 0x7FFFFFFF)), SEEK_SET);
								fread(&thunk.wWord, 2, 1, pFile);
								fgets(name_buf, 0x100, pFile);
								thunk.Name = name_buf;
							}
						}
					}
				}

				return true;
			}
		}
	}

	return false;
}

DWORD PortableExecutable::GetImageBase() const {
	return this->GetPEHeader().OptionalHeader.ImageBase;
}

bool PortableExecutable::ReadBytes(DWORD dwRawAddress, size_t Size, void *Dest) const {
	if(!Filename.empty()) {
		assert(Handle);
		if(!fseek(Handle, static_cast<long>(dwRawAddress), SEEK_SET)) {
			return (fread(Dest, Size, 1, Handle) == 1);
		}
	}
	return false;
}

bool PortableExecutable::ReadCString(DWORD dwRawAddress, std::string &Result) const {
	if(!Filename.empty()) {
		assert(Handle);
		if(!fseek(Handle, static_cast<long>(dwRawAddress), SEEK_SET)) {
			constexpr size_t sz = 0x100;
			char tmpBuf[sz];

			tmpBuf[0] = 0;
			if(fread(tmpBuf, 1, sz, Handle) == sz) {
				tmpBuf[sz - 1] = 0;
				Result.assign(tmpBuf);
				return true;
			}
		}
	}
	return false;
}

const IMAGE_SECTION_HEADER * PortableExecutable::FindSection(const char *findName) const {
	const size_t slen = strlen(findName);
	assert(slen <= IMAGE_SIZEOF_SHORT_NAME);
	auto found = std::find_if(vecPESections.begin(), vecPESections.end(), [slen, findName](auto const& section) {
		return !memcmp(findName, section.Name, slen);
	});

	if(found == vecPESections.end()) {
		return nullptr;
	} else {
		return &(*found);
	}
}

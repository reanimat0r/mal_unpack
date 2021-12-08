#include "file_util.h"

#include "ntddk.h"

namespace file_util {

	const SIZE_T MAX_NT_PATH = (MAX_PATH * 2);

	char get_system_drive()
	{
		char buf[MAX_PATH] = { 0 };
		GetWindowsDirectoryA(buf, MAX_PATH);
		const char drive_letter = buf[0];
		return drive_letter;
	}

	NTSTATUS fetch_volume_handle(char driveLetter, HANDLE& RootHandle)
	{
		RootHandle = NULL;

		UNICODE_STRING RootDirectory = { 0 };
		OBJECT_ATTRIBUTES Attributes = { 0 };
		IO_STATUS_BLOCK Io = { 0 };

		WCHAR volume_path[] = L"\\??\\A:\\";
		wchar_t* drive_letter_ptr = wcsstr(volume_path, L"A");
		if (drive_letter_ptr) {
			memcpy(drive_letter_ptr, &driveLetter, sizeof(driveLetter));
		}
		RtlInitUnicodeString(&RootDirectory, volume_path);
		InitializeObjectAttributes(&Attributes, &RootDirectory, OBJ_CASE_INSENSITIVE, NULL, NULL);

		return NtOpenFile(&RootHandle, SYNCHRONIZE | FILE_READ_ATTRIBUTES, &Attributes, &Io, FILE_SHARE_READ, FILE_OPEN);
	}

	NTSTATUS set_to_delete(wchar_t file_name[MAX_NT_PATH])
	{
		HANDLE hFile = NULL;
		IO_STATUS_BLOCK ioStatusBlock = { 0 };
		OBJECT_ATTRIBUTES objAttr = { 0 };

		UNICODE_STRING filePathU = { 0 };
		RtlInitUnicodeString(&filePathU, file_name);
		InitializeObjectAttributes(&objAttr, &filePathU, OBJ_CASE_INSENSITIVE, NULL, NULL);

		NTSTATUS status = NtCreateFile(&hFile, SYNCHRONIZE | DELETE, &objAttr, &ioStatusBlock, 
			NULL, FILE_ATTRIBUTE_NORMAL, FILE_SHARE_DELETE, FILE_OPEN, FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
		if (status != STATUS_SUCCESS) {
			if (ioStatusBlock.Information == FILE_DOES_NOT_EXIST) {
				return STATUS_SUCCESS; // file already deleted
			}
			std::cout << "Failed to open the file for deletion:" << std::hex << status << "\n";
			return status;
		}
		FILE_DISPOSITION_INFORMATION disposition = { TRUE };
		status = NtSetInformationFile(hFile, &ioStatusBlock, &disposition, sizeof(FILE_DISPOSITION_INFORMATION), FileDispositionInformation);
		NtClose(hFile);
//#ifdef _DEBUG
		std::cout << "Attempted to set delete disposition, status: " << std::hex << status << " IO status:" << ioStatusBlock.Status << "\n";
//#endif
		return status;
	}

	bool get_file_path(HANDLE volumeHndl, LONGLONG file_id, LPWSTR file_name_buf, const DWORD file_name_len, DWORD path_type = VOLUME_NAME_DOS)
	{
		FILE_ID_DESCRIPTOR FileDesc = { 0 };
		FileDesc.dwSize = sizeof(FILE_ID_DESCRIPTOR);
		FileDesc.Type = FileIdType;
		FileDesc.FileId.QuadPart = file_id;

		HANDLE hFile = OpenFileById(volumeHndl, &FileDesc, SYNCHRONIZE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ, NULL, 0);
		if (!hFile || hFile == INVALID_HANDLE_VALUE) {
			return false;
		}
		DWORD got_len = GetFinalPathNameByHandleW(hFile, file_name_buf, file_name_len, path_type);
		NtClose(hFile);
		return (got_len != 0) ? true: false;
	}

};


size_t file_util::list_files(std::set<LONGLONG>& filesIds)
{
	FILE_ID_DESCRIPTOR FileDesc = { 0 };
	FileDesc.dwSize = sizeof(FILE_ID_DESCRIPTOR);
	FileDesc.Type = FileIdType;

	wchar_t file_name[MAX_PATH] = { 0 };
	HANDLE volumeHndl = NULL;
	if (fetch_volume_handle(get_system_drive(), volumeHndl) != STATUS_SUCCESS) {
		return 0;
	}
	size_t processed = 0;
	std::set<LONGLONG>::iterator itr = filesIds.begin();

	for (itr = filesIds.begin(); itr != filesIds.end(); ++itr) {
		LONGLONG fileId = *itr;

		const bool gotName = get_file_path(volumeHndl, fileId, file_name, MAX_PATH, VOLUME_NAME_DOS);
		if (!gotName) {
			std::cerr << "Failed to retrieve the name of the file with ID: " << std::hex << FileDesc.FileId.QuadPart << "\n";
			continue;
		}
		processed++;
		std::wcout << "File: " << file_name << "\n";
	}
	return processed;
}


size_t file_util::delete_dropped_files(std::set<LONGLONG>& filesIds)
{
	FILE_ID_DESCRIPTOR FileDesc = { 0 };
	FileDesc.dwSize = sizeof(FILE_ID_DESCRIPTOR);
	FileDesc.Type = FileIdType;

	wchar_t file_name[MAX_PATH] = { 0 };
	HANDLE volumeHndl = NULL;
	if (fetch_volume_handle(get_system_drive(), volumeHndl) != STATUS_SUCCESS) {
		return 0;
	}
	size_t processed = 0;
	std::set<LONGLONG>::iterator itr = filesIds.begin();

	for (itr = filesIds.begin(); itr != filesIds.end(); ++itr) {
		LONGLONG fileId = *itr;

		bool isDeleted = false;
		const bool gotName = get_file_path(volumeHndl, fileId, file_name, MAX_PATH, VOLUME_NAME_DOS);
		if (!gotName) {
			std::cerr << "Failed to retrieve the name of the file with ID: " << std::hex << FileDesc.FileId.QuadPart << "\n";
			continue;
		}
		std::wcout << "File: " << file_name << "\n";
		std::wstring new_name = std::wstring(file_name) + L".unsafe";
		if (MoveFileExW(file_name, new_name.c_str(), MOVEFILE_WRITE_THROUGH | MOVEFILE_REPLACE_EXISTING)) {
			std::cout << "file moved!\n";
			if (DeleteFileW(new_name.c_str())) {
				isDeleted = true;
				filesIds.erase(FileDesc.FileId.QuadPart);
				std::cout << "file deleted!\n";
			}
			processed++;
		}
	}
	return processed;
}

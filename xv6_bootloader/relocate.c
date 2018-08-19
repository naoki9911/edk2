#include  <Uefi.h>
#include  <Library/UefiLib.h>
#include  <Library/UefiBootServicesTableLib.h>
#include  <Protocol/BlockIo.h>
#include  <Protocol/LoadedImage.h>
#include  <Protocol/SimpleFileSystem.h>
#include  <Library/DevicePathLib.h>
#include  <Guid/FileInfo.h>
#include  <Library/MemoryAllocationLib.h>
#include  <Library/BaseMemoryLib.h>
#include "elf.h"
#include "relocate.h"
EFI_STATUS RelocateELF(CHAR16* KernelPath, EFI_PHYSICAL_ADDRESS* RelocateAddr){
  EFI_STATUS Status;
  EFI_FILE_PROTOCOL *Root;
  EFI_FILE_PROTOCOL *File;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SimpleFile;
  Status = gBS->LocateProtocol (
    &gEfiSimpleFileSystemProtocolGuid,
    NULL,
    (VOID **)&SimpleFile
  );
  Print(L"SimpleFileSystemProtocol=%d\n", Status);
  if (EFI_ERROR (Status)) {
    Print(L"Failed to Locate EFI Simple File System Protocol.\n");
    return Status;
  }
  Status = SimpleFile->OpenVolume (SimpleFile, &Root);
  Print(L"SimpleFileOpenVolume=%d\n",Status);
  if (EFI_ERROR (Status)) {
    Print(L"Failed to Open volume.\n");
    return Status;
  }
  Print(L"Loading ELF Binary %s\n",KernelPath);
  Status = Root->Open (Root, &File, KernelPath, EFI_FILE_MODE_READ, 0);
  Print(L"SimpleFileOpenFile=%d\n",Status);
  if (EFI_ERROR (Status)) {
    Print(L"Cannot open %s\n", KernelPath);
    return Status;
  }

  UINTN FileInfoBufferSize = sizeof(EFI_FILE_INFO) + sizeof(CHAR16) * StrLen(KernelPath) + 2;
  UINT8 FileInfoBuffer[FileInfoBufferSize];
  Status = File->GetInfo(File, &gEfiFileInfoGuid, &FileInfoBufferSize, FileInfoBuffer);
  Print(L"FileInfoGet=%d\n",Status);
  if (EFI_ERROR(Status)) {
    Print(L"Failed to Get FileInfo\n");
    return Status;
  }
  
  EFI_FILE_INFO *FileInfo = (EFI_FILE_INFO*)FileInfoBuffer;
  UINTN KernelFileSize = FileInfo->FileSize;
  Print(L"KernelSize=%d\n",KernelFileSize);
  UINTN BufferPageSize = (KernelFileSize + 4095) / 4096;
  EFI_PHYSICAL_ADDRESS BufferAddress = 0;
  Status = gBS->AllocatePages(
  AllocateAnyPages,
  EfiLoaderData,
  BufferPageSize,
  &BufferAddress);
  Print(L"AllocatePages=%d\n",Status);
  if (EFI_ERROR(Status)) {
    Print(L"Could not allocate pages\n");
    return Status;
  }
  

 
  Status = File->Read(
    File,
    &KernelFileSize,
    (VOID *)BufferAddress
    );
  if (EFI_ERROR (Status)) {
    Print(L"Failed to Load Kernel\n");
    return Status;
  }

  Print(L"FileLoad=%d\n",Status);
  
  Elf32_Ehdr *Ehdr = (Elf32_Ehdr*)BufferAddress;
  Elf32_Phdr *Phdr = (Elf32_Phdr*)(BufferAddress + Ehdr->e_phoff);
  Print(L"Ehdr Address:%x\n",Ehdr);
  Print(L"Phdr Address:%x\n",Phdr);
  UINTN i;
  EFI_PHYSICAL_ADDRESS alloc_start_address;
  EFI_PHYSICAL_ADDRESS alloc_end_address;
  UINT8 init = 0;
  for(i=0; i<Ehdr->e_phnum;i++){
    if(Phdr[i].p_type == PT_LOAD){
      EFI_PHYSICAL_ADDRESS start_address = Phdr[i].p_paddr;
      EFI_PHYSICAL_ADDRESS end_address = start_address + Phdr[i].p_memsz;
      UINTN mask = Phdr[i].p_align-1;
      end_address = (end_address + mask) & ~mask;
      Print(L"Start Address:%x\n",start_address);
      Print(L"End Address:%x\n",end_address);
      if(init == 0){
        alloc_start_address = start_address;
        alloc_end_address = end_address;
        init = 1;
      }else{
        if(start_address < alloc_start_address) alloc_start_address = start_address;
        if(end_address > alloc_end_address) alloc_end_address = end_address;
      }
    }
  }
  
  Print(L"Allocate Start Address:%x\n",alloc_start_address);
  Print(L"Allocate End Address:%x\n",alloc_end_address);
  Print(L"Entry Address:%x\n",Ehdr->e_entry);
  UINTN page_size = ((alloc_end_address - alloc_start_address)/4096)+1;
  Status = gBS->AllocatePages(
    AllocateAddress,
    EfiLoaderData,
    page_size,
    &alloc_start_address);
  Print(L"AllocatePages=%d\n",Status);
  if (EFI_ERROR(Status)) {
    Print(L"Could not allocate pages at %08lx \n", &alloc_start_address);
    return Status;
  }
  for(i=0; i<Ehdr->e_phnum; i++){
    if(Phdr[i].p_type == PT_LOAD){
      EFI_PHYSICAL_ADDRESS start_address = Phdr[i].p_paddr;
      EFI_PHYSICAL_ADDRESS end_address = start_address + Phdr[i].p_memsz;
      UINTN mask = Phdr[i].p_align-1;
      end_address = (end_address + mask) & ~mask;
      CopyMem((VOID *)start_address,(VOID *)(BufferAddress + Phdr[i].p_offset),Phdr[i].p_filesz);
      if(Phdr[i].p_memsz > Phdr[i].p_filesz){
        SetMem((VOID *)(start_address + Phdr[i].p_filesz),Phdr[i].p_memsz - Phdr[i].p_filesz,0);
        Print(L"Zero Clear at %x - %x\n",start_address + Phdr[i].p_filesz,end_address);
      }
        Print(L"Relocated at %x - %x\n",start_address,end_address);
    }
  }
  *RelocateAddr = (EFI_PHYSICAL_ADDRESS)Ehdr->e_entry;
  FreePages((VOID *)BufferAddress,BufferPageSize);
  
  return Status;
}

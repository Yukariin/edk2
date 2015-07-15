#include "Common.h"
#include "Bds.h"

#include <PiDxe.h>

#include <Library/DevicePathLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/GenericBdsLib.h>
#include <Protocol/SimpleFileSystem.h>

#include <Guid/FileInfo.h>

EFI_STATUS
BdsFileSystemLoadImage (
  IN     EFI_HANDLE Handle,
  IN     EFI_ALLOCATE_TYPE     Type,
  IN OUT EFI_PHYSICAL_ADDRESS* Image,
  OUT    UINTN                 *ImageSize
  )
{
  EFI_STATUS                      Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *FsVolume;
  EFI_FILE_HANDLE                 FsRoot;
  EFI_FILE_INFO                   *FileInfo;
  EFI_FILE_HANDLE                 File;
  UINTN                           Size;

  /* FilePathDevicePath = (FILEPATH_DEVICE_PATH*)RemainingDevicePath; */

  // Get volume from handle
  Status = gBS->HandleProtocol (Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID *) &FsVolume);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Try to Open the volume and get root directory
  Status = FsVolume->OpenVolume (FsVolume, &FsRoot);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  File = NULL;
  Status = FsRoot->Open (FsRoot, &File, EFI_CORESERVICES, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Size = 0;
  File->GetInfo (File, &gEfiFileInfoGuid, &Size, NULL);
  FileInfo = AllocatePool (Size);
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &Size, FileInfo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Get the file size
  Size = FileInfo->FileSize;
  if (ImageSize) {
    *ImageSize = Size;
  }
  FreePool (FileInfo);

  Status = gBS->AllocatePages (Type, EfiBootServicesCode, EFI_SIZE_TO_PAGES(Size), Image);
  // Try to allocate in any pages if failed to allocate memory at the defined location
  if ((Status == EFI_OUT_OF_RESOURCES) && (Type != AllocateAnyPages)) {
    Status = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesCode, EFI_SIZE_TO_PAGES(Size), Image);
  }
  if (!EFI_ERROR (Status)) {
    Status = File->Read (File, &Size, (VOID*)(UINTN)(*Image));
  }

  return Status;
}


/**
  Start an EFI Application from a Device Path

  @param  ParentImageHandle     Handle of the calling image
  @param  DevicePath            Location of the EFI Application

  @retval EFI_SUCCESS           All drivers have been connected
  @retval EFI_NOT_FOUND         The Linux kernel Device Path has not been found
  @retval EFI_OUT_OF_RESOURCES  There is not enough resource memory to store the matching results.

**/
EFI_STATUS
BdsStartEfiApplication (
  IN EFI_HANDLE                  Handle,
  IN EFI_HANDLE                  ParentImageHandle,
  IN EFI_DEVICE_PATH_PROTOCOL    *DevicePath
  )
{
  EFI_STATUS                   Status;
  EFI_HANDLE                   ImageHandle;
  EFI_PHYSICAL_ADDRESS         BinaryBuffer;
  UINTN                        BinarySize;

  // Find the nearest supported file loader
  Status = BdsFileSystemLoadImage (Handle, AllocateAnyPages, &BinaryBuffer, &BinarySize);
  if (EFI_ERROR (Status)) {
    DEBUG((EFI_D_INFO, "== Bds could not load System image ==\n"));
    return Status;
  }

  // Load the image from the Buffer with Boot Services function
  Status = gBS->LoadImage (TRUE, ParentImageHandle, DevicePath, (VOID*)(UINTN)BinaryBuffer, BinarySize, &ImageHandle);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Before calling the image, enable the Watchdog Timer for  the 5 Minute period
  gBS->SetWatchdogTimer (5 * 60, 0x0000, 0x00, NULL);
  // Start the image
  Status = gBS->StartImage (ImageHandle, NULL, NULL);
  // Clear the Watchdog Timer after the image returns
  gBS->SetWatchdogTimer (0x0000, 0x0000, 0x0000, NULL);

  return Status;
}


EFI_STATUS
BdsBootApple ()
{
  EFI_STATUS                      Status;
  UINTN                           Index;
  UINTN                           TempSize;
  EFI_DEVICE_PATH_PROTOCOL        *TempDevicePath;
  EFI_HANDLE                      *SimpleFileSystemHandles;
  UINTN                           NumberSimpleFileSystemHandles;
  DEBUG((EFI_D_INFO, "Apple Specific Initialization Start\n"));

  Status = gBS->LocateHandleBuffer (
      ByProtocol,
      &gEfiSimpleFileSystemProtocolGuid,
      NULL,
      &NumberSimpleFileSystemHandles,
      &SimpleFileSystemHandles
      );
  DEBUG((EFI_D_INFO, "Number Device File System: %d\n", NumberSimpleFileSystemHandles));
  if (EFI_ERROR (Status)) {
    DEBUG((EFI_D_ERROR, "LocateHandleBuffer error: %r\n", Status));
    return Status;
  }
  for (Index = 0; Index < NumberSimpleFileSystemHandles; Index++) {
    //
    // Get the device path size of SimpleFileSystem handle
    //
    DEBUG((EFI_D_INFO, "Protocol: %d\n", Index));
    TempDevicePath = DevicePathFromHandle (SimpleFileSystemHandles[Index]);
    TempSize = GetDevicePathSize (TempDevicePath)- sizeof (EFI_DEVICE_PATH_PROTOCOL); // minus the end node
    BdsStartEfiApplication (
      SimpleFileSystemHandles[Index],
      gImageHandle,
      TempDevicePath
      );
      //

  }
  DEBUG((EFI_D_INFO, "Apple Specific Initialization End\n"));
  return EFI_SUCCESS;
}
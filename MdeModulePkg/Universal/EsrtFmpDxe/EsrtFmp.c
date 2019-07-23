/** @file
  Publishes ESRT table from Firmware Management Protocol instances

  Copyright (c) 2016, Microsoft Corporation
  Copyright (c) 2018 - 2019, Intel Corporation. All rights reserved.<BR>

  All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiLib.h>
#include <Protocol/FirmwareManagement.h>
#include <Guid/EventGroup.h>
#include <Guid/SystemResourceTable.h>

/**
 Print ESRT to debug console.

 @param[in]  Table   Pointer to the ESRT table.

**/
VOID
EFIAPI
PrintTable (
  IN EFI_SYSTEM_RESOURCE_TABLE  *Table
  );

//
// Number of ESRT entries to grow by each time we run out of room
//
#define GROWTH_STEP  10

/**
  Install EFI System Resource Table into the UEFI Configuration Table

  @param[in] Table                  Pointer to the ESRT.

  @return  Status code.

**/
EFI_STATUS
InstallEfiSystemResourceTableInUefiConfigurationTable (
  IN EFI_SYSTEM_RESOURCE_TABLE      *Table
  )
{
  EFI_STATUS Status;

  Status = EFI_SUCCESS;
  if (Table->FwResourceCount == 0) {
    DEBUG ((DEBUG_ERROR, "EsrtFmpDxe: Can't install ESRT table because it has zero Entries. \n"));
    Status = EFI_UNSUPPORTED;
  } else {
    //
    // Install the pointer into config table
    //
    Status = gBS->InstallConfigurationTable (&gEfiSystemResourceTableGuid, Table);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "EsrtFmpDxe: Can't install ESRT table.  Status: %r. \n", Status));
    } else {
      DEBUG ((DEBUG_INFO, "EsrtFmpDxe: Installed ESRT table. \n"));
    }
  }
  return Status;
}

/**
  Return if this FMP is a system FMP or a device FMP, based upon FmpImageInfo.

  @param[in] FmpImageInfo A pointer to EFI_FIRMWARE_IMAGE_DESCRIPTOR

  @return TRUE  It is a system FMP.
  @return FALSE It is a device FMP.
**/
BOOLEAN
IsSystemFmp (
  IN EFI_FIRMWARE_IMAGE_DESCRIPTOR  *FmpImageInfo
  )
{
  GUID   *Guid;
  UINTN  Count;
  UINTN  Index;

  Guid  = PcdGetPtr (PcdSystemFmpCapsuleImageTypeIdGuid);
  Count = PcdGetSize (PcdSystemFmpCapsuleImageTypeIdGuid) / sizeof(GUID);

  for (Index = 0; Index < Count; Index++, Guid++) {
    if (CompareGuid (&FmpImageInfo->ImageTypeId, Guid)) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Function to create a single ESRT Entry and add it to the ESRT
  given a FMP descriptor.  If the guid is already in the ESRT, then the ESRT
  entry is updated.  The ESRT will grow if it does not have enough room.

  @param[in, out] Table             On input, pointer to the pointer to the ESRT.
                                    On output, same as input or pointer to the pointer
                                    to new enlarged ESRT.
  @param[in]      FmpImageInfoBuf   Pointer to the EFI_FIRMWARE_IMAGE_DESCRIPTOR.
  @param[in]      FmpVersion        FMP Version.

  @return  Status code.

**/
EFI_STATUS
CreateEsrtEntry (
  IN OUT EFI_SYSTEM_RESOURCE_TABLE  **Table,
  IN OUT UINT64                     **HardwareInstances,
  IN EFI_FIRMWARE_IMAGE_DESCRIPTOR  *FmpImageInfoBuf,
  IN UINT32                         FmpVersion
  )
{
  UINTN                      Index;
  EFI_SYSTEM_RESOURCE_ENTRY  *Entry;
  UINTN                      NewSize;
  EFI_SYSTEM_RESOURCE_TABLE  *NewTable;
  UINT64                     *NewHardwareInstances;
  UINT64                     FmpHardwareInstance;

  Index = 0;
  Entry = NULL;

  Entry = (EFI_SYSTEM_RESOURCE_ENTRY *)((*Table) + 1);
  //
  // Check to see if GUID is already in the table
  //
  for (Index = 0; Index < (*Table)->FwResourceCount; Index++) {
    if (CompareGuid (&Entry->FwClass, &FmpImageInfoBuf->ImageTypeId)) {
      //
      // If HardwareInstance in ESRT and FmpImageInfoBuf are the same value
      // for the same ImageTypeId GUID, then there is more than one FMP
      // instance for the same FW device, which is an error condition.
      // If FmpVersion is less than 3, then assume HardwareInstance is 0.
      //
      FmpHardwareInstance = 0;
      if (FmpVersion >= 3) {
        FmpHardwareInstance = FmpImageInfoBuf->HardwareInstance;
      }
      if ((*HardwareInstances)[Index] == FmpHardwareInstance) {
        DEBUG ((DEBUG_ERROR, "EsrtFmpDxe: ESRT Entry already exists for FMP Instance with GUID %g and HardwareInstance %016lx\n", &Entry->FwClass, (*HardwareInstances)[Index]));
        ASSERT ((*HardwareInstances)[Index] != FmpHardwareInstance);
        return EFI_UNSUPPORTED;
      }

      DEBUG ((DEBUG_INFO, "EsrtFmpDxe: ESRT Entry already exists for FMP Instance with GUID %g\n", &Entry->FwClass));

      //
      // Set ESRT FwVersion to the smaller of the two values
      //
      Entry->FwVersion = MIN (FmpImageInfoBuf->Version, Entry->FwVersion);

      //
      // VERSION 2 has Lowest Supported
      //
      if (FmpVersion >= 2) {
        //
        // Set ESRT LowestSupportedFwVersion to the smaller of the two values
        //
        Entry->LowestSupportedFwVersion =
          MIN (
            FmpImageInfoBuf->LowestSupportedImageVersion,
            Entry->LowestSupportedFwVersion
            );
      }

      //
      // VERSION 3 supports last attempt values
      //
      if (FmpVersion >= 3) {
        //
        // Update the ESRT entry with the last attempt status and last attempt
        // version from the first FMP instance whose last attempt status is not
        // SUCCESS.  If all FMP instances are SUCCESS, then set version to the 
        // smallest value from all FMP instances.
        //
        if (Entry->LastAttemptStatus == LAST_ATTEMPT_STATUS_SUCCESS) {
          if (FmpImageInfoBuf->LastAttemptStatus != LAST_ATTEMPT_STATUS_SUCCESS) {
            Entry->LastAttemptStatus = FmpImageInfoBuf->LastAttemptStatus;
            Entry->LastAttemptVersion = FmpImageInfoBuf->LastAttemptVersion;
          } else {
            Entry->LastAttemptVersion =
              MIN (
                FmpImageInfoBuf->LastAttemptVersion,
                Entry->LastAttemptVersion
                );
          }
        }
      }

      return EFI_SUCCESS;
    }
    Entry++;
  }

  //
  // Grow table if needed
  //
  if ((*Table)->FwResourceCount >= (*Table)->FwResourceCountMax) {
    NewSize  = (((*Table)->FwResourceCountMax + GROWTH_STEP) * sizeof (EFI_SYSTEM_RESOURCE_ENTRY)) + sizeof (EFI_SYSTEM_RESOURCE_TABLE);
    NewTable = AllocateZeroPool (NewSize);
    if (NewTable == NULL) {
      DEBUG ((DEBUG_ERROR, "EsrtFmpDxe: Failed to allocate memory larger table for ESRT. \n"));
      return EFI_OUT_OF_RESOURCES;
    }
    //
    // Copy the whole old table into new table buffer
    //
    CopyMem (
      NewTable,
      (*Table),
      (((*Table)->FwResourceCountMax) * sizeof (EFI_SYSTEM_RESOURCE_ENTRY)) + sizeof (EFI_SYSTEM_RESOURCE_TABLE)
      );
    //
    // Update max
    //
    NewTable->FwResourceCountMax = NewTable->FwResourceCountMax + GROWTH_STEP;
    //
    // Free old table
    //
    FreePool ((*Table));
    //
    // Reassign pointer to new table.
    //
    (*Table) = NewTable;

    NewSize  = ((*Table)->FwResourceCountMax) * sizeof (UINT64);
    NewHardwareInstances = AllocateZeroPool (NewSize);
    if (NewHardwareInstances == NULL) {
      DEBUG ((DEBUG_ERROR, "EsrtFmpDxe: Failed to allocate memory larger table for Hardware Instances.\n"));
      return EFI_OUT_OF_RESOURCES;
    }
    //
    // Copy the whole old table into new table buffer
    //
    CopyMem (
      NewHardwareInstances,
      (*HardwareInstances),
      ((*Table)->FwResourceCountMax) * sizeof (UINT64)
      );
    //
    // Free old table
    //
    FreePool ((*HardwareInstances));
    //
    // Reassign pointer to new table.
    //
    (*HardwareInstances) = NewHardwareInstances;
  }

  //
  // ESRT table has enough room for the new entry so add new entry
  //
  Entry = (EFI_SYSTEM_RESOURCE_ENTRY *)(((UINT8 *)(*Table)) + sizeof (EFI_SYSTEM_RESOURCE_TABLE));
  //
  // Move to the location of new entry
  //
  Entry = Entry + (*Table)->FwResourceCount;

  CopyGuid (&Entry->FwClass, &FmpImageInfoBuf->ImageTypeId);

  if (IsSystemFmp (FmpImageInfoBuf)) {
    DEBUG ((DEBUG_INFO, "EsrtFmpDxe: Found an ESRT entry for a System Device.\n"));
    Entry->FwType = (UINT32)(ESRT_FW_TYPE_SYSTEMFIRMWARE);
  } else {
    Entry->FwType = (UINT32)(ESRT_FW_TYPE_DEVICEFIRMWARE);
  }

  Entry->FwVersion                = FmpImageInfoBuf->Version;
  Entry->LowestSupportedFwVersion = 0;
  Entry->CapsuleFlags             = 0;
  Entry->LastAttemptVersion       = 0;
  Entry->LastAttemptStatus        = 0;

  //
  // VERSION 2 has Lowest Supported
  //
  if (FmpVersion >= 2) {
    Entry->LowestSupportedFwVersion = FmpImageInfoBuf->LowestSupportedImageVersion;
  }

  //
  // VERSION 3 supports last attempt values
  //
  if (FmpVersion >= 3) {
    Entry->LastAttemptVersion = FmpImageInfoBuf->LastAttemptVersion;
    Entry->LastAttemptStatus  = FmpImageInfoBuf->LastAttemptStatus;
  }

  //
  // VERSION 3 supports hardware instance
  //
  (*HardwareInstances)[(*Table)->FwResourceCount] = 0;
  if (FmpVersion >= 3) {
    (*HardwareInstances)[(*Table)->FwResourceCount] = FmpImageInfoBuf->HardwareInstance;
  }

  //
  // Increment resource count
  //
  (*Table)->FwResourceCount++;

  return EFI_SUCCESS;
}

/**
  Function to create ESRT based on FMP Instances.
  Create ESRT table, get the descriptors from FMP Instance and
  create ESRT entries (ESRE).

  @return Pointer to the ESRT created.

**/
EFI_SYSTEM_RESOURCE_TABLE *
CreateFmpBasedEsrt (
  VOID
  )
{
  EFI_STATUS                        Status;
  EFI_SYSTEM_RESOURCE_TABLE         *Table;
  UINT64                            *HardwareInstances;
  UINTN                             NoProtocols;
  VOID                              **Buffer;
  UINTN                             Index;
  EFI_FIRMWARE_MANAGEMENT_PROTOCOL  *Fmp;
  UINTN                             DescriptorSize;
  EFI_FIRMWARE_IMAGE_DESCRIPTOR     *FmpImageInfoBuf;
  EFI_FIRMWARE_IMAGE_DESCRIPTOR     *FmpImageInfoBufOrg;
  UINT8                             FmpImageInfoCount;
  UINT32                            FmpImageInfoDescriptorVer;
  UINTN                             ImageInfoSize;
  UINT32                            PackageVersion;
  CHAR16                            *PackageVersionName;

  Status             = EFI_SUCCESS;
  Table              = NULL;
  HardwareInstances  = NULL;
  NoProtocols        = 0;
  Buffer             = NULL;
  PackageVersionName = NULL;
  FmpImageInfoBuf    = NULL;
  FmpImageInfoBufOrg = NULL;
  Fmp                = NULL;

  Status = EfiLocateProtocolBuffer (
             &gEfiFirmwareManagementProtocolGuid,
             &NoProtocols,
             &Buffer
             );
  if (EFI_ERROR(Status) || (Buffer == NULL)) {
    return NULL;
  }

  //
  // Allocate Memory for tables
  //
  Table = AllocateZeroPool (
             (GROWTH_STEP * sizeof (EFI_SYSTEM_RESOURCE_ENTRY)) + sizeof (EFI_SYSTEM_RESOURCE_TABLE)
             );
  if (Table == NULL) {
    DEBUG ((DEBUG_ERROR, "EsrtFmpDxe: Failed to allocate memory for ESRT.\n"));
    gBS->FreePool (Buffer);
    return NULL;
  }

  HardwareInstances = AllocateZeroPool (GROWTH_STEP * sizeof (UINT64));
  if (HardwareInstances == NULL) {
    DEBUG ((DEBUG_ERROR, "EsrtFmpDxe: Failed to allocate memory for HW Instance Table.\n"));
    gBS->FreePool (Table);
    gBS->FreePool (Buffer);
    return NULL;
  }

  Table->FwResourceCount    = 0;
  Table->FwResourceCountMax = GROWTH_STEP;
  Table->FwResourceVersion  = EFI_SYSTEM_RESOURCE_TABLE_FIRMWARE_RESOURCE_VERSION;

  for (Index = 0; Index < NoProtocols; Index++) {
    Fmp = (EFI_FIRMWARE_MANAGEMENT_PROTOCOL *) Buffer[Index];

    ImageInfoSize = 0;
    Status = Fmp->GetImageInfo (
                    Fmp,                         // FMP Pointer
                    &ImageInfoSize,              // Buffer Size (in this case 0)
                    NULL,                        // NULL so we can get size
                    &FmpImageInfoDescriptorVer,  // DescriptorVersion
                    &FmpImageInfoCount,          // DescriptorCount
                    &DescriptorSize,             // DescriptorSize
                    &PackageVersion,             // PackageVersion
                    &PackageVersionName          // PackageVersionName
                    );

    if (Status != EFI_BUFFER_TOO_SMALL) {
      DEBUG ((DEBUG_ERROR, "EsrtFmpDxe: Unexpected Failure in GetImageInfo.  Status = %r\n", Status));
      continue;
    }

    FmpImageInfoBuf = AllocateZeroPool (ImageInfoSize);
    if (FmpImageInfoBuf == NULL) {
      DEBUG ((DEBUG_ERROR, "EsrtFmpDxe: Failed to get memory for descriptors.\n"));
      continue;
    }

    FmpImageInfoBufOrg = FmpImageInfoBuf;
    PackageVersionName = NULL;
    Status = Fmp->GetImageInfo (
                    Fmp,
                    &ImageInfoSize,              // ImageInfoSize
                    FmpImageInfoBuf,             // ImageInfo
                    &FmpImageInfoDescriptorVer,  // DescriptorVersion
                    &FmpImageInfoCount,          // DescriptorCount
                    &DescriptorSize,             // DescriptorSize
                    &PackageVersion,             // PackageVersion
                    &PackageVersionName          // PackageVersionName
                    );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "EsrtFmpDxe: Failure in GetImageInfo.  Status = %r\n", Status));
      FreePool (FmpImageInfoBufOrg);
      FmpImageInfoBufOrg = NULL;
      continue;
    }

    //
    // Check each descriptor and read from the one specified
    //
    while (FmpImageInfoCount > 0) {
      //
      // If the descriptor has the IN USE bit set, create ESRT entry otherwise ignore.
      //
      if ((FmpImageInfoBuf->AttributesSetting & FmpImageInfoBuf->AttributesSupported & IMAGE_ATTRIBUTE_IN_USE) == IMAGE_ATTRIBUTE_IN_USE) {
        //
        // Create ESRT entry
        //
        CreateEsrtEntry (&Table, &HardwareInstances, FmpImageInfoBuf, FmpImageInfoDescriptorVer);
      }
      FmpImageInfoCount--;
      //
      // Increment the buffer pointer ahead by the size of the descriptor
      //
      FmpImageInfoBuf = (EFI_FIRMWARE_IMAGE_DESCRIPTOR *)(((UINT8 *)FmpImageInfoBuf) + DescriptorSize);
    }

    if (PackageVersionName != NULL) {
      FreePool (PackageVersionName);
      PackageVersionName = NULL;
    }
    FreePool (FmpImageInfoBufOrg);
    FmpImageInfoBufOrg = NULL;
  }

  gBS->FreePool (Buffer);
  return Table;
}

/**
  Notify function for event group EFI_EVENT_GROUP_READY_TO_BOOT. This is used to
  install the Efi System Resource Table.

  @param[in]  Event    The Event that is being processed.
  @param[in]  Context  The Event Context.

**/
VOID
EFIAPI
EsrtReadyToBootEventNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                 Status;
  EFI_SYSTEM_RESOURCE_TABLE  *Table;

  Table = CreateFmpBasedEsrt ();
  if (Table != NULL) {
    //
    // Print table on debug builds
    //
    DEBUG_CODE_BEGIN ();
    PrintTable (Table);
    DEBUG_CODE_END ();

    Status = InstallEfiSystemResourceTableInUefiConfigurationTable (Table);
    if (EFI_ERROR (Status)) {
      FreePool (Table);
    }
  } else {
    DEBUG ((DEBUG_ERROR, "EsrtFmpDxe: Can't install ESRT table because it is NULL. \n"));
  }

  //
  // Close the event to prevent it be signalled again.
  //
  gBS->CloseEvent (Event);
}

/**
  The module Entry Point of the Efi System Resource Table DXE driver.

  @param[in]  ImageHandle  The firmware allocated handle for the EFI image.
  @param[in]  SystemTable  A pointer to the EFI System Table.

  @retval  EFI_SUCCESS  The entry point is executed successfully.
  @retval  Other        Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
EsrtFmpEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   EsrtReadyToBootEvent;

  //
  // Register notify function to install ESRT on ReadyToBoot Event.
  //
  Status = EfiCreateEventReadyToBootEx (
             TPL_CALLBACK,
             EsrtReadyToBootEventNotify,
             NULL,
             &EsrtReadyToBootEvent
             );

  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "EsrtFmpDxe: Failed to register for ready to boot\n"));
  }

  return Status;
}

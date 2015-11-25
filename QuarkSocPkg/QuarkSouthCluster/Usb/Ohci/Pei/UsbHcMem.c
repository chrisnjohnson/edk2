/** @file
Routine procedures for memory allocate/free.

Copyright (c) 2013-2015 Intel Corporation.

This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/


#include "OhcPeim.h"


/**
  Allocate a block of memory to be used by the buffer pool.

  Use Redirect memory services to allocate memmory so that USB DMA transfers do
  not cause IMR violations on Quark.

  @param  Pool           The buffer pool to allocate memory for.
  @param  Pages          How many pages to allocate.

  @return The allocated memory block or NULL if failed.

**/
USBHC_MEM_BLOCK *
UsbHcAllocMemBlock (
  IN  USBHC_MEM_POOL      *Pool,
  IN  UINTN               Pages
  )
{
  USBHC_MEM_BLOCK         *Block;
  VOID                    *BufHost;
  VOID                    *Mapping;
  EFI_PHYSICAL_ADDRESS    MappedAddr;
  EFI_STATUS              Status;
  UINTN                   PageNumber;
  EFI_PHYSICAL_ADDRESS    TempPtr;

  Mapping = NULL;
  PageNumber =  sizeof(USBHC_MEM_BLOCK)/PAGESIZE +1;
  Status = PeiServicesAllocatePages (
             EfiBootServicesCode,
             PageNumber,
             &TempPtr
             );

  if (EFI_ERROR (Status)) {
    return NULL;
  }
  ZeroMem ((VOID   *)(UINTN)TempPtr, PageNumber*EFI_PAGE_SIZE);

  //
  // each bit in the bit array represents USBHC_MEM_UNIT
  // bytes of memory in the memory block.
  //
  ASSERT (USBHC_MEM_UNIT * 8 <= EFI_PAGE_SIZE);

  Block = (USBHC_MEM_BLOCK*)(UINTN)TempPtr;
  Block->BufLen   = EFI_PAGES_TO_SIZE (Pages);
  Block->BitsLen  = Block->BufLen / (USBHC_MEM_UNIT * 8);

  PageNumber =  (Block->BitsLen)/PAGESIZE +1;
  Status = PeiServicesAllocatePages (
             EfiBootServicesCode,
             PageNumber,
             &TempPtr
             );
  if (EFI_ERROR (Status)) {
    return NULL;
  }
  ZeroMem ((VOID   *)(UINTN)TempPtr, PageNumber*EFI_PAGE_SIZE);

  Block->Bits  = (UINT8 *)(UINTN)TempPtr;

  Status = PeiServicesAllocatePages (
             EfiBootServicesCode,
             Pages,
             &TempPtr
             );
  ZeroMem ((VOID   *)(UINTN)TempPtr, Pages*EFI_PAGE_SIZE);

  BufHost  = (VOID *)(UINTN)TempPtr;
  MappedAddr = (EFI_PHYSICAL_ADDRESS) (UINTN) BufHost;
  //
  // Check whether the data structure used by the host controller
  // should be restricted into the same 4G
  //
  if (Pool->Check4G && (Pool->Which4G != USB_HC_HIGH_32BIT (MappedAddr))) {
    return NULL;
  }

  Block->BufHost  = BufHost;
  Block->Buf      = (UINT8 *) ((UINTN) MappedAddr);
  Block->Mapping  = Mapping;
  Block->Next     = NULL;

  return Block;

}


/**
  Free the memory block from the memory pool.

  @param  Pool           The memory pool to free the block from.
  @param  Block          The memory block to free.

**/
VOID
UsbHcFreeMemBlock (
  IN USBHC_MEM_POOL       *Pool,
  IN USBHC_MEM_BLOCK      *Block
  )
{

  ASSERT ((Pool != NULL) && (Block != NULL));
}


/**
  Alloc some memory from the block.

  @param  Block          The memory block to allocate memory from.
  @param  Units          Number of memory units to allocate.

  @return The pointer to the allocated memory. If couldn't allocate the needed memory,
          the return value is NULL.

**/
VOID *
UsbHcAllocMemFromBlock (
  IN  USBHC_MEM_BLOCK     *Block,
  IN  UINTN               Units
  )
{
  UINTN                   Byte;
  UINT8                   Bit;
  UINTN                   StartByte;
  UINT8                   StartBit;
  UINTN                   Available;
  UINTN                   Count;

  ASSERT ((Block != 0) && (Units != 0));

  StartByte  = 0;
  StartBit   = 0;
  Available  = 0;

  for (Byte = 0, Bit = 0; Byte < Block->BitsLen;) {
    //
    // If current bit is zero, the corresponding memory unit is
    // available, otherwise we need to restart our searching.
    // Available counts the consective number of zero bit.
    //
    if (!USB_HC_BIT_IS_SET (Block->Bits[Byte], Bit)) {
      Available++;

      if (Available >= Units) {
        break;
      }

      NEXT_BIT (Byte, Bit);

    } else {
      NEXT_BIT (Byte, Bit);

      Available  = 0;
      StartByte  = Byte;
      StartBit   = Bit;
    }
  }

  if (Available < Units) {
    return NULL;
  }

  //
  // Mark the memory as allocated
  //
  Byte  = StartByte;
  Bit   = StartBit;

  for (Count = 0; Count < Units; Count++) {
    ASSERT (!USB_HC_BIT_IS_SET (Block->Bits[Byte], Bit));

    Block->Bits[Byte] = (UINT8) (Block->Bits[Byte] | (UINT8) USB_HC_BIT (Bit));
    NEXT_BIT (Byte, Bit);
  }

  return Block->BufHost + (StartByte * 8 + StartBit) * USBHC_MEM_UNIT;
}

/**
  Insert the memory block to the pool's list of the blocks.

  @param  Head           The head of the memory pool's block list.
  @param  Block          The memory block to insert.

**/
VOID
UsbHcInsertMemBlockToPool (
  IN USBHC_MEM_BLOCK      *Head,
  IN USBHC_MEM_BLOCK      *Block
  )
{
  ASSERT ((Head != NULL) && (Block != NULL));
  Block->Next = Head->Next;
  Head->Next  = Block;
}


/**
  Is the memory block empty?

  @param  Block   The memory block to check.

  @retval TRUE    The memory block is empty.
  @retval FALSE   The memory block isn't empty.

**/
BOOLEAN
UsbHcIsMemBlockEmpty (
  IN USBHC_MEM_BLOCK     *Block
  )
{
  UINTN                   Index;

  for (Index = 0; Index < Block->BitsLen; Index++) {
    if (Block->Bits[Index] != 0) {
      return FALSE;
    }
  }

  return TRUE;
}


/**
  Unlink the memory block from the pool's list.

  @param  Head           The block list head of the memory's pool.
  @param  BlockToUnlink  The memory block to unlink.

**/
VOID
UsbHcUnlinkMemBlock (
  IN USBHC_MEM_BLOCK      *Head,
  IN USBHC_MEM_BLOCK      *BlockToUnlink
  )
{
  USBHC_MEM_BLOCK         *Block;

  ASSERT ((Head != NULL) && (BlockToUnlink != NULL));

  for (Block = Head; Block != NULL; Block = Block->Next) {
    if (Block->Next == BlockToUnlink) {
      Block->Next         = BlockToUnlink->Next;
      BlockToUnlink->Next = NULL;
      break;
    }
  }
}


/**
  Initialize the memory management pool for the host controller.

  @param  PciIo                The PciIo that can be used to access the host controller.
  @param  Check4G              Whether the host controller requires allocated memory
                               from one 4G address space.
  @param  Which4G              The 4G memory area each memory allocated should be from.

  @retval EFI_SUCCESS          The memory pool is initialized.
  @retval EFI_OUT_OF_RESOURCE  Fail to init the memory pool.

**/
USBHC_MEM_POOL *
UsbHcInitMemPool (
  IN BOOLEAN              Check4G,
  IN UINT32               Which4G
  )
{
  USBHC_MEM_POOL          *Pool;
  UINTN                   PageNumber;
  EFI_STATUS              Status;
  EFI_PHYSICAL_ADDRESS    TempPtr;

  PageNumber =  sizeof(USBHC_MEM_POOL)/PAGESIZE +1;
  Status = PeiServicesAllocatePages (
             EfiBootServicesCode,
             PageNumber,
             &TempPtr
             );
  if (EFI_ERROR (Status)) {
    return NULL;
  }
  ZeroMem ((VOID   *)(UINTN)TempPtr, PageNumber*EFI_PAGE_SIZE);

  Pool = (USBHC_MEM_POOL *) ((UINTN) TempPtr);
  Pool->Check4G = Check4G;
  Pool->Which4G = Which4G;
  Pool->Head    = UsbHcAllocMemBlock (Pool, USBHC_MEM_DEFAULT_PAGES);

  if (Pool->Head == NULL) {
    Pool = NULL;
  }

  return Pool;
}


/**
  Release the memory management pool.

  @param  Pool              The USB memory pool to free.

  @retval EFI_SUCCESS       The memory pool is freed.
  @retval EFI_DEVICE_ERROR  Failed to free the memory pool.

**/
EFI_STATUS
UsbHcFreeMemPool (
  IN USBHC_MEM_POOL       *Pool
  )
{
  USBHC_MEM_BLOCK *Block;

  ASSERT (Pool->Head != NULL);

  //
  // Unlink all the memory blocks from the pool, then free them.
  // UsbHcUnlinkMemBlock can't be used to unlink and free the
  // first block.
  //
  for (Block = Pool->Head->Next; Block != NULL; Block = Pool->Head->Next) {
    UsbHcFreeMemBlock (Pool, Block);
  }

  UsbHcFreeMemBlock (Pool, Pool->Head);

  return EFI_SUCCESS;
}


/**
  Allocate some memory from the host controller's memory pool
  which can be used to communicate with host controller.

  @param  Pool           The host controller's memory pool.
  @param  Size           Size of the memory to allocate.

  @return The allocated memory or NULL.

**/
VOID *
UsbHcAllocateMem (
  IN  USBHC_MEM_POOL      *Pool,
  IN  UINTN               Size
  )
{
  USBHC_MEM_BLOCK         *Head;
  USBHC_MEM_BLOCK         *Block;
  USBHC_MEM_BLOCK         *NewBlock;
  VOID                    *Mem;
  UINTN                   AllocSize;
  UINTN                   Pages;

  Mem       = NULL;
  AllocSize = USBHC_MEM_ROUND (Size);
  Head      = Pool->Head;
  ASSERT (Head != NULL);

  //
  // First check whether current memory blocks can satisfy the allocation.
  //
  for (Block = Head; Block != NULL; Block = Block->Next) {
    Mem = UsbHcAllocMemFromBlock (Block, AllocSize / USBHC_MEM_UNIT);

    if (Mem != NULL) {
      ZeroMem (Mem, Size);
      break;
    }
  }

  if (Mem != NULL) {
    return Mem;
  }

  //
  // Create a new memory block if there is not enough memory
  // in the pool. If the allocation size is larger than the
  // default page number, just allocate a large enough memory
  // block. Otherwise allocate default pages.
  //
  if (AllocSize > EFI_PAGES_TO_SIZE (USBHC_MEM_DEFAULT_PAGES)) {
    Pages = EFI_SIZE_TO_PAGES (AllocSize) + 1;
  } else {
    Pages = USBHC_MEM_DEFAULT_PAGES;
  }

  NewBlock = UsbHcAllocMemBlock (Pool, Pages);

  if (NewBlock == NULL) {
    DEBUG ((EFI_D_INFO, "UsbHcAllocateMem: failed to allocate block\n"));
    return NULL;
  }

  //
  // Add the new memory block to the pool, then allocate memory from it
  //
  UsbHcInsertMemBlockToPool (Head, NewBlock);
  Mem = UsbHcAllocMemFromBlock (NewBlock, AllocSize / USBHC_MEM_UNIT);

  if (Mem != NULL) {
    ZeroMem (Mem, Size);
  }

  return Mem;
}


/**
  Free the allocated memory back to the memory pool.

  @param  Pool           The memory pool of the host controller.
  @param  Mem            The memory to free.
  @param  Size           The size of the memory to free.

**/
VOID
UsbHcFreeMem (
  IN USBHC_MEM_POOL       *Pool,
  IN VOID                 *Mem,
  IN UINTN                Size
  )
{
  USBHC_MEM_BLOCK         *Head;
  USBHC_MEM_BLOCK         *Block;
  UINT8                   *ToFree;
  UINTN                   AllocSize;
  UINTN                   Byte;
  UINTN                   Bit;
  UINTN                   Count;

  Head      = Pool->Head;
  AllocSize = USBHC_MEM_ROUND (Size);
  ToFree    = (UINT8 *) Mem;

  for (Block = Head; Block != NULL; Block = Block->Next) {
    //
    // scan the memory block list for the memory block that
    // completely contains the memory to free.
    //
    if ((Block->BufHost <= ToFree) && ((ToFree + AllocSize) <= (Block->BufHost + Block->BufLen))) {
      //
      // compute the start byte and bit in the bit array
      //
      Byte  = ((ToFree - Block->BufHost) / USBHC_MEM_UNIT) / 8;
      Bit   = ((ToFree - Block->BufHost) / USBHC_MEM_UNIT) % 8;

      //
      // reset associated bits in bit arry
      //
      for (Count = 0; Count < (AllocSize / USBHC_MEM_UNIT); Count++) {
        ASSERT (USB_HC_BIT_IS_SET (Block->Bits[Byte], Bit));

        Block->Bits[Byte] = (UINT8) (Block->Bits[Byte] ^ USB_HC_BIT (Bit));
        NEXT_BIT (Byte, Bit);
      }

      break;
    }
  }

  //
  // If Block == NULL, it means that the current memory isn't
  // in the host controller's pool. This is critical because
  // the caller has passed in a wrong memory point
  //
  ASSERT (Block != NULL);

  //
  // Release the current memory block if it is empty and not the head
  //
  if ((Block != Head) && UsbHcIsMemBlockEmpty (Block)) {
    UsbHcFreeMemBlock (Pool, Block);
  }

  return ;
}

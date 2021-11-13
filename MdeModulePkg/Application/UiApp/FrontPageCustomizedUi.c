/** @file

  This library class defines a set of interfaces to customize Ui module

Copyright (c) 2016, Intel Corporation. All rights reserved.<BR>
SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include <Uefi.h>
#include <Protocol/HiiConfigAccess.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include "FrontPage.h"
#include "FrontPageCustomizedUiSupport.h"

extern FRONT_PAGE_CALLBACK_DATA  gFrontPagePrivate;

/**
  Customize menus in the page.

  @param[in]  HiiHandle             The HII Handle of the form to update.
  @param[in]  StartOpCodeHandle     The context used to insert opcode.
  @param[in]  CustomizePageType     The page type need to be customized.

**/
VOID
UiCustomizeFrontPage (
  IN EFI_HII_HANDLE  HiiHandle,
  IN VOID            *StartOpCodeHandle
  )
{
  //
  // Create "Select Language" menu with Oneof opcode.
  //
  UiCreateLanguageMenu (HiiHandle, StartOpCodeHandle);

  //
  // Create empty line.
  //
  UiCreateEmptyLine (HiiHandle, StartOpCodeHandle);

  //
  // Find third party drivers which need to be shown in the front page.
  //
  UiListThirdPartyDrivers (HiiHandle, &gEfiIfrFrontPageGuid, NULL, StartOpCodeHandle);

  //
  // Create empty line.
  //
  UiCreateEmptyLine (HiiHandle, StartOpCodeHandle);

  //
  // Create "Continue" menu.
  //
  UiCreateContinueMenu (HiiHandle, StartOpCodeHandle);

  //
  // Create reset menu.
  //
  UiCreateResetMenu (HiiHandle, StartOpCodeHandle);
}

/**
  This function processes the results of changes in configuration.


  @param HiiHandle       Points to the hii handle for this formset.
  @param Action          Specifies the type of action taken by the browser.
  @param QuestionId      A unique value which is sent to the original exporting driver
                         so that it can identify the type of data to expect.
  @param Type            The type of value for the question.
  @param Value           A pointer to the data being sent to the original exporting driver.
  @param ActionRequest   On return, points to the action requested by the callback function.

  @retval  EFI_SUCCESS           The callback successfully handled the action.
  @retval  EFI_OUT_OF_RESOURCES  Not enough storage is available to hold the variable and its data.
  @retval  EFI_DEVICE_ERROR      The variable could not be saved.
  @retval  EFI_UNSUPPORTED       The specified Action is not supported by the callback.

**/
EFI_STATUS
UiFrontPageCallbackHandler (
  IN  EFI_HII_HANDLE                         HiiHandle,
  IN  EFI_BROWSER_ACTION                     Action,
  IN  EFI_QUESTION_ID                        QuestionId,
  IN  UINT8                                  Type,
  IN  EFI_IFR_TYPE_VALUE                     *Value,
  OUT EFI_BROWSER_ACTION_REQUEST             *ActionRequest
  )
{
  EFI_STATUS  Status;

  if (UiSupportLibCallbackHandler (HiiHandle, Action, QuestionId, Type, Value, ActionRequest, &Status)) {
    return Status;
  }

  return EFI_UNSUPPORTED;
}

/**
  Update the banner string in the front page.

  Current layout for the banner string like below:
  PS: Totally only 5 lines of banner supported.

  Line 1: Left BannerStr                           RightBannerStr
  Line 2: Left BannerStr                           RightBannerStr
  Line 3: Left BannerStr                           RightBannerStr
  Line 4: Left BannerStr                           RightBannerStr
  Line 5: Left BannerStr                           RightBannerStr
  <EmptyLine>
  First menu in front page.
  ...

  @param  LineIndex         The line index of the banner need to check.
  @param  LeftOrRight       The left or right banner need to check.
  @param  BannerStr         Banner string need to update.
                            Input the current string and user can update
                            it and return the new string.

**/
VOID
UiCustomizeFrontPageBanner (
  IN     UINTN          LineIndex,
  IN     BOOLEAN        LeftOrRight,
  IN OUT EFI_STRING     *BannerStr
  )
{
  if ((LineIndex == 5) && LeftOrRight) {
    // Update STR_CUSTOMIZE_BANNER_LINE5_LEFT
    if (PcdGetBool (PcdTestKeyUsed)) {
      if (BannerStr != NULL) {
        FreePool (*BannerStr);
      }

      *BannerStr = HiiGetString (gFrontPagePrivate.HiiHandle, STRING_TOKEN (STR_TEST_KEY_USED), NULL);
    }
  }

  return;
}

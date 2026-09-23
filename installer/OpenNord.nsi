Unicode true
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!include "LogicLib.nsh"
!include "x64.nsh"

!ifndef STAGING
  !define STAGING "..\staging"
!endif
!ifndef OUTPUT
  !define OUTPUT "OpenNord-Setup.exe"
!endif
!ifndef UNINSTALL_INCLUDE
  !error "Use scripts/Package-Windows.ps1 to generate the exact uninstall file list."
!endif

; Sign the embedded uninstaller before NSIS puts it in the setup payload.
!ifdef SIGNTOOL
  !uninstfinalize '"${SIGNTOOL}" sign /sha1 "${CERTIFICATE_THUMBPRINT}" /fd SHA256 /tr "${TIMESTAMP_URL}" /td SHA256 "%1"' = 0
  !uninstfinalize '"${SIGNTOOL}" verify /pa /all /tw "%1"' = 0
!endif

Name "OpenNord"
OutFile "${OUTPUT}"
InstallDir "$PROGRAMFILES64\OpenNord"
VIProductVersion "0.1.0.0"
VIAddVersionKey /LANG=1033 "ProductName" "OpenNord"
VIAddVersionKey /LANG=1033 "FileDescription" "OpenNord installer"
VIAddVersionKey /LANG=1033 "CompanyName" "OpenNord contributors"
VIAddVersionKey /LANG=1033 "FileVersion" "0.1.0"
VIAddVersionKey /LANG=1033 "LegalCopyright" "OpenNord contributors"

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "OpenNord requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
  ; Read after selecting the 64-bit registry view. Also recognize old installers.
  ReadRegStr $0 HKLM "Software\OpenNord" "InstallDir"
  ${If} $0 == ""
    SetRegView 32
    ReadRegStr $0 HKLM "Software\OpenNord" "InstallDir"
    SetRegView 64
  ${EndIf}
  ${If} $0 != ""
    StrCpy $INSTDIR $0
  ${EndIf}
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Section "OpenNord" SEC_MAIN
  IfFileExists "$INSTDIR\OpenNordService.exe" 0 install_files
  ClearErrors
  ExecWait '"$INSTDIR\OpenNordService.exe" --uninstall' $0
  ${If} ${Errors}
    MessageBox MB_ICONSTOP "The previous OpenNord service executable could not be started. Setup has stopped."
    Abort
  ${EndIf}
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "The previous OpenNord service could not be removed. Error code: $0"
    Abort
  ${EndIf}

  install_files:
  SetOutPath "$INSTDIR"
  File /r "${STAGING}\*.*"
  ; Leave a recovery uninstaller even if service installation fails.
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  ClearErrors
  ExecWait '"$INSTDIR\OpenNordService.exe" --install' $0
  ${If} ${Errors}
    MessageBox MB_ICONSTOP "The OpenNord service executable could not be started. Setup has stopped."
    Abort
  ${EndIf}
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "The OpenNord service could not be installed. Error code: $0"
    Abort
  ${EndIf}
  WriteRegStr HKLM "Software\OpenNord" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord" "DisplayName" "OpenNord"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord" "DisplayVersion" "0.1.0"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord" "Publisher" "OpenNord contributors"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord" "DisplayIcon" "$INSTDIR\OpenNord.exe"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord" "NoRepair" 1
  ; Remove the previous installer's duplicate 32-bit registration after success.
  SetRegView 32
  DeleteRegKey HKLM "Software\OpenNord"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord"
  SetRegView 64
  CreateDirectory "$SMPROGRAMS\OpenNord"
  CreateShortcut "$SMPROGRAMS\OpenNord\OpenNord.lnk" "$INSTDIR\OpenNord.exe"
  CreateShortcut "$DESKTOP\OpenNord.lnk" "$INSTDIR\OpenNord.exe"
SectionEnd

Section "Uninstall"
  ClearErrors
  ExecWait '"$INSTDIR\OpenNordService.exe" --uninstall' $0
  ${If} ${Errors}
    MessageBox MB_ICONSTOP "The OpenNord service executable could not be started. No application files were deleted."
    Abort
  ${EndIf}
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "OpenNord could not stop and remove its service. No application files were deleted. Error code: $0"
    Abort
  ${EndIf}
  Delete "$DESKTOP\OpenNord.lnk"
  Delete "$SMPROGRAMS\OpenNord\OpenNord.lnk"
  RMDir "$SMPROGRAMS\OpenNord"
  ; Delete only files included in this package; preserve unrelated user files.
  !include "${UNINSTALL_INCLUDE}"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"
  DeleteRegKey HKLM "Software\OpenNord"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord"
SectionEnd

Unicode true
RequestExecutionLevel admin
SetCompressor /SOLID lzma

!include "LogicLib.nsh"

!ifndef STAGING
  !define STAGING "..\staging"
!endif

Name "OpenNord"
OutFile "OpenNord-Setup.exe"
InstallDir "$PROGRAMFILES64\OpenNord"
InstallDirRegKey HKLM "Software\OpenNord" "InstallDir"

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "OpenNord" SEC_MAIN
  IfFileExists "$INSTDIR\OpenNordService.exe" 0 install_files
  ExecWait '"$INSTDIR\OpenNordService.exe" --uninstall' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "The previous OpenNord service could not be removed. Error code: $0"
    Abort
  ${EndIf}

  install_files:
  SetOutPath "$INSTDIR"
  File /r "${STAGING}\*.*"
  ExecWait '"$INSTDIR\OpenNordService.exe" --install' $0
  ${If} $0 != 0
    MessageBox MB_ICONSTOP "The OpenNord service could not be installed. Error code: $0"
    Abort
  ${EndIf}
  WriteRegStr HKLM "Software\OpenNord" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord" "DisplayName" "OpenNord"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord" "DisplayVersion" "0.1.0"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord" "Publisher" "OpenNord contributors"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  CreateDirectory "$SMPROGRAMS\OpenNord"
  CreateShortcut "$SMPROGRAMS\OpenNord\OpenNord.lnk" "$INSTDIR\OpenNord.exe"
  CreateShortcut "$DESKTOP\OpenNord.lnk" "$INSTDIR\OpenNord.exe"
SectionEnd

Section "Uninstall"
  ExecWait '"$INSTDIR\OpenNordService.exe" --uninstall'
  Delete "$DESKTOP\OpenNord.lnk"
  RMDir /r "$SMPROGRAMS\OpenNord"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "Software\OpenNord"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenNord"
SectionEnd

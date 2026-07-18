; NSIS installer for the Multi-Scene Stream OBS plugin.
; Installs to the machine-wide OBS plugin search path
; (C:\ProgramData\obs-studio\plugins\<name>) so no admin rights and no
; knowledge of the OBS install directory are required.

Unicode true
!define NAME "obs-multiscene-stream"
!define DISPLAY "Multi-Scene Stream (OBS plugin)"
!define VERSION "1.0.0"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${NAME}"

Name "${DISPLAY} ${VERSION}"
OutFile "${OUTDIR}\obs-multiscene-stream-${VERSION}-setup.exe"
RequestExecutionLevel user
ShowInstDetails show
ShowUninstDetails show

Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Function .onInit
  SetShellVarContext all
  StrCpy $INSTDIR "$APPDATA\obs-studio\plugins\${NAME}"

  MessageBox MB_OKCANCEL|MB_ICONINFORMATION \
    "This will install ${DISPLAY} ${VERSION}.$\r$\n$\r$\nPlease close OBS Studio before continuing." \
    /SD IDOK IDOK +2
  Abort
FunctionEnd

Section "Install"
  SetShellVarContext all

  SetOutPath "$INSTDIR\bin\64bit"
  File "${SRCDIR}\build_x64\RelWithDebInfo\obs-multiscene-stream.dll"

  SetOutPath "$INSTDIR\data\locale"
  File "${SRCDIR}\data\locale\en-US.ini"
  File "${SRCDIR}\data\locale\ru-RU.ini"

  SetOutPath "$INSTDIR"
  File "${SRCDIR}\README.md"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${DISPLAY}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "obs-multiscene-stream contributors"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1

  DetailPrint ""
  DetailPrint "Done. Start OBS and open Docks -> Multi-Scene Stream."
SectionEnd

Section "Uninstall"
  SetShellVarContext all
  Delete "$INSTDIR\bin\64bit\obs-multiscene-stream.dll"
  Delete "$INSTDIR\data\locale\en-US.ini"
  Delete "$INSTDIR\data\locale\ru-RU.ini"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR\bin\64bit"
  RMDir "$INSTDIR\bin"
  RMDir "$INSTDIR\data\locale"
  RMDir "$INSTDIR\data"
  RMDir "$INSTDIR"
  DeleteRegKey HKCU "${UNINST_KEY}"
SectionEnd

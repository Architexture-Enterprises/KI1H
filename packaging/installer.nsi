; installer.nsi — Windows (.exe) installer for the Architexture Rack plugin.
;
; The Windows counterpart to packaging/build-pkg.sh (macOS .pkg). It drops the
; same two payloads into the *current user's* Rack 2 folders — no admin needed:
;
;   plugin  -> %LOCALAPPDATA%\Rack2\plugins-win-x64\Architexture
;   patches -> %LOCALAPPDATA%\Rack2\patches
;
; It is compiled by packaging/build-exe.sh (or `make exe`), which passes the
; staged payload dirs and metadata in via /D defines — do not run makensis on
; this file directly. Required defines:
;
;   SLUG, VERSION, PLUGIN_PAYLOAD (staged Architexture/ dir), OUTFILE
; Optional:
;   PATCH_PAYLOAD (a dir of *.vcv patches; omit to skip the patches component)

Unicode true
!include "MUI2.nsh"

!ifndef SLUG
  !error "SLUG must be defined (pass -DSLUG=...)"
!endif
!ifndef VERSION
  !error "VERSION must be defined (pass -DVERSION=...)"
!endif
!ifndef PLUGIN_PAYLOAD
  !error "PLUGIN_PAYLOAD must be defined (pass -DPLUGIN_PAYLOAD=...)"
!endif
!ifndef OUTFILE
  !error "OUTFILE must be defined (pass -DOUTFILE=...)"
!endif

; --- Per-user install: no elevation, everything under %LOCALAPPDATA% ----------
RequestExecutionLevel user
SetCompressor /SOLID lzma

Name "${SLUG} ${VERSION}"
OutFile "${OUTFILE}"
BrandingText "${SLUG} ${VERSION}"

; INSTDIR is only the anchor for the uninstaller/manifest; the actual plugin and
; patches go to fixed Rack 2 locations under $LOCALAPPDATA (set in the section).
InstallDir "$LOCALAPPDATA\Rack2\plugins-win-x64\${SLUG}"

!define REGKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${SLUG}"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; ============================================================================
Section "Plugin" SecPlugin
  SectionIn RO
  ; Plugin folder -> %LOCALAPPDATA%\Rack2\plugins-win-x64\Architexture
  SetOutPath "$LOCALAPPDATA\Rack2\plugins-win-x64\${SLUG}"
  ; Clear any prior install of this plugin so removed files don't linger.
  RMDir /r "$LOCALAPPDATA\Rack2\plugins-win-x64\${SLUG}"
  SetOutPath "$LOCALAPPDATA\Rack2\plugins-win-x64\${SLUG}"
  File /r "${PLUGIN_PAYLOAD}/*"
SectionEnd

!ifdef PATCH_PAYLOAD
Section "Patches" SecPatches
  ; Patches -> %LOCALAPPDATA%\Rack2\patches (Rack's patch browser location).
  SetOutPath "$LOCALAPPDATA\Rack2\patches"
  File /r "${PATCH_PAYLOAD}/*"
SectionEnd
!endif

Section "-Uninstaller"
  ; Write the uninstaller + an Add/Remove Programs entry under HKCU (per-user).
  SetOutPath "$LOCALAPPDATA\Rack2\plugins-win-x64\${SLUG}"
  WriteUninstaller "$LOCALAPPDATA\Rack2\plugins-win-x64\${SLUG}\Uninstall.exe"
  WriteRegStr HKCU "${REGKEY}" "DisplayName"     "${SLUG} (VCV Rack 2 plugin)"
  WriteRegStr HKCU "${REGKEY}" "DisplayVersion"  "${VERSION}"
  WriteRegStr HKCU "${REGKEY}" "Publisher"       "Architexture"
  WriteRegStr HKCU "${REGKEY}" "UninstallString" "$\"$LOCALAPPDATA\Rack2\plugins-win-x64\${SLUG}\Uninstall.exe$\""
  WriteRegDWORD HKCU "${REGKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${REGKEY}" "NoRepair" 1
SectionEnd

; ============================================================================
Section "Uninstall"
  ; Remove the plugin folder. Patches are user data (they may have edited or
  ; created their own), so we remove only the ones we shipped, by name.
  RMDir /r "$LOCALAPPDATA\Rack2\plugins-win-x64\${SLUG}"
!ifdef PATCH_MANIFEST
  !include "${PATCH_MANIFEST}"
!endif
  DeleteRegKey HKCU "${REGKEY}"
SectionEnd

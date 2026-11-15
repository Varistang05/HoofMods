#pragma once
/* fill these in b4 uploading */

#define HM_VERSION_TEXT L"1.1.6" /* change FILEVERSION and "FileVersion" in app.rc too */

/* "Whats new" popup ppl see once after updating. keep it short + human, one bullet per line.
   small stuff = just "Bug fixes and small improvements", only spell out big changes */
#define HM_WHATS_NEW L"\x2022 HoofMods can now update itself, just press Update now when a new version is out"

/* where ppl get new versions, the update note + popup open this. leave it empty and theres no button */
#define HM_UPDATE_URL L"https://www.patreon.com/cw/Varistang"

/* where Update now downloads the new version from, %ls = the version (twice). the version file needs the zips SHA256 on line 2 */
#define HM_UPDATE_ZIP_URL L"https://github.com/Varistang05/HoofMods/releases/download/v%ls/HoofMods_v%ls.zip"

/* where bug reports go: the bug report popup opens this link + shows the email */
#define HM_REPORT_URL L"https://www.patreon.com/cw/Varistang"
#define HM_REPORT_EMAIL L"varistang05@gmail.com"

/* link to a tiny text file w/ the newest version on the first line, like "1.0.1" (a raw GitHub file works)
   leave it empty and HoofMods never checks for updates or goes online */
#define HM_VERSION_FILE_URL L"https://raw.githubusercontent.com/Varistang05/HoofMods/main/version.txt"

/* 1 = update check is built in (needs HM_VERSION_FILE_URL and winhttp linked, build.bat does that)
   0 = no internet code at all, antivirus programs like that better */
#define HM_CHECK_UPDATES 1

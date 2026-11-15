#pragma once
/* fill these in b4 uploading */

#define HM_VERSION_TEXT L"1.1.4" /* change FILEVERSION and "FileVersion" in app.rc too */

/* where ppl get new versions, the update note + popup open this. leave it empty and theres no button */
#define HM_UPDATE_URL L"https://www.patreon.com/cw/Varistang"

/* where bug reports go: the bug report popup opens this link + shows the email */
#define HM_REPORT_URL L"https://www.patreon.com/cw/Varistang"
#define HM_REPORT_EMAIL L"varistang05@gmail.com"

/* link to a tiny text file w/ the newest version on the first line, like "1.0.1" (a raw GitHub file works)
   leave it empty and HoofMods never checks for updates or goes online */
#define HM_VERSION_FILE_URL L"https://raw.githubusercontent.com/Varistang05/HoofMods/main/version.txt"

/* 1 = update check is built in (needs HM_VERSION_FILE_URL and winhttp linked, build.bat does that)
   0 = no internet code at all, antivirus programs like that better */
#define HM_CHECK_UPDATES 1

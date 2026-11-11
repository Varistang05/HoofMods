#pragma once
/* fill these in b4 uploading */

#define HM_VERSION_TEXT L"1.0.0" /* change FILEVERSION and "FileVersion" in app.rc too */

/* your HoofMods page on Nexus, leave it empty and theres no "get it on Nexus" button */
#define HM_NEXUS_URL L""

/* link to a tiny text file w/ the newest version on the first line, like "1.0.1" (a raw GitHub file works)
   leave it empty and HoofMods never checks for updates or goes online */
#define HM_VERSION_FILE_URL L""

/* 1 = update check is built in (needs HM_VERSION_FILE_URL and winhttp linked, build.bat does that)
   0 = no internet code at all, antivirus programs like that better */
#define HM_CHECK_UPDATES 1

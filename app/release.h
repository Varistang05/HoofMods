#pragma once
/* Fill these in before an upload. */

#define HM_VERSION_TEXT L"1.0.0" /* also update FILEVERSION / "FileVersion" in app.rc */

/* The HoofMods page on Nexus. Empty = no "get it on Nexus" button. */
#define HM_NEXUS_URL L""

/* A small text file whose first line is the newest version, e.g. "1.0.1" (a raw GitHub file works).
   Empty = HoofMods never checks for updates and never goes online. */
#define HM_VERSION_FILE_URL L""

/* 1 = build the update check in (needs HM_VERSION_FILE_URL, and link with -lwinhttp).
   0 = the app has no internet code at all, which antivirus scanners like better. */
#define HM_CHECK_UPDATES 1

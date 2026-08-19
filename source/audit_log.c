#include "audit_log.h"

#if TEST_ANY_AUDIT

#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/iosupport.h>
#include <sys/stat.h>

#include "save.h"   // SAVE_DIR - the log sits beside the save

#define AUDIT_LOG_PATH SAVE_DIR "/audit.log"

static FILE*           logFile;
static devoptab_t      teeTab;      // our copy of the console's device table
static const devoptab_t* realTab;   // the console's own, still doing the drawing

// Every byte the console is asked to print, on its way to the screen.
//
// The real write is what the return value comes from: if the copy to the card
// ever fails, the audit still runs and still prints, it just goes unrecorded.
// The alternative - failing the print because the log failed - would turn a full
// SD card into a game that appears to hang with a blank top screen.
static ssize_t auditTeeWrite(struct _reent* r, void* fd, const char* ptr, size_t len)
{
	if (logFile)
	{
		fwrite(ptr, 1, len, logFile);
		// Flushed per write, not per line and not at the end. An audit that
		// crashes or hangs is exactly the case the harness most needs a log for,
		// and buffered output would be lost precisely then.
		fflush(logFile);
	}
	return realTab->write_r(r, fd, ptr, len);
}

void auditLogBegin(const char* name)
{
	if (logFile) return;

	mkdir("sdmc:/3ds", 0777);
	mkdir(SAVE_DIR, 0777);

	// "wb", so each run starts from empty. A log that appended would make the
	// harness read last week's verdict and call it today's.
	logFile = fopen(AUDIT_LOG_PATH, "wb");
	if (!logFile) return;

	realTab = devoptab_list[STD_OUT];
	if (!realTab || !realTab->write_r)
	{
		// consoleInit has not run, so there is nothing to tee. Leave stdout
		// alone rather than installing a table that would call a null pointer.
		fclose(logFile);
		logFile = NULL;
		return;
	}

	teeTab = *realTab;
	teeTab.write_r = auditTeeWrite;
	devoptab_list[STD_OUT] = &teeTab;

	printf("AUDIT LOG BEGIN %s\n", name ? name : "?");
}

void auditLogEnd(void)
{
	if (!logFile) return;

	printf("AUDIT LOG END\n");

	// Put the console's own table back before the file goes, so anything printed
	// afterwards cannot reach a closed FILE*.
	devoptab_list[STD_OUT] = realTab;
	fclose(logFile);
	logFile = NULL;
}

#endif

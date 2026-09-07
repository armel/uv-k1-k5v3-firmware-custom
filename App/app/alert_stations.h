/* Station-name lookup for decoded ALERT addresses.
 *
 * The table lives in flash and is generated from the MegaNet repository by
 * tools/gen_stations.py into app/alert_stations_gen.h (never edit that file).
 */
#ifndef APP_ALERT_STATIONS_H
#define APP_ALERT_STATIONS_H

#include <stdbool.h>
#include <stdint.h>

enum {
	ALERT_KIND_NONE  = 0,
	ALERT_KIND_RAIN  = 1,
	ALERT_KIND_LEVEL = 2,
	ALERT_KIND_BATT  = 3,
	ALERT_KIND_REP   = 4,
	ALERT_KIND_OTHER = 5,
	ALERT_KIND_CHECK = 6,
};

// Look up an ALERT address. Returns true when the address is in the table;
// *name points at a NUL-terminated upper-case name in flash and *kind is an
// ALERT_KIND_* code. On a miss *name is "" and *kind is ALERT_KIND_NONE.
bool ALERT_LookupStation(uint16_t id, const char **name, uint8_t *kind);

// Short label for a kind code, e.g. "RAIN", "LVL", "BATT".
const char *ALERT_KindLabel(uint8_t kind);

// Number of sites in the table and its source tag (for the about screen).
uint16_t    ALERT_StationCount(void);
const char *ALERT_StationSource(void);

#endif

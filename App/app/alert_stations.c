/* Station-name lookup - binary search over the generated flash table. */
#include "app/alert_stations.h"
#include "app/alert_stations_gen.h"

bool ALERT_LookupStation(uint16_t id, const char **name, uint8_t *kind)
{
	// find the last site whose base_id <= id
	int lo = 0, hi = ALERT_STATIONS_COUNT - 1, best = -1;
	while (lo <= hi) {
		const int mid = (lo + hi) / 2;
		if (gAlertSites[mid].base_id <= id) {
			best = mid;
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}

	if (best >= 0) {
		const AlertSite_t *s = &gAlertSites[best];
		const uint16_t off = id - s->base_id;
		if (off < 5) {
			const uint8_t k = (s->kinds >> (3 * off)) & 7u;
			if (k != ALERT_KIND_NONE) {
				*name = &gAlertNames[s->name_off];
				*kind = k;
				return true;
			}
		}
	}

	*name = "";
	*kind = ALERT_KIND_NONE;
	return false;
}

const char *ALERT_KindLabel(uint8_t kind)
{
	static const char labels[8][5] = { "", "RAIN", "LVL", "BATT", "REP", "SNSR", "CHK", "" };
	return labels[kind & 7u];
}

uint16_t ALERT_StationCount(void)
{
	return ALERT_STATIONS_COUNT;
}

const char *ALERT_StationSource(void)
{
	return ALERT_STATIONS_SOURCE;
}

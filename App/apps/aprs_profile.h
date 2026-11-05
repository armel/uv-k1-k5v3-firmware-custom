#ifndef APPS_APRS_PROFILE_H
#define APPS_APRS_PROFILE_H

/* Mutable APRS TX asset block.  APRS Edit owns the UI; APRS TX only consumes
 * this fixed layout when it builds a frame. Bump the marker whenever a field
 * gains values an older APRS TX would reject: the configuration proxy then
 * finds no target and APRS Edit shows "TX/FW MISMATCH" instead of saving. */
#define APRS_PROFILE_MAGIC        0x37435041u /* "APC7": path 3 = ARISS */
#define APRS_PROFILE_LEN          117u
#define APRS_COMMENT_MAX          89u
#define APRS_POS_DIGITS           13u
#define APRS_CALL_MAX             6u

#define APRS_PROFILE_COMMENT_LEN  4u
#define APRS_PROFILE_ROUTE        5u  /* path in bits 5:4, SSID in bits 3:0 */
#define APRS_PROFILE_SYMBOL       6u
#define APRS_PROFILE_HEMI         7u
#define APRS_PROFILE_POS          8u
#define APRS_PROFILE_COMMENT      21u
#define APRS_PROFILE_CALL_LEN     110u
#define APRS_PROFILE_CALL         111u

/* APRS overlay hand-off protocol.  TX asks the editor to return after saving;
 * the profile itself stays in the configuration proxy, not in the mailbox. */
#define APRS_TX_APP_NAME          "APRS TX"
#define APRS_EDIT_APP_NAME        "APRS Edit"
#define APRS_HANDOFF_EDIT_RETURN  1u

_Static_assert(APRS_PROFILE_POS + APRS_POS_DIGITS == APRS_PROFILE_COMMENT &&
               APRS_PROFILE_COMMENT + APRS_COMMENT_MAX == APRS_PROFILE_CALL_LEN &&
               APRS_PROFILE_CALL + APRS_CALL_MAX == APRS_PROFILE_LEN,
               "APRS profile layout must match its declared length");

#endif

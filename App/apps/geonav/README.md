# GeoNav

GeoNav is a snapshot navigation overlay for the Fusion firmware. It shows the
bearing and approximate surface distance from the current home position to the
last position decoded by a supported radiosonde application. A recent GPS fix
forwarded by K5Viewer is used first; otherwise the APRS profile position is the
home position.

The left half is an azimuth dial, north up: a radial from home ends in an
arrow on the graduation at the target azimuth, and the target sits on it at
scale. The dotted range ring follows a 1-2-5 sequence so the target always
stays inside the dial. The right half shows the distance in large digits,
then `AZ` (whole degrees and 16-point direction), the target altitude, `EL`
(elevation over a flat horizon, for pointing an antenna) and `SCALE`, the
radius of the dotted range ring. The sonde ID is shown in the status line.

## Use

1. Set fallback latitude and longitude in **APRS Edit**, or connect a supported
   GPS to K5Viewer. The stored fields belong to the mutable **APRS TX** profile;
   GeoNav reads the same profile through the configuration proxy and never
   creates a second home-position setting. A live GPS fix is kept in RAM only.
2. Decode a position in **Sonde RS41**, **Sonde M10**, or **Sonde M20**.
3. Press `MENU` in the sonde application to open GeoNav.
4. Press `EXIT` in GeoNav to return to that sonde application.

Only one overlay runs at a time, so GeoNav displays the handed-off snapshot;
the sonde receiver resumes when GeoNav returns to it. GeoNav hands the sonde
message back on `EXIT`, so the sonde application shows its last frame again:
every field its screen shows lies within the 32-byte hand-off.

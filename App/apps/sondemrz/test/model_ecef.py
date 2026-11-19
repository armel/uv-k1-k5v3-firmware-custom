#!/usr/bin/env python3
"""Check the compact integer CORDIC ECEF conversion against WGS84."""
import math

ARC = [round(math.degrees(math.atan(2.0 ** -i)) * 360000.0) for i in range(24)]
EPS_C0 = 139012
EPS_CM = 1424


def cdiv(n, d):
    """C99 signed division, truncated toward zero."""
    return (-1 if (n < 0) != (d < 0) else 1) * (abs(n) // abs(d))


def atan_mag(y, x):
    angle = 0
    if not x and not y:
        return 0, 0
    if x < 0:
        angle = 64800000 if y >= 0 else -64800000
        x, y = -x, -y
    for i, arc in enumerate(ARC):
        old_x = x
        if y > 0:
            x += y >> i
            y -= old_x >> i
            angle += arc
        else:
            x -= y >> i
            y += old_x >> i
            angle -= arc
    # 1 / CORDIC gain, canonical signed-digit approximation. Its error is
    # below 0.25 ppm, or two metres at the Earth's radius.
    mag = ((x >> 1) + (x >> 3) - (x >> 6) - (x >> 9) -
           (x >> 13) - (x >> 14) + (x >> 16) - (x >> 20))
    return angle, mag


def encode(lat_deg, lon_deg, alt_m):
    lat, lon = math.radians(lat_deg), math.radians(lon_deg)
    a = 6378137.0
    e2 = 6.69437999014e-3
    n = a / math.sqrt(1.0 - e2 * math.sin(lat) ** 2)
    return tuple(round(v * 100.0) for v in (
        (n + alt_m) * math.cos(lat) * math.cos(lon),
        (n + alt_m) * math.cos(lat) * math.sin(lon),
        (n * (1.0 - e2) + alt_m) * math.sin(lat),
    ))


def decode(x, y, z):
    lon, h_cm = atan_mag(y, x)
    geocentric, r_cm = atan_mag(z, h_cm)
    radius_m = (r_cm + 50) // 100
    # A common power-of-two position scale is enough for projections and the
    # ellipsoid correction; it avoids five divisions in the overlay.
    xu, yu, zu = (v >> 14 for v in (x, y, z))
    hu = h_cm >> 14
    ru = r_cm >> 14
    azu = abs(zu)
    h2 = hu * hu
    z2 = azu * azu
    r2 = h2 + z2
    q = (azu << 15) // ru
    s2 = (q * q) >> 15
    corr = (s2 * (21493 - ((108 * s2) >> 15))) >> 15
    alt = radius_m - (6378137 - corr)
    scale = EPS_C0 - ((alt * EPS_CM) >> 16)
    delta = azu * hu // ((r2 + (z2 >> 16) * 442) // scale)
    lat = geocentric + (-delta if z < 0 else delta)
    return lat, lon, alt, (xu, yu, zu, hu, ru)


def velocity(x, y, z, vx, vy, vz):
    _, _, _, (xu, yu, zu, hu, ru) = decode(x, y, z)
    vertical = cdiv(xu * vx + yu * vy + zu * vz, ru * 10)
    x4, y4, z4, h4 = xu >> 2, yu >> 2, zu >> 2, hu >> 2
    east = cdiv(x4 * vy - y4 * vx, h4)
    radial = cdiv(x4 * vx + y4 * vy, h4)
    north = cdiv(h4 * vz - z4 * radial, ru >> 2)
    heading, speed = atan_mag(east, north)
    return vertical, (speed + 50) // 100, round((heading % 129600000) / 360000)


def encode_velocity(lat_deg, lon_deg, east, north, up):
    lat, lon = math.radians(lat_deg), math.radians(lon_deg)
    return tuple(round(v * 100.0) for v in (
        -math.sin(lon) * east - math.sin(lat) * math.cos(lon) * north +
        math.cos(lat) * math.cos(lon) * up,
        math.cos(lon) * east - math.sin(lat) * math.sin(lon) * north +
        math.cos(lat) * math.sin(lon) * up,
        math.cos(lat) * north + math.sin(lat) * up,
    ))


def main():
    worst = [0.0, 0.0, 0.0]
    velocity_worst = [0, 0, 0]
    for lat in range(-80, 81, 10):
        for lon in range(-180, 180, 30):
            for alt in (-500, 0, 1200, 30000, 80000):
                xyz = encode(lat, lon, alt)
                got_lat, got_lon, got_alt, _ = decode(*xyz)
                lat_err = abs(got_lat / 360000.0 - lat) * 111200.0
                lon_delta = (got_lon / 360000.0 - lon + 180.0) % 360.0 - 180.0
                lon_err = abs(lon_delta) * 111200.0 * math.cos(math.radians(lat))
                alt_err = abs(got_alt - alt)
                worst = [max(worst[0], lat_err), max(worst[1], lon_err),
                         max(worst[2], alt_err)]
                for east, north, up in ((12.3, 4.7, 5.2), (-3.1, 18.4, -2.7)):
                    got_up, got_speed, got_heading = velocity(
                        *xyz, *encode_velocity(lat, lon, east, north, up))
                    wanted_heading = round(math.degrees(math.atan2(east, north))) % 360
                    heading_error = abs((got_heading - wanted_heading + 180) % 360 - 180)
                    velocity_worst = [max(velocity_worst[0], abs(got_up - round(up * 10))),
                                      max(velocity_worst[1], abs(got_speed - round(math.hypot(east, north)))),
                                      max(velocity_worst[2], heading_error)]
    print("max latitude error:  %.2f m" % worst[0])
    print("max longitude error: %.2f m" % worst[1])
    print("max altitude error:  %.2f m" % worst[2])
    print("max velocity errors: %d dm/s, %d m/s, %d deg" % tuple(velocity_worst))
    if (worst[0] > 15.0 or worst[1] > 15.0 or worst[2] > 3.0 or
            velocity_worst[0] > 1 or velocity_worst[1] > 1 or velocity_worst[2] > 1):
        raise SystemExit("compact ECEF accuracy regression")


if __name__ == "__main__":
    main()

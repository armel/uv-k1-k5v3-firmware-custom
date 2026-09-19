#!/usr/bin/env python3
"""
cat_tester.py — UV-K5 CAT Command Tester (Windows)
====================================================
Testuje wszystkie komendy Kenwood CAT zaimplementowane w wariancie CAT
firmware F4HWN 6.0.0 (uv-k1-k5v3-firmware-custom).

Wymagania:
    pip install pyserial

Użycie:
    python cat_tester.py                    # automatyczne wykrycie COM
    python cat_tester.py COM16              # konkretny port
    python cat_tester.py COM16 38400        # port + prędkość
    python cat_tester.py COM16 --verbose    # z pełnym logiem
    python cat_tester.py --list-ports       # lista portów COM

Komendy CAT testowane:
    FA, FB, FR, TX, RX, MO, MD, PC, OF, CT, DT, SQ, OS, OV, IF,
    SM, S1, SL, SCF, SC, RD
"""

import sys
import time
import argparse
import threading
from typing import Optional

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("\n  Brak modulu 'pyserial'. Zainstaluj:\n")
    print("      pip install pyserial\n")
    sys.exit(1)


# ─────────────────────────────────────────────
# Kolory ANSI
# ─────────────────────────────────────────────
class C:
    RESET  = "\033[0m"
    BOLD   = "\033[1m"
    RED    = "\033[91m"
    GREEN  = "\033[92m"
    YELLOW = "\033[93m"
    CYAN   = "\033[96m"
    GRAY   = "\033[90m"
    WHITE  = "\033[97m"

    @staticmethod
    def ok(s):   return f"{C.GREEN}OK   {s}{C.RESET}"
    @staticmethod
    def fail(s): return f"{C.RED}FAIL {s}{C.RESET}"
    @staticmethod
    def warn(s): return f"{C.YELLOW}WARN {s}{C.RESET}"
    @staticmethod
    def info(s): return f"{C.CYAN}  >> {s}{C.RESET}"
    @staticmethod
    def hdr(s):  return f"{C.BOLD}{C.WHITE}{s}{C.RESET}"


# ─────────────────────────────────────────────
# Komunikacja z radiem — pojedynczy wątek
# ─────────────────────────────────────────────
class CATRadio:
    """
    Prosta, niezawodna klasa do komunikacji z radiem przez CAT.

    Architektura: jeden wątek.  Wysyłanie bajt-po-bajcie (jak Realterm),
    odczyt z pętlą poll na in_waiting — bez konfliktu wątków.
    """

    def __init__(self, port: str, baud: int = 38400, timeout: float = 2.0,
                 verbose: bool = False):
        self.port    = port
        self.baud    = baud
        self.timeout = timeout
        self.verbose = verbose
        self._ser: Optional[serial.Serial] = None

    # ── Połączenie ──────────────────────────
    def connect(self):
        self._ser = serial.Serial(
            port      = self.port,
            baudrate  = self.baud,
            bytesize  = 8,
            parity    = 'N',
            stopbits  = 1,
            timeout   = 0,          # nieblokujący odczyt (polling)
            xonxoff   = False,
            rtscts    = False,
            dsrdtr    = False,
            write_timeout = 2.0,
        )
        time.sleep(0.15)
        self._ser.reset_input_buffer()

    def disconnect(self):
        if self._ser and self._ser.is_open:
            self._ser.close()

    # ── Wysyłanie ───────────────────────────
    def _send_raw(self, cmd: str):
        """Wyślij bajt po bajcie z inter-char delay (emulacja Realtermowego Send)."""
        if self.verbose:
            print(f"  {C.GRAY}>>> {cmd}{C.RESET}")
        for ch in cmd:
            self._ser.write(ch.encode("ascii"))
            self._ser.flush()
            time.sleep(0.003)   # 3ms między bajtami — MCU nadąża przy 38400

    # ── Odczyt ─────────────────────────────
    def _read_until_semicolon(self, timeout: float) -> Optional[str]:
        """Czeka na odpowiedź zakończoną ';'. Poll na in_waiting."""
        buf = ""
        deadline = time.time() + timeout
        while time.time() < deadline:
            n = self._ser.in_waiting
            if n:
                data = self._ser.read(n).decode("ascii", errors="ignore")
                buf += data
                if ";" in buf:
                    # Bierz pierwszą kompletną ramkę
                    idx  = buf.index(";")
                    resp = buf[:idx + 1].strip()
                    if self.verbose:
                        print(f"  {C.GRAY}<<< {resp}{C.RESET}")
                    return resp
            time.sleep(0.005)
        if self.verbose and buf:
            print(f"  {C.GRAY}<<< (timeout, partial: {buf!r}){C.RESET}")
        return None

    # ── Główne API ──────────────────────────
    def send(self, cmd: str, wait_response: bool = True,
             timeout: Optional[float] = None) -> Optional[str]:
        """Wyślij komendę i opcjonalnie poczekaj na odpowiedź."""
        if not cmd.endswith(";"):
            cmd += ";"
        self._ser.reset_input_buffer()
        self._send_raw(cmd)
        time.sleep(0.015)   # czas przetworzenia komendy przez MCU

        if not wait_response:
            time.sleep(0.05)
            return None
        return self._read_until_semicolon(timeout or self.timeout)

    def drain(self, wait: float = 0.5) -> list[str]:
        """Zbiera wszystkie asynchroniczne odpowiedzi przez `wait` sekund."""
        deadline = time.time() + wait
        buf  = ""
        msgs = []
        while time.time() < deadline:
            n = self._ser.in_waiting
            if n:
                buf += self._ser.read(n).decode("ascii", errors="ignore")
                while ";" in buf:
                    idx  = buf.index(";")
                    msg  = buf[:idx + 1].strip()
                    buf  = buf[idx + 1:]
                    if msg:
                        msgs.append(msg)
                        if self.verbose:
                            print(f"  {C.GRAY}[ASYNC] {msg}{C.RESET}")
            time.sleep(0.01)
        return msgs


# ─────────────────────────────────────────────
# Wyniki testów
# ─────────────────────────────────────────────
class Result:
    def __init__(self, name: str, cmd: str, resp: Optional[str],
                 passed: bool, note: str = ""):
        self.name   = name
        self.cmd    = cmd
        self.resp   = resp
        self.passed = passed
        self.note   = note

    def print(self):
        status = C.ok("PASS") if self.passed else C.fail("FAIL")
        resp   = self.resp or "(brak odpowiedzi — timeout)"
        note   = f"  {C.YELLOW}{self.note}{C.RESET}" if self.note else ""
        print(f"  {status}  {C.BOLD}{self.name:<24}{C.RESET}"
              f"  {C.CYAN}{self.cmd:<28}{C.RESET}"
              f"  {C.GRAY}{resp}{C.RESET}{note}")


# ─────────────────────────────────────────────
# Tester
# ─────────────────────────────────────────────
class CATTester:
    def __init__(self, radio: CATRadio):
        self.r       = radio
        self.results: list[Result] = []

    # ── Pomocnicze ──────────────────────────
    def _t(self, name: str, cmd: str, *,
           expect_prefix: Optional[str] = None,
           expect_contains: Optional[str] = None,
           no_response: bool = False,
           timeout: Optional[float] = None,
           note: str = "") -> Result:
        t0   = time.time()
        resp = self.r.send(cmd, wait_response=not no_response, timeout=timeout)
        ms   = (time.time() - t0) * 1000

        if no_response:
            passed = True
            note   = note or f"komenda jednostronna ({ms:.0f}ms)"
        elif resp is None:
            passed = False
            note   = note or "TIMEOUT — brak odpowiedzi z radia"
        elif expect_prefix and not resp.startswith(expect_prefix):
            passed = False
            note   = note or f"oczekiwano prefix '{expect_prefix}', dostano: {resp!r}"
        elif expect_contains and expect_contains not in resp:
            passed = False
            note   = note or f"oczekiwano '{expect_contains}' w: {resp!r}"
        else:
            passed = True
            note   = note or f"{ms:.0f}ms"

        r = Result(name, cmd, resp, passed, note)
        self.results.append(r)
        r.print()
        return r

    def _section(self, title: str):
        print(f"\n  {C.hdr('─' * 58)}")
        print(f"  {C.hdr(title)}")
        print(f"  {C.hdr('─' * 58)}")

    def _get_freq_a(self) -> str:
        """Pobierz bieżącą częstotliwość VFO A jako 11-cyfrowy string."""
        resp = self.r.send("FA;")
        if resp and resp.startswith("FA") and len(resp) >= 13:
            return resp[2:13]
        return "00145000000"

    # ── Testy ────────────────────────────────
    def run_all(self):

        # ────────────────────────────────────
        self._section("1. VFO — Odczyt i zapis częstotliwości")

        self._t("FA odczyt",       "FA;",              expect_prefix="FA")
        self._t("FB odczyt",       "FB;",              expect_prefix="FB")

        self._t("FA ustaw 145MHz", "FA00145000000;",   no_response=True)
        time.sleep(0.12)
        self._t("FA verify 145",   "FA;",              expect_contains="145000000")

        self._t("FB ustaw 433MHz", "FB00433000000;",   no_response=True)
        time.sleep(0.12)
        self._t("FB verify 433",   "FB;",              expect_contains="433000000")

        self._t("FR VFO 0",        "FR0;",             no_response=True)
        time.sleep(0.1)
        self._t("FR VFO 1",        "FR1;",             no_response=True)
        time.sleep(0.1)
        self._t("FR powrot VFO 0", "FR0;",             no_response=True)

        # ────────────────────────────────────
        self._section("2. Modulacja i moc nadajnika")

        self._t("MD odczyt",     "MD;",    expect_prefix="MD")
        self._t("MD ustaw FM",   "MD4;",   no_response=True)
        time.sleep(0.1)
        self._t("MD verify FM",  "MD;",    expect_contains="MD4")
        self._t("MD ustaw AM",   "MD5;",   no_response=True)
        time.sleep(0.1)
        self._t("MD verify AM",  "MD;",    expect_contains="MD5")
        self._t("MD ustaw USB",  "MD2;",   no_response=True)
        time.sleep(0.1)
        self._t("MD verify USB", "MD;",    expect_contains="MD2")
        self._t("MD reset FM",   "MD4;",   no_response=True)

        self._t("PC LOW1 (0)",   "PC0;",   no_response=True)
        self._t("PC MID  (5)",   "PC5;",   no_response=True)
        self._t("PC HIGH (6)",   "PC6;",   no_response=True)

        # ────────────────────────────────────
        self._section("3. Subtony CTCSS / DCS")

        self._t("OF wylacz",     "OF;",    no_response=True)
        self._t("CT 67.0 Hz",    "CT0670;", no_response=True,
                note="CTCSS 67.0 Hz")
        self._t("CT 88.5 Hz",    "CT0885;", no_response=True,
                note="CTCSS 88.5 Hz")
        self._t("DT DCS 023",    "DT023;",  no_response=True,
                note="DCS octal 023")
        self._t("DT DCS 047",    "DT047;",  no_response=True)
        self._t("OF reset",      "OF;",    no_response=True)

        # ────────────────────────────────────
        self._section("4. Squelch")

        for lvl in [0, 3, 5, 9]:
            self._t(f"SQ poziom {lvl}", f"SQ{lvl};", no_response=True)
        self._t("SQ reset do 5", "SQ5;",  no_response=True)

        # ────────────────────────────────────
        self._section("5. Offset nadajnika (duplex)")

        self._t("OS wylacz",       "OS0;",           no_response=True)
        self._t("OS offset +",     "OS1;",           no_response=True)
        self._t("OS offset -",     "OS2;",           no_response=True)
        self._t("OS reset",        "OS0;",           no_response=True)
        self._t("OV 600kHz",       "OV00000600000;", no_response=True,
                note="600000 Hz = 600 kHz")

        # ────────────────────────────────────
        self._section("6. Monitor (squelch override)")

        self._t("MO otworz",     "MO1;", no_response=True)
        time.sleep(0.2)
        self._t("MO zamknij",    "MO0;", no_response=True)

        # ────────────────────────────────────
        self._section("7. IF — Status transceivera")

        self._t("IF status",     "IF;",  expect_prefix="IF")

        # ────────────────────────────────────
        self._section("8. RSSI / S-metr")

        self._t("S1 RSSI biezacy",   "S1;",                   expect_prefix="S1")

        freq = self._get_freq_a()
        self._t("SM biezaca freq",   f"SM{freq};",             expect_prefix="SM")
        self._t("SM 145.000 MHz",    "SM00145000000;",         expect_prefix="SM")

        # ────────────────────────────────────
        self._section("9. Auto-raportowanie RSSI (RD)")

        self._t("RD wlacz",   "RD1;",  no_response=True,
                note="radio wysyla RR...; co ~200ms")

        print(C.info("Czekam 0.8s na asynchroniczne raporty RR..."))
        msgs = self.r.drain(0.8)
        rr   = [m for m in msgs if m.startswith("RR")]
        r    = Result("RD raporty async", "RD1;",
                       rr[0] if rr else None,
                       bool(rr),
                       f"odebrano {len(rr)} raport(ow): {rr[0]}" if rr else
                       "brak raportow RR — wlac ENABLE_CAT i ENABLE_UART")
        self.results.append(r)
        r.print()

        self._t("RD wylacz",  "RD0;",  no_response=True)
        time.sleep(0.3)

        # ────────────────────────────────────
        self._section("10. SCF — asynchroniczny pomiar sprzętowy")

        # Wyczysc bufor, wyslij SCF, poczekaj na SQ...;
        self.r.drain(0.05)
        self._t("SCF 145MHz start",  "SCF00145000000;",
                no_response=True,
                note="asynchroniczny — wynik SQ... przyjdzie po ~250ms")
        print(C.info("Czekam 0.8s na SQ... (SCF response)"))
        msgs = self.r.drain(0.8)
        sq   = [m for m in msgs if m.startswith("SQ")]
        r    = Result("SCF async SQ", "SCF00145000000;",
                       sq[0] if sq else None,
                       bool(sq),
                       f"async: {sq[0]}" if sq else "brak SQ...; w ciagu 0.8s")
        self.results.append(r)
        r.print()

        self.r.drain(0.1)
        self._t("SCF 433MHz",        "SCF00433000000;",
                no_response=True)
        self.r.drain(0.8)

        # ────────────────────────────────────
        self._section("11. SC/SL — sprzętowy skaner listy")

        print(C.info("Laduje 3 kanaly do listy skanera (SL)..."))
        self._t("SL slot 00 145MHz", "SL0000145000000;", expect_contains="SL_OK")
        self._t("SL slot 01 433MHz", "SL0100433500000;", expect_contains="SL_OK")
        self._t("SL slot 02 446MHz", "SL0200446000000;", expect_contains="SL_OK")

        self.r.drain(0.05)
        self._t("SC start x3",       "SC03;",
                no_response=True,
                note="wynik SR...; przyjdzie asynchronicznie po ~1s")
        print(C.info("Czekam 2s na SR... (SC response)"))
        msgs = self.r.drain(2.0)
        sr   = [m for m in msgs if m.startswith("SR")]
        r    = Result("SC async SR", "SC03;",
                       sr[0] if sr else None,
                       bool(sr),
                       f"wynik: {sr[0]}" if sr else "brak SR...; w ciagu 2s")
        self.results.append(r)
        r.print()

    # ── Podsumowanie ────────────────────────
    def summary(self) -> bool:
        total  = len(self.results)
        passed = sum(1 for r in self.results if r.passed)
        failed = total - passed
        pct    = (passed / total * 100) if total else 0

        print(f"\n  {'=' * 58}")
        print(f"  {C.hdr('PODSUMOWANIE TESTOW CAT')}")
        print(f"  {'=' * 58}")
        print(f"  Lacznie  : {C.BOLD}{total}{C.RESET}")
        print(f"  Zaliczone: {C.GREEN}{passed}{C.RESET}")
        cl = C.RED if failed else C.GREEN
        print(f"  Bledy    : {cl}{failed}{C.RESET}")

        if failed:
            print(f"\n  {C.hdr('Nieudane:')}")
            for r in self.results:
                if not r.passed:
                    print(f"    {C.RED}x{C.RESET}  {r.name:<24}  cmd={r.cmd}  {r.note}")

        print(f"\n  Wynik: {C.BOLD}{pct:.0f}%{C.RESET}  ", end="")
        if failed == 0:
            print(C.ok("Wszystkie komendy CAT dzialaja!"))
        elif pct >= 80:
            print(C.warn("Wiekszosc dziala — sprawdz bledy powyzej"))
        else:
            print(C.fail("Wiele bledow — sprawdz polaczenie / firmware"))
        print()
        return failed == 0


# ─────────────────────────────────────────────
# Autodetekcja portu COM
# ─────────────────────────────────────────────
def auto_detect_port() -> Optional[str]:
    ports = list(serial.tools.list_ports.comports())
    for p in ports:
        desc = (p.description or "").upper()
        if any(kw in desc for kw in ("USB", "CH34", "CP210", "FTDI", "SILABS")):
            return p.device
    return ports[0].device if ports else None


# ─────────────────────────────────────────────
# main
# ─────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="UV-K5 CAT Command Tester — F4HWN 6.0.0 CAT variant",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("port",  nargs="?", help="Port COM (np. COM16)")
    parser.add_argument("baud",  nargs="?", type=int, default=38400,
                        help="Predkosc UART (domyslnie 38400)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Pokazuj kazda ramke wyslan/odbioru")
    parser.add_argument("--timeout", "-t", type=float, default=2.0,
                        help="Timeout odpowiedzi w sekundach (domyslnie 2.0)")
    parser.add_argument("--list-ports", "-l", action="store_true",
                        help="Wypisz porty COM i wyjdz")
    args = parser.parse_args()

    # Aktywuj kolory ANSI w Windows cmd / PowerShell
    if sys.platform == "win32":
        import os
        os.system("")

    if args.list_ports:
        ports = list(serial.tools.list_ports.comports())
        if not ports:
            print("Brak portow COM.")
        else:
            print("Dostepne porty COM:")
            for p in ports:
                print(f"  {p.device:8}  {p.description}")
        return

    port = args.port
    if not port:
        port = auto_detect_port()
        if port:
            print(C.info(f"Automatycznie wykryty port: {port}"))
        else:
            print(C.fail("Nie znaleziono portow COM."))
            print("Podaj port recznie: python cat_tester.py COM16")
            sys.exit(1)

    print(f"\n  {'=' * 58}")
    print(f"  {C.hdr('UV-K5 CAT Tester — F4HWN 6.0.0 CAT variant')}")
    print(f"  {'=' * 58}")
    print(f"  Port    : {C.CYAN}{port}{C.RESET}")
    print(f"  Baud    : {C.CYAN}{args.baud}{C.RESET}")
    print(f"  Timeout : {C.CYAN}{args.timeout}s{C.RESET}")
    print(f"  Verbose : {C.CYAN}{args.verbose}{C.RESET}")
    print()

    radio = CATRadio(port, baud=args.baud, timeout=args.timeout,
                     verbose=args.verbose)
    try:
        radio.connect()
        print(C.ok(f"Polaczono z {port} @ {args.baud} baud"))
    except serial.SerialException as e:
        print(C.fail(f"Nie mozna otworzyc {port}: {e}"))
        sys.exit(1)

    tester = CATTester(radio)
    try:
        tester.run_all()
    except KeyboardInterrupt:
        print(f"\n{C.warn('Przerwano (Ctrl+C)')}")
    finally:
        radio.disconnect()

    ok = tester.summary()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()

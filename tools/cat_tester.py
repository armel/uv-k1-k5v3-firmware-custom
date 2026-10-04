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
    FA, FB, FR, TX, RX, MO, MD, PC, OF, CT, DT, SQ, BY, OS, OV, IF,
    SM, S1, SL, SCF, SC, RD, FE, FC, FM, FTA, FTX
"""

import sys
import time
import argparse
import threading
from typing import Optional

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")

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
    def _read_until_semicolon(self, timeout: float,
                              expect_prefix: Optional[str] = None) -> Optional[str]:
        """Czeka na odpowiedź zakończoną ';'. Poll na in_waiting."""
        buf = ""
        deadline = time.time() + timeout
        while time.time() < deadline:
            n = self._ser.in_waiting
            if n:
                data = self._ser.read(n).decode("ascii", errors="ignore")
                buf += data
                while ";" in buf:
                    idx  = buf.index(";")
                    resp = buf[:idx + 1].strip()
                    buf  = buf[idx + 1:]
                    if not resp:
                        continue
                    # Jeśli oczekujemy konkretnej odpowiedzi, a nadeszło asynchroniczne powiadomienie
                    if expect_prefix and not resp.startswith(expect_prefix):
                        if resp.startswith(("BY", "RR", "SR")):
                            if self.verbose:
                                print(f"  {C.GRAY}[ASYNC w tle podczas oczekiwania na {expect_prefix}] {resp}{C.RESET}")
                            continue
                    if self.verbose:
                        print(f"  {C.GRAY}<<< {resp}{C.RESET}")
                    return resp
            time.sleep(0.005)
        if self.verbose and buf:
            print(f"  {C.GRAY}<<< (timeout, partial: {buf!r}){C.RESET}")
        return None

    # ── Główne API ──────────────────────────
    def send(self, cmd: str, wait_response: bool = True,
             timeout: Optional[float] = None,
             expect_prefix: Optional[str] = None) -> Optional[str]:
        """Wyślij komendę i opcjonalnie poczekaj na odpowiedź."""
        if not cmd.endswith(";"):
            cmd += ";"
        self._ser.reset_input_buffer()
        self._send_raw(cmd)
        time.sleep(0.015)   # czas przetworzenia komendy przez MCU

        if not wait_response:
            time.sleep(0.05)
            return None
        return self._read_until_semicolon(timeout or self.timeout, expect_prefix=expect_prefix)

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
        auto_prefix = expect_prefix
        if not auto_prefix and not no_response and len(cmd) >= 2 and cmd[:2].isalpha() and not cmd.startswith("HELP"):
            auto_prefix = cmd[:2]

        t0   = time.time()
        resp = self.r.send(cmd, wait_response=not no_response, timeout=timeout,
                           expect_prefix=auto_prefix)
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
        time.sleep(0.05)
        return r

    def _section(self, title: str):
        print(f"\n  {C.hdr('-' * 58)}")
        print(f"  {C.hdr(title)}")
        print(f"  {C.hdr('-' * 58)}")

    def _get_freq_a(self) -> str:
        """Pobierz bieżącą częstotliwość VFO A jako 11-cyfrowy string."""
        resp = self.r.send("FA;")
        if resp and resp.startswith("FA") and len(resp) >= 13:
            return resp[2:13]
        return "00145000000"

    # ── Testy ────────────────────────────────
    def run_all(self):

        # ────────────────────────────────────
        self._section("0. Identyfikacja transceivera (Kenwood Handshake)")

        self._t("ID odczyt (TS-2000)", "ID;", expect_prefix="ID020")
        self._t("AI odczyt (Auto-Info)", "AI;", expect_prefix="AI0")
        self._t("VR wersja firmware", "VR;", expect_prefix="VR")

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

        self._t("FR odczyt",       "FR;",              expect_prefix="FR")
        self._t("FR VFO 1",        "FR1;",             no_response=True)
        time.sleep(0.1)
        self._t("FR verify 1",     "FR;",              expect_contains="FR1")
        self._t("FR powrot VFO 0", "FR0;",             no_response=True)
        time.sleep(0.1)
        self._t("FR verify 0",     "FR;",              expect_contains="FR0")

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

        self._t("PC odczyt",     "PC;",    expect_prefix="PC")
        self._t("PC LOW1 (0)",   "PC0;",   no_response=True)
        time.sleep(0.1)
        self._t("PC verify 0",   "PC;",    expect_contains="PC0")
        self._t("PC MID  (5)",   "PC5;",   no_response=True)
        time.sleep(0.1)
        self._t("PC verify 5",   "PC;",    expect_contains="PC5")
        self._t("PC HIGH (6)",   "PC6;",   no_response=True)
        time.sleep(0.1)
        self._t("PC verify 6",   "PC;",    expect_contains="PC6")

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

        self._t("SQ odczyt",     "SQ;",    expect_prefix="SQ")
        for lvl in [0, 3, 5, 9]:
            self._t(f"SQ poziom {lvl}", f"SQ{lvl};", no_response=True)
        self._t("SQ reset do 5", "SQ5;",  no_response=True)
        time.sleep(0.1)
        self._t("SQ verify 5",   "SQ;",    expect_contains="SQ5")
        self._t("BY odczyt (squelch status)", "BY;", expect_prefix="BY")

        # ────────────────────────────────────
        self._section("5. Offset nadajnika (duplex)")

        self._t("OS odczyt",       "OS;",            expect_prefix="OS")
        self._t("OS wylacz",       "OS0;",           no_response=True)
        self._t("OS offset +",     "OS1;",           no_response=True)
        time.sleep(0.1)
        self._t("OS verify 1",     "OS;",            expect_contains="OS1")
        self._t("OS offset -",     "OS2;",           no_response=True)
        self._t("OS reset",        "OS0;",           no_response=True)
        time.sleep(0.1)
        self._t("OS verify 0",     "OS;",            expect_contains="OS0")
        self._t("OV odczyt",       "OV;",            expect_prefix="OV")
        self._t("OV 600kHz",       "OV00000600000;", no_response=True,
                note="600000 Hz = 600 kHz")
        time.sleep(0.1)
        self._t("OV verify 600k",  "OV;",            expect_contains="00000600000")

        # ────────────────────────────────────
        self._section("6. Monitor (squelch override)")

        self._t("MO odczyt 0",   "MO;",  expect_contains="MO0")
        self._t("MO otworz",     "MO1;", no_response=True)
        time.sleep(0.1)
        self._t("MO odczyt 1",   "MO;",  expect_contains="MO1")
        time.sleep(0.1)
        self._t("MO zamknij",    "MO0;", no_response=True)
        time.sleep(0.1)
        self._t("MO verify 0",   "MO;",  expect_contains="MO0")

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

        self._t("RD odczyt",  "RD;",   expect_prefix="RD")
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

        # ────────────────────────────────────
        self._section("12. FSK Modem — Konfiguracja i sterowanie (FE, FC, FM)")

        self._t("FE odczyt",          "FE;",              expect_prefix="FE")
        self._t("FE wlacz (audible)",  "FE1;",             expect_contains="FE_OK")
        time.sleep(0.1)
        self._t("FE verify 1",         "FE;",              expect_contains="FE1")
        self._t("FE wlacz (auto-mute)","FE2;",             expect_contains="FE_OK")
        time.sleep(0.1)
        self._t("FE verify 2",         "FE;",              expect_contains="FE2")

        self._t("FC odczyt",          "FC;",              expect_prefix="FC")
        self._t("FC 1200 baud",       "FC1200,ABCD,64;",  expect_contains="FC_OK")
        time.sleep(0.1)
        self._t("FC verify 1200",     "FC;",              expect_contains="FC1200,ABCD,64")
        self._t("FC 2400 baud",       "FC2400,1234,32;",  expect_contains="FC_OK")
        time.sleep(0.1)
        self._t("FC verify 2400",     "FC;",              expect_contains="FC2400,1234,32")

        self._t("FM odczyt",          "FM;",              expect_prefix="FM")
        self._t("FM busy lockout on",  "FM1;",             expect_contains="FM_OK")
        time.sleep(0.1)
        self._t("FM verify 1",         "FM;",              expect_contains="FM1")
        self._t("FM busy lockout off", "FM0;",             expect_contains="FM_OK")
        time.sleep(0.1)
        self._t("FM verify 0",         "FM;",              expect_contains="FM0")

        self._t("FE wylacz",          "FE0;",             expect_contains="FE_OK")
        time.sleep(0.1)
        self._t("FE verify 0",         "FE;",              expect_contains="FE0")

        # ────────────────────────────────────
        self._section("13. Status, Watchdog & Pomoc (RA, QS, HELP, HELPJ)")

        self._t("RA status dump",     "RA;",              expect_prefix="RA")
        self._t("QS status dump",     "QS;",              expect_prefix="QS")
        self._t("HELP command list",  "HELP;",            expect_contains="CAT COMMANDS")
        self._t("HELPJ JSON schema",  "HELPJ;",           expect_contains='"commands"')

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
    parser.add_argument("--send-fsk", help="Wyslij wiadomosc tekstowa FSK (komenda FTA)")
    parser.add_argument("--send-fsk-hex", help="Wyslij dane binarne FSK w formacie HEX (komenda FTX)")
    parser.add_argument("--listen-fsk", action="store_true", help="Nasluchuj i wypisuj odebrane pakiety FSK (FPA / FPX)")
    parser.add_argument("--auto-mute", action="store_true", help="Uzyj trybu auto-mute przy --listen-fsk (FE2)")
    parser.add_argument("--listen-squelch", action="store_true", help="Nasluchuj asynchronicznych zmian squelcha (BY1/BY0)")
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

    if args.send_fsk:
        try:
            print(f"  {C.info(f'Nadawanie tekstu FSK: {args.send_fsk!r}')}")
            resp = radio.send(f"FTA{args.send_fsk};", wait_response=True, timeout=1.5)
            if resp and "?" in resp:
                print(f"  {C.fail('Kanal zajety (Busy Lockout) lub odrzucono!')}")
            else:
                print(f"  {C.ok('Wyslano pakiet FSK.')}")
        finally:
            radio.disconnect()
        return

    if args.send_fsk_hex:
        try:
            print(f"  {C.info(f'Nadawanie danych binarnych HEX: {args.send_fsk_hex}')}")
            resp = radio.send(f"FTX{args.send_fsk_hex};", wait_response=True, timeout=1.5)
            if resp and "?" in resp:
                print(f"  {C.fail('Kanal zajety (Busy Lockout) lub odrzucono!')}")
            else:
                print(f"  {C.ok('Wyslano pakiet binarny FSK.')}")
        finally:
            radio.disconnect()
        return

    if args.listen_squelch:
        print(f"  {C.hdr('Nasluchiwanie zmian squelcha (BY1/BY0). Nacisnij Ctrl+C, aby zakonczyc.')}")
        print()
        try:
            while True:
                msgs = radio.drain(0.05)
                for m in msgs:
                    if m.startswith("BY"):
                        is_open = "1" in m
                        st = "OTWARTY  (Sygnal wykryty / Carrier Detect)" if is_open else "ZAMKNIETY (Cisza / Squelch zamkniety)"
                        col = C.GREEN if is_open else C.GRAY
                        print(f"  {col}[SQUELCH]{C.RESET} {C.BOLD}{st}{C.RESET}  [{m}]")
                    elif m.startswith("RD") or m.startswith("RR"):
                        print(f"  {C.GRAY}[CAT ASYNC] {m}{C.RESET}")
        except KeyboardInterrupt:
            print(f"\n  {C.warn('Zakonczono nasluchiwanie squelcha.')}")
        finally:
            radio.disconnect()
        return

    if args.listen_fsk:
        mode = "2" if args.auto_mute else "1"
        mode_desc = "Auto-Mute" if args.auto_mute else "Audible"
        print(f"  {C.info(f'Wlaczam odbiornik FSK ({mode_desc})...')}")
        radio.send(f"FE{mode};", wait_response=False)
        print(f"  {C.hdr('Nasluchiwanie FSK aktywne. Nacisnij Ctrl+C, aby zakonczyc.')}")
        print()
        try:
            while True:
                msgs = radio.drain(0.1)
                for m in msgs:
                    if m.startswith("FPA"):
                        body = m[3:-1] if m.endswith(";") else m[3:]
                        if "," in body:
                            text, rssi = body.rsplit(",", 1)
                            print(f"  {C.GREEN}[FSK ASCII]{C.RESET} {C.BOLD}{text}{C.RESET} (RSSI: {rssi} dBm)")
                        else:
                            print(f"  {C.GREEN}[FSK ASCII]{C.RESET} {body}")
                    elif m.startswith("FPX"):
                        body = m[3:-1] if m.endswith(";") else m[3:]
                        if "," in body:
                            hex_data, rssi = body.rsplit(",", 1)
                            print(f"  {C.CYAN}[FSK HEX]{C.RESET} {C.BOLD}{hex_data}{C.RESET} (RSSI: {rssi} dBm)")
                        else:
                            print(f"  {C.CYAN}[FSK HEX]{C.RESET} {body}")
                    elif m.startswith("BY"):
                        is_open = "1" in m
                        st = "OTWARTY" if is_open else "ZAMKNIETY"
                        col = C.GREEN if is_open else C.GRAY
                        print(f"  {col}[SQUELCH] {st} ({m}){C.RESET}")
                    elif m.startswith("RD") or m.startswith("RR"):
                        print(f"  {C.GRAY}[CAT ASYNC] {m}{C.RESET}")
        except KeyboardInterrupt:
            print(f"\n  {C.warn('Zatrzymywanie nasluchu FSK...')}")
        finally:
            radio.send("FE0;", wait_response=False)
            print(f"  {C.ok('Odbiornik FSK wylaczony.')}")
            radio.disconnect()
        return

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

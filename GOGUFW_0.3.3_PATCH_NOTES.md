# GOGUFW 0.3.3 patch notes

Base: GOGUFW 0.3.2 ack_timer_overflow_fix_source

Applied changes:
- Messenger RF temporary NARROW lock during FSK TX/RX runtime operations, with WIDE/NARROW restored afterwards.
- Duplicate RX filter using FROM + MSGID before inbox enqueue. Duplicate retries do not create a second inbox entry and do not trigger unread/beep; ACK resend is still allowed.
- Hidden menu cleanup: MsgHop is now in hidden menu with OFF/1/2/3/4/5; MsgDbg first-render value is guarded against stale LOW 1 display.
- SysInf QR rebrand: CODE points to GOGUFW GitHub, WIKI label changed to TELEGRAM and QR points to the Telegram message link.
- T9 multi-tap refinement: 800 ms commit timeout retained; letter modes cycle only letters (ABCABC / abcabc), digits via long press or numeric mode.
- Removed unused print_wrapped_small helper warning.

Validation performed in this environment:
- Docker build could not be run because docker is not installed in the sandbox.
- Host gcc syntax-only check was run on modified files with project include paths and passed with no reported errors for the modified files.

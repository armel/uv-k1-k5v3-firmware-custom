"use strict";

const {
  COMMAND_READ_REGISTER,
  decodeRegisterReply,
  extractFrames,
  makeReadCommand,
  makeWriteCommand,
} = globalThis.BKRegProtocol;

const QUICK_REGISTERS = [0x28, 0x29, 0x31];
const READ_TIMEOUT_MS = 1500;
const webSerialSupported = "serial" in navigator;

const elements = {
  status: document.querySelector("#status"),
  statusText: document.querySelector("#statusText"),
  connect: document.querySelector("#connectButton"),
  disconnect: document.querySelector("#disconnectButton"),
  refresh: document.querySelector("#refreshButton"),
  watch: document.querySelector("#watchButton"),
  watchInterval: document.querySelector("#watchInterval"),
  form: document.querySelector("#registerForm"),
  register: document.querySelector("#registerInput"),
  value: document.querySelector("#valueInput"),
  mask: document.querySelector("#maskInput"),
  read: document.querySelector("#readButton"),
  write: document.querySelector("#writeButton"),
  readResult: document.querySelector("#readResult"),
  log: document.querySelector("#log"),
  clearLog: document.querySelector("#clearLogButton"),
};

let port = null;
let reader = null;
let writer = null;
let readLoopPromise = null;
let receiveBuffer = new Uint8Array();
let commandQueue = Promise.resolve();
let pendingRead = null;
let watching = false;
let closing = false;

function timestamp() {
  return new Date().toLocaleTimeString([], { hour12: false });
}

function log(message, level = "info") {
  const marker = level === "error" ? "ERROR" : level === "write" ? "WRITE" : "INFO ";
  elements.log.textContent += `[${timestamp()}] ${marker}  ${message}\n`;
  elements.log.scrollTop = elements.log.scrollHeight;
}

function hex(value, digits) {
  return `0x${value.toString(16).toUpperCase().padStart(digits, "0")}`;
}

function setStatus(text, kind = "offline") {
  elements.statusText.textContent = text;
  elements.status.className = `status status-${kind}`;
}

function setConnected(connected) {
  if (!connected) {
    stopWatch();
  }
  elements.connect.disabled = connected || !webSerialSupported;
  elements.disconnect.disabled = !connected;
  elements.refresh.disabled = !connected;
  elements.watch.disabled = !connected;
  elements.read.disabled = !connected;
  elements.write.disabled = !connected;
  elements.watchInterval.disabled = !connected;
}

function parseNumber(input, minimum, maximum, label) {
  const text = input.value.trim();
  if (!/^(?:0x[0-9a-f]+|[0-9]+)$/i.test(text)) {
    throw new Error(`${label} must be a decimal number or use the 0x hexadecimal prefix.`);
  }
  const value = Number.parseInt(text, text.toLowerCase().startsWith("0x") ? 16 : 10);
  if (!Number.isInteger(value) || value < minimum || value > maximum) {
    throw new Error(`${label} must be between ${hex(minimum, 2)} and ${hex(maximum, maximum > 0xff ? 4 : 2)}.`);
  }
  return value;
}

function appendBytes(left, right) {
  const joined = new Uint8Array(left.length + right.length);
  joined.set(left);
  joined.set(right, left.length);
  return joined;
}

function rejectPending(error) {
  if (pendingRead !== null) {
    clearTimeout(pendingRead.timer);
    pendingRead.reject(error);
    pendingRead = null;
  }
}

function dispatchFrame(frame) {
  if (frame.commandId !== COMMAND_READ_REGISTER) {
    return;
  }
  const reply = decodeRegisterReply(frame);
  if (reply === null || pendingRead === null || reply.register !== pendingRead.register) {
    return;
  }
  const pending = pendingRead;
  pendingRead = null;
  clearTimeout(pending.timer);
  pending.resolve(reply.value);
}

async function readLoop() {
  reader = port.readable.getReader();
  try {
    while (true) {
      const { value, done } = await reader.read();
      if (done) {
        break;
      }
      if (value === undefined || value.length === 0) {
        continue;
      }
      receiveBuffer = appendBytes(receiveBuffer, value);
      const result = extractFrames(receiveBuffer);
      receiveBuffer = result.remainder;
      result.frames.forEach(dispatchFrame);
    }
  } catch (error) {
    if (!closing) {
      log(`USB read failed: ${error.message}`, "error");
    }
  } finally {
    reader.releaseLock();
    reader = null;
    if (!closing && port !== null) {
      rejectPending(new Error("The radio disconnected."));
      port = null;
      setConnected(false);
      setStatus("Disconnected", "error");
      log("Radio disconnected.", "error");
      if (writer !== null) {
        writer.releaseLock();
        writer = null;
      }
    }
  }
}

function enqueue(task) {
  const result = commandQueue.then(task, task);
  commandQueue = result.catch(() => undefined);
  return result;
}

async function send(packet) {
  if (writer === null) {
    throw new Error("The radio is not connected.");
  }
  await writer.write(packet);
}

function waitForRegister(register) {
  if (pendingRead !== null) {
    throw new Error("Another register read is still pending.");
  }
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      if (pendingRead !== null && pendingRead.register === register) {
        pendingRead = null;
      }
      reject(new Error(
        "No reply. Check that Fusion was built with ENABLE_UART_RW_BK_REGS=ON and close UV Studio/K5Viewer.",
      ));
    }, READ_TIMEOUT_MS);
    pendingRead = { register, resolve, reject, timer };
  });
}

async function readRegisterUnlocked(register) {
  const reply = waitForRegister(register);
  try {
    await send(makeReadCommand(register));
    return await reply;
  } catch (error) {
    rejectPending(error);
    throw error;
  }
}

function readRegister(register) {
  return enqueue(() => readRegisterUnlocked(register));
}

function writeAndVerify(register, requestedValue, mask) {
  return enqueue(async () => {
    const previous = await readRegisterUnlocked(register);
    const next = ((previous & (~mask & 0xffff)) | (requestedValue & mask)) & 0xffff;
    const accepted = window.confirm(
      `Write ${hex(register, 2)}?\n\n` +
      `Current: ${hex(previous, 4)}\n` +
      `New:     ${hex(next, 4)}\n` +
      `Mask:    ${hex(mask, 4)}\n\n` +
      "This changes the live BK4829 state.",
    );
    if (!accepted) {
      return { cancelled: true, previous, next };
    }

    await send(makeWriteCommand(register, next));
    await new Promise((resolve) => setTimeout(resolve, 50));
    const actual = await readRegisterUnlocked(register);
    return { cancelled: false, previous, next, actual };
  });
}

async function connect() {
  if (!webSerialSupported) {
    setStatus("Web Serial unsupported", "error");
    log("This browser does not provide Web Serial. Use desktop Chrome, Edge, or Brave.", "error");
    return;
  }

  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 38400 });
    writer = port.writable.getWriter();
    receiveBuffer = new Uint8Array();
    closing = false;
    readLoopPromise = readLoop();
    setConnected(true);
    setStatus("Connected", "online");
    log("Radio connected at 38400 baud.");
  } catch (error) {
    if (error.name !== "NotFoundError") {
      setStatus("Connection failed", "error");
      log(`Connection failed: ${error.message}`, "error");
    }
    if (port !== null) {
      try {
        await port.close();
      } catch {
        // The port may already be closed.
      }
      port = null;
    }
  }
}

async function disconnect() {
  if (port === null) {
    return;
  }
  closing = true;
  stopWatch();
  rejectPending(new Error("The radio disconnected."));
  try {
    if (reader !== null) {
      await reader.cancel();
    }
    if (readLoopPromise !== null) {
      await readLoopPromise;
    }
    if (writer !== null) {
      writer.releaseLock();
      writer = null;
    }
    await port.close();
  } catch (error) {
    log(`Disconnect warning: ${error.message}`, "error");
  } finally {
    port = null;
    readLoopPromise = null;
    closing = false;
    setConnected(false);
    setStatus("Disconnected");
    log("Radio disconnected.");
  }
}

function updateQuickRegister(register, value) {
  const card = document.querySelector(`[data-register="${hex(register, 2)}"]`);
  if (card !== null) {
    card.querySelector(".register-value").textContent = hex(value, 4);
  }
}

async function refreshQuickRegisters() {
  const readings = [];
  for (const register of QUICK_REGISTERS) {
    if (port === null) {
      return;
    }
    const value = await readRegister(register);
    updateQuickRegister(register, value);
    readings.push(`REG_${hex(register, 2).slice(2)}=${hex(value, 4)}`);
  }
  log(readings.join("  "));
}

async function watchLoop() {
  while (watching && port !== null) {
    try {
      await refreshQuickRegisters();
    } catch (error) {
      log(error.message, "error");
      stopWatch();
      return;
    }
    const interval = Number.parseInt(elements.watchInterval.value, 10);
    await new Promise((resolve) => setTimeout(resolve, interval));
  }
}

function startWatch() {
  if (watching || port === null) {
    return;
  }
  watching = true;
  elements.watch.textContent = "Stop watch";
  elements.refresh.disabled = true;
  elements.watchInterval.disabled = true;
  log("Register watch started.");
  void watchLoop();
}

function stopWatch() {
  if (watching) {
    log("Register watch stopped.");
  }
  watching = false;
  elements.watch.textContent = "Start watch";
  elements.refresh.disabled = port === null;
  elements.watchInterval.disabled = port === null;
}

elements.connect.addEventListener("click", () => void connect());
elements.disconnect.addEventListener("click", () => void disconnect());

elements.refresh.addEventListener("click", async () => {
  elements.refresh.disabled = true;
  try {
    await refreshQuickRegisters();
  } catch (error) {
    log(error.message, "error");
  } finally {
    elements.refresh.disabled = watching || port === null;
  }
});

elements.watch.addEventListener("click", () => {
  if (watching) {
    stopWatch();
  } else {
    startWatch();
  }
});

elements.read.addEventListener("click", async () => {
  elements.read.disabled = true;
  try {
    const register = parseNumber(elements.register, 0, 0x7f, "Register");
    const value = await readRegister(register);
    elements.value.value = hex(value, 4);
    elements.readResult.textContent = `${hex(register, 2)} = ${hex(value, 4)}`;
    updateQuickRegister(register, value);
    log(`Read REG_${hex(register, 2).slice(2)}=${hex(value, 4)}.`);
  } catch (error) {
    elements.readResult.textContent = error.message;
    log(error.message, "error");
  } finally {
    elements.read.disabled = port === null;
  }
});

elements.form.addEventListener("submit", async (event) => {
  event.preventDefault();
  elements.write.disabled = true;
  try {
    const register = parseNumber(elements.register, 0, 0x7f, "Register");
    const value = parseNumber(elements.value, 0, 0xffff, "Value");
    const mask = parseNumber(elements.mask, 0, 0xffff, "Mask");
    const result = await writeAndVerify(register, value, mask);
    if (result.cancelled) {
      log(`Write to REG_${hex(register, 2).slice(2)} cancelled.`);
      return;
    }

    updateQuickRegister(register, result.actual);
    elements.value.value = hex(result.actual, 4);
    if (result.actual !== result.next) {
      const message = `Read-back mismatch: wrote ${hex(result.next, 4)}, read ${hex(result.actual, 4)}. ` +
        "The firmware or transceiver may have rewritten the register.";
      elements.readResult.textContent = message;
      log(message, "error");
      return;
    }

    const message = `Verified REG_${hex(register, 2).slice(2)}: ${hex(result.previous, 4)} → ${hex(result.actual, 4)} ` +
      `(mask ${hex(mask, 4)}).`;
    elements.readResult.textContent = message;
    log(message, "write");
  } catch (error) {
    elements.readResult.textContent = error.message;
    log(error.message, "error");
  } finally {
    elements.write.disabled = port === null;
  }
});

elements.clearLog.addEventListener("click", () => {
  elements.log.textContent = "";
});

if (webSerialSupported) {
  navigator.serial.addEventListener("disconnect", (event) => {
    if (event.port === port && !closing) {
      void disconnect();
    }
  });
  log("Ready. Connect a radio running the test firmware.");
} else {
  setStatus("Web Serial unsupported", "error");
  elements.connect.disabled = true;
  log("Web Serial is unavailable. Use desktop Chrome, Edge, or Brave.", "error");
}

setConnected(false);

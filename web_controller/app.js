/**
 * CYBER-RC // IoT Remote Cockpit Controller Logic
 * Connects directly to NodeMCU ESP8266 Access Point (192.168.4.1)
 */

// STATE
const state = {
  connMode: 'mqtt', // 'mqtt' (Cloud IoT) or 'http' (Local Direct Wi-Fi)
  mqttBroker: 'broker.hivemq.com',
  mqttPort: 8884, // WSS port for broker.hivemq.com
  mqttPath: '/mqtt',
  mqttTopic: 'cyber_rc_car',
  targetIp: window.location.hostname && window.location.hostname !== 'localhost' && window.location.hostname !== '' 
            ? window.location.hostname 
            : '192.168.4.1',
  currentSpeed: 200, // 0 - 255
  activeCommand: 'S',
  activeGear: 'NEUTRAL',
  lightsOn: false,
  hornOn: false,
  pollRate: 100, // ms
  hapticsEnabled: true,
  soundEnabled: true,
  packetCount: 0,
  latency: 0,
  isConnected: false,
  lastSendTime: 0,
  streamTimer: null,
};

// AUDIO SYNTHESIZER (Pure Web Audio API - no external assets)
let audioCtx = null;
function getAudioContext() {
  if (!audioCtx) {
    const AudioContext = window.AudioContext || window.webkitAudioContext;
    if (AudioContext) audioCtx = new AudioContext();
  }
  if (audioCtx && audioCtx.state === 'suspended') {
    audioCtx.resume();
  }
  return audioCtx;
}

function playBeep(freq = 600, duration = 0.06, type = 'sine') {
  if (!state.soundEnabled) return;
  try {
    const ctx = getAudioContext();
    if (!ctx) return;
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.type = type;
    osc.frequency.setValueAtTime(freq, ctx.currentTime);
    gain.gain.setValueAtTime(0.12, ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + duration);
    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.start();
    osc.stop(ctx.currentTime + duration);
  } catch (e) {
    // Ignore audio errors
  }
}

let hornOsc1 = null;
let hornOsc2 = null;
let hornGain = null;

function startHornSound() {
  if (!state.soundEnabled) return;
  try {
    const ctx = getAudioContext();
    if (!ctx) return;
    if (hornOsc1) stopHornSound();
    hornOsc1 = ctx.createOscillator();
    hornOsc2 = ctx.createOscillator();
    hornGain = ctx.createGain();

    hornOsc1.type = 'sawtooth';
    hornOsc2.type = 'sawtooth';
    hornOsc1.frequency.setValueAtTime(440, ctx.currentTime);
    hornOsc2.frequency.setValueAtTime(466.16, ctx.currentTime); // Dissonant dual horn

    hornGain.gain.setValueAtTime(0.2, ctx.currentTime);
    hornOsc1.connect(hornGain);
    hornOsc2.connect(hornGain);
    hornGain.connect(ctx.destination);

    hornOsc1.start();
    hornOsc2.start();
  } catch (e) {}
}

function stopHornSound() {
  try {
    if (hornGain && audioCtx) {
      hornGain.gain.exponentialRampToValueAtTime(0.0001, audioCtx.currentTime + 0.05);
      setTimeout(() => {
        if (hornOsc1) { hornOsc1.stop(); hornOsc1.disconnect(); hornOsc1 = null; }
        if (hornOsc2) { hornOsc2.stop(); hornOsc2.disconnect(); hornOsc2 = null; }
      }, 50);
    }
  } catch (e) {}
}

function triggerHaptic(duration = 25) {
  if (state.hapticsEnabled && navigator.vibrate) {
    navigator.vibrate(duration);
  }
}

// DOM ELEMENTS
const dialBar = document.getElementById('dial-bar');
const speedDisplay = document.getElementById('speed-display');
const activeGear = document.getElementById('active-gear');
const pingVal = document.getElementById('ping-val');
const connBadge = document.getElementById('conn-badge');
const connText = document.getElementById('conn-text');
const packetCountEl = document.getElementById('packet-count');

const btnLights = document.getElementById('btn-lights');
const btnHorn = document.getElementById('btn-horn');
const btnBrake = document.getElementById('btn-brake');

const modeDpadBtn = document.getElementById('mode-dpad-btn');
const modeJoyBtn = document.getElementById('mode-joy-btn');
const dpadZone = document.getElementById('dpad-zone');
const joystickZone = document.getElementById('joystick-zone');

// JOYSTICK ELEMENTS
const joystickBase = document.getElementById('joystick-base');
const joystickKnob = document.getElementById('joystick-knob');
const joyAngleEl = document.getElementById('joy-angle');
const joyDistEl = document.getElementById('joy-dist');

// SETTINGS MODAL
const btnSettings = document.getElementById('btn-settings');
const settingsModal = document.getElementById('settings-modal');
const btnCloseModal = document.getElementById('btn-close-modal');
const btnSaveSettings = document.getElementById('btn-save-settings');
const inputTargetIp = document.getElementById('input-target-ip');
const inputPollRate = document.getElementById('input-poll-rate');
const checkHaptic = document.getElementById('check-haptic');
const checkSound = document.getElementById('check-sound');

// SPEED GAUGE UPDATE
function updateGauge(speedValue, gearName) {
  const percentage = Math.round((speedValue / 255) * 100);
  speedDisplay.textContent = percentage;
  
  // Total arc length is 236
  const offset = 236 - (236 * (percentage / 100));
  dialBar.style.strokeDashoffset = Math.max(0, offset);

  if (gearName) {
    state.activeGear = gearName;
    activeGear.textContent = gearName;
  }
}

// MQTT CLIENT INSTANCE & LIFECYCLE
let mqttClient = null;
let mqttConnected = false;

function initMQTT() {
  if (typeof Paho === 'undefined') {
    console.warn('[MQTT]: Paho library not yet loaded, retrying in 500ms...');
    setTimeout(initMQTT, 500);
    return;
  }

  if (mqttClient) {
    try { mqttClient.disconnect(); } catch (e) {}
  }

  const clientId = 'cyber_web_' + Math.random().toString(16).substr(2, 8);
  console.log(`[MQTT]: Connecting to wss://${state.mqttBroker}:${state.mqttPort}${state.mqttPath} as ${clientId}...`);
  connText.textContent = 'CONNECTING...';
  connBadge.style.color = '#ffb300';

  mqttClient = new Paho.MQTT.Client(state.mqttBroker, Number(state.mqttPort), state.mqttPath, clientId);

  mqttClient.onConnectionLost = (responseObject) => {
    mqttConnected = false;
    state.isConnected = false;
    connText.textContent = 'MQTT OFFLINE';
    connBadge.style.color = '#ff3366';
    pingVal.textContent = 'DISC';
    console.warn('[MQTT]: Connection lost:', responseObject.errorMessage);
    if (state.connMode === 'mqtt') {
      setTimeout(initMQTT, 3000);
    }
  };

  mqttClient.onMessageArrived = (message) => {
    const topic = message.destinationName;
    const payload = message.payloadString;
    console.log(`[MQTT RX] ${topic}: ${payload}`);

    if (topic.endsWith('/status')) {
      connText.textContent = 'CAR ONLINE';
      connBadge.style.color = '#00ff88';
      state.isConnected = true;
      if (state.lastSendTime > 0) {
        const roundTrip = Math.max(12, Math.round(Date.now() - state.lastSendTime));
        pingVal.textContent = `${roundTrip} ms`;
      }
    }
  };

  mqttClient.connect({
    useSSL: true,
    timeout: 6,
    keepAliveInterval: 30,
    cleanSession: true,
    onSuccess: () => {
      mqttConnected = true;
      state.isConnected = true;
      connText.textContent = 'CLOUD READY';
      connBadge.style.color = '#00f2fe';
      pingVal.textContent = '0 ms';
      console.log('[MQTT]: Connected successfully to HiveMQ Cloud!');

      // Subscribe to telemetry/status topic from car
      const statusTopic = `${state.mqttTopic}/status`;
      mqttClient.subscribe(statusTopic);
      console.log(`[MQTT]: Subscribed to ${statusTopic}`);
    },
    onFailure: (err) => {
      mqttConnected = false;
      state.isConnected = false;
      connText.textContent = 'BROKER ERR';
      connBadge.style.color = '#ff3366';
      pingVal.textContent = 'ERR';
      console.error('[MQTT]: Connect failed:', err);
      if (state.connMode === 'mqtt') {
        setTimeout(initMQTT, 4000);
      }
    }
  });
}

// NETWORK TRANSMITTER
let inFlightRequest = false;

function sendCarCommand(cmd, speed = state.currentSpeed, extra = {}) {
  state.activeCommand = cmd;
  state.packetCount++;
  packetCountEl.textContent = state.packetCount;

  // --- 1. MQTT CLOUD MODE (Primary IoT Mode) ---
  if (state.connMode === 'mqtt') {
    let payload = `${cmd}:${speed}`;
    if (extra.x !== undefined && extra.y !== undefined) {
      payload = `V:${extra.x}:${extra.y}:${speed}`;
    } else if (extra.light !== undefined) {
      payload = `LIGHT:${extra.light}`;
    } else if (extra.horn !== undefined) {
      payload = `HORN:${extra.horn}`;
    }

    if (mqttClient && mqttConnected) {
      try {
        const message = new Paho.MQTT.Message(payload);
        message.destinationName = `${state.mqttTopic}/cmd`;
        message.qos = 0; // Ultra low latency
        state.lastSendTime = Date.now();
        mqttClient.send(message);
        
        connText.textContent = (cmd === 'S') ? 'CLOUD IDLE' : 'DRIVING';
        connBadge.style.color = (cmd === 'S') ? '#00f2fe' : '#00ff88';
      } catch (e) {
        console.error('[MQTT]: Error sending message:', e);
      }
    } else {
      connText.textContent = 'RECONNECTING';
      connBadge.style.color = '#ffb300';
    }
    return;
  }

  // --- 2. HTTP FALLBACK (Direct Local Wi-Fi AP) ---
  let url = `http://${state.targetIp}/cmd?dir=${cmd}&spd=${speed}`;
  if (extra.x !== undefined && extra.y !== undefined) {
    url = `http://${state.targetIp}/vector?x=${extra.x}&y=${extra.y}&spd=${speed}`;
  } else if (extra.light !== undefined) {
    url = `http://${state.targetIp}/light?val=${extra.light}`;
  } else if (extra.horn !== undefined) {
    url = `http://${state.targetIp}/horn?val=${extra.horn}`;
  }

  if (inFlightRequest) return;
  inFlightRequest = true;

  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), 600);

  const startTime = performance.now();
  fetch(url, {
    method: 'GET',
    mode: 'no-cors',
    signal: controller.signal
  }).then(() => {
    clearTimeout(timeoutId);
    const elapsed = Math.round(performance.now() - startTime);
    state.latency = elapsed;
    pingVal.textContent = `${elapsed} ms`;
    connText.textContent = 'ONLINE';
    connBadge.style.color = '#00ff88';
    state.isConnected = true;
  }).catch(() => {
    clearTimeout(timeoutId);
    if (window.location.protocol === 'file:' || window.location.hostname === 'localhost') {
      pingVal.textContent = 'SIM';
      connText.textContent = 'TEST MODE';
      connBadge.style.color = '#ffb300';
    } else {
      connText.textContent = 'OFFLINE';
      connBadge.style.color = '#ff3366';
      state.isConnected = false;
    }
  }).finally(() => {
    inFlightRequest = false;
  });
}

// STOP COMMAND
function emergencyStop() {
  triggerHaptic(60);
  playBeep(300, 0.1, 'sawtooth');
  updateGauge(0, 'STOPPED');
  sendCarCommand('S', 0);
  document.querySelectorAll('.ctrl-btn').forEach(b => b.classList.remove('pressed'));
}

// SPEED PRESET BUTTONS
document.querySelectorAll('.preset-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    document.querySelectorAll('.preset-btn').forEach(b => b.classList.remove('active'));
    btn.classList.add('active');
    state.currentSpeed = parseInt(btn.dataset.speed, 10);
    triggerHaptic(20);
    playBeep(700, 0.05);

    if (state.activeCommand !== 'S') {
      updateGauge(state.currentSpeed);
      sendCarCommand(state.activeCommand, state.currentSpeed);
    }
  });
});

// UTILITY BUTTONS
btnLights.addEventListener('click', () => {
  state.lightsOn = !state.lightsOn;
  btnLights.classList.toggle('active', state.lightsOn);
  triggerHaptic(30);
  playBeep(880, 0.05);
  sendCarCommand('LIGHT', state.lightsOn ? 1 : 0, { light: state.lightsOn ? 1 : 0 });
});

// HORN BUTTON (Hold to sound)
function onHornPress(e) {
  e.preventDefault();
  btnHorn.classList.add('active');
  startHornSound();
  triggerHaptic(40);
  sendCarCommand('HORN', 1, { horn: 1 });
}

function onHornRelease(e) {
  e.preventDefault();
  btnHorn.classList.remove('active');
  stopHornSound();
  sendCarCommand('HORN', 0, { horn: 0 });
}

btnHorn.addEventListener('pointerdown', onHornPress);
btnHorn.addEventListener('pointerup', onHornRelease);
btnHorn.addEventListener('pointerleave', onHornRelease);
btnHorn.addEventListener('pointercancel', onHornRelease);

btnBrake.addEventListener('click', emergencyStop);

// D-PAD CONTROLS
const dpadCommandMap = {
  'btn-f': { cmd: 'F', gear: 'FORWARD' },
  'btn-b': { cmd: 'B', gear: 'REVERSE' },
  'btn-l': { cmd: 'L', gear: 'TURN LEFT' },
  'btn-r': { cmd: 'R', gear: 'TURN RIGHT' },
  'btn-fl': { cmd: 'G', gear: 'FWD-LEFT' },
  'btn-fr': { cmd: 'I', gear: 'FWD-RIGHT' },
  'btn-bl': { cmd: 'H', gear: 'REV-LEFT' },
  'btn-br': { cmd: 'J', gear: 'REV-RIGHT' },
  'btn-stop': { cmd: 'S', gear: 'STOP' },
  'btn-spin-l': { cmd: 'SL', gear: '360° LEFT' },
  'btn-spin-r': { cmd: 'SR', gear: '360° RIGHT' },
};

function handleDpadPress(element) {
  const mapping = dpadCommandMap[element.id];
  if (!mapping) return;

  element.classList.add('pressed');
  triggerHaptic(30);
  playBeep(520, 0.05);

  if (mapping.cmd === 'S') {
    emergencyStop();
  } else {
    updateGauge(state.currentSpeed, mapping.gear);
    sendCarCommand(mapping.cmd, state.currentSpeed);

    // Continuous keep-alive stream while holding
    clearInterval(state.streamTimer);
    state.streamTimer = setInterval(() => {
      sendCarCommand(mapping.cmd, state.currentSpeed);
    }, state.pollRate);
  }
}

function handleDpadRelease(element) {
  element.classList.remove('pressed');
  clearInterval(state.streamTimer);
  state.streamTimer = null;
  emergencyStop();
}

document.querySelectorAll('.ctrl-btn, .spin-btn').forEach(btn => {
  btn.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    handleDpadPress(btn);
  });
  btn.addEventListener('pointerup', (e) => {
    e.preventDefault();
    handleDpadRelease(btn);
  });
  btn.addEventListener('pointerleave', (e) => {
    e.preventDefault();
    if (btn.classList.contains('pressed')) {
      handleDpadRelease(btn);
    }
  });
  btn.addEventListener('pointercancel', (e) => {
    e.preventDefault();
    if (btn.classList.contains('pressed')) {
      handleDpadRelease(btn);
    }
  });
});

// KEYBOARD CONTROLS FOR DESKTOP / LAPTOP TESTING
const keyMap = {
  'ArrowUp': 'btn-f', 'w': 'btn-f', 'W': 'btn-f',
  'ArrowDown': 'btn-b', 's': 'btn-b', 'S': 'btn-b',
  'ArrowLeft': 'btn-l', 'a': 'btn-l', 'A': 'btn-l',
  'ArrowRight': 'btn-r', 'd': 'btn-r', 'D': 'btn-r',
  ' ': 'btn-stop', 'Escape': 'btn-stop'
};

const activeKeys = new Set();
window.addEventListener('keydown', (e) => {
  if (e.repeat) return;
  const targetId = keyMap[e.key];
  if (targetId) {
    e.preventDefault();
    activeKeys.add(e.key);
    const btn = document.getElementById(targetId);
    if (btn) handleDpadPress(btn);
  }
});

window.addEventListener('keyup', (e) => {
  const targetId = keyMap[e.key];
  if (targetId) {
    e.preventDefault();
    activeKeys.delete(e.key);
    const btn = document.getElementById(targetId);
    if (btn) handleDpadRelease(btn);
  }
});

// MODE SWITCHER (D-Pad vs Joystick)
modeDpadBtn.addEventListener('click', () => {
  modeDpadBtn.classList.add('active');
  modeJoyBtn.classList.remove('active');
  dpadZone.classList.remove('hidden');
  joystickZone.classList.add('hidden');
  triggerHaptic(20);
});

modeJoyBtn.addEventListener('click', () => {
  modeJoyBtn.classList.add('active');
  modeDpadBtn.classList.remove('active');
  joystickZone.classList.remove('hidden');
  dpadZone.classList.add('hidden');
  triggerHaptic(20);
});

// VIRTUAL JOYSTICK LOGIC
let joyActive = false;
let joyCenter = { x: 0, y: 0 };
const maxRadius = 60; // Max distance knob can travel from center

function initJoystickCenter() {
  const rect = joystickBase.getBoundingClientRect();
  joyCenter = {
    x: rect.left + rect.width / 2,
    y: rect.top + rect.height / 2
  };
}

function handleJoystickMove(touchX, touchY) {
  let dx = touchX - joyCenter.x;
  let dy = touchY - joyCenter.y;
  let distance = Math.hypot(dx, dy);

  if (distance > maxRadius) {
    dx = (dx / distance) * maxRadius;
    dy = (dy / distance) * maxRadius;
    distance = maxRadius;
  }

  joystickKnob.style.transform = `translate(${dx}px, ${dy}px)`;

  // Normalized vector (-100 to 100)
  const normX = Math.round((dx / maxRadius) * 100);
  const normY = Math.round((-dy / maxRadius) * 100); // Inverted so UP is positive

  const angleDeg = Math.round((Math.atan2(-dy, dx) * 180 / Math.PI + 360) % 360);
  const powerPercent = Math.round((distance / maxRadius) * 100);

  joyAngleEl.textContent = `${angleDeg}°`;
  joyDistEl.textContent = `${powerPercent}%`;

  const joySpeed = Math.round((powerPercent / 100) * state.currentSpeed);
  updateGauge(joySpeed, getGearFromVector(normX, normY));

  // Send vector to NodeMCU
  sendCarCommand('V', joySpeed, { x: normX, y: normY });
}

function getGearFromVector(x, y) {
  if (Math.abs(x) < 20 && Math.abs(y) < 20) return 'CENTER';
  if (y >= 20) {
    if (x > 35) return 'FWD-RIGHT';
    if (x < -35) return 'FWD-LEFT';
    return 'FORWARD';
  } else if (y <= -20) {
    if (x > 35) return 'REV-RIGHT';
    if (x < -35) return 'REV-LEFT';
    return 'REVERSE';
  } else {
    return x > 0 ? 'TURN RIGHT' : 'TURN LEFT';
  }
}

function handleJoystickEnd() {
  joyActive = false;
  joystickKnob.style.transform = `translate(0px, 0px)`;
  joyAngleEl.textContent = '0°';
  joyDistEl.textContent = '0%';
  emergencyStop();
}

joystickBase.addEventListener('pointerdown', (e) => {
  e.preventDefault();
  joyActive = true;
  initJoystickCenter();
  handleJoystickMove(e.clientX, e.clientY);
});

window.addEventListener('pointermove', (e) => {
  if (!joyActive) return;
  e.preventDefault();
  handleJoystickMove(e.clientX, e.clientY);
});

window.addEventListener('pointerup', (e) => {
  if (joyActive) handleJoystickEnd();
});
window.addEventListener('pointercancel', (e) => {
  if (joyActive) handleJoystickEnd();
});

// SETTINGS MODAL INTERACTIONS
const selectConnMode = document.getElementById('select-conn-mode');
const inputMqttTopic = document.getElementById('input-mqtt-topic');
const inputMqttBroker = document.getElementById('input-mqtt-broker');
const groupMqttTopic = document.getElementById('group-mqtt-topic');
const groupMqttBroker = document.getElementById('group-mqtt-broker');
const groupHttpIp = document.getElementById('group-http-ip');

function updateModalVisibility() {
  const isMqtt = (selectConnMode.value === 'mqtt');
  if (groupMqttTopic) groupMqttTopic.style.display = isMqtt ? 'block' : 'none';
  if (groupMqttBroker) groupMqttBroker.style.display = isMqtt ? 'block' : 'none';
  if (groupHttpIp) groupHttpIp.style.display = isMqtt ? 'none' : 'block';
}

if (selectConnMode) {
  selectConnMode.addEventListener('change', updateModalVisibility);
}

btnSettings.addEventListener('click', () => {
  if (selectConnMode) selectConnMode.value = state.connMode;
  if (inputMqttTopic) inputMqttTopic.value = state.mqttTopic;
  if (inputMqttBroker) inputMqttBroker.value = state.mqttBroker;
  inputTargetIp.value = state.targetIp;
  inputPollRate.value = state.pollRate;
  checkHaptic.checked = state.hapticsEnabled;
  checkSound.checked = state.soundEnabled;
  updateModalVisibility();
  settingsModal.classList.remove('hidden');
});

btnCloseModal.addEventListener('click', () => {
  settingsModal.classList.add('hidden');
});

btnSaveSettings.addEventListener('click', () => {
  const oldMode = state.connMode;
  if (selectConnMode) state.connMode = selectConnMode.value;
  if (inputMqttTopic) state.mqttTopic = inputMqttTopic.value.trim() || 'cyber_rc_car';
  if (inputMqttBroker) state.mqttBroker = inputMqttBroker.value.trim() || 'broker.hivemq.com';
  state.targetIp = inputTargetIp.value.trim() || '192.168.4.1';
  state.pollRate = parseInt(inputPollRate.value, 10);
  state.hapticsEnabled = checkHaptic.checked;
  state.soundEnabled = checkSound.checked;
  settingsModal.classList.add('hidden');
  triggerHaptic(40);
  playBeep(800, 0.08);

  // Switch connection mode if needed
  if (state.connMode === 'mqtt') {
    initMQTT();
  } else if (mqttClient) {
    try { mqttClient.disconnect(); } catch (e) {}
    connText.textContent = 'HTTP MODE';
    connBadge.style.color = '#00f2fe';
  }
});

// PERIODIC HTTP HEARTBEAT / PING (Only active in HTTP mode)
setInterval(async () => {
  if (state.connMode === 'http' && state.activeCommand === 'S' && !inFlightRequest) {
    try {
      const start = performance.now();
      await fetch(`http://${state.targetIp}/ping`, { mode: 'no-cors', cache: 'no-store' });
      const ms = Math.round(performance.now() - start);
      pingVal.textContent = `${ms} ms`;
      connText.textContent = 'CONNECTED';
      connBadge.style.color = '#00ff88';
    } catch (e) {
      if (window.location.protocol === 'file:' || window.location.hostname === 'localhost') {
        pingVal.textContent = 'SIM';
        connText.textContent = 'TEST MODE';
        connBadge.style.color = '#ffb300';
      } else {
        connText.textContent = 'OFFLINE';
        connBadge.style.color = '#ff3366';
      }
    }
  }
}, 2500);

// INITIAL LOAD
updateGauge(0, 'STANDBY');
if (state.connMode === 'mqtt') {
  initMQTT();
}
console.log('CYBER-RC IoT Cockpit Initialized. Mode:', state.connMode, 'Topic:', state.mqttTopic);

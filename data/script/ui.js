let toastTimer;
const profileNamePattern = /^[A-Za-z0-9_-]+$/;
const maxTokenNameLength = 64;

function showToast(message, isError = false) {
  const toast = document.getElementById('toast');
  toast.textContent = message;
  toast.classList.toggle('error', isError);
  toast.classList.add('visible');

  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => {
    toast.classList.remove('visible');
  }, 3200);
}

function setStatus(text, statusClass) {
  const state = document.getElementById('state');
  state.lastChild.textContent = ` ${text}`;
  state.className = `status-badge ${statusClass}`;
}

function handleError(error) {
  setStatus('API error', 'status-error');
  showToast(error.message || 'Request failed', true);
}

function closeMacroHelp() {
  const modal = document.getElementById('macroHelpModal');
  modal.classList.remove('open');
  modal.setAttribute('aria-hidden', 'true');
}

function closeTtyHelp() {
  const modal = document.getElementById('ttyHelpModal');
  modal.classList.remove('open');
  modal.setAttribute('aria-hidden', 'true');
}

function setValueIfIdle(id, value) {
  const element = document.getElementById(id);
  if (document.activeElement !== element) {
    element.value = value || '';
  }
}

function sameOptions(select, names) {
  return select.options.length === names.length && [...select.options].every((opt, index) => opt.value === names[index]);
}

function setFieldError(id, message) {
  const field = document.getElementById(id);
  field.setCustomValidity(message || '');
  field.classList.toggle('field-invalid', Boolean(message));
  return !message;
}

function assertFieldValid(id, message) {
  const field = document.getElementById(id);
  if (message) {
    setFieldError(id, message);
    field.reportValidity();
    throw new Error(message);
  }
  setFieldError(id, '');
}

function validateTokenNameValue(value, label, required = true) {
  const clean = (value || '').trim();
  if (!clean) {
    return required ? `${label} is required` : '';
  }
  if (!profileNamePattern.test(clean)) {
    return `${label} may contain only A-Z, a-z, 0-9, "_" and "-"; spaces are not allowed`;
  }
  if (clean.length > maxTokenNameLength) {
    return `${label} must not exceed ${maxTokenNameLength} characters`;
  }
  return '';
}

function assertTokenNameField(id, label, required = true) {
  const field = document.getElementById(id);
  field.value = field.value.trim();
  assertFieldValid(id, validateTokenNameValue(field.value, label, required));
}

function validateOptionalPassword(id, label) {
  const value = document.getElementById(id).value;
  if (value && (value.length < 8 || value.length > 63)) {
    return `${label} must be 8 to 63 characters, or empty`;
  }
  return '';
}

function assertPositiveIntegerField(id, label) {
  const value = document.getElementById(id).value.trim();
  if (!/^[1-9]\d*$/.test(value)) {
    assertFieldValid(id, `${label} must be a positive integer`);
  }
  assertFieldValid(id, '');
}

function showPage(pageName) {
  const pages = ['control', 'tty', 'gpio', 'wifi', 'ble', 'settings', 'observability', 'update'];
  const activePage = pages.includes(pageName) ? pageName : 'control';
  pages.forEach((page) => {
    document.getElementById(`${page}Page`).classList.toggle('active', page === activePage);
    document.getElementById(`${page}PageBtn`).classList.toggle('active', page === activePage);
  });
}

function formatUptime(ms) {
  const totalSeconds = Math.floor((Number(ms) || 0) / 1000);
  const days = Math.floor(totalSeconds / 86400);
  const hours = Math.floor((totalSeconds % 86400) / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  const seconds = totalSeconds % 60;
  return `${days}d ${hours}h ${minutes}m ${seconds}s`;
}

function formatBytes(value) {
  const bytes = Number(value);
  if (!Number.isFinite(bytes) || bytes < 0) return '-';
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
}

function formatUsedBytes(used, total) {
  const usedBytes = Number(used);
  const totalBytes = Number(total);
  if (!Number.isFinite(usedBytes) || !Number.isFinite(totalBytes) || totalBytes <= 0) return '-';
  const percent = Math.round((usedBytes / totalBytes) * 100);
  return `${formatBytes(usedBytes)} / ${formatBytes(totalBytes)} (${percent}%)`;
}

function formatBoolState(value, yes, no) {
  return value ? yes : no;
}

async function refreshObservability() {
  const response = await getJson('api/observability');
  const data = response.data || {};
  document.getElementById('obsFirmwareVersion').textContent = data.firmwareVersion || '-';
  document.getElementById('obsBuildTime').textContent = `${data.buildDate || '-'} ${data.buildTime || ''}`.trim();
  document.getElementById('obsUptime').textContent = formatUptime(data.uptimeMs);
  document.getElementById('obsChip').textContent = data.chipModel
    ? `${data.chipModel} rev ${data.chipRevision ?? '-'}`
    : '-';
  document.getElementById('obsCpu').textContent = data.cpuFreqMhz ? `${data.cpuFreqMhz} MHz` : '-';
  document.getElementById('obsSdk').textContent = data.sdkVersion || '-';
  document.getElementById('obsHeapFree').textContent = formatUsedBytes(data.heapFreeBytes, data.heapTotalBytes);
  document.getElementById('obsHeapMin').textContent = formatBytes(data.heapMinFreeBytes);
  document.getElementById('obsHeapMaxAlloc').textContent = formatBytes(data.heapMaxAllocBytes);
  document.getElementById('obsSketch').textContent = formatBytes(data.sketchSizeBytes);
  document.getElementById('obsOtaFree').textContent = formatBytes(data.freeSketchSpaceBytes);
  document.getElementById('obsFlash').textContent = formatBytes(data.flashSizeBytes);
  document.getElementById('obsFilesystem').textContent = data.filesystemReady
    ? formatUsedBytes(data.filesystemUsedBytes, data.filesystemTotalBytes)
    : 'not mounted';
  document.getElementById('obsProfileCount').textContent = data.profileCount ?? '-';
  document.getElementById('obsGpioBindingCount').textContent = data.gpioBindingCount ?? '-';
  document.getElementById('obsDefaultProfile').textContent = data.defaultProfile || '-';
  document.getElementById('obsHost').textContent = data.host || '-';
  document.getElementById('obsWifiState').textContent = data.wifiConnected
    ? 'connected'
    : data.wifiConnecting
      ? 'connecting'
      : data.wifiApActive
        ? 'AP fallback'
        : (data.wifiStatusText || 'offline');
  document.getElementById('obsStaIp').textContent = data.staIp || '-';
  document.getElementById('obsApIp').textContent = data.apIp || '-';
  document.getElementById('obsWifiRssi').textContent = Number.isFinite(Number(data.wifiRssi))
    ? `${data.wifiRssi} dBm`
    : '-';
  document.getElementById('obsWifiChannel').textContent = data.wifiChannel || '-';
  document.getElementById('obsHidTransport').textContent = data.hidTransport === 'ble' ? 'Bluetooth LE' : 'USB';
  document.getElementById('obsBleMode').textContent = data.bleMode || '-';
  document.getElementById('obsBleName').textContent = data.bleName || '-';
  document.getElementById('obsBleState').textContent = data.bleEnabled
    ? (data.blePairing ? 'pairing' : formatBoolState(data.bleConnected, 'connected', 'advertising'))
    : 'disabled';
  document.getElementById('obsBleBondCount').textContent = data.bleBondCount ?? '-';
  document.getElementById('obsMacroState').textContent = data.macroBusy
    ? `running ${data.currentProfile || ''}`.trim()
    : 'idle';
  document.getElementById('obsApiErrors').textContent = data.apiErrors ?? '-';
  document.getElementById('obsWifiReconnects').textContent = data.wifiReconnects ?? '-';
  document.getElementById('obsMacroStarts').textContent = data.macroStarts ?? '-';
  syncTtyLogLevelSummary();
}

function syncGpioProfiles(data) {
  const select = document.getElementById('gpioProfile');
  const prev = select.value;
  const profiles = data.profiles || [];
  const options = ['STOP', ...profiles.filter((name) => name !== 'STOP')];

  if (!sameOptions(select, options)) {
    select.innerHTML = '';
    options.forEach((name) => {
      const option = document.createElement('option');
      option.value = name;
      option.textContent = name === 'STOP' ? 'STOP (system)' : name;
      select.appendChild(option);
    });
  }

  if ([...select.options].some((option) => option.value === prev)) {
    select.value = prev;
  } else if ([...select.options].some((option) => option.value === data.defaultProfile)) {
    select.value = data.defaultProfile;
  } else if (select.options.length > 0) {
    select.selectedIndex = 0;
  }
  updateGpioClearState();
}

function wifiDetails(data) {
  if (data.wifiConnected) {
    return data.staIp || 'connected';
  }

  return `${data.wifiStatusText || 'disconnected'}`
    + `${data.ssid && data.wifiSsidFound === false ? ' / SSID not found' : ''}`
    + `${data.ssid && !data.wifiPasswordSet ? ' / no password' : ''}`
    + `${data.wifiSsidFound ? ` / ch ${data.wifiChannel} / ${data.wifiRssi} dBm` : ''}`;
}

function onlineStatusText(data) {
  const rssi = Number.parseInt(data.wifiRssi, 10);
  if (Number.isFinite(rssi) && rssi < 0) {
    return `Online ${rssi} dBm`;
  }
  return 'Online';
}

function wifiConnectionText(data) {
  if (data.wifiConnected) {
    return `connected${data.staIp ? ` / ${data.staIp}` : ''}`;
  }
  if (data.wifiConnecting) {
    return `connecting / ${data.wifiStatusText || 'waiting'}`;
  }
  return data.wifiStatusText || 'disconnected';
}

function wifiSignalText(data) {
  const rssi = Number.parseInt(data.wifiRssi, 10);
  const channel = Number.parseInt(data.wifiChannel, 10);
  if (Number.isFinite(rssi) && rssi < 0) {
    return `${rssi} dBm${Number.isFinite(channel) && channel > 0 ? ` / ch ${channel}` : ''}`;
  }
  if (data.ssid && data.wifiSsidFound === false) {
    return 'SSID not found';
  }
  return '-';
}

function hidModeText(data) {
  if (data.hidTransport === 'ble') {
    return data.bleMode === 'mouse' ? 'BLE mouse' : 'BLE keyboard';
  }
  return 'USB keyboard + mouse';
}

async function refresh() {
  const data = await getJson('api/status');
  window.lastStatus = data;

  setStatus(data.wifiConnected ? onlineStatusText(data) : 'AP-only', data.wifiConnected ? 'status-online' : 'status-ap');
  document.getElementById('ap').textContent = data.wifiApActive ? (data.apIp || '-') : 'off';
  const staDetails = wifiDetails(data);
  document.getElementById('sta').textContent = staDetails || '-';
  document.getElementById('hidMode').textContent = hidModeText(data);
  document.getElementById('settingsSsid').textContent = data.ssid || '-';
  document.getElementById('settingsWifiConnection').textContent = wifiConnectionText(data);
  document.getElementById('settingsWifiSignal').textContent = wifiSignalText(data);
  document.getElementById('settingsHost').textContent = data.host || '-';
  document.getElementById('settingsDefaultProfile').textContent = data.defaultProfile || '-';
  document.getElementById('settingsDeviceTtyAuth').textContent = data.ttyAuthRequired ? 'enabled' : 'disabled';
  const repeat = data.busy && data.infinite
    ? ' loop'
    : data.busy && data.repeatTarget > 1
      ? ` ${data.repeatDone + 1}/${data.repeatTarget}`
      : '';
  document.getElementById('macro').textContent = `${data.busy ? 'RUNNING ' : 'IDLE '}${data.message || ''}${repeat}`.trim();

  syncWifiForm(data);
  syncBleForm(data);
  syncProfiles(data);
  syncGpioProfiles(data);
  syncGpioBindings(data);
}

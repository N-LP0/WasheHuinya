let updateProgressTimer;

function bytesToHex(bytes) {
  return [...bytes].map((value) => value.toString(16).padStart(2, '0')).join('');
}

function fallbackSha256(buffer) {
  const data = new Uint8Array(buffer);
  const bitLength = data.length * 8;
  const paddedLength = Math.ceil((data.length + 9) / 64) * 64;
  const padded = new Uint8Array(paddedLength);
  padded.set(data);
  padded[data.length] = 0x80;
  const view = new DataView(padded.buffer);
  view.setUint32(paddedLength - 8, Math.floor(bitLength / 0x100000000), false);
  view.setUint32(paddedLength - 4, bitLength >>> 0, false);
  const constants = new Uint32Array(64);
  const initial = new Uint32Array(8);
  let prime = 2;
  let found = 0;
  while (found < 64) {
    let isPrime = true;
    for (let divisor = 2; divisor * divisor <= prime; divisor += 1) {
      if (prime % divisor === 0) {
        isPrime = false;
        break;
      }
    }
    if (isPrime) {
      if (found < 8) initial[found] = Math.floor((Math.sqrt(prime) % 1) * 0x100000000);
      constants[found] = Math.floor((Math.cbrt(prime) % 1) * 0x100000000);
      found += 1;
    }
    prime += 1;
  }
  const hash = initial;
  const words = new Uint32Array(64);
  const rotate = (value, bits) => (value >>> bits) | (value << (32 - bits));
  for (let offset = 0; offset < paddedLength; offset += 64) {
    for (let index = 0; index < 16; index += 1) words[index] = view.getUint32(offset + index * 4, false);
    for (let index = 16; index < 64; index += 1) {
      const s0 = rotate(words[index - 15], 7) ^ rotate(words[index - 15], 18) ^ (words[index - 15] >>> 3);
      const s1 = rotate(words[index - 2], 17) ^ rotate(words[index - 2], 19) ^ (words[index - 2] >>> 10);
      words[index] = (words[index - 16] + s0 + words[index - 7] + s1) >>> 0;
    }
    let [a, b, c, d, e, f, g, h] = hash;
    for (let index = 0; index < 64; index += 1) {
      const sum1 = rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25);
      const choice = (e & f) ^ (~e & g);
      const temp1 = (h + sum1 + choice + constants[index] + words[index]) >>> 0;
      const sum0 = rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22);
      const majority = (a & b) ^ (a & c) ^ (b & c);
      const temp2 = (sum0 + majority) >>> 0;
      h = g; g = f; f = e; e = (d + temp1) >>> 0;
      d = c; c = b; b = a; a = (temp1 + temp2) >>> 0;
    }
    [a, b, c, d, e, f, g, h].forEach((value, index) => {
      hash[index] = (hash[index] + value) >>> 0;
    });
  }
  const output = new Uint8Array(32);
  const outputView = new DataView(output.buffer);
  hash.forEach((value, index) => outputView.setUint32(index * 4, value, false));
  return bytesToHex(output);
}

async function fileSha256(file) {
  const buffer = await file.arrayBuffer();
  if (window.crypto?.subtle) {
    return bytesToHex(new Uint8Array(await window.crypto.subtle.digest('SHA-256', buffer)));
  }
  return fallbackSha256(buffer);
}

function newBundleId() {
  const random = new Uint32Array(2);
  if (window.crypto?.getRandomValues) {
    window.crypto.getRandomValues(random);
  } else {
    random[0] = Math.floor(Math.random() * 0xffffffff);
    random[1] = Math.floor(Math.random() * 0xffffffff);
  }
  return `bundle-${Date.now().toString(36)}-${random[0].toString(36)}${random[1].toString(36)}`;
}

function setUploadStatus(id, message, isError = false) {
  const status = document.getElementById(id);
  status.textContent = message;
  status.classList.toggle('upload-status-error', isError);
  status.classList.remove('field-hidden');
}

function setUploadProgress(type, percent) {
  const bar = document.getElementById(`${type}ProgressBar`);
  const text = document.getElementById(`${type}ProgressText`);
  bar.style.width = `${percent}%`;
  text.textContent = `${percent}%`;
}

function showUploadProgress(type, visible) {
  document.getElementById(`${type}Progress`).classList.toggle('field-hidden', !visible);
}

function resetUpdateUi() {
  ['firmware', 'filesystem'].forEach((type) => {
    document.getElementById(`${type}File`).value = '';
    showUploadProgress(type, false);
    setUploadProgress(type, 0);
    document.getElementById(`${type}Status`).classList.add('field-hidden');
  });
  document.getElementById('bundleStatus').classList.add('field-hidden');
  document.getElementById('uploadBundleBtn').disabled = false;
}

function returnToControlAfterUpdate() {
  setTimeout(() => {
    resetUpdateUi();
    window.location.hash = '';
    window.location.reload();
  }, 4500);
}

async function pollUpdateProgress(type) {
  const response = await getJson('api/update/progress');
  const data = response.data || {};
  setUploadProgress(type, data.percentage || 0);
  if (data.stage && data.stage !== 'uploading') {
    setUploadStatus(`${type}Status`, `${data.stage}: do not disconnect power`);
  }
}

async function uploadUpdateFile(type, restartAfterUpdate, bundleId = '') {
  const fileInput = document.getElementById(`${type}File`);
  const statusId = `${type}Status`;
  const file = fileInput.files[0];
  if (!file) {
    setUploadStatus(statusId, 'Select a .bin file first', true);
    return;
  }

  const endpoint = type === 'firmware' ? 'api/update/firmware' : 'api/update/filesystem';
  const body = new FormData();
  body.append('file', file, file.name);

  setUploadProgress(type, 0);
  showUploadProgress(type, true);
  document.getElementById(statusId).classList.add('field-hidden');
  setUploadStatus(statusId, 'Calculating SHA-256...');
  const sha256 = await fileSha256(file);
  setUploadStatus(statusId, 'Uploading: do not disconnect power');

  clearInterval(updateProgressTimer);
  updateProgressTimer = setInterval(() => {
    pollUpdateProgress(type).catch(() => {});
  }, 500);

  try {
    const response = await fetch(endpoint, {
      method: 'POST',
      headers: {
        'X-Update-Size': String(file.size),
        'X-Update-SHA256': sha256,
        'X-Restart-After-Update': restartAfterUpdate ? '1' : '0',
        ...(bundleId ? { 'X-Bundle-ID': bundleId } : {}),
      },
      body,
    });
    const payload = await response.json();
    if (!response.ok) {
      throw new Error(payload.error?.message || payload.error?.code || 'Update failed');
    }
    setUploadProgress(type, 100);
    setUploadStatus(statusId, payload.data?.message || 'Update completed.');
  } catch (error) {
    setUploadStatus(statusId, error.message || 'Update failed', true);
    throw error;
  } finally {
    clearInterval(updateProgressTimer);
  }
}

async function uploadUpdateBundle() {
  const firmware = document.getElementById('firmwareFile').files[0];
  const filesystem = document.getElementById('filesystemFile').files[0];
  const button = document.getElementById('uploadBundleBtn');

  if (!firmware || !filesystem) {
    setUploadStatus('bundleStatus', 'Select both firmware.bin and littlefs.bin', true);
    return;
  }

  button.disabled = true;
  document.getElementById('bundleStatus').classList.add('field-hidden');

  try {
    const bundleId = newBundleId();
    setUploadStatus('bundleStatus', 'Uploading firmware...');
    await uploadUpdateFile('firmware', false, bundleId);
    setUploadStatus('bundleStatus', 'Firmware uploaded. Uploading filesystem...');
    await uploadUpdateFile('filesystem', true, bundleId);
    setUploadStatus('bundleStatus', 'Update completed. Device will restart.');
    returnToControlAfterUpdate();
  } catch (error) {
    setUploadStatus('bundleStatus', error.message || 'Update failed', true);
    button.disabled = false;
  }
}

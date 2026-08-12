let updateProgressTimer;

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
}

async function uploadUpdateFile(type, restartAfterUpdate) {
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

  clearInterval(updateProgressTimer);
  updateProgressTimer = setInterval(() => {
    pollUpdateProgress(type).catch(() => {});
  }, 500);

  try {
    const response = await fetch(endpoint, {
      method: 'POST',
      headers: {
        'X-Update-Size': String(file.size),
        'X-Restart-After-Update': restartAfterUpdate ? '1' : '0',
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
    setUploadStatus('bundleStatus', 'Uploading firmware...');
    await uploadUpdateFile('firmware', false);
    setUploadStatus('bundleStatus', 'Firmware uploaded. Uploading filesystem...');
    await uploadUpdateFile('filesystem', true);
    setUploadStatus('bundleStatus', 'Update completed. Device will restart.');
    returnToControlAfterUpdate();
  } catch (error) {
    setUploadStatus('bundleStatus', error.message || 'Update failed', true);
    button.disabled = false;
  }
}

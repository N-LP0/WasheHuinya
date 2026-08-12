let wifiFormDirty = false;

function markWifiFormDirty() {
  wifiFormDirty = true;
}

function syncWifiForm(data) {
  if (wifiFormDirty) {
    return;
  }

  setValueIfIdle('host', data.host);
  setValueIfIdle('ssid', data.ssid);
  syncDefaultProfileSelect(data);
}

function syncDefaultProfileSelect(data) {
  const select = document.getElementById('def');
  const profiles = data.profiles || [];
  const options = ['', ...profiles];

  if (!sameOptions(select, options)) {
    select.innerHTML = '';
    const none = document.createElement('option');
    none.value = '';
    none.textContent = 'None';
    select.appendChild(none);
    profiles.forEach((name) => {
      const option = document.createElement('option');
      option.value = name;
      option.textContent = name;
      select.appendChild(option);
    });
  }

  if (data.defaultProfile && [...select.options].some((option) => option.value === data.defaultProfile)) {
    select.value = data.defaultProfile;
  } else {
    select.value = '';
  }
}

async function saveWifi(message = 'Settings saved', syncControlProfile = false) {
  assertTokenNameField('host', 'Hostname', false);
  assertTokenNameField('def', 'Default profile', false);
  assertFieldValid('pass', validateOptionalPassword('pass', 'Wi-Fi password'));

  const data = {
    hostname: document.getElementById('host').value,
    ssid: document.getElementById('ssid').value,
    defaultProfile: document.getElementById('def').value,
  };

  const wifiPass = document.getElementById('pass').value;
  if (wifiPass) {
    data.pass = wifiPass;
  }

  const ttyPass = document.getElementById('ttyPass').value;
  if (ttyPass || document.getElementById('ttyAuthOff').checked) {
    data.ttyPass = ttyPass;
  }

  await postForm('api/wifi/save', data);

  wifiFormDirty = false;
  if (syncControlProfile) {
    profileSelectionTouched = false;
  }
  document.getElementById('pass').value = '';
  document.getElementById('ttyPass').value = '';
  document.getElementById('ttyAuthOff').checked = false;
  await refresh();
  if (syncControlProfile) {
    await loadProfile();
  }
  showToast(message);
}

async function resetWifi() {
  await postForm('api/wifi/reset', {});
  wifiFormDirty = false;
  document.getElementById('ssid').value = '';
  document.getElementById('pass').value = '';
  await refresh().catch(() => {});
  showToast('Wi-Fi credentials reset');
}

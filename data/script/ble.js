let bleFormDirty = false;

function updateHidTypeOptions(selectedMode) {
  const transport = document.getElementById('hidTransport').value;
  const select = document.getElementById('bleMode');
  const options = transport === 'ble'
    ? [
        { value: 'keyboard', label: 'Keyboard' },
        { value: 'mouse', label: 'Mouse' },
      ]
    : [{ value: 'keyboard', label: 'Keyboard + mouse' }];

  select.innerHTML = '';
  options.forEach(({ value, label }) => {
    const option = document.createElement('option');
    option.value = value;
    option.textContent = label;
    select.appendChild(option);
  });
  select.value = options.some((option) => option.value === selectedMode)
    ? selectedMode
    : options[0].value;
}

function markBleFormDirty() {
  bleFormDirty = true;
}

function syncBleForm(data) {
  if (!bleFormDirty) {
    setValueIfIdle('hidTransport', data.hidTransport || 'usb');
    updateHidTypeOptions(data.bleMode || 'keyboard');
  }

  document.getElementById('bleTransportState').textContent = data.hidTransport === 'ble' ? 'Bluetooth LE' : 'USB';
  document.getElementById('bleModeState').textContent = data.hidTransport === 'ble'
    ? (data.bleMode === 'mouse' ? 'Mouse' : 'Keyboard')
    : 'Keyboard + mouse';
  document.getElementById('bleNameState').textContent = data.bleName || '-';
  document.getElementById('bleConnectionState').textContent = data.bleEnabled
    ? (data.blePairing ? 'pairing' : data.bleConnected ? 'connected' : 'advertising')
    : 'disabled';
  renderBleBonds(data);
}

function renderBleBonds(data) {
  const container = document.getElementById('bleBonds');
  const clearButton = document.getElementById('clearBleBondsBtn');
  const bonds = data.bleBonds || [];
  clearButton.disabled = !data.bleBondsAvailable || bonds.length === 0;
  container.innerHTML = '';

  if (!data.bleBondsAvailable) {
    container.innerHTML = '<div class="bond-empty">Enable Bluetooth LE to manage saved bonds.</div>';
    return;
  }
  if (bonds.length === 0) {
    container.innerHTML = '<div class="bond-empty">No bonded devices.</div>';
    return;
  }

  bonds.forEach((address) => {
    const row = document.createElement('div');
    row.className = 'bond-row';
    const value = document.createElement('code');
    value.textContent = address;
    const remove = document.createElement('button');
    remove.className = 'button-danger bond-remove-button';
    remove.type = 'button';
    remove.textContent = 'Remove';
    remove.addEventListener('click', () => removeBleBond(address).catch(handleError));
    row.append(value, remove);
    container.appendChild(row);
  });
}

async function removeBleBond(address) {
  if (!window.confirm(`Remove Bluetooth bond ${address}?`)) return;
  await postForm('api/ble/bond/delete', { address });
  await refresh();
  showToast('Bluetooth bond removed');
}

async function clearBleBonds() {
  if (!window.confirm('Remove all saved Bluetooth bonds?')) return;
  await postForm('api/ble/bonds/clear', {});
  await refresh();
  showToast('Bluetooth bonds cleared');
}

async function saveBleSettings() {
  await postForm('api/ble/save', {
    transport: document.getElementById('hidTransport').value,
    mode: document.getElementById('bleMode').value,
  });
  showToast('HID settings saved. Device is restarting.');
}

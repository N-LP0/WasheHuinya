document.getElementById('profiles').addEventListener('change', () => {
  profileSelectionTouched = true;
  loadProfile().catch(handleError);
});

document.getElementById('controlPageBtn').addEventListener('click', () => {
  showPage('control');
  window.location.hash = '';
});

document.getElementById('ttyPageBtn').addEventListener('click', () => {
  showPage('tty');
  window.location.hash = 'tty';
});

document.getElementById('gpioPageBtn').addEventListener('click', () => {
  showPage('gpio');
  window.location.hash = 'gpio';
  updateGpioClearState();
});

document.getElementById('wifiPageBtn').addEventListener('click', () => {
  showPage('wifi');
  window.location.hash = 'wifi';
});

document.getElementById('blePageBtn').addEventListener('click', () => {
  showPage('ble');
  window.location.hash = 'ble';
});

document.getElementById('hidTransport').addEventListener('change', () => {
  updateHidTypeOptions(document.getElementById('bleMode').value);
});

document.getElementById('settingsPageBtn').addEventListener('click', () => {
  showPage('settings');
  window.location.hash = 'settings';
});

document.getElementById('observabilityPageBtn').addEventListener('click', () => {
  showPage('observability');
  window.location.hash = 'observability';
  refreshObservability().catch(handleError);
});

document.getElementById('updatePageBtn').addEventListener('click', () => {
  showPage('update');
  window.location.hash = 'update';
});

document.getElementById('saveWifiBtn').addEventListener('click', () => {
  saveWifi('Wi-Fi settings saved').catch(handleError);
});

document.getElementById('resetWifiBtn').addEventListener('click', () => {
  resetWifi().catch(handleError);
});

document.getElementById('saveBleBtn').addEventListener('click', () => {
  saveBleSettings().catch(handleError);
});

document.getElementById('clearBleBondsBtn').addEventListener('click', () => {
  clearBleBonds().catch(handleError);
});

['hidTransport', 'bleMode'].forEach((id) => {
  document.getElementById(id).addEventListener('input', markBleFormDirty);
  document.getElementById(id).addEventListener('change', markBleFormDirty);
});

document.getElementById('uploadBundleBtn').addEventListener('click', () => {
  uploadUpdateBundle().catch(handleError);
});

document.getElementById('saveDeviceSettingsBtn').addEventListener('click', () => {
  saveWifi('Device settings saved', true).catch(handleError);
});

['host', 'ssid', 'pass', 'def', 'ttyPass', 'ttyAuthOff'].forEach((id) => {
  document.getElementById(id).addEventListener('input', markWifiFormDirty);
  document.getElementById(id).addEventListener('change', markWifiFormDirty);
});

document.getElementById('name').addEventListener('input', () => {
  setFieldError('name', validateTokenNameValue(document.getElementById('name').value, 'Profile name'));
});

document.getElementById('host').addEventListener('input', () => {
  setFieldError('host', validateTokenNameValue(document.getElementById('host').value, 'Hostname', false));
});

document.getElementById('pass').addEventListener('input', () => {
  setFieldError('pass', validateOptionalPassword('pass', 'Wi-Fi password'));
});

document.getElementById('ttyCmd').addEventListener('input', () => {
  setFieldError('ttyCmd', document.getElementById('ttyCmd').value.trim() ? '' : 'TTY command is required');
});

document.getElementById('saveProfileBtn').addEventListener('click', () => {
  saveProfile().catch(handleError);
});

document.getElementById('deleteProfileBtn').addEventListener('click', () => {
  deleteProfile().catch(handleError);
});

document.getElementById('exportProfilesBtn').addEventListener('click', () => {
  exportProfiles().catch(handleError);
});

document.getElementById('importProfilesBtn').addEventListener('click', () => {
  document.getElementById('importProfilesFile').click();
});

document.getElementById('importProfilesFile').addEventListener('change', (event) => {
  importProfilesFromFile(event.target.files[0])
    .catch(handleError)
    .finally(() => {
      event.target.value = '';
    });
});

document.getElementById('testScriptBtn').addEventListener('click', () => {
  testScript().catch(handleError);
});

document.getElementById('runProfileBtn').addEventListener('click', () => {
  runProfile().catch(handleError);
});

document.getElementById('stopProfileBtn').addEventListener('click', () => {
  stopProfile().catch(handleError);
});

document.getElementById('execTtyBtn').addEventListener('click', () => {
  execTty().catch(handleError);
});

document.getElementById('gpioAssignBtn').addEventListener('click', () => {
  saveGpioBinding().catch(handleError);
});

document.getElementById('gpioClearBtn').addEventListener('click', () => {
  deleteGpioBinding().catch(handleError);
});

document.getElementById('gpioPin').addEventListener('change', () => {
  updateGpioClearState();
});

document.getElementById('macroHelpBtn').addEventListener('click', () => {
  document.getElementById('macroHelpModal').classList.add('open');
  document.getElementById('macroHelpModal').setAttribute('aria-hidden', 'false');
});

document.getElementById('macroHelpCloseBtn').addEventListener('click', () => {
  closeMacroHelp();
});

document.getElementById('macroHelpModal').addEventListener('click', (event) => {
  if (event.target.hasAttribute('data-close-modal')) {
    closeMacroHelp();
  }
});

document.getElementById('ttyHelpBtn').addEventListener('click', () => {
  document.getElementById('ttyHelpModal').classList.add('open');
  document.getElementById('ttyHelpModal').setAttribute('aria-hidden', 'false');
});

document.getElementById('ttyHelpCloseBtn').addEventListener('click', () => {
  closeTtyHelp();
});

document.getElementById('ttyHelpModal').addEventListener('click', (event) => {
  if (event.target.hasAttribute('data-close-modal')) {
    closeTtyHelp();
  }
});

['ttyLogInfo', 'ttyLogWarn', 'ttyLogError'].forEach((id) => {
  document.getElementById(id).addEventListener('change', () => {
    renderTtyLog(ttyLogRaw);
    syncTtyLogLevelSummary();
  });
});

document.getElementById('script').addEventListener('input', () => {
  if (!handleMacroEditorEndIndent()) {
    updateMacroEditor();
    syncMacroEditorScroll();
  }
});

document.getElementById('script').addEventListener('keyup', () => {
  updateMacroSuggestions();
});

document.getElementById('script').addEventListener('click', () => {
  updateMacroSuggestions();
});

document.getElementById('script').addEventListener('keydown', (event) => {
  if (event.key === 'Enter') {
    handleMacroEditorEnter(event);
    return;
  }
  handleMacroAutocompleteKey(event);
});

document.getElementById('script').addEventListener('scroll', () => {
  syncMacroEditorScroll();
});

document.getElementById('ttyToken').addEventListener('change', () => {
  saveTtyToken();
  connectTtyLogSocket();
});

document.addEventListener('keydown', (event) => {
  if (event.key === 'Escape') {
    closeMacroHelp();
    closeTtyHelp();
  }
});

setInterval(() => {
  refresh().catch(handleError);
}, 1500);

setInterval(() => {
  refreshObservability().catch(handleError);
}, 10000);

loadSavedTtyToken();
showPage(
  window.location.hash === '#tty'
    ? 'tty'
    : window.location.hash === '#gpio'
      ? 'gpio'
      : window.location.hash === '#wifi'
      ? 'wifi'
        : window.location.hash === '#ble'
          ? 'ble'
          : window.location.hash === '#settings'
          ? 'settings'
          : window.location.hash === '#observability'
            ? 'observability'
            : window.location.hash === '#update'
              ? 'update'
              : 'control',
);
formatAndUpdateMacroEditor(false);
refresh()
  .then(() => refreshObservability())
  .then(loadProfile)
  .catch(handleError);
connectTtyLogSocket();

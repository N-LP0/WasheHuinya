function syncGpioBindings(data) {
  const table = document.getElementById('gpioBindings');
  table.querySelectorAll('.gpio-row:not(.gpio-row-head)').forEach((row) => row.remove());

  const bindings = data.gpioBindings || [];
  if (bindings.length === 0) {
    const row = document.createElement('div');
    row.className = 'gpio-row gpio-empty';
    row.innerHTML = '<span>-</span><span>No GPIO bindings</span><span>-</span>';
    table.appendChild(row);
    return;
  }

  bindings.forEach((binding) => {
    const row = document.createElement('button');
    row.type = 'button';
    row.className = 'gpio-row gpio-row-button';
    const behavior = binding.reserved ? 'Stop' : 'Hold';
    row.innerHTML = `<span>GPIO ${binding.pin}</span><span>${binding.profile}</span><span>${behavior}</span>`;
    row.addEventListener('click', () => {
      document.getElementById('gpioPin').value = String(binding.pin);
      document.getElementById('gpioProfile').value = binding.profile;
      updateGpioClearState();
    });
    table.appendChild(row);
  });
  updateGpioClearState();
}

function selectedGpioBinding() {
  const pin = Number.parseInt(document.getElementById('gpioPin').value, 10);
  return (window.lastStatus?.gpioBindings || []).find((binding) => binding.pin === pin);
}

function updateGpioClearState() {
  const binding = selectedGpioBinding();
  const reserved = Boolean(binding?.reserved);
  const clearBtn = document.getElementById('gpioClearBtn');
  clearBtn.disabled = reserved;
  clearBtn.title = reserved ? 'STOP binding cannot be deleted' : '';
}

async function saveGpioBinding() {
  const pin = document.getElementById('gpioPin').value;
  const profile = document.getElementById('gpioProfile').value;
  if (!['4', '5', '6', '7', '8', '9', '10'].includes(pin)) {
    throw new Error('GPIO pin is not allowed');
  }
  if (profile !== 'STOP' && validateTokenNameValue(profile, 'GPIO profile')) {
    throw new Error(validateTokenNameValue(profile, 'GPIO profile'));
  }

  await postForm('api/gpio/save', {
    pin,
    profile,
  });

  await refresh();
  showToast('GPIO binding saved');
}

async function deleteGpioBinding() {
  const pin = document.getElementById('gpioPin').value;
  if (!['4', '5', '6', '7', '8', '9', '10'].includes(pin)) {
    throw new Error('GPIO pin is not allowed');
  }
  if (selectedGpioBinding()?.reserved) {
    throw new Error('GPIO STOP binding cannot be deleted');
  }

  await postForm('api/gpio/delete', {
    pin,
  });

  await refresh();
  showToast('GPIO binding deleted');
}

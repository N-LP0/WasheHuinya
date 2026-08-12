async function errorMessage(response) {
  const contentType = response.headers.get('Content-Type') || '';
  if (contentType.includes('application/json')) {
    const data = await response.json();
    return data.error?.message || data.error?.code || response.statusText;
  }

  return response.text();
}

async function postForm(url, data) {
  const body = new URLSearchParams(data);
  const response = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body,
  });

  if (!response.ok) {
    throw new Error(await errorMessage(response));
  }

  return response.text();
}

async function postFormJson(url, data) {
  const body = new URLSearchParams(data);
  const response = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body,
  });

  if (!response.ok) {
    throw new Error(await errorMessage(response));
  }

  return response.json();
}

async function postJson(url, data) {
  const response = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data),
  });

  if (!response.ok) {
    throw new Error(await errorMessage(response));
  }

  return response.json();
}

async function getJson(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(await errorMessage(response));
  }

  return response.json();
}

const macroCommands = new Set(['TEXT', 'TYPE', 'DELAY', 'WAIT', 'KEY', 'HOTKEY', 'REPEAT', 'LOOP', 'END', 'MOUSE', 'RELEASEALL']);
const macroMouseWords = new Set(['MOVE', 'WHEEL', 'SCROLL', 'PAN', 'HWHEEL', 'CLICK', 'PRESS', 'RELEASE']);
const macroKeyWords = new Set([
  'CTRL',
  'CONTROL',
  'ALT',
  'SHIFT',
  'GUI',
  'WIN',
  'CMD',
  'ENTER',
  'RETURN',
  'TAB',
  'ESC',
  'ESCAPE',
  'SPACE',
  'BACKSPACE',
  'DEL',
  'DELETE',
  'INS',
  'INSERT',
  'UP',
  'DOWN',
  'LEFT',
  'RIGHT',
  'HOME',
  'END',
  'PGUP',
  'PGDN',
]);
const macroButtonWords = new Set(['LEFT', 'RIGHT', 'MIDDLE', 'WHEEL', 'MBUTTON', 'MOUSE3']);
const macroCommandSuggestions = [
  'TEXT ',
  'TYPE ',
  'DELAY ',
  'WAIT ',
  'KEY ',
  'HOTKEY ',
  'REPEAT ',
  'LOOP',
  'END',
  'MOUSE ',
  'RELEASEALL',
];
const macroIndent = '  ';
const maxMacroScriptBytes = 32 * 1024;
const maxMacroDelayMs = 86400000;
const maxMacroRepeat = 100000000;
const maxMacroMove = 32767;
const maxMacroScroll = 10000;
const macroMouseSuggestions = [
  'MOVE ',
  'WHEEL ',
  'SCROLL ',
  'PAN ',
  'HWHEEL ',
  'CLICK ',
  'PRESS ',
  'RELEASE ',
];
const macroButtonSuggestions = [
  'LEFT',
  'RIGHT',
  'MIDDLE',
  'WHEEL',
  'MBUTTON',
  'MOUSE3',
];
const macroKeySuggestions = [
  'CTRL',
  'CONTROL',
  'ALT',
  'SHIFT',
  'GUI',
  'WIN',
  'CMD',
  'ENTER',
  'RETURN',
  'ESC',
  'ESCAPE',
  'TAB',
  'SPACE',
  'BACKSPACE',
  'DEL',
  'DELETE',
  'INS',
  'INSERT',
  'UP',
  'DOWN',
  'LEFT',
  'RIGHT',
  'HOME',
  'END',
  'PGUP',
  'PGDN',
  'F1',
  'F2',
  'F3',
  'F4',
  'F5',
  'F6',
  'F7',
  'F8',
  'F9',
  'F10',
  'F11',
  'F12',
];
let macroSuggestIndex = 0;
let profileSelectionTouched = false;

function escapeHtml(text) {
  return text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

function span(className, value) {
  return `<span class="${className}">${escapeHtml(value)}</span>`;
}

function macroCommandFromLine(line) {
  const trimmed = line.trim();
  if (!trimmed || trimmed.startsWith('#') || trimmed.startsWith(';') || trimmed.startsWith('//')) {
    return '';
  }
  return trimmed.split(/\s+/)[0].toUpperCase();
}

function macroCaretLineColumn(text, caret) {
  const before = text.slice(0, caret);
  const lines = before.split('\n');
  return {
    line: lines.length - 1,
    column: lines[lines.length - 1].length,
  };
}

function macroOffsetFromLineColumn(text, line, column) {
  const lines = text.split('\n');
  let offset = 0;
  for (let index = 0; index < Math.min(line, lines.length); index += 1) {
    offset += lines[index].length + 1;
  }
  return offset + Math.min(column, lines[line]?.length || 0);
}

function expectedMacroIndentByLine(lines) {
  let depth = 0;
  return lines.map((line) => {
    const cmd = macroCommandFromLine(line);
    if (cmd === 'END') {
      depth = Math.max(0, depth - 1);
    }
    const indent = line.trim() ? macroIndent.repeat(depth) : '';
    if (cmd === 'REPEAT' || cmd === 'LOOP') {
      depth = Math.min(depth + 1, 8);
    }
    return indent;
  });
}

function formatMacroScript(preserveCaret = true) {
  const script = document.getElementById('script');
  const original = script.value;
  const selectionStart = script.selectionStart;
  const selectionEnd = script.selectionEnd;
  const startPosition = macroCaretLineColumn(original, selectionStart);
  const endPosition = macroCaretLineColumn(original, selectionEnd);
  const lines = original.split('\n');
  const indents = expectedMacroIndentByLine(lines);
  const formatted = lines
    .map((line, index) => (line.trim() ? indents[index] + line.trimStart() : ''))
    .join('\n');

  if (formatted === original) {
    return false;
  }

  script.value = formatted;
  if (preserveCaret) {
    const newStart = macroOffsetFromLineColumn(formatted, startPosition.line, startPosition.column);
    const newEnd = macroOffsetFromLineColumn(formatted, endPosition.line, endPosition.column);
    script.setSelectionRange(newStart, newEnd);
  }
  return true;
}

function currentMacroLineInfo(text, caret) {
  const lineStart = text.lastIndexOf('\n', caret - 1) + 1;
  const nextBreak = text.indexOf('\n', caret);
  const lineEnd = nextBreak < 0 ? text.length : nextBreak;
  const line = text.slice(lineStart, lineEnd);
  return { lineStart, lineEnd, line };
}

function handleMacroEditorEnter(event) {
  const script = document.getElementById('script');
  const { line } = currentMacroLineInfo(script.value, script.selectionStart);
  const currentIndent = line.match(/^\s*/)[0];
  const cmd = macroCommandFromLine(line);
  const nextIndent = cmd === 'REPEAT' || cmd === 'LOOP' ? currentIndent + macroIndent : currentIndent;
  const before = script.value.slice(0, script.selectionStart);
  const after = script.value.slice(script.selectionEnd);
  script.value = `${before}\n${nextIndent}${after}`;
  const caret = before.length + 1 + nextIndent.length;
  script.setSelectionRange(caret, caret);
  updateMacroEditor();
  syncMacroEditorScroll();
  event.preventDefault();
}

function handleMacroEditorEndIndent() {
  const script = document.getElementById('script');
  const caret = script.selectionStart;
  const { lineStart, lineEnd, line } = currentMacroLineInfo(script.value, caret);
  if (macroCommandFromLine(line) !== 'END') {
    return false;
  }

  const linesBefore = script.value.slice(0, lineStart).split('\n');
  const expectedIndent = expectedMacroIndentByLine([...linesBefore.slice(0, -1), line]).pop() || '';
  const replacement = expectedIndent + line.trimStart();
  if (replacement === line) {
    return false;
  }

  script.value = script.value.slice(0, lineStart) + replacement + script.value.slice(lineEnd);
  const newCaret = lineStart + Math.min(replacement.length, Math.max(0, caret - lineStart - (line.length - replacement.length)));
  script.setSelectionRange(newCaret, newCaret);
  updateMacroEditor();
  syncMacroEditorScroll();
  return true;
}

function highlightMacroLine(line) {
  if (!line.trim()) {
    return ' ';
  }

  const trimmed = line.trimStart();
  const indent = line.slice(0, line.length - trimmed.length);
  if (trimmed.startsWith('#') || trimmed.startsWith(';') || trimmed.startsWith('//')) {
    return escapeHtml(indent) + span('macro-comment', trimmed);
  }

  return escapeHtml(indent) + trimmed.split(/(\s+)/).map((part, index) => {
    if (!part || /^\s+$/.test(part)) {
      return escapeHtml(part);
    }

    const upper = part.toUpperCase();
    if (index === 0 && macroCommands.has(upper)) {
      return span('macro-cmd', part);
    }
    if (macroMouseWords.has(upper) || macroKeyWords.has(upper)) {
      return span('macro-keyword', part);
    }
    if (/^-?\d+$/.test(part)) {
      return span('macro-number', part);
    }
    if (index === 0) {
      return span('macro-error', part);
    }
    return escapeHtml(part);
  }).join('');
}

function updateMacroEditor() {
  const script = document.getElementById('script');
  const lines = document.getElementById('scriptLines');
  const highlight = document.getElementById('scriptHighlight');
  const value = script.value;
  const lineCount = Math.max(1, value.split('\n').length);

  lines.textContent = Array.from({ length: lineCount }, (_, index) => index + 1).join('\n');
  highlight.innerHTML = value.split('\n').map(highlightMacroLine).join('\n');
  updateMacroValidation();
  updateMacroSuggestions();
}

function formatAndUpdateMacroEditor(preserveCaret = true) {
  formatMacroScript(preserveCaret);
  updateMacroEditor();
}

function syncMacroEditorScroll() {
  const script = document.getElementById('script');
  document.getElementById('scriptLines').scrollTop = script.scrollTop;
  document.getElementById('scriptHighlight').scrollTop = script.scrollTop;
  document.getElementById('scriptHighlight').scrollLeft = script.scrollLeft;
}

function isIntegerToken(value) {
  return /^-?\d+$/.test(value || '');
}

function validateKeyTokens(tokens) {
  if (tokens.length === 0) {
    return 'KEY requires a key or key combination';
  }

  let primary = false;
  for (const token of tokens) {
    const upper = token.toUpperCase();
    if (['CTRL', 'CONTROL', 'ALT', 'SHIFT', 'GUI', 'WIN', 'CMD'].includes(upper)) {
      continue;
    }
    if (macroKeyWords.has(upper) || /^F([1-9]|1[0-2])$/.test(upper) || token.length === 1) {
      primary = true;
      continue;
    }
    return `Unknown key token: ${token}`;
  }

  return primary ? '' : 'KEY requires a primary key';
}

function validateMacroLine(line) {
  const trimmed = line.trim();
  if (!trimmed || trimmed.startsWith('#') || trimmed.startsWith(';') || trimmed.startsWith('//')) {
    return '';
  }

  const tokens = trimmed.split(/\s+/);
  const cmd = tokens[0].toUpperCase();
  const args = tokens.slice(1);

  if (cmd === 'TEXT' || cmd === 'TYPE') {
    return '';
  }

  if (cmd === 'DELAY' || cmd === 'WAIT') {
    const delay = Number.parseInt(args[0], 10);
    if (args.length !== 1 || !isIntegerToken(args[0]) || delay < 0 || delay > maxMacroDelayMs) {
      return `${cmd} requires one integer from 0 to ${maxMacroDelayMs}`;
    }
    return '';
  }

  if (cmd === 'KEY' || cmd === 'HOTKEY') {
    return validateKeyTokens(args);
  }

  if (cmd === 'REPEAT') {
    const repeat = Number.parseInt(args[0], 10);
    if (args.length !== 1 || !isIntegerToken(args[0]) || repeat <= 0 || repeat > maxMacroRepeat) {
      return `REPEAT requires one integer from 1 to ${maxMacroRepeat}`;
    }
    return '';
  }

  if (cmd === 'LOOP' || cmd === 'END') {
    return args.length === 0 ? '' : `${cmd} does not take arguments`;
  }

  if (cmd === 'MOUSE') {
    const sub = (args[0] || '').toUpperCase();
    const rest = args.slice(1);
    if (!macroMouseWords.has(sub)) {
      return 'MOUSE requires MOVE, WHEEL, SCROLL, PAN, HWHEEL, CLICK, PRESS or RELEASE';
    }
    if (sub === 'MOVE') {
      const x = Number.parseInt(rest[0], 10);
      const y = Number.parseInt(rest[1], 10);
      if (
        (rest.length !== 2 && rest.length !== 3) ||
        !isIntegerToken(rest[0]) ||
        !isIntegerToken(rest[1]) ||
        (rest.length === 3 &&
          (!isIntegerToken(rest[2]) ||
            Number.parseInt(rest[2], 10) < 0 ||
            Number.parseInt(rest[2], 10) > 12)) ||
        Math.abs(x) > maxMacroMove ||
        Math.abs(y) > maxMacroMove
      ) {
        return `MOUSE MOVE requires X/Y from -${maxMacroMove} to ${maxMacroMove} and optional jitter 0..12`;
      }
      return '';
    }
    if (sub === 'WHEEL' || sub === 'SCROLL' || sub === 'PAN' || sub === 'HWHEEL') {
      const amount = Number.parseInt(rest[0], 10);
      if (rest.length !== 1 || !isIntegerToken(rest[0]) || Math.abs(amount) > maxMacroScroll) {
        return `MOUSE ${sub} requires one integer from -${maxMacroScroll} to ${maxMacroScroll}`;
      }
      return '';
    }
    if (sub === 'CLICK' || sub === 'PRESS' || sub === 'RELEASE') {
      if (rest.length !== 1 || !macroButtonWords.has(rest[0].toUpperCase())) {
        return `MOUSE ${sub} requires LEFT, RIGHT, MIDDLE, WHEEL, MBUTTON or MOUSE3`;
      }
      return '';
    }
  }

  if (cmd === 'RELEASEALL') {
    return args.length === 0 ? '' : 'RELEASEALL does not take arguments';
  }

  return `Unknown command: ${tokens[0]}`;
}

function validateMacroScript() {
  const script = document.getElementById('script').value;
  if (new TextEncoder().encode(script).length > maxMacroScriptBytes) {
    return { ok: false, line: 0, message: 'Macro script exceeds 32 KiB' };
  }
  const lines = script.split('\n');
  const stack = [];
  for (let index = 0; index < lines.length; index += 1) {
    const error = validateMacroLine(lines[index]);
    if (error) {
      return { ok: false, line: index + 1, message: error };
    }
    const trimmed = lines[index].trim();
    if (!trimmed || trimmed.startsWith('#') || trimmed.startsWith(';') || trimmed.startsWith('//')) {
      continue;
    }
    const cmd = trimmed.split(/\s+/)[0].toUpperCase();
    if (cmd === 'REPEAT' || cmd === 'LOOP') {
      stack.push({ cmd, line: index + 1 });
    } else if (cmd === 'END') {
      if (stack.length === 0) {
        return { ok: false, line: index + 1, message: 'END without REPEAT or LOOP' };
      }
      stack.pop();
    }
    if (stack.length > 8) {
      return { ok: false, line: index + 1, message: 'Loop nesting is too deep' };
    }
  }
  if (stack.length > 0) {
    const open = stack[stack.length - 1];
    return { ok: false, line: open.line, message: `${open.cmd} block is missing END` };
  }
  return { ok: true, line: 0, message: 'Syntax OK' };
}

function updateMacroValidation() {
  const box = document.getElementById('macroValidation');
  const result = validateMacroScript();
  box.classList.toggle('invalid', !result.ok);
  box.textContent = result.ok ? result.message : `Line ${result.line}: ${result.message}`;
  return result;
}

function assertMacroValid() {
  const result = updateMacroValidation();
  if (!result.ok) {
    throw new Error(`Macro syntax error on line ${result.line}: ${result.message}`);
  }
}

function currentTokenRange(text, caret) {
  let start = caret;
  while (start > 0 && /[A-Za-z0-9_]/.test(text[start - 1])) {
    start -= 1;
  }
  let end = caret;
  while (end < text.length && /[A-Za-z0-9_]/.test(text[end])) {
    end += 1;
  }
  return { start, end, value: text.slice(start, caret) };
}

function matchingMacroSuggestions() {
  const script = document.getElementById('script');
  const range = currentTokenRange(script.value, script.selectionStart);
  const query = range.value.toUpperCase();
  if (!query) {
    return [];
  }

  const lineStart = script.value.lastIndexOf('\n', range.start - 1) + 1;
  const lineBeforeToken = script.value.slice(lineStart, range.start).trim();
  const tokensBefore = lineBeforeToken ? lineBeforeToken.split(/\s+/).map((token) => token.toUpperCase()) : [];
  let source = macroCommandSuggestions;
  if (tokensBefore[0] === 'MOUSE' && tokensBefore.length === 1) {
    source = macroMouseSuggestions;
  } else if (
    tokensBefore[0] === 'MOUSE' &&
    ['CLICK', 'PRESS', 'RELEASE'].includes(tokensBefore[1]) &&
    tokensBefore.length === 2
  ) {
    source = macroButtonSuggestions;
  } else if ((tokensBefore[0] === 'KEY' || tokensBefore[0] === 'HOTKEY') && tokensBefore.length >= 1) {
    source = macroKeySuggestions;
  }

  return source
    .filter((item) => item.toUpperCase().startsWith(query))
    .slice(0, 8)
    .map((item) => ({ value: item, range }));
}

function updateMacroSuggestions() {
  const menu = document.getElementById('macroSuggest');
  const suggestions = matchingMacroSuggestions();
  if (suggestions.length === 0) {
    menu.classList.remove('open');
    menu.innerHTML = '';
    macroSuggestIndex = 0;
    return;
  }

  macroSuggestIndex = Math.min(macroSuggestIndex, suggestions.length - 1);
  menu.innerHTML = '';
  suggestions.forEach((suggestion, index) => {
    const item = document.createElement('button');
    item.type = 'button';
    item.textContent = suggestion.value;
    item.classList.toggle('active', index === macroSuggestIndex);
    item.addEventListener('mousedown', (event) => {
      event.preventDefault();
      applyMacroSuggestion(index);
    });
    menu.appendChild(item);
  });
  menu.classList.add('open');
}

function applyMacroSuggestion(index = macroSuggestIndex) {
  const script = document.getElementById('script');
  const suggestions = matchingMacroSuggestions();
  const suggestion = suggestions[index];
  if (!suggestion) {
    return false;
  }

  const before = script.value.slice(0, suggestion.range.start);
  const after = script.value.slice(suggestion.range.end);
  script.value = before + suggestion.value + after;
  const caret = before.length + suggestion.value.length;
  script.setSelectionRange(caret, caret);
  updateMacroEditor();
  syncMacroEditorScroll();
  return true;
}

function handleMacroAutocompleteKey(event) {
  const menu = document.getElementById('macroSuggest');
  if (!menu.classList.contains('open')) {
    if (event.key === 'Tab') {
      updateMacroSuggestions();
      if (menu.classList.contains('open') && applyMacroSuggestion()) {
        event.preventDefault();
      }
    }
    return;
  }

  const count = menu.querySelectorAll('button').length;
  if (event.key === 'ArrowDown') {
    macroSuggestIndex = (macroSuggestIndex + 1) % count;
    updateMacroSuggestions();
    event.preventDefault();
  } else if (event.key === 'ArrowUp') {
    macroSuggestIndex = (macroSuggestIndex + count - 1) % count;
    updateMacroSuggestions();
    event.preventDefault();
  } else if (event.key === 'Tab') {
    applyMacroSuggestion();
    event.preventDefault();
  } else if (event.key === 'Escape') {
    menu.classList.remove('open');
  }
}

function syncProfiles(data) {
  const select = document.getElementById('profiles');
  const prev = select.value;
  const profiles = data.profiles || [];
  const options = ['', ...profiles];
  const currentProfile = data.busy && profiles.includes(data.currentProfile)
    ? data.currentProfile
    : '';

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

  if (profileSelectionTouched && [...select.options].some((option) => option.value === prev)) {
    select.value = prev;
  } else if (currentProfile) {
    select.value = currentProfile;
  } else if (data.defaultProfile && [...select.options].some((option) => option.value === data.defaultProfile)) {
    select.value = data.defaultProfile;
  } else {
    select.value = '';
  }
}

async function loadProfile() {
  const name = document.getElementById('profiles').value;
  if (!name) {
    document.getElementById('name').value = '';
    document.getElementById('script').value = '';
    formatAndUpdateMacroEditor(false);
    syncMacroEditorScroll();
    return;
  }

  document.getElementById('name').value = name;
  const response = await getJson(`api/profile/load?name=${encodeURIComponent(name)}`);
  document.getElementById('script').value = response.data?.script || '';
  formatAndUpdateMacroEditor(false);
  syncMacroEditorScroll();
}

async function saveProfile() {
  assertProfileFormValid();
  formatAndUpdateMacroEditor();
  assertMacroValid();
  await postForm('api/profile/save', {
    name: document.getElementById('name').value,
    script: document.getElementById('script').value,
  });

  await refresh();
  showToast('Profile saved');
}

function assertProfileFormValid() {
  assertTokenNameField('name', 'Profile name');
}

async function deleteProfile() {
  assertTokenNameField('name', 'Profile name');
  await postForm('api/profile/delete', {
    name: document.getElementById('name').value,
  });

  await refresh();
  await loadProfile();
  showToast('Profile deleted');
}

async function exportProfiles() {
  const response = await getJson('api/profile/export');
  const payload = response.data || { version: 1, profiles: [] };
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement('a');
  link.href = url;
  link.download = 'hidpad-profiles.json';
  document.body.appendChild(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
  showToast('Profiles exported');
}

async function importProfilesFromFile(file) {
  if (!file) {
    return;
  }
  if (file.size > 256 * 1024) {
    throw new Error('Import file exceeds 256 KiB');
  }

  const text = await file.text();
  const payload = JSON.parse(text);
  const profiles = payload.profiles || payload.data?.profiles || [];
  if (!Array.isArray(profiles) || profiles.length === 0) {
    throw new Error('Import file must contain a non-empty profiles array');
  }
  if (profiles.length > 64) {
    throw new Error('Import file contains more than 64 profiles');
  }
  const importedNames = new Set();
  profiles.forEach((profile, index) => {
    const error = validateTokenNameValue(profile?.name || '', `Imported profile ${index + 1} name`);
    if (error) {
      throw new Error(error);
    }
    if (typeof profile?.script !== 'string') {
      throw new Error(`Imported profile ${index + 1} script must be a string`);
    }
    if (new TextEncoder().encode(profile.script).length > maxMacroScriptBytes) {
      throw new Error(`Imported profile ${index + 1} script exceeds 32 KiB`);
    }
    if (importedNames.has(profile.name)) {
      throw new Error(`Imported profile name is duplicated: ${profile.name}`);
    }
    importedNames.add(profile.name);
  });
  await postJson('api/profile/import', payload);
  await refresh();
  await loadProfile();
  showToast('Profiles imported');
}

async function runProfile() {
  assertProfileFormValid();
  formatAndUpdateMacroEditor();
  assertMacroValid();
  await postForm('api/run', {
    name: document.getElementById('profiles').value || document.getElementById('name').value,
  });

  await refresh();
  showToast('Saved profile started');
}

async function testScript() {
  assertProfileFormValid();
  formatAndUpdateMacroEditor();
  assertMacroValid();
  await postForm('api/run/script', {
    name: document.getElementById('name').value || 'draft',
    script: document.getElementById('script').value,
  });

  await refresh();
  showToast('Draft macro started');
}

async function stopProfile() {
  await postForm('api/stop', {});
  await refresh();
  showToast('Macro stopped');
}

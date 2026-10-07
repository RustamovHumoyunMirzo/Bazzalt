/* Static HTML, relative URLs and a local JS search index also work under file://. */
(() => {
  'use strict';
  const catalog = window.BazzaltDocs;
  if (!catalog) return; // Content and ordinary links remain usable without JS.
  if (document.documentElement.dataset.docsReady) return;
  document.documentElement.dataset.docsReady = 'true'; // Never install duplicate handlers/renderers.
  document.documentElement.classList.add('js-enabled');
  const body = document.body;
  const root = new URL(body.dataset.root || './', location.href);
  const landing = body.dataset.landing === 'true';
  const version = body.dataset.version || catalog.defaultVersion;
  const page = body.dataset.page || 'manual';
  let language = document.documentElement.lang;
  const read = key => { try { return localStorage.getItem(key); } catch (_) { return null; } };
  const save = (key, value) => { try { localStorage.setItem(key, value); } catch (_) {} };
  const entries = (v, l) => catalog.pages.filter(p => p.version === v && p.language === l);
  // A single local script contains the shared tree: no per-page copies, fetch
  // polling, lazy asset loaders, or HTTP dependency (including under file://).
  if (!landing) {
    const sidebar = document.getElementById('sidebar');
    const available = new Map(entries(version, language).map(p => [p.id, p]));
    function renderNavigation(nodes) {
      const fragment = document.createDocumentFragment();
      for (const node of nodes) {
        const record = available.get(node.id);
        const children = renderNavigation(node.children || []);
        if (!record && !children.childElementCount) continue;
        let container = fragment;
        if (children.childElementCount) {
          const details = document.createElement('details');
          const summary = document.createElement('summary');summary.textContent = node.title;
          const containsCurrent = branch => branch.some(n => n.id === page || containsCurrent(n.children || []));
          details.open = node.id === page || containsCurrent(node.children || []);
          const list = document.createElement('div');list.className = 'tree-list';
          details.append(summary, list);fragment.append(details);container = list;
        }
        if (record) {
          const link = document.createElement('a');link.className = 'nav-link';
          link.href = new URL(record.url, root).href;link.textContent = record.title;
          if (record.id === page) link.setAttribute('aria-current', 'page');
          container.append(link);
        }
        container.append(children);
      }
      return fragment;
    }
    sidebar?.replaceChildren(renderNavigation(catalog.navigation?.[version]?.[language] || []));
  }
  const languageSelect = document.getElementById('language');
  const versionSelect = document.getElementById('version');
  const languageName = code => {
    try {return new Intl.DisplayNames([code], {type:'language'}).of(code) || code;} catch (_) {return catalog.languages[code] || code;}
  };
  languageSelect?.querySelectorAll('option').forEach(option => {option.textContent = languageName(option.value);});
  function destination(v, l, id) {
    // Never invent a translation or a versioned URL that has not been built.
    const pages = entries(v, l);
    return pages.find(p => p.id === id) || entries(v, catalog.defaultLanguage).find(p => p.id === id) || pages.find(p => p.id === 'manual') || pages[0];
  }
  function navigate(v, l) {
    const target = destination(v, l, page) || destination(v, catalog.defaultLanguage, page);
    if (!target) return;
    const url = new URL(target.url, root);
    if (target.id === page && target.anchors.includes(location.hash.slice(1))) url.hash = location.hash;
    if (url.href !== location.href) location.assign(url.href);
  }
  function landingLanguage(l) {
    language = catalog.landing[l] ? l : catalog.defaultLanguage;
    document.documentElement.lang = language;
    const texts = catalog.landing[language];
    document.querySelectorAll('[data-i18n]').forEach(node => {
      if (texts[node.dataset.i18n]) node.textContent = texts[node.dataset.i18n];
    });
    document.title = texts.title;
    languageSelect.value = language;
    document.querySelectorAll('[data-doc-link]').forEach(link => {
      const target = destination(catalog.defaultVersion, language, link.dataset.docLink);
      if (target) link.href = new URL(target.url, root).href;
    });
  }
  if (landing) {
    languageSelect.replaceChildren(...Object.keys(catalog.landing).map(l => new Option(languageName(l), l)));
    landingLanguage(read('bz-doc-language') || catalog.defaultLanguage);
  }
  languageSelect?.addEventListener('change', () => {
    save('bz-doc-language', languageSelect.value);
    if (landing) landingLanguage(languageSelect.value);
    else navigate(version, languageSelect.value);
  });
  versionSelect?.addEventListener('change', () => navigate(versionSelect.value, language));
  document.querySelectorAll('.theme-btn').forEach(button => {
    button.addEventListener('click', () => {
      const dark = document.documentElement.classList.toggle('dark');
      save('bz-theme', dark ? 'dark' : 'light');
    });
  });
  const toggle = document.querySelector('.nav-toggle');
  toggle?.addEventListener('click', () => {
    toggle.setAttribute('aria-expanded', String(body.classList.toggle('nav-open')));
  });

  // Native dialog supplies focus trapping and Escape. Opening/closing restores focus.
  const dialog = document.getElementById('search-dialog');
  const input = document.getElementById('search-input');
  const results = document.getElementById('search-results');
  const status = document.getElementById('search-status');
  let previousFocus;
  function search() {
    const words = input.value.trim().toLowerCase().split(/\s+/).filter(Boolean);
    const hits = entries(version, language).filter(p => words.every(w => (p.title + ' ' + p.text).toLowerCase().includes(w)));
    hits.sort((a, b) => Number(words.some(w => b.title.toLowerCase().includes(w))) - Number(words.some(w => a.title.toLowerCase().includes(w))));
    results.replaceChildren();
    status.textContent = hits.length ? `${hits.length} matching ${hits.length === 1 ? 'page' : 'pages'}` : 'No matching pages. Try a different term.';
    hits.forEach(hit => {
      const li = document.createElement('li');
      const link = document.createElement('a');
      const detail = document.createElement('small');
      const heading = words.length && hit.headings.find(h => words.every(w => h.text.toLowerCase().includes(w)));
      const url = new URL(hit.url, root);
      if (heading) url.hash = heading.id;
      link.href = url.href;link.textContent = hit.title;
      detail.textContent = heading ? heading.text : hit.description;
      link.append(detail);li.append(link);results.append(li);
    });
  }
  function openSearch() {
    if (dialog.open) { input.focus();return; }
    previousFocus = document.activeElement;input.value = '';search();dialog.showModal();input.focus();
  }
  document.querySelectorAll('[data-search-open]').forEach(b => b.addEventListener('click', openSearch));
  document.querySelector('[data-search-close]')?.addEventListener('click', () => dialog.close());
  dialog?.addEventListener('close', () => previousFocus?.focus());
  dialog?.addEventListener('click', event => {
    const rect = dialog.getBoundingClientRect();
    if (event.target === dialog && (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom)) dialog.close();
  });
  input?.addEventListener('input', search);
  input?.addEventListener('keydown', event => {
    if (event.key === 'ArrowDown') {event.preventDefault();results.querySelector('a')?.focus();}
    if (event.key === 'Enter') {event.preventDefault();results.querySelector('a')?.click();}
  });
  results?.addEventListener('keydown', event => {
    const links = [...results.querySelectorAll('a')];const index = links.indexOf(document.activeElement);
    if (event.key === 'ArrowDown' || event.key === 'ArrowUp') {
      event.preventDefault();const next = index + (event.key === 'ArrowDown' ? 1 : -1);
      if (next < 0) input.focus();else links[Math.min(next, links.length - 1)]?.focus();
    }
  });
  document.addEventListener('keydown', event => {
    if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'k') {event.preventDefault();openSearch();}
    if (event.key === 'Escape') {body.classList.remove('nav-open');toggle?.setAttribute('aria-expanded', 'false');}
  });

  document.querySelectorAll('pre > code').forEach(code => {
    const original = code.textContent;
    const explicit = [...code.classList].find(name => name.startsWith('language-'))?.slice(9);
    const lang = explicit || (page.startsWith('lua') ? 'lua' : page.startsWith('native') ? 'cpp' : 'text');
    const names = {cpp:'C++',c:'C',lua:'Lua',bshader:'Bshader',glsl:'GLSL',json:'JSON',yaml:'YAML',bash:'Shell',text:'Text'};
    const toolbar = document.createElement('div');toolbar.className = 'code-toolbar';
    const label = document.createElement('span');label.className = 'code-language';label.textContent = names[lang] || lang;
    const button = document.createElement('button');button.className = 'copy-code';button.textContent = 'Copy';
    button.setAttribute('aria-label', 'Copy code example');
    button.addEventListener('click', async () => {
      let copied = false;
      try { await navigator.clipboard.writeText(original);copied = true; } catch (_) {
        // Clipboard API can be unavailable for file:// or non-secure HTTP.
        const field = document.createElement('textarea');field.value = original;
        field.style.position = 'fixed';field.style.opacity = '0';document.body.append(field);field.select();
        try {copied = document.execCommand('copy');} catch (_) {} field.remove();button.focus();
      }
      if (!copied) {const range = document.createRange();range.selectNodeContents(code);const selection = window.getSelection();selection.removeAllRanges();selection.addRange(range);}
      button.textContent = copied ? 'Copied' : 'Selected: press Ctrl+C';setTimeout(() => {button.textContent = 'Copy';}, 1800);
    });
    toolbar.append(label,button);code.parentElement.prepend(toolbar);
    // Small local lexer, not HTML replacement: text stays byte-for-byte intact
    // and code containing markup cannot inject DOM nodes. No CDN/plugins.
    if (['cpp','c','lua','bshader','glsl','json','yaml'].includes(lang)) {
      const tokens = /(--\[\[[\s\S]*?\]\]|--[^\n]*|\/\*[\s\S]*?\*\/|\/\/[^\n]*|#[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\b\d+(?:\.\d*)?(?:[eE][+-]?\d+)?\b|\b[A-Za-z_][A-Za-z_0-9]*\b)/g;
      const keywords = new Set(('shader properties material vertex options float int bool vec2 vec3 vec4 mat3 mat4 texture2d if else for while break continue return true false nil local function end then do elseif and or not in repeat until class struct enum namespace const auto static void public private protected using include nullptr new delete virtual override template typename').split(' '));
      const fragment = document.createDocumentFragment();let position = 0;
      for (const match of original.matchAll(tokens)) {
        fragment.append(document.createTextNode(original.slice(position,match.index)));
        const text = match[0];let kind = null;
        if (text.startsWith('--') || text.startsWith('//') || text.startsWith('/*')) kind = 'comment';
        else if (text.startsWith('#')) kind = 'directive';
        else if (text.startsWith('"') || text.startsWith("'")) kind = 'string';
        else if (/^\d/.test(text)) kind = 'number';
        else if (keywords.has(text)) kind = 'keyword';
        else if (/^\s*\(/.test(original.slice(match.index + text.length))) kind = 'function';
        if (kind) {const span = document.createElement('span');span.className = 'token-'+kind;span.textContent = text;fragment.append(span);}
        else fragment.append(document.createTextNode(text));
        position = match.index + text.length;
      }
      fragment.append(document.createTextNode(original.slice(position)));code.replaceChildren(fragment);
    }
  });
  const headings = [...document.querySelectorAll('.doc h2,.doc h3')];
  const toc = [...document.querySelectorAll('.toc-link')];let scheduled = false;let clickedHeading = null;
  function markCurrent(current) {
    toc.forEach(link => {if (link.hash.slice(1) === current) link.setAttribute('aria-current', 'location');else link.removeAttribute('aria-current');});
  }
  toc.forEach(link => link.addEventListener('click', () => {
    clickedHeading = link.hash.slice(1);markCurrent(clickedHeading);
  }));
  function hashHeading() {
    let id;try {id = decodeURIComponent(location.hash.slice(1));} catch (_) {id = '';}
    if (headings.some(h => h.id === id)) {clickedHeading = id;markCurrent(id);}
    else clickedHeading = null;
  }
  window.addEventListener('hashchange', hashHeading);
  // A clicked small section may never reach the sticky-header threshold.
  // Keep explicit anchor selection until the reader deliberately scrolls again.
  ['wheel', 'touchstart'].forEach(event => window.addEventListener(event, () => {clickedHeading = null;}, {passive:true}));
  document.addEventListener('keydown', event => {
    if (['PageDown','PageUp','ArrowDown','ArrowUp','Home','End',' '].includes(event.key) && !dialog?.open && !['INPUT','TEXTAREA','SELECT'].includes(document.activeElement?.tagName)) clickedHeading = null;
  });
  window.addEventListener('pointerdown', event => {if (!event.target.closest('.toc-link')) clickedHeading = null;}, {passive:true});
  function spy() {
    let current = headings[0]?.id;
    headings.forEach(h => {if (h.getBoundingClientRect().top < 140) current = h.id;});
    const scrollable = document.documentElement.scrollHeight > window.innerHeight + 2;
    if (scrollable && window.scrollY + window.innerHeight >= document.documentElement.scrollHeight - 3) current = headings.at(-1)?.id;
    markCurrent(clickedHeading || current);
    scheduled = false;
  }
  window.addEventListener('scroll', () => {if (!scheduled) {scheduled = true;requestAnimationFrame(spy);}}, {passive:true});
  window.addEventListener('resize', spy);hashHeading();spy();
})();
